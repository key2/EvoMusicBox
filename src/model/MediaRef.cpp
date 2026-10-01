#include "model/MediaRef.h"
#include <filesystem>

namespace fs = std::filesystem;

namespace evobox
{

std::string MediaRef::fileName() const
{
    if (!path.empty()) return fs::path(path).filename().string();
    if (!relPath.empty()) return fs::path(relPath).filename().string();
    return "";
}

std::string MediaRef::resolve(const std::string& bundleDir) const
{
    std::error_code ec;
    if (!relPath.empty() && !bundleDir.empty())
    {
        fs::path p = fs::path(bundleDir) / relPath;
        if (fs::exists(p, ec)) return p.string();
    }
    if (!path.empty() && fs::exists(path, ec)) return path;
    return "";
}

json MediaRef::toJson() const
{
    json j;
    j["path"] = path;
    j["relPath"] = relPath;
    j["sizeBytes"] = sizeBytes;
    j["contentHash"] = contentHash;
    j["durationSec"] = durationSec;
    j["sampleRate"] = sampleRate;
    j["channels"] = channels;
    j["container"] = container;
    j["codec"] = codec;
    j["hasVideo"] = hasVideo;
    return j;
}

MediaRef MediaRef::fromJson(const json& j)
{
    MediaRef r;
    if (!j.is_object()) return r;
    r.path = jget<std::string>(j, "path", "");
    r.relPath = jget<std::string>(j, "relPath", "");
    r.sizeBytes = jget<uint64_t>(j, "sizeBytes", 0);
    r.contentHash = jget<std::string>(j, "contentHash", "");
    r.durationSec = jget<double>(j, "durationSec", 0.0);
    r.sampleRate = jget<int>(j, "sampleRate", 0);
    r.channels = jget<int>(j, "channels", 0);
    r.container = jget<std::string>(j, "container", "");
    r.codec = jget<std::string>(j, "codec", "");
    r.hasVideo = jget<bool>(j, "hasVideo", false);
    return r;
}

} // namespace evobox
