#include "model/GiftAction.h"
#include "util/Localize.h"
#include "model/InspectorHooks.h"

namespace evobox
{

GiftAction::GiftAction() : organic::BaseItem(kType, "Gift"), oscActions(this)
{
    renamable = false;
    streakModeP  = addEnum("Streak mode", { "Once at streak end", "Every event", "Per repeat" }, 0,
                           "Streakable gifts (combos):\n"
                           "Once at streak end: fire once when the combo ends (~3 s after the last tap).\n"
                           "Every event: fire immediately on every tap (the end-of-combo message never re-fires).\n"
                           "Per repeat: fire once per tap when the combo ends");
    retriggerP   = addEnum("Retrigger", { "Restart", "Ignore", "Queue" }, 0,
                           "Same gift while the Stop timer is pending: restart the timer, ignore, or queue");
    cooldownMsP  = addInt("Cooldown", 0, 0, 600000, "Minimum time between two firings (ms)");
    minDiamondsP = addInt("Min diamonds", 0, 0, 1000000, "Ignore gifts worth less than this (diamonds x repeat)");
    cooldownMsP->unit = "ms";
    hideParam(colorP);

    oscActions.addPhase("On gift", Anchor::Start, 0);
    oscActions.addPhase("Stop",    Anchor::Timer, kDefaultStopMs); // "when to send the OSC to stop"
}

int GiftAction::stopTimerMs() const
{
    if (OscPhase* p = oscActions.phase(Anchor::Timer)) return p->delayMs();
    return kDefaultStopMs;
}

void GiftAction::setSoundUidUndoable(Uid uid)
{
    setFieldUndoable<Uid>(this, soundUid, uid, LTR("undo.setGiftSound", "Set gift sound"));
}

void GiftAction::seedFromCatalog(int64_t id, const std::string& name, int diamonds, const std::string& iconUrl, int type)
{
    giftId = id;
    if (!name.empty()) cachedName = name;
    if (diamonds > 0) cachedDiamonds = diamonds;
    if (!iconUrl.empty()) cachedIconUrl = iconUrl;
    if (type) cachedType = type;
    setNiceName(cachedName.empty() ? (std::string(LTR("gift.fallbackNamePrefix", "Gift ")) + std::to_string(id)) : cachedName);
}

Uid GiftAction::triggerableUid() const
{
    // transient actions have uid 0: derive a stable synthetic id from the gift id
    if (uid != 0) return makeTriggerableUid(TriggerableKind::Gift, uid);
    return makeTransientTriggerableUid((Uid)giftId);
}

void GiftAction::inspectorGui()
{
    if (InspectorHooks::gift) InspectorHooks::gift(*this);
    else organic::Container::inspectorGui();
}

json GiftAction::save() const
{
    json j = BaseItem::save();
    j["giftId"] = giftId;
    j["cachedName"] = cachedName;
    j["cachedDiamonds"] = cachedDiamonds;
    j["cachedIconUrl"] = cachedIconUrl;
    j["cachedType"] = cachedType;
    j["soundUid"] = soundUid;
    j["actions"] = oscActions.save();
    return j;
}

void GiftAction::load(const json& j)
{
    BaseItem::load(j);
    giftId = jget<int64_t>(j, "giftId", 0);
    cachedName = jget<std::string>(j, "cachedName", "");
    cachedDiamonds = jget<int>(j, "cachedDiamonds", 0);
    cachedIconUrl = jget<std::string>(j, "cachedIconUrl", "");
    cachedType = jget<int>(j, "cachedType", 0);
    soundUid = jget<Uid>(j, "soundUid", 0);
    if (j.contains("actions")) oscActions.load(j["actions"]);
}

// ================================================================ GiftActionManager
GiftActionManager::GiftActionManager(organic::Container* parent)
    : organic::BaseManager("Gift Actions", parent)
{
    addDef("Gift action", GiftAction::kType, [] { return std::make_unique<GiftAction>(); });
    userCanAdd = false; // created through the gallery only
}

GiftAction* GiftActionManager::find(int64_t giftId) const
{
    for (auto& i : items)
        if (static_cast<GiftAction*>(i.get())->giftId == giftId) return static_cast<GiftAction*>(i.get());
    return nullptr;
}

std::vector<GiftAction*> GiftActionManager::actions() const
{
    std::vector<GiftAction*> out;
    for (auto& i : items) out.push_back(static_cast<GiftAction*>(i.get()));
    return out;
}

GiftAction* GiftActionManager::addFromTransientUndoable(const GiftAction& transient)
{
    if (GiftAction* existing = find(transient.giftId)) return existing;
    json data = transient.save();
    data["uid"] = 0;
    data.erase("_index");
    GiftAction* raw = static_cast<GiftAction*>(addItemFromJson(data));
    if (!raw) return nullptr;
    raw->rt.transient = false;
    json saved = raw->save();
    saved["_index"] = indexOf(raw);
    Uid uid = raw->uid;
    GiftActionManager* self = this;
    organic::UndoManager::get().pushDone(LTR("undo.configureGift", "Configure gift"),
        [self, saved] { self->addItemFromJson(saved); },
        [self, uid]   { self->removeItem(uid); },
        { self });
    return raw;
}

void GiftActionManager::clearSoundReferences(Uid soundUid)
{
    bool any = false;
    for (auto& i : items)
    {
        auto* a = static_cast<GiftAction*>(i.get());
        if (a->soundUid == soundUid) { a->soundUid = 0; any = true; }
    }
    if (any) notifyStructureChanged(this);
}

void GiftActionManager::onItemsChanged() { notifyStructureChanged(this); }

} // namespace evobox
