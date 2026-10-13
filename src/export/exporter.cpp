#include "exporter.hpp"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <iostream>
#include <format>
#include <filesystem>
#include <cmath>
#include <string>
#include <vector>
#include <cstring>

namespace fs = std::filesystem;

#ifndef RAMMB_HW_H264_ENCODER
#ifdef __APPLE__
#define RAMMB_HW_H264_ENCODER "h264_videotoolbox"
#else
#define RAMMB_HW_H264_ENCODER "h264_nvenc"
#endif
#endif

// Platform preference chain for MP4 H.264 (one exporter, pluggable backends).
//   Linux:  h264_nvenc → libx264 → AV_CODEC_ID_H264
//   macOS:  h264_videotoolbox → libx264 → AV_CODEC_ID_H264
static std::vector<const char*> h264_encoder_chain(bool prefer_hw) {
    std::vector<const char*> chain;
    if (prefer_hw)
        chain.push_back(RAMMB_HW_H264_ENCODER);
    chain.push_back("libx264");
    return chain;
}

static void apply_encoder_options(AVCodecContext* cc, const AVCodec* codec,
                                  const ExportSettings& settings) {
    const char* name = codec->name ? codec->name : "";
    if (std::strstr(name, "nvenc")) {
        av_opt_set(cc->priv_data, "preset", "p4", 0);
        av_opt_set(cc->priv_data, "rc",     "vbr", 0);
        av_opt_set_int(cc->priv_data, "cq", settings.crf, 0);
    } else if (std::strstr(name, "videotoolbox")) {
        // VT has no CRF; map quality slider → bitrate (rough bpp from CRF).
        float q = std::clamp((51.0f - float(settings.crf)) / 51.0f, 0.08f, 1.0f);
        int64_t br = int64_t(double(cc->width) * double(cc->height)
                             * double(settings.fps) * 0.20 * double(q));
        cc->bit_rate = std::max<int64_t>(br, 500000);
        if (cc->priv_data) {
            av_opt_set(cc->priv_data, "realtime", "false", 0);
            av_opt_set(cc->priv_data, "allow_sw", "true", 0);
        }
    } else {
        // libx264 / generic software
        if (cc->priv_data) {
            av_opt_set(cc->priv_data, "preset",  "fast", 0);
            av_opt_set(cc->priv_data, "crf",     std::to_string(settings.crf).c_str(), 0);
            av_opt_set(cc->priv_data, "profile", "main", 0);
        }
    }
}

// Find + open first working encoder in the preference chain.
static bool open_h264_encoder(const ExportSettings& settings, int W, int H,
                              float fps, AVCodecContext** out_cc,
                              const AVCodec** out_codec, std::string& err) {
    auto chain = h264_encoder_chain(settings.prefer_hw_encoder);
    const int     TB_DEN = 1000;
    const int     fps_n  = std::max(1, int(std::lround(fps)));

    for (const char* name : chain) {
        const AVCodec* codec = avcodec_find_encoder_by_name(name);
        if (!codec)
            continue;

        AVCodecContext* cc = avcodec_alloc_context3(codec);
        if (!cc)
            continue;
        cc->width        = W;
        cc->height       = H;
        cc->time_base    = { 1, TB_DEN };
        cc->framerate    = { fps_n, 1 };
        cc->pix_fmt      = AV_PIX_FMT_YUV420P;
        cc->gop_size     = 10;
        cc->max_b_frames = 0;
        // MP4 muxer needs global headers; must be set before open.
        cc->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        apply_encoder_options(cc, codec, settings);

        if (avcodec_open2(cc, codec, nullptr) < 0) {
            avcodec_free_context(&cc);
            continue; // try next candidate (e.g. NVENC present but no GPU)
        }
        *out_cc    = cc;
        *out_codec = codec;
        return true;
    }

    // Last resort: any H.264 encoder FFmpeg knows about
    const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_H264);
    if (!codec) {
        err = "No H.264 encoder found";
        return false;
    }
    AVCodecContext* cc = avcodec_alloc_context3(codec);
    if (!cc) {
        err = "avcodec_alloc_context3 failed";
        return false;
    }
    cc->width        = W;
    cc->height       = H;
    cc->time_base    = { 1, TB_DEN };
    cc->framerate    = { fps_n, 1 };
    cc->pix_fmt      = AV_PIX_FMT_YUV420P;
    cc->gop_size     = 10;
    cc->max_b_frames = 0;
    cc->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    apply_encoder_options(cc, codec, settings);
    if (avcodec_open2(cc, codec, nullptr) < 0) {
        avcodec_free_context(&cc);
        err = "avcodec_open2 failed for all H.264 encoders";
        return false;
    }
    *out_cc    = cc;
    *out_codec = codec;
    return true;
}

