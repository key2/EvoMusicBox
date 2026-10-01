#include "model/RoomEventAction.h"
#include "model/InspectorHooks.h"

namespace evobox
{

const char* roomEventKindName(RoomEventKind k)
{
    switch (k)
    {
    case RoomEventKind::Like:      return "Like";
    case RoomEventKind::Follow:    return "Follow";
    case RoomEventKind::Share:     return "Share";
    case RoomEventKind::Subscribe: return "Subscribe";
    case RoomEventKind::Join:      return "Join";
    default: return "?";
    }
}

const char* roomEventKindKey(RoomEventKind k)
{
    switch (k)
    {
    case RoomEventKind::Like:      return "like";
    case RoomEventKind::Follow:    return "follow";
    case RoomEventKind::Share:     return "share";
    case RoomEventKind::Subscribe: return "subscribe";
    case RoomEventKind::Join:      return "join";
    default: return "like";
    }
}

bool roomEventKindFromKey(const std::string& key, RoomEventKind& out)
{
    for (int i = 0; i < (int)RoomEventKind::Count; i++)
        if (key == roomEventKindKey((RoomEventKind)i)) { out = (RoomEventKind)i; return true; }
    return false;
}

RoomEventAction::RoomEventAction(RoomEventKind k)
    : organic::BaseItem(kType, roomEventKindName(k)), oscActions(this)
{
    renamable = false;
    userCanRemove = false;
    userCanDuplicate = false;
    thresholdP  = addInt("Threshold", 10, 1, 100000, "Like: fire every N likes");
    cooldownMsP = addInt("Cooldown", 0, 0, 600000, "Minimum time between two firings (ms)");
    cooldownMsP->unit = "ms";
    hideParam(colorP);

    oscActions.addPhase("On event", Anchor::Start, 0);
    oscActions.addPhase("Stop",     Anchor::Timer, 3000);
    setKind(k);
}

void RoomEventAction::setKind(RoomEventKind k)
{
    kind = k;
    setNiceName(roomEventKindName(k));
    if (k == RoomEventKind::Join && cooldownMsP->intValue() == 0)
    {
        cooldownMsP->setValue(2000, false);
        cooldownMsP->defaultValue = 2000;
    }
    thresholdP->hideInEditor = (k != RoomEventKind::Like);
}

int RoomEventAction::stopTimerMs() const
{
    if (OscPhase* p = oscActions.phase(Anchor::Timer)) return p->delayMs();
    return 3000;
}

void RoomEventAction::setSoundUidUndoable(Uid uid)
{
    setFieldUndoable<Uid>(this, soundUid, uid, "Set room event sound");
}

void RoomEventAction::inspectorGui()
{
    if (InspectorHooks::roomEvent) InspectorHooks::roomEvent(*this);
    else organic::Container::inspectorGui();
}

json RoomEventAction::save() const
{
    json j = BaseItem::save();
    j["kind"] = roomEventKindKey(kind);
    j["soundUid"] = soundUid;
    j["actions"] = oscActions.save();
    return j;
}

void RoomEventAction::load(const json& j)
{
    BaseItem::load(j);
    RoomEventKind k;
    if (roomEventKindFromKey(jget<std::string>(j, "kind", ""), k)) setKind(k);
    soundUid = jget<Uid>(j, "soundUid", 0);
    if (j.contains("actions")) oscActions.load(j["actions"]);
}

// ================================================================ RoomEventActionManager
RoomEventActionManager::RoomEventActionManager(organic::Container* parent)
    : organic::BaseManager("Room Events", parent)
{
    userCanAdd = false;
    addDef("Room event", RoomEventAction::kType, [] { return std::make_unique<RoomEventAction>(); });
    ensureFixedItems();
}

RoomEventAction* RoomEventActionManager::find(RoomEventKind k) const
{
    for (auto& i : items)
        if (static_cast<RoomEventAction*>(i.get())->kind == k) return static_cast<RoomEventAction*>(i.get());
    return nullptr;
}

std::vector<RoomEventAction*> RoomEventActionManager::actions() const
{
    std::vector<RoomEventAction*> out;
    for (auto& i : items) out.push_back(static_cast<RoomEventAction*>(i.get()));
    return out;
}

void RoomEventActionManager::ensureFixedItems()
{
    for (int k = 0; k < (int)RoomEventKind::Count; k++)
        if (!find((RoomEventKind)k))
            addItem(std::make_unique<RoomEventAction>((RoomEventKind)k));
    // keep kind order
    std::stable_sort(items.begin(), items.end(), [](const auto& a, const auto& b)
    {
        return (int)static_cast<RoomEventAction*>(a.get())->kind < (int)static_cast<RoomEventAction*>(b.get())->kind;
    });
}

void RoomEventActionManager::clearSoundReferences(Uid soundUid)
{
    bool any = false;
    for (auto& i : items)
    {
        auto* a = static_cast<RoomEventAction*>(i.get());
        if (a->soundUid == soundUid) { a->soundUid = 0; any = true; }
    }
    if (any) notifyStructureChanged(this);
}

void RoomEventActionManager::onItemsChanged() { notifyStructureChanged(this); }

void RoomEventActionManager::load(const json& j)
{
    BaseManager::load(j);
    // drop duplicates of a kind (corrupt files), then complete the set
    std::vector<Uid> dups;
    for (int k = 0; k < (int)RoomEventKind::Count; k++)
    {
        bool seen = false;
        for (auto& i : items)
        {
            auto* a = static_cast<RoomEventAction*>(i.get());
            if (a->kind != (RoomEventKind)k) continue;
            if (seen) dups.push_back(a->uid);
            seen = true;
        }
    }
    for (Uid u : dups) removeItem(u);
    ensureFixedItems();
}

} // namespace evobox
