#include "model/OscCommand.h"
#include "util/Localize.h"

namespace evobox
{

// ================================================================ OscCommand
OscCommand::OscCommand() : organic::BaseItem(kType, "OSC command")
{
    renamable = false;
    textP = addString("Command", "/", "OSC address followed by arguments, e.g. /light/flash 1");
    hideParam(colorP);
    userCanDuplicate = true;
}

void OscCommand::setTargetUidUndoable(Uid uid)
{
    setFieldUndoable<Uid>(this, targetUid, uid, LTR("undo.setOscTarget", "Set OSC target"));
}

json OscCommand::save() const
{
    json j = BaseItem::save();
    j["targetUid"] = targetUid;
    return j;
}

void OscCommand::load(const json& j)
{
    BaseItem::load(j);
    targetUid = jget<Uid>(j, "targetUid", 0);
}

// ================================================================ OscCommandManager
OscCommandManager::OscCommandManager(organic::Container* parent)
    : organic::BaseManager("Commands", parent)
{
    // Rows must never steal the main selection (the Inspector follows the *owner*), so
    // organic's built-in undoableAdd/Duplicate/Paste select into a private scope.
    selectionScopeName = "osc-commands";
    hideInOutliner = true;
    addDef("OSC command", OscCommand::kType, [] { return std::make_unique<OscCommand>(); });
}

std::vector<OscCommand*> OscCommandManager::commands() const
{
    std::vector<OscCommand*> out;
    out.reserve(items.size());
    for (auto& i : items) out.push_back(static_cast<OscCommand*>(i.get()));
    return out;
}

size_t OscCommandManager::enabledCount() const
{
    size_t n = 0;
    for (auto& i : items) if (i->enabled()) n++;
    return n;
}

OscCommand* OscCommandManager::addCommandUndoable(const std::string& text, Uid targetUid, int index)
{
    auto item = std::make_unique<OscCommand>();
    item->textP->setValue(text, false);
    item->targetUid = targetUid;
    OscCommand* raw = item.get();
    addItem(std::move(item), index);
    json data = raw->save();
    data["_index"] = indexOf(raw);
    Uid uid = raw->uid;
    OscCommandManager* self = this;
    organic::UndoManager::get().pushDone(LTR("undo.addOscCommand", "Add OSC command"),
        [self, data] { self->addItemFromJson(data); },
        [self, uid]  { self->removeItem(uid); },
        { self });
    return raw;
}

void OscCommandManager::removeCommandUndoable(OscCommand* c)
{
    if (!c) return;
    undoableRemove({ c });
}

int OscCommandManager::retarget(Uid from, Uid to)
{
    int n = 0;
    for (auto& i : items)
    {
        auto* c = static_cast<OscCommand*>(i.get());
        if (c->targetUid == from) { c->targetUid = to; n++; }
    }
    if (n) notifyStructureChanged(this);
    return n;
}

void OscCommandManager::onItemsChanged()
{
    notifyStructureChanged(this);
}

} // namespace evobox
