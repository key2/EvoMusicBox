// MediaLibrary.h — main-thread registry of decoded source assets keyed by path / content hash.
// Dedupes tiles that share a file, keeps sources alive while they are being edited, evicts on
// memory pressure.
#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include "media/IMediaSource.h"
#include "model/MediaAsset.h"

namespace evobox
{

class MediaLibrary
{
public:
    // strong cache: assets stay until evicted; weak: resurrect while someone still holds them
    void put(std::shared_ptr<const MediaAsset> asset);
    std::shared_ptr<const MediaAsset> find(const std::string& path) const;
    std::shared_ptr<const MediaAsset> findByHash(const std::string& contentHash) const;
    void remove(const std::string& path);
    void clear();

    // Drops strong references beyond the budget (least recently used first).
    void evict(size_t keepBytes);
    size_t residentBytes() const;
    size_t count() const { return strong_.size(); }

    std::shared_ptr<IMediaSource> source(const std::string& path) const;

    // Contents may move: remember the "last opened" paths for relink prompts.
    void touch(const std::string& path);

    InMemoryMediaSource sourceFor(const std::shared_ptr<const MediaAsset>& a) const { return InMemoryMediaSource(a); }

private:
    struct Entry
    {
        std::shared_ptr<const MediaAsset> asset;
        uint64_t lastUse = 0;
    };
    std::unordered_map<std::string, Entry> strong_;
    std::unordered_map<std::string, std::weak_ptr<const MediaAsset>> weak_;
    uint64_t clock_ = 0;
};

} // namespace evobox
