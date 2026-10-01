// MediaService.h — worker pool for everything that must never block the UI thread:
// probe + decode + peaks, rendered-clip loading, clip rendering (write MP3), image decoding.
// Results come back through drain() once per frame on the main thread.
#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include "media/ClipRenderer.h"
#include "media/ImageDecoder.h"
#include "media/VideoFrames.h"
#include "model/MediaAsset.h"
#include "util/ThreadSafeQueue.h"
#include "util/WorkerPool.h"

namespace evobox
{

struct MediaEvent
{
    enum class Kind { Decoded, ClipLoaded, Rendered, ClipEncoded, Image, Frames, Progress, LibavLog };
    Kind kind = Kind::Decoded;
    uint64_t requestId = 0;      // decode request / sound uid / image tag / frames tag
    std::string path;            // file read or written (ClipEncoded: the new clip file)
    bool ok = false;
    std::string error;
    float progress = 0.f;
    int logLevel = 0;
    // Decoded / ClipLoaded / Rendered
    std::shared_ptr<const MediaAsset> asset;                // Decoded (full source)
    std::shared_ptr<const organic::AudioBuffer> buffer;     // ClipLoaded / Rendered
    // ClipLoaded / Rendered: source position (s) of the buffer's first frame = the trim start the
    // clip was rendered with, echoed from the request (the trim may have moved on meanwhile)
    double sourceStartSec = 0;
    // Image
    std::shared_ptr<const RgbaImage> image;
    // Frames (video thumbnails, in time order)
    std::shared_ptr<const std::vector<VideoFrame>> frames;
};

class MediaService
{
public:
    MediaService();
    ~MediaService();

    void start(int threads = 2);
    void stop();

    // Probe + decode + peaks of a source file (cancellable).
    CancelToken requestDecode(const std::string& path, uint64_t requestId, int peaksSamplesPerBin = 512);
    // Load a rendered clip (MP3, legacy WAV — any container really) as a playable buffer.
    // `sourceStartSec` (the trim start the file was rendered with) is echoed in the event.
    CancelToken requestClipLoad(const std::string& path, uint64_t soundUid, double sourceStartSec = 0);
    // Render a clip from an in-memory source and write it to outPath (format by extension:
    // ".mp3" or float32 ".wav", see ClipEncoder). Two events: Rendered carries the in-memory
    // buffer as soon as it exists, ClipEncoded reports the file write (skipped for an empty outPath).
    CancelToken requestRender(std::shared_ptr<const organic::AudioBuffer> source, const RenderParams& params,
                              const std::string& outPath, uint64_t soundUid);
    // Write an already decoded clip buffer to outPath (legacy .wav clip -> .mp3 conversion) -> ClipEncoded.
    CancelToken requestClipEncode(std::shared_ptr<const organic::AudioBuffer> buffer, const std::string& outPath,
                                  uint64_t soundUid);
    // Decode an image file to RGBA (max edge in px).
    CancelToken requestImage(const std::string& path, uint64_t tag, int maxEdge = 128);
    // Extract `count` evenly spaced frames of a video as RGBA thumbnails (max edge in px).
    CancelToken requestVideoFrames(const std::string& path, uint64_t tag, int count = 16, int maxEdge = 160);

    size_t drain(const std::function<void(const MediaEvent&)>& fn);
    size_t pending() const { return pool_.pending(); }
    int activeJobs() const { return active_.load(); }

    // progress of the most recent decode request (0..1)
    float lastDecodeProgress() const { return decodeProgress_.load(); }

private:
    WorkerPool pool_;
    ThreadSafeQueue<MediaEvent> events_;
    std::atomic<int> active_{ 0 };
    std::atomic<float> decodeProgress_{ 0.f };
};

} // namespace evobox
