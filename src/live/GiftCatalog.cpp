#include "live/GiftCatalog.h"
#include "util/Localize.h"
#include <algorithm>
#include <chrono>
#include <fstream>
#include "json.hpp"

namespace evobox
{

using json = nlohmann::json;

GiftInfo* GiftCatalog::find(int64_t id)
{
    auto it = gifts.find(id);
    return it == gifts.end() ? nullptr : &it->second;
}

const GiftInfo* GiftCatalog::find(int64_t id) const
{
    auto it = gifts.find(id);
    return it == gifts.end() ? nullptr : &it->second;
}

static long long nowUnix()
{
    return (long long)std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

int GiftCatalog::merge(const std::vector<GiftInfo>& snapshot, const std::string& user)
{
    int added = 0;
    for (const GiftInfo& g : snapshot)
    {
        if (g.id == 0) continue;
        auto it = gifts.find(g.id);
        if (it == gifts.end())
        {
            gifts[g.id] = g;
            added++;
        }
        else
        {
            GiftInfo& d = it->second;
            if (!g.name.empty()) d.name = g.name;
            if (g.diamondCount > 0) d.diamondCount = g.diamondCount;
            if (!g.describe.empty()) d.describe = g.describe;
            if (g.type) d.type = g.type;
            if (!g.iconUrl.empty() && g.iconUrl != d.iconUrl)
            {
                d.iconUrl = g.iconUrl;
                d.iconState = IconState::NotRequested; // re-fetch the new artwork
                d.iconFile.clear();
            }
        }
    }
    if (!user.empty()) roomUser = user;
    lastUpdateUnix = nowUnix();
    revision++;
    return added;
}

bool GiftCatalog::mergeFromEvent(const LiveEvent& e)
{
    if (e.type != LiveEventType::Gift || e.giftId == 0) return false;
    auto it = gifts.find(e.giftId);
    if (it == gifts.end())
    {
        GiftInfo g;
        g.id = e.giftId;
        g.name = e.giftName.empty() ? (std::string(LTR("gift.fallbackNamePrefix", "Gift ")) + std::to_string(e.giftId)) : e.giftName;
        g.diamondCount = e.diamondCount;
        g.type = e.giftType;
        g.iconUrl = e.giftIconUrl;
        gifts[g.id] = g;
        revision++;
        return true;
    }
    GiftInfo& d = it->second;
    bool changed = false;
    if (d.name.empty() && !e.giftName.empty()) { d.name = e.giftName; changed = true; }
    if (d.diamondCount == 0 && e.diamondCount > 0) { d.diamondCount = e.diamondCount; changed = true; }
    if (d.type == 0 && e.giftType) { d.type = e.giftType; changed = true; }
    if (d.iconUrl.empty() && !e.giftIconUrl.empty()) { d.iconUrl = e.giftIconUrl; changed = true; }
    if (changed) revision++;
    return changed;
}

void GiftCatalog::noteReceived(int64_t id, double now, int units)
{
    // `units` = gifts this message adds (0 for an end-of-combo summary: only the pulse time moves)
    if (GiftInfo* g = find(id))
    {
        g->receivedCount += std::max(0, units);
        g->lastReceivedTime = now;
    }
}

void GiftCatalog::resetSessionCounters()
{
    for (auto& [id, g] : gifts) { g.receivedCount = 0; g.lastReceivedTime = -1.0; }
}

bool GiftCatalog::load(const std::string& path)
{
    std::ifstream f(path);
    if (!f.is_open()) return false;
    json j;
    try { f >> j; }
    catch (...) { return false; }
    if (!j.is_object()) return false;
    roomUser = j.value("roomUser", "");
    lastUpdateUnix = j.value("lastUpdate", 0LL);
    if (j.contains("gifts") && j["gifts"].is_array())
    {
        for (auto& gj : j["gifts"])
        {
            GiftInfo g;
            g.id = gj.value("id", (int64_t)0);
            if (g.id == 0) continue;
            g.name = gj.value("name", "");
            g.diamondCount = gj.value("diamonds", 0);
            g.describe = gj.value("describe", "");
            g.type = gj.value("type", 0);
            g.iconUrl = gj.value("iconUrl", "");
            gifts[g.id] = g;
        }
    }
    revision++;
    return true;
}

bool GiftCatalog::save(const std::string& path) const
{
    json j;
    j["roomUser"] = roomUser;
    j["lastUpdate"] = lastUpdateUnix;
    json arr = json::array();
    for (auto& [id, g] : gifts)
    {
        json gj;
        gj["id"] = g.id;
        gj["name"] = g.name;
        gj["diamonds"] = g.diamondCount;
        gj["describe"] = g.describe;
        gj["type"] = g.type;
        gj["iconUrl"] = g.iconUrl;
        arr.push_back(gj);
    }
    j["gifts"] = arr;
    std::ofstream f(path);
    if (!f.is_open()) return false;
    f << j.dump(1);
    return f.good();
}

} // namespace evobox