// ── Exporter control ──────────────────────────────────────────────────────────

bool Exporter::start(const ExportSettings&           settings,
                     std::vector<ExportFrame>         frames,
                     std::shared_ptr<ExportProgress>  progress) {
    // Join the previous thread if it finished but hasn't been joined yet.
    // This is the normal case after a successful (or failed) export.
    if (m_thread.joinable()) {
        if (m_progress && !m_progress->running.load()) {
            m_thread.join(); // safe: thread has already returned
        } else {
            return false;   // genuinely still running — don't interrupt
        }
    }

    m_cancel.store(false);
    progress->running.store(true);
    progress->finished.store(false);
    progress->failed.store(false);
    progress->frames_done.store(0);
    progress->frames_total.store(int(frames.size()));
    progress->error_msg.clear();
    progress->output_path.clear();

    m_progress = progress;
    m_thread   = std::thread(&Exporter::run, this,
                             settings, std::move(frames), progress);
    return true;
}

void Exporter::cancel() {
    m_cancel.store(true);
    if (m_thread.joinable()) m_thread.join();
}

void Exporter::run(ExportSettings settings,
                   std::vector<ExportFrame> frames,
                   std::shared_ptr<ExportProgress> prog) {
    bool ok = false;
    switch (settings.format) {
        case ExportFormat::MP4:         ok = write_mp4     (settings, frames, *prog); break;
        case ExportFormat::GIF:         ok = write_gif     (settings, frames, *prog); break;
        case ExportFormat::PNG_SEQUENCE:ok = write_png_seq (settings, frames, *prog); break;
    }
    prog->running.store(false);
    prog->finished.store(true);
    if (!ok) prog->failed.store(true);
}

// ── RGB conversion helper ─────────────────────────────────────────────────────
// FFmpeg wants YUV420 for most codecs; we convert per-frame via swscale.

static SwsContext* make_sws(int w, int h, AVPixelFormat dst_fmt) {
    return sws_getContext(w, h, AV_PIX_FMT_RGBA,
                          w, h, dst_fmt,
                          SWS_BILINEAR, nullptr, nullptr, nullptr);
}

// ── MP4 export ────────────────────────────────────────────────────────────────

