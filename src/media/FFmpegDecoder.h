// FFmpegDecoder.h — probe / decode any media container to float PCM (libavformat/libavcodec/
// libswresample, FFmpeg >= 6 API). Contexts are per call (never shared across threads).
// v1 implements the in-memory path (decodeAll) plus decodeRange; streaming peaks for very long
// media is the documented upgrade behind IMediaSource.
#pragma once

#include <atomic>
#include <functional>
#include <string>
#include "OrganicAudio.h"
#include "model/MediaRef.h"
#include "util/WorkerPool.h"

namespace evobox
{

struct DecodeBudget
{
    double maxDurationSec = 20 * 60;               // 20 min
    uint64_t maxBytes = 1024ull * 1024ull * 1024ull; // 1 GB of float PCM
};

class FFmpegDecoder
{
public:
    // Fills duration, sample rate, channels, codec, container, hasVideo. No decoding.
    static bool probe(const std::string& path, MediaRef& out, std::string* err = nullptr);

    // Decodes the whole file (audio stream) to interleaved float at the source rate.
    // Channels: mono -> 1, stereo -> 2, >2 -> downmixed to stereo.
    static bool decodeAll(const std::string& path, organic::AudioBuffer& out, std::string* err = nullptr,
                          const CancelToken* cancel = nullptr, std::atomic<float>* progress = nullptr,
                          const DecodeBudget& budget = DecodeBudget());

    // Decodes [t0, t1] seconds (seek + decode + trim).
    static bool decodeRange(const std::string& path, double t0, double t1, organic::AudioBuffer& out,
                            std::string* err = nullptr, const CancelToken* cancel = nullptr);

    // organic::AudioCache::decodeFallback-compatible signature.
    static bool decodeFallback(const std::string& path, organic::AudioBuffer& out, std::string* err);

    // Routes libav log messages (warning and below) to the given sink; call once at startup.
    // The sink runs on whatever thread libav is used from -> queue, never touch the model.
    static void installLogSink(std::function<void(int level, const std::string& msg)> sink);
    static std::string versionString();
};

} // namespace evobox
