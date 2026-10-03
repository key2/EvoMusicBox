#include "model/OscTarget.h"
#include "util/Localize.h"

namespace evobox
{

// ================================================================ OscTarget
OscTarget::OscTarget() : organic::BaseItem(kType, "Target")
{
    hostP = addString("Host", "127.0.0.1", "IP address or host name");
    portP = addInt("Port", kDefaultPort, 1, 65535, "UDP port (8000 by default)");
    isDefaultP = addBool("Default", false, "Commands without an explicit target go here");
    hideParam(colorP);
    hideParam(enabledP);
}

void OscTarget::onParamChanged(organic::Parameter* p)
{
    if (p == hostP || p == portP) endpointRevision++;
    if (p == isDefaultP && manager)
        if (auto* m = dynamic_cast<OscTargetManager*>(manager)) m->onDefaultToggled(this);
}

json OscTarget::save() const
{
    json j = BaseItem::save();
    j["builtin"] = builtin;
    return j;
}

void OscTarget::load(const json& j)
{
    BaseItem::load(j);
    builtin = jget<bool>(j, "builtin", false);
    if (builtin) { userCanRemove = false; }
    endpointRevision++;
}

// ================================================================ OscTargetManager
OscTargetManager::OscTargetManager(organic::Container* parent)
    : organic::BaseManager("OSC Targets", parent)
{
    selectionScopeName = "osc-targets";
    addDef("OSC target", OscTarget::kType, [] { return std::make_unique<OscTarget>(); });
    ensureLocalhost();
}

OscTarget* OscTargetManager::localhost() const
{
    for (auto& i : items)
        if (static_cast<OscTarget*>(i.get())->builtin) return static_cast<OscTarget*>(i.get());
    return nullptr;
}

OscTarget* OscTargetManager::defaultTarget() const
{
    for (auto& i : items)
        if (static_cast<OscTarget*>(i.get())->isDefault()) return static_cast<OscTarget*>(i.get());
    if (auto* lh = localhost()) return lh;
    return items.empty() ? nullptr : static_cast<OscTarget*>(items[0].get());
}

OscTarget* OscTargetManager::resolve(Uid uid) const
{
    if (uid != 0)
        if (auto* t = find(uid)) return t;
    return defaultTarget();
}

std::vector<OscTarget*> OscTargetManager::targets() const
{
    std::vector<OscTarget*> out;
    for (auto& i : items) out.push_back(static_cast<OscTarget*>(i.get()));
    return out;
}

void OscTargetManager::ensureLocalhost()
{
    if (localhost()) return;
    auto lh = std::make_unique<OscTarget>();
    lh->setNiceName("Localhost");
    lh->builtin = true;
    lh->userCanRemove = false;
    lh->hostP->setValue(std::string("127.0.0.1"), false);
    lh->hostP->defaultValue = lh->hostP->value;
    lh->portP->setValue(OscTarget::kDefaultPort, false);
    lh->isDefaultP->setValue(true, false);
    addItem(std::move(lh), 0);
}

OscTarget* OscTargetManager::addTargetUndoable(const std::string& name, const std::string& host, int port)
{
    auto item = std::make_unique<OscTarget>();
    item->setNiceName(name.empty() ? LTR("osc.newTargetName", "New target") : name);
    item->hostP->setValue(host.empty() ? std::string("127.0.0.1") : host, false);
    item->portP->setValue(port > 0 ? port : OscTarget::kDefaultPort, false);
    OscTarget* raw = item.get();
    addItem(std::move(item));
    json data = raw->save();
    data["_index"] = indexOf(raw);
    Uid uid = raw->uid;
    OscTargetManager* self = this;
    organic::UndoManager::get().pushDone(LTR("undo.addOscTarget", "Add OSC target"),
        [self, data] { self->addItemFromJson(data); },
        [self, uid]  { self->removeItem(uid); },
        { self });
    return raw;
}

void OscTargetManager::setDefaultUndoable(OscTarget* t)
{
    if (!t || t->isDefault()) return;
    OscTarget* prev = defaultTarget();
    Uid newUid = t->uid, oldUid = prev ? prev->uid : 0;
    OscTargetManager* self = this;
    auto apply = [self](Uid uid)
    {
        for (auto& i : self->items)
            static_cast<OscTarget*>(i.get())->isDefaultP->setValue(i->uid == uid, false);
        self->repairDefaultInvariant();
        notifyStructureChanged(self);
    };
    organic::UndoManager::get().perform(LTR("undo.setDefaultOscTarget", "Set default OSC target"),
        [apply, newUid] { apply(newUid); },
        [apply, oldUid] { apply(oldUid); },
        { self });
}

void OscTargetManager::removeTargetUndoable(OscTarget* t, const std::function<int(Uid, Uid)>& retargetAll)
{
    if (!t || t->builtin || !t->userCanRemove) return;
    json data = t->save();
    data["_index"] = indexOf(t);
    Uid uid = t->uid;
    OscTargetManager* self = this;
    // do: re-point commands then remove
    int n = retargetAll ? retargetAll(uid, 0) : 0;
    removeItem(uid);
    // The re-pointing is not undone (commands pointing at a re-added target would be
    // surprising after an intermediate edit); undo re-adds the target with its uid, and a
    // second "retarget" pass would be needed to restore references — kept simple on purpose.
    (void)n;
    organic::UndoManager::get().pushDone(LTR("undo.deleteOscTarget", "Delete OSC target"),
        [self, uid, retargetAll] { if (retargetAll) retargetAll(uid, 0); self->removeItem(uid); },
        [self, data] { self->addItemFromJson(data); },
        { self });
}

void OscTargetManager::repairDefaultInvariant()
{
    if (repairing_) return;
    repairing_ = true;
    int defaults = 0;
    for (auto& i : items) if (static_cast<OscTarget*>(i.get())->isDefault()) defaults++;
    if (defaults != 1)
    {
        OscTarget* keep = nullptr;
        if (defaults > 1)
        {
            // keep the LAST one flagged (the one the user just ticked)
            for (auto& i : items) if (static_cast<OscTarget*>(i.get())->isDefault()) keep = static_cast<OscTarget*>(i.get());
        }
        else keep = localhost() ? localhost() : (items.empty() ? nullptr : static_cast<OscTarget*>(items[0].get()));
        for (auto& i : items)
            static_cast<OscTarget*>(i.get())->isDefaultP->setValue(i.get() == keep, false);
    }
    repairing_ = false;
}

void OscTargetManager::onDefaultToggled(OscTarget* t)
{
    if (repairing_ || !t) return;
    repairing_ = true;
    if (t->isDefault())
    {
        for (auto& i : items)
            if (i.get() != t) static_cast<OscTarget*>(i.get())->isDefaultP->setValue(false, false);
    }
    else
    {
        bool any = false;
        for (auto& i : items) if (static_cast<OscTarget*>(i.get())->isDefault()) any = true;
        if (!any) t->isDefaultP->setValue(true, false); // the only default cannot be un-ticked
    }
    repairing_ = false;
    notifyStructureChanged(this);
}

void OscTargetManager::onItemsChanged()
{
    repairDefaultInvariant();
    notifyStructureChanged(this);
}

void OscTargetManager::load(const json& j)
{
    BaseManager::load(j);
    ensureLocalhost();
    repairDefaultInvariant();
}

} // namespace evobox
