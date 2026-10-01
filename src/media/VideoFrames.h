// VideoFrames.h — extracts N evenly spaced frames of a movie as small RGBA thumbnails
// (libavformat seek + libavcodec decode + libswscale), used as sticker candidates when a video
// is imported as a sound. Worker-thread code: one set of contexts per call.
#pragma once

#include <string>
#include <vector>
#include "media/ImageDecoder.h"
#include "util/WorkerPool.h"

namespace evobox
{

struct VideoFrame
{
    double time = 0;   // seconds in the source
    RgbaImage image;
};

class VideoFrames
{
public:
    // Picks `count` times spread over the duration (skipping the very first/last 5 %), decodes the
    // nearest frame at or after each and scales it so its longer edge is `maxEdge` px. Returns
    // false only when nothing could be decoded (a partially filled result still returns true).
    static bool extract(const std::string& path, int count, int maxEdge, std::vector<VideoFrame>& out,
                        std::string* err = nullptr, const CancelToken* cancel = nullptr);
};

} // namespace evobox
