#include "media/MediaLibrary.h"
#include <algorithm>
#include <vector>

namespace evobox
{

organic::AudioBuffer InMemoryMediaSource::read(double t0, double t1) const
{
    organic::AudioBuffer out;
    if (!asset_ || !asset_->buffer) return out;
    const organic::AudioBuffer& b = *asset_->buffer;
    out.sampleRate = b.sampleRate;
    out.channels = b.channels;
    if (t1 < t0) std::swap(t0, t1);
    size_t f0 = std::min((size_t)b.frames(), (size_t)std::max(0.0, t0 * b.sampleRate));
    size_t f1 = std::min((size_t)b.frames(), (size_t)std::max(0.0, t1 * b.sampleRate));
    size_t ch = (size_t)std::max(1, b.channels);
    out.samples.assign(b.samples.begin() + (long)(f0 * ch), b.samples.begin() + (long)(f1 * ch));
    return out;
}

void MediaLibrary::put(std::shared_ptr<const MediaAsset> asset)
{
    if (!asset) return;
    const std::string key = asset->ref.path;
    strong_[key] = Entry{ asset, ++clock_ };
    weak_[key] = asset;
}

std::shared_ptr<const MediaAsset> MediaLibrary::find(const std::string& path) const
{
    auto it = strong_.find(path);
    if (it != strong_.end()) return it->second.asset;
    auto w = weak_.find(path);
    if (w != weak_.end()) return w->second.lock();
    return nullptr;
}

std::shared_ptr<const MediaAsset> MediaLibrary::findByHash(const std::string& hash) const
{
    if (hash.empty()) return nullptr;
    for (auto& [k, e] : strong_) if (e.asset->ref.contentHash == hash) return e.asset;
    for (auto& [k, w] : weak_) if (auto a = w.lock()) if (a->ref.contentHash == hash) return a;
    return nullptr;
}

void MediaLibrary::remove(const std::string& path)
{
    strong_.erase(path);
    weak_.erase(path);
}

void MediaLibrary::clear()
{
    strong_.clear();
    weak_.clear();
}

size_t MediaLibrary::residentBytes() const
{
    size_t n = 0;
    for (auto& [k, e] : strong_)
        if (e.asset->buffer) n += e.asset->buffer->samples.size() * sizeof(float);
    return n;
}

void MediaLibrary::evict(size_t keepBytes)
{
    if (residentBytes() <= keepBytes) return;
    std::vector<std::pair<uint64_t, std::string>> order;
    for (auto& [k, e] : strong_) order.push_back({ e.lastUse, k });
    std::sort(order.begin(), order.end());
    for (auto& [use, key] : order)
    {
        if (residentBytes() <= keepBytes) break;
        strong_.erase(key); // the weak entry keeps it reachable while in use
    }
}

std::shared_ptr<IMediaSource> MediaLibrary::source(const std::string& path) const
{
    auto a = find(path);
    if (!a) return nullptr;
    return std::make_shared<InMemoryMediaSource>(a);
}

void MediaLibrary::touch(const std::string& path)
{
    auto it = strong_.find(path);
    if (it != strong_.end()) it->second.lastUse = ++clock_;
}

} // namespace evobox
