// OscCommand.h — one OSC command row: text ("/address arg ..."), target reference, enabled.
// OscCommandManager is a BaseManager of rows -> undoable add/remove/reorder/duplicate and
// clipboard for free.
#pragma once

#include "model/ModelCommon.h"

namespace evobox
{

class OscCommand : public organic::BaseItem
{
public:
    static constexpr const char* kType = "OscCommand";

    OscCommand();

    organic::Parameter* textP = nullptr; // "/address arg arg ..."
    Uid targetUid = 0;                   // 0 = default target (Localhost unless another is flagged default)

    std::string text() const { return textP->stringValue(); }
    void setTargetUidUndoable(Uid uid);

    // runtime (never serialized)
    struct Runtime
    {
        double lastSentTime = -1.0;   // app seconds; drives the ~300 ms row pulse
        double lastErrorTime = -1.0;
        std::string lastError;        // parse/send error shown inline
    } rt;

    std::string inspectableTypeName() const override { return "OSC command"; }
    json save() const override;
    void load(const json& j) override;
};

class OscCommandManager : public organic::BaseManager
{
public:
    explicit OscCommandManager(organic::Container* parent = nullptr);

    OscCommand* command(size_t i) const { return static_cast<OscCommand*>(items[i].get()); }
    OscCommand* find(Uid uid) const { return static_cast<OscCommand*>(findItem(uid)); }
    std::vector<OscCommand*> commands() const;
    size_t enabledCount() const;

    // Adds a row (one undo step) WITHOUT changing the main selection.
    OscCommand* addCommandUndoable(const std::string& text = "/", Uid targetUid = 0, int index = -1);
    void removeCommandUndoable(OscCommand* c);
    // Re-point every command referencing `from` to `to` (used when a target is deleted).
    int retarget(Uid from, Uid to);

    void onItemsChanged() override;
};

} // namespace evobox
