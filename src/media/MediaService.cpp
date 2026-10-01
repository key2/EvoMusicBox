#include "media/MediaService.h"
#include "media/ClipEncoder.h"
#include "media/FFmpegDecoder.h"
#include "util/Hash.h"
#include <algorithm>
#include <filesystem>

namespace evobox
{

MediaService::MediaService() = default;
MediaService::~MediaService() { stop(); }

void MediaService::start(int threads)
{
    if (pool_.running()) return;
    pool_.start(threads);
    // libav warnings -> events -> Logger on the main thread
    ThreadSafeQueue<MediaEvent>* q = &events_;
    FFmpegDecoder::installLogSink([q](int level, const std::string& msg)
    {
        MediaEvent e;
        e.kind = MediaEvent::Kind::LibavLog;
        e.logLevel = level;
        e.error = msg;
        q->push(std::move(e));
    });
}

void MediaService::stop()
{
    pool_.stop();
    FFmpegDecoder::installLogSink(nullptr);
}

CancelToken MediaService::requestDecode(const std::string& path, uint64_t requestId, int spb)
{
    active_++;
    decodeProgress_ = 0.f;
    return pool_.post([this, path, requestId, spb](const CancelToken& tok)
    {
        MediaEvent e;
        e.kind = MediaEvent::Kind::Decoded;
        e.requestId = requestId;
        e.path = path;
        auto asset = std::make_shared<MediaAsset>();
        std::string err;
        if (!FFmpegDecoder::probe(path, asset->ref, &err))
        {
            e.error = err;
        }
        else
        {
            auto buf = std::make_shared<organic::AudioBuffer>();
            std::atomic<float>& prog = decodeProgress_;
            if (FFmpegDecoder::decodeAll(path, *buf, &err, &tok, &prog))
            {
                asset->ref.durationSec = buf->duration();
                asset->ref.sampleRate = buf->sampleRate;
                asset->ref.channels = buf->channels;
                std::error_code ec;
                auto sz = std::filesystem::file_size(path, ec);
                if (!ec) asset->ref.sizeBytes = (uint64_t)sz;
                asset->ref.contentHash = fileContentHash(path);
                if (!tok.cancelled())
                {
                    auto peaks = std::make_shared<organic::Peaks>();
                    peaks->build(*buf, spb);
                    asset->peaks = peaks;
                    asset->buffer = buf;
                    e.asset = asset;
                    e.ok = true;
                }
                else e.error = "cancelled";
            }
            else e.error = err;
        }
        active_--;
        if (!tok.cancelled() || !e.ok) events_.push(std::move(e));
    });
}

CancelToken MediaService::requestClipLoad(const std::string& path, uint64_t soundUid, double sourceStartSec)
{
    active_++;
    return pool_.post([this, path, soundUid, sourceStartSec](const CancelToken& tok)
    {
        MediaEvent e;
        e.kind = MediaEvent::Kind::ClipLoaded;
        e.requestId = soundUid;
        e.path = path;
        e.sourceStartSec = sourceStartSec;
        auto buf = std::make_shared<organic::AudioBuffer>();
        std::string err;
        if (FFmpegDecoder::decodeAll(path, *buf, &err, &tok))
        {
            e.buffer = buf;
            e.ok = true;
        }
        else e.error = err;
        active_--;
        if (!tok.cancelled()) events_.push(std::move(e));
    });
}

CancelToken MediaService::requestRender(std::shared_ptr<const organic::AudioBuffer> source, const RenderParams& params,
                                        const std::string& outPath, uint64_t soundUid)
{
    active_++;
    return pool_.post([this, source, params, outPath, soundUid](const CancelToken& tok)
    {
        MediaEvent e;
        e.kind = MediaEvent::Kind::Rendered;
        e.requestId = soundUid;
        e.path = outPath;
        e.sourceStartSec = std::max(0.0, params.trimStartSec); // where the clip's first frame sits in the source
        if (!source)
        {
            e.error = "no source buffer";
            active_--;
            events_.push(std::move(e));
            return;
        }
        auto rendered = std::make_shared<organic::AudioBuffer>(ClipRenderer::render(*source, params));
        if (tok.cancelled()) { active_--; return; }
        // the buffer goes out right away (the tile plays the new clip while the file is still being
        // encoded); the write is reported separately as ClipEncoded once it is on disk
        e.buffer = rendered;
        e.ok = true;
        events_.push(e);
        if (!outPath.empty())
        {
            MediaEvent w;
            w.kind = MediaEvent::Kind::ClipEncoded;
            w.requestId = soundUid;
            w.path = outPath;
            std::string err;
            if (!ClipEncoder::encode(outPath, *rendered, &err)) w.error = err.empty() ? "cannot write " + outPath : err;
            w.ok = w.error.empty();
            events_.push(std::move(w));
        }
        active_--;
    });
}

CancelToken MediaService::requestClipEncode(std::shared_ptr<const organic::AudioBuffer> buffer, const std::string& outPath,
                                            uint64_t soundUid)
{
    active_++;
    return pool_.post([this, buffer, outPath, soundUid](const CancelToken& tok)
    {
        MediaEvent e;
        e.kind = MediaEvent::Kind::ClipEncoded;
        e.requestId = soundUid;
        e.path = outPath;
        std::string err;
        if (!buffer) e.error = "no clip buffer";
        else if (outPath.empty()) e.error = "no output path";
        else if (!ClipEncoder::encode(outPath, *buffer, &err)) e.error = err.empty() ? "cannot write " + outPath : err;
        e.ok = e.error.empty();
        active_--;
        if (!tok.cancelled()) events_.push(std::move(e));
    });
}

CancelToken MediaService::requestImage(const std::string& path, uint64_t tag, int maxEdge)
{
    active_++;
    return pool_.post([this, path, tag, maxEdge](const CancelToken& tok)
    {
        MediaEvent e;
        e.kind = MediaEvent::Kind::Image;
        e.requestId = tag;
        e.path = path;
        auto img = std::make_shared<RgbaImage>();
        std::string err;
        if (ImageDecoder::decodeFile(path, *img, maxEdge, &err)) { e.image = img; e.ok = true; }
        else e.error = err;
        active_--;
        if (!tok.cancelled()) events_.push(std::move(e));
    });
}

CancelToken MediaService::requestVideoFrames(const std::string& path, uint64_t tag, int count, int maxEdge)
{
    active_++;
    return pool_.post([this, path, tag, count, maxEdge](const CancelToken& tok)
    {
        MediaEvent e;
        e.kind = MediaEvent::Kind::Frames;
        e.requestId = tag;
        e.path = path;
        auto frames = std::make_shared<std::vector<VideoFrame>>();
        std::string err;
        if (VideoFrames::extract(path, count, maxEdge, *frames, &err, &tok)) { e.frames = frames; e.ok = true; }
        else e.error = err;
        active_--;
        if (!tok.cancelled()) events_.push(std::move(e));
    });
}

size_t MediaService::drain(const std::function<void(const MediaEvent&)>& fn)
{
    return events_.drain([&](MediaEvent& e) { fn(e); });
}

} // namespace evobox
