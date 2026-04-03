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

namespace fs = std::filesystem;

// ── Exporter control ──────────────────────────────────────────────────────────

bool Exporter::start(const ExportSettings&           settings,
                     std::vector<ExportFrame>         frames,
                     std::shared_ptr<ExportProgress>  progress) {
    if (m_thread.joinable()) return false;
    m_cancel.store(false);
    progress->running.store(true);
    progress->finished.store(false);
    progress->failed.store(false);
    progress->frames_done.store(0);
    progress->frames_total.store(int(frames.size()));

    m_thread = std::thread(&Exporter::run, this,
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

    int W = settings.out_width, H = settings.out_height;
    std::string path = settings.output_path;

    // Pick encoder: prefer NVENC, fall back to libx264
    const AVCodec* codec = nullptr;
    if (settings.use_nvenc) codec = avcodec_find_encoder_by_name("h264_nvenc");
    if (!codec)             codec = avcodec_find_encoder_by_name("libx264");
    if (!codec)             codec = avcodec_find_encoder(AV_CODEC_ID_H264);
    if (!codec) { prog.error_msg = "No H.264 encoder found"; return false; }

    AVFormatContext* fmt_ctx = nullptr;
    avformat_alloc_output_context2(&fmt_ctx, nullptr, nullptr, path.c_str());
    if (!fmt_ctx) { prog.error_msg = "avformat_alloc failed"; return false; }

    AVStream* stream = avformat_new_stream(fmt_ctx, nullptr);
    if (!stream) { avformat_free_context(fmt_ctx); prog.error_msg = "avformat_new_stream failed"; return false; }

    AVCodecContext* cc = avcodec_alloc_context3(codec);
    cc->width        = W;
    cc->height       = H;
    cc->time_base    = { 1, int(settings.fps * 1000) };
    cc->framerate    = { int(settings.fps * 1000), 1000 };
    cc->pix_fmt      = AV_PIX_FMT_YUV420P;
    cc->gop_size     = 10;
    cc->max_b_frames = 0;

    bool is_nvenc = std::string(codec->name).find("nvenc") != std::string::npos;
    if (is_nvenc) {
        av_opt_set(cc->priv_data, "preset", "p4", 0);
        av_opt_set(cc->priv_data, "rc",     "vbr", 0);
        av_opt_set_int(cc->priv_data, "cq", settings.crf, 0);
    } else {
        av_opt_set(cc->priv_data, "preset", "fast", 0);
        av_opt_set(cc->priv_data, "crf",    std::to_string(settings.crf).c_str(), 0);
    }

    if (fmt_ctx->oformat->flags & AVFMT_GLOBALHEADER)
        cc->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    stream->time_base = cc->time_base;

    if (avcodec_open2(cc, codec, nullptr) < 0) {
        avcodec_free_context(&cc); avformat_free_context(fmt_ctx);
        prog.error_msg = "avcodec_open2 failed";
        return false;
    }
    avcodec_parameters_from_context(stream->codecpar, cc);

    if (!(fmt_ctx->oformat->flags & AVFMT_NOFILE)) {
        if (avio_open(&fmt_ctx->pb, path.c_str(), AVIO_FLAG_WRITE) < 0) {
            avcodec_free_context(&cc); avformat_free_context(fmt_ctx);
            prog.error_msg = "avio_open failed: " + path;
            return false;
        }
    }
    avformat_write_header(fmt_ctx, nullptr);

    AVFrame*  frame = av_frame_alloc();
    frame->format = AV_PIX_FMT_YUV420P;
    frame->width  = W;
    frame->height = H;
    av_frame_get_buffer(frame, 0);

    SwsContext* sws = make_sws(W, H, AV_PIX_FMT_YUV420P);
    AVPacket*   pkt = av_packet_alloc();

    int64_t pts = 0;
    int     frames_per_src = std::max(1, int(1000.0f / settings.fps / (1000.0f / (settings.fps * 1000))));

    for (int i = 0; i < int(frames.size()) && !m_cancel; ++i) {
        const uint8_t* src    = frames[i].rgba.data();
        int            stride = W * 4;
        sws_scale(sws, &src, &stride, 0, H, frame->data, frame->linesize);
        frame->pts = pts++;

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

    avcodec_open2(cc, codec, nullptr);
    avcodec_parameters_from_context(stream->codecpar, cc);

    avio_open(&fmt_ctx->pb, path.c_str(), AVIO_FLAG_WRITE);
    avformat_write_header(fmt_ctx, nullptr);

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