bool Exporter::write_mp4(const ExportSettings&         settings,
                          const std::vector<ExportFrame>& frames,
                          ExportProgress&               prog) {
    if (frames.empty()) { prog.error_msg = "No frames"; return false; }

    // H.264 requires even dimensions
    int W = (settings.out_width  / 2) * 2;
    int H = (settings.out_height / 2) * 2;
    std::string path = settings.output_path;

    // Use millisecond time base — supports any FPS cleanly.
    // pts increments by pts_per_frame each frame so duration is exact.
    const int     TB_DEN        = 1000;
    const int64_t pts_per_frame = int64_t(TB_DEN / settings.fps);  // ms per frame

    AVCodecContext* cc    = nullptr;
    const AVCodec*  codec = nullptr;
    if (!open_h264_encoder(settings, W, H, settings.fps, &cc, &codec, prog.error_msg))
        return false;
    std::cout << "[export] H.264 encoder: " << codec->name << "\n";

    AVFormatContext* fmt_ctx = nullptr;
    avformat_alloc_output_context2(&fmt_ctx, nullptr, nullptr, path.c_str());
    if (!fmt_ctx) {
        avcodec_free_context(&cc);
        prog.error_msg = "avformat_alloc failed";
        return false;
    }

    AVStream* stream = avformat_new_stream(fmt_ctx, nullptr);
    if (!stream) {
        avcodec_free_context(&cc);
        avformat_free_context(fmt_ctx);
        prog.error_msg = "avformat_new_stream failed";
        return false;
    }

    // faststart: move moov atom to front of file so players can seek without buffering
    av_opt_set(fmt_ctx->priv_data, "movflags", "faststart", 0);

    stream->time_base = { 1, TB_DEN };

    avcodec_parameters_from_context(stream->codecpar, cc);

    if (!(fmt_ctx->oformat->flags & AVFMT_NOFILE)) {
        if (avio_open(&fmt_ctx->pb, path.c_str(), AVIO_FLAG_WRITE) < 0) {
            avcodec_free_context(&cc); avformat_free_context(fmt_ctx);
            prog.error_msg = "avio_open failed: " + path;
            return false;
        }
    }
    if (avformat_write_header(fmt_ctx, nullptr) < 0) {
        avcodec_free_context(&cc);
        avformat_free_context(fmt_ctx);
        return false;
    }

    AVFrame*  frame = av_frame_alloc();
    frame->format = AV_PIX_FMT_YUV420P;
    frame->width  = W;
    frame->height = H;
    av_frame_get_buffer(frame, 0);

    SwsContext* sws = make_sws(W, H, AV_PIX_FMT_YUV420P);
    AVPacket*   pkt = av_packet_alloc();

    int64_t pts = 0;

    for (int i = 0; i < int(frames.size()) && !m_cancel; ++i) {
        const uint8_t* src    = frames[i].rgba.data();
        int            stride = W * 4;
        sws_scale(sws, &src, &stride, 0, H, frame->data, frame->linesize);
        frame->pts  = pts;
        pts        += pts_per_frame;   // advance by ms-per-frame, not by 1

        if (avcodec_send_frame(cc, frame) >= 0) {
            while (avcodec_receive_packet(cc, pkt) >= 0) {
                av_packet_rescale_ts(pkt, cc->time_base, stream->time_base);
                pkt->stream_index = stream->index;
                av_interleaved_write_frame(fmt_ctx, pkt);
                av_packet_unref(pkt);
            }
        }
        prog.frames_done.fetch_add(1);
    }

    // Flush
    avcodec_send_frame(cc, nullptr);
    while (avcodec_receive_packet(cc, pkt) >= 0) {
        av_packet_rescale_ts(pkt, cc->time_base, stream->time_base);
        pkt->stream_index = stream->index;
        av_interleaved_write_frame(fmt_ctx, pkt);
        av_packet_unref(pkt);
    }

    av_write_trailer(fmt_ctx);
    sws_freeContext(sws);
    av_frame_free(&frame);
    av_packet_free(&pkt);
    avcodec_free_context(&cc);
    if (!(fmt_ctx->oformat->flags & AVFMT_NOFILE)) avio_closep(&fmt_ctx->pb);
    avformat_free_context(fmt_ctx);

    prog.output_path = path;
    return !m_cancel.load();
}

// ── GIF export ────────────────────────────────────────────────────────────────

