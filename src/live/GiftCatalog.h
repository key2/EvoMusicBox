// GiftCatalog.h — app-level cache (NOT project data) of the room's gift list, merged from every
// Connect snapshot and from gift events. Persisted to <cache>/gift-catalog.json. The gallery
// shows the catalog; the project stores only actions.
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>
#include "live/LiveEvent.h"

namespace evobox
{

struct TextureHandle
{
    unsigned int glId = 0;
    int width = 0;
    int height = 0;
    bool valid() const { return glId != 0; }
};

enum class IconState { NotRequested, Fetching, Ready, Failed };

struct GiftInfo
{
    int64_t id = 0;
    std::string name;
    int diamondCount = 0;
    std::string describe;
    int type = 0;              // 1 = streakable
    std::string iconUrl;       // first URL of TikTok's icon.url_list (often WebP)

    // runtime (never serialized)
    IconState iconState = IconState::NotRequested;
    TextureHandle icon;
    std::string iconFile;      // cached file on disk
    int receivedCount = 0;     // this session
    double lastReceivedTime = -1.0;

    bool streakable() const { return type == 1; }
};

class GiftCatalog
{
public:
    std::map<int64_t, GiftInfo> gifts;
    std::string roomUser;      // @user the catalog came from
    long long lastUpdateUnix = 0; // seconds since epoch (0 = never)
    uint32_t revision = 0;     // bumped on merge -> UI can re-sort lazily

    GiftInfo* find(int64_t id);
    const GiftInfo* find(int64_t id) const;
    size_t size() const { return gifts.size(); }
    bool empty() const { return gifts.empty(); }

    // Merges a snapshot (name/diamonds/describe/type/iconUrl, newest wins, runtime kept).
    int merge(const std::vector<GiftInfo>& snapshot, const std::string& roomUser);
    // Adds/updates a gift from an event (gifts absent from the list, e.g. basic gifts).
    bool mergeFromEvent(const LiveEvent& e);
    void noteReceived(int64_t id, double now, int units); // units = gifts this message adds (0 = summary)
    void resetSessionCounters();

    bool load(const std::string& path);
    bool save(const std::string& path) const;
};

} // namespace evobox
