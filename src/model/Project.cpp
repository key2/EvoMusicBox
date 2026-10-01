#include "model/Project.h"
#include <algorithm>
#include <filesystem>

namespace evobox
{

Project::Project()
    : organic::Container("Project"),
      categories(this), sounds(this), giftActions(this), roomEvents(this), oscTargets(this), settings(this)
{
    renamable = false;
    organic::registerRoot(this);
    resetToDefaults();
}

Project::~Project()
{
    organic::unregisterRoot(this);
}

std::string Project::title() const
{
    if (!archivePath.empty()) return std::filesystem::path(archivePath).stem().string();
    if (bundleDir.empty()) return "Untitled";
    return std::filesystem::path(bundleDir).stem().string();
}

void Project::resetToDefaults()
{
    loading_ = true;
    organic::UndoManager::get().clear();
    // BaseManager::load(json) with an empty items array clears the items; the managers then
    // re-create their defaults (categories, Localhost, the five room events) numbered from 1.
    for (organic::BaseManager* m : { (organic::BaseManager*)&sounds, (organic::BaseManager*)&giftActions,
                                     (organic::BaseManager*)&categories, (organic::BaseManager*)&oscTargets,
                                     (organic::BaseManager*)&roomEvents })
    {
        m->items.clear();
        m->nextUid = 1;
        m->load(json{ { "items", json::array() } });
    }
    for (auto& p : settings.params) p->resetToDefault(false);
    bundleDir.clear();
    archivePath.clear();
    formatVersion = kFormatVersion;
    loading_ = false;
    revision = 0;
    savedRevision = 0;
}

// ---------------------------------------------------------------- cross references
std::vector<Triggerable*> Project::triggerables()
{
    std::vector<Triggerable*> out;
    for (auto* s : sounds.sounds()) out.push_back(s);
    for (auto* g : giftActions.actions()) out.push_back(g);
    for (auto* r : roomEvents.actions()) out.push_back(r);
    return out;
}

Triggerable* Project::findTriggerable(Uid triggerableUid)
{
    // Identities are kind-tagged (Triggerable.h): never look a gift's uid up among the sounds.
    TriggerableKind kind;
    if (!triggerableKindOf(triggerableUid, kind)) return nullptr;
    Uid uid = triggerableItemUid(triggerableUid);
    switch (kind)
    {
    case TriggerableKind::Sound:     return sounds.find(uid);
    case TriggerableKind::Gift:      return giftActions.findByUid(uid);
    case TriggerableKind::RoomEvent: return static_cast<RoomEventAction*>(roomEvents.findItem(uid));
    }
    return nullptr;
}

int Project::retargetAll(Uid from, Uid to)
{
    int n = 0;
    for (auto* t : triggerables()) n += t->actions().retarget(from, to);
    return n;
}

void Project::deleteSoundsUndoable(const std::vector<Sound*>& list)
{
    struct SoundRec { json data; };
    struct RefRec { Uid ownerUid; bool gift; Uid soundUid; };
    std::vector<json> recs;
    std::vector<RefRec> refs;
    for (Sound* s : list)
    {
        if (!s || s->manager != &sounds) continue;
        json j = s->save();
        j["_index"] = sounds.indexOf(s);
        recs.push_back(j);
        for (auto* g : giftActions.actions()) if (g->soundUid == s->uid) refs.push_back({ g->uid, true, s->uid });
        for (auto* r : roomEvents.actions()) if (r->soundUid == s->uid) refs.push_back({ r->uid, false, s->uid });
    }
    if (recs.empty()) return;
    std::sort(recs.begin(), recs.end(), [](const json& a, const json& b) { return a.value("_index", 0) < b.value("_index", 0); });

    Project* self = this;
    auto doFn = [self, recs, refs]
    {
        for (auto& r : refs)
        {
            if (r.gift) { if (auto* g = self->giftActions.findByUid(r.ownerUid)) g->soundUid = 0; }
            else if (auto* re = static_cast<RoomEventAction*>(self->roomEvents.findItem(r.ownerUid))) re->soundUid = 0;
        }
        for (auto& r : recs) self->sounds.removeItem(r.value("uid", (Uid)0));
        self->touch();
    };
    auto undoFn = [self, recs, refs]
    {
        for (auto& r : recs) self->sounds.addItemFromJson(r, r.value("_index", -1));
        for (auto& r : refs)
        {
            if (r.gift) { if (auto* g = self->giftActions.findByUid(r.ownerUid)) g->soundUid = r.soundUid; }
            else if (auto* re = static_cast<RoomEventAction*>(self->roomEvents.findItem(r.ownerUid))) re->soundUid = r.soundUid;
        }
        self->touch();
    };
    organic::UndoManager::get().perform(recs.size() == 1 ? "Delete sound" : "Delete sounds", doFn, undoFn, { this, &sounds });
}

bool Project::categoryDeletable(const Category* c) const
{
    if (!c) return false;
    if (categories.items.size() <= 1) return false;
    return true; // builtin categories may be deleted too; their sounds move to Custom
}

void Project::deleteCategoryUndoable(Category* c)
{
    if (!categoryDeletable(c)) return;
    Category* fallback = categories.customCategory();
    if (fallback == c)
    {
        for (auto* other : categories.categories()) if (other != c) { fallback = other; break; }
    }
    Uid catUid = c->uid, fbUid = fallback ? fallback->uid : 0;
    json data = c->save();
    data["_index"] = categories.indexOf(c);
    std::vector<Uid> moved;
    for (auto* s : sounds.sounds()) if (s->categoryUid == catUid) moved.push_back(s->uid);

    Project* self = this;
    organic::UndoManager::get().perform("Delete category",
        [self, catUid, fbUid, moved]
        {
            for (Uid u : moved) if (Sound* s = self->sounds.find(u)) s->categoryUid = fbUid;
            self->categories.removeItem(catUid);
            self->touch();
        },
        [self, data, catUid, moved]
        {
            self->categories.addItemFromJson(data, data.value("_index", -1));
            for (Uid u : moved) if (Sound* s = self->sounds.find(u)) s->categoryUid = catUid;
            self->touch();
        },
        { this, &categories, &sounds });
}

void Project::deleteTargetUndoable(OscTarget* t)
{
    Project* self = this;
    oscTargets.removeTargetUndoable(t, [self](Uid from, Uid to) { return self->retargetAll(from, to); });
}

// ---------------------------------------------------------------- persistence
json Project::save() const
{
    json j;
    j["app"] = "EvoMusicBox";
    j["formatVersion"] = kFormatVersion;
    j["categories"] = categories.save();
    j["sounds"] = sounds.save();
    j["giftActions"] = giftActions.save();
    j["roomEvents"] = roomEvents.save();
    j["oscTargets"] = oscTargets.save();
    j["settings"] = settings.save();
    return j;
}

void Project::migrate(json& j) const
{
    int v = jget<int>(j, "formatVersion", 1);
    // ordered migration steps: each lambda upgrades v -> v+1
    static const std::vector<std::function<void(json&)>> steps = {
        // v1 -> v2: PlaybackPolicy gained "Music stops previous music" as index 0 (the new
        // default); v1 stored Overlap = 0 / StopOthers = 1. Explicit "Stop others" is kept; the
        // v1 default (Overlap, index 0) becomes the new default behaviour on purpose.
        [](json& d)
        {
            if (!d.contains("settings") || !d["settings"].is_object()) return;
            json& s = d["settings"];
            if (!s.contains("params") || !s["params"].is_object()) return;
            json& p = s["params"];
            if (p.contains("playbackPolicy") && p["playbackPolicy"].is_number_integer())
            {
                int old = p["playbackPolicy"].get<int>();
                p["playbackPolicy"] = old == 1 ? 2 : 0;
            }
        },
    };
    for (int i = std::max(0, v - 1); i < (int)steps.size(); i++) steps[(size_t)i](j);
    j["formatVersion"] = kFormatVersion;
}

// organic's addItemFromJson allocates a uid before loading the persisted one, so nextUid drifts
// upward on every load; re-derive it (max item uid + 1, never below the saved value) so that a
// re-save is byte-identical.
static void loadManager(organic::BaseManager& m, const json& parent, const char* key)
{
    json empty{ { "items", json::array() } };
    const json& j = (parent.contains(key) && parent[key].is_object()) ? parent[key] : empty;
    m.load(j);
    uint64_t next = 1;
    for (auto& it : m.items) next = std::max(next, it->uid + 1);
    m.nextUid = std::max(next, jget<uint64_t>(j, "nextUid", 1));
}

void Project::load(const json& jin)
{
    json j = jin;
    migrate(j);
    loading_ = true;
    organic::UndoManager::get().clear();
    loadManager(categories, j, "categories");
    loadManager(oscTargets, j, "oscTargets");
    loadManager(sounds, j, "sounds");
    loadManager(giftActions, j, "giftActions");
    loadManager(roomEvents, j, "roomEvents");
    if (j.contains("settings")) settings.load(j["settings"]);
    // repair dangling category references
    Category* fb = categories.customCategory();
    for (auto* s : sounds.sounds())
        if (!categories.find(s->categoryUid) && fb) s->categoryUid = fb->uid;
    // dangling sound references
    for (auto* g : giftActions.actions()) if (g->soundUid && !sounds.find(g->soundUid)) g->soundUid = 0;
    for (auto* r : roomEvents.actions()) if (r->soundUid && !sounds.find(r->soundUid)) r->soundUid = 0;
    formatVersion = kFormatVersion;
    loading_ = false;
    revision = 0;
    savedRevision = 0;
    if (onAnyChange) onAnyChange();
}

// ---------------------------------------------------------------- notifications
void Project::onStructureChanged(organic::Container*)
{
    touch();
    if (!loading_ && onAnyChange) onAnyChange();
}

void Project::onChildParamChanged(organic::Container*, organic::Parameter*)
{
    touch();
    if (!loading_ && onAnyChange) onAnyChange();
}

void Project::onParamChanged(organic::Parameter*)
{
    touch();
    if (!loading_ && onAnyChange) onAnyChange();
}

} // namespace evobox
