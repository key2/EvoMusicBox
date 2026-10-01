// MediaRef.h — reference to a source media file plus the probe facts needed to display and
// relink it. Paths are stored absolute AND bundle-relative (relPath wins on load).
#pragma once

#include <cstdint>
#include <string>
#include "model/ModelCommon.h"

namespace evobox
{

struct MediaRef
{
    std::string path;        // absolute path as imported
    std::string relPath;     // bundle-relative ("media/foo.mp4") when copied into the project
    uint64_t    sizeBytes = 0;
    std::string contentHash; // size + first/last MB (util/Hash)
    double      durationSec = 0;
    int         sampleRate = 0;
    int         channels = 0;
    std::string container;   // "mov,mp4,m4a,..." / "wav"
    std::string codec;       // "aac" / "pcm_s16le"
    bool        hasVideo = false;

    bool empty() const { return path.empty() && relPath.empty(); }
    std::string fileName() const;
    // Best existing path: relPath resolved against bundleDir, then absolute. "" when neither exists.
    std::string resolve(const std::string& bundleDir) const;

    json toJson() const;
    static MediaRef fromJson(const json& j);
};

} // namespace evobox
