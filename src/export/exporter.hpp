#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <functional>
#include <atomic>
#include <thread>
#include <memory>

// Export format
enum class ExportFormat { MP4, GIF, PNG_SEQUENCE };

// Crop region in world space (same coordinates as pan/zoom system)
struct CropRegion {
    float x_min{-0.5f}, x_max{0.5f};
    float y_min{-0.5f}, y_max{0.5f};

    float width()  const { return x_max - x_min; }
    float height() const { return y_max - y_min; }
    bool  valid()  const { return width() > 0.001f && height() > 0.001f; }
};

struct ExportSettings {
    ExportFormat format      = ExportFormat::MP4;
    std::string  output_path = "";
    CropRegion   crop;

    // Resolution of output pixels
    int out_width  = 1920;
    int out_height = 1080;

    // Video/GIF
    float fps      = 10.0f;
    int   crf      = 18;      // quality for x264/x265 (lower=better)
    bool  use_nvenc= true;    // prefer NVENC h264 if available

    // PNG sequence: prefix, e.g. "/path/to/frame_"  → frame_000.png
    std::string png_prefix = "frame_";
};

// One RGBA frame passed to the exporter
struct ExportFrame {
    std::vector<uint8_t> rgba; // top-to-bottom, out_width × out_height × 4
    int64_t              timestamp;
};

// Thread-safe progress/status for UI
struct ExportProgress {
    std::atomic<int>  frames_done{0};
    std::atomic<int>  frames_total{0};
    std::atomic<bool> running{false};
    std::atomic<bool> finished{false};
    std::atomic<bool> failed{false};
    std::string       error_msg;   // set before failed=true
    std::string       output_path; // final path written
};

class Exporter {
public:
    Exporter()  = default;
    ~Exporter() { cancel(); }

    Exporter(const Exporter&) = delete;
    Exporter& operator=(const Exporter&) = delete;

    // Begin async export. frames must be fully populated before calling.
    // Returns false immediately if already running.
    bool start(const ExportSettings&        settings,
               std::vector<ExportFrame>     frames,
               std::shared_ptr<ExportProgress> progress);

    void cancel();

    // True only while export is actively running (not merely "thread not yet joined").
    bool running() const {
        return m_thread.joinable()
            && m_progress
            && m_progress->running.load();
    }

private:
    void run(ExportSettings settings,
             std::vector<ExportFrame> frames,
             std::shared_ptr<ExportProgress> progress);

    bool write_mp4     (const ExportSettings&, const std::vector<ExportFrame>&, ExportProgress&);
    bool write_gif     (const ExportSettings&, const std::vector<ExportFrame>&, ExportProgress&);
    bool write_png_seq (const ExportSettings&, const std::vector<ExportFrame>&, ExportProgress&);

    std::thread                     m_thread;
    std::atomic<bool>               m_cancel{false};
    std::shared_ptr<ExportProgress> m_progress;   // held so we can check finished state
};