bool Exporter::write_gif(const ExportSettings&         settings,
                          const std::vector<ExportFrame>& frames,
                          ExportProgress&               prog) {
    if (frames.empty()) { prog.error_msg = "No frames"; return false; }

    int W = settings.out_width, H = settings.out_height;
    std::string path = settings.output_path;

    // GIF via FFmpeg: mux frames into gif container
    const AVCodec*     codec   = avcodec_find_encoder(AV_CODEC_ID_GIF);
    AVFormatContext*   fmt_ctx = nullptr;
    avformat_alloc_output_context2(&fmt_ctx, nullptr, "gif", path.c_str());
    if (!fmt_ctx || !codec) { prog.error_msg = "GIF encoder unavailable"; return false; }

    AVStream*      stream = avformat_new_stream(fmt_ctx, nullptr);
    AVCodecContext* cc    = avcodec_alloc_context3(codec);
    cc->width      = W;
    cc->height     = H;
    cc->pix_fmt    = AV_PIX_FMT_PAL8;
    cc->time_base  = { 1, 100 }; // GIF uses centiseconds
    cc->framerate  = { 100, std::max(1, int(100.0f / settings.fps)) };
    stream->time_base = cc->time_base;

    if (avcodec_open2(cc, codec, nullptr) < 0) {
        avcodec_free_context(&cc);
        avformat_free_context(fmt_ctx);
        prog.error_msg = "GIF codec open failed";
        return false;
    }
    avcodec_parameters_from_context(stream->codecpar, cc);

    if (avio_open(&fmt_ctx->pb, path.c_str(), AVIO_FLAG_WRITE) < 0) {
        avcodec_free_context(&cc);
        avformat_free_context(fmt_ctx);
        prog.error_msg = "Cannot open output file: " + path;
        return false;
    }
    if (avformat_write_header(fmt_ctx, nullptr) < 0) {
        avcodec_free_context(&cc);
        avio_closep(&fmt_ctx->pb);
        avformat_free_context(fmt_ctx);
        prog.error_msg = "GIF write_header failed";
        return false;
    }

    SwsContext* sws   = make_sws(W, H, AV_PIX_FMT_PAL8);
    AVFrame*    frame = av_frame_alloc();
    frame->format = AV_PIX_FMT_PAL8;
    frame->width  = W;
    frame->height = H;
    av_frame_get_buffer(frame, 0);
    AVPacket* pkt = av_packet_alloc();

    int delay_cs = std::max(1, int(100.0f / settings.fps)); // centiseconds per frame
    int64_t pts  = 0;

    for (int i = 0; i < int(frames.size()) && !m_cancel; ++i) {
        const uint8_t* src    = frames[i].rgba.data();
        int            stride = W * 4;
        sws_scale(sws, &src, &stride, 0, H, frame->data, frame->linesize);
        frame->pts = pts;
        pts += delay_cs;

        if (avcodec_send_frame(cc, frame) >= 0) {
            while (avcodec_receive_packet(cc, pkt) >= 0) {
                av_packet_rescale_ts(pkt, cc->time_base, stream->time_base);
                pkt->stream_index = stream->index;
                av_interleaved_write_frame(fmt_ctx, pkt);
                av_packet_unref(pkt);
            }
        }
        prog.frames_done.fetch_add(1);
    }

    avcodec_send_frame(cc, nullptr);
    while (avcodec_receive_packet(cc, pkt) >= 0) {
        av_packet_rescale_ts(pkt, cc->time_base, stream->time_base);
        pkt->stream_index = stream->index;
        av_interleaved_write_frame(fmt_ctx, pkt);
        av_packet_unref(pkt);
    }

    av_write_trailer(fmt_ctx);
    sws_freeContext(sws);
    av_frame_free(&frame);
    av_packet_free(&pkt);
    avcodec_free_context(&cc);
    avio_closep(&fmt_ctx->pb);
    avformat_free_context(fmt_ctx);

    prog.output_path = path;
    return !m_cancel.load();
}

// ── PNG sequence ──────────────────────────────────────────────────────────────

bool Exporter::write_png_seq(const ExportSettings&         settings,
                              const std::vector<ExportFrame>& frames,
                              ExportProgress&               prog) {
    fs::path dir = fs::path(settings.output_path);
    if (!fs::exists(dir)) fs::create_directories(dir);

    int W = settings.out_width, H = settings.out_height;

    for (int i = 0; i < int(frames.size()) && !m_cancel; ++i) {
        std::string name = std::format("{}{:04d}.png", settings.png_prefix, i);
        fs::path    full = dir / name;
        stbi_write_png(full.string().c_str(), W, H, 4,
                       frames[i].rgba.data(), W * 4);
        prog.frames_done.fetch_add(1);
    }

    prog.output_path = dir.string();
    return !m_cancel.load();
}
