// RoomEventAction.h — the five fixed room events (Like, Follow, Share, Subscribe, Join):
// OSC {On event, Stop (timer)}, threshold (likes per fire), cooldown, optional sound.
#pragma once

#include "model/ModelCommon.h"
#include "model/OscActions.h"
#include "model/Triggerable.h"

namespace evobox
{

enum class RoomEventKind { Like = 0, Follow, Share, Subscribe, Join, Count };
const char* roomEventKindName(RoomEventKind k);   // "Like" ...
const char* roomEventKindKey(RoomEventKind k);    // "like" ... (persistence)
bool roomEventKindFromKey(const std::string& key, RoomEventKind& out);

class RoomEventAction : public organic::BaseItem, public Triggerable
{
public:
    static constexpr const char* kType = "RoomEventAction";

    explicit RoomEventAction(RoomEventKind kind = RoomEventKind::Like);

    RoomEventKind kind = RoomEventKind::Like;
    organic::Parameter* thresholdP = nullptr;  // Like: likes per fire (ignored otherwise)
    organic::Parameter* cooldownMsP = nullptr; // Join defaults to 2000 ms (raids)
    Uid soundUid = 0;

    OscActions oscActions;

    struct Runtime
    {
        bool     active = false;
        uint64_t sessionId = 0;
        double   lastFireTime = -1e9;
        double   lastPulseTime = -1.0;
        double   lastOscPulseTime = -1.0;
        long long likeAccumulator = 0;
        int      receivedCount = 0;
    } rt;

    int  threshold() const { return thresholdP->intValue(); }
    int  cooldownMs() const { return cooldownMsP->intValue(); }
    int  stopTimerMs() const;
    void setKind(RoomEventKind k);
    void setSoundUidUndoable(Uid uid);

    // Triggerable
    OscActions& actions() override { return oscActions; }
    const OscActions& actions() const override { return oscActions; }
    std::string displayName() const override { return niceName; }
    TriggerableKind triggerableKind() const override { return TriggerableKind::RoomEvent; }
    Uid triggerableUid() const override { return makeTriggerableUid(TriggerableKind::RoomEvent, uid); }
    organic::Container* asContainer() override { return this; }

    std::string inspectableTypeName() const override { return "Room event"; }
    void inspectorGui() override;

    json save() const override;
    void load(const json& j) override;
};

class RoomEventActionManager : public organic::BaseManager
{
public:
    explicit RoomEventActionManager(organic::Container* parent = nullptr);

    RoomEventAction* find(RoomEventKind k) const;
    std::vector<RoomEventAction*> actions() const;
    void ensureFixedItems(); // exactly five, in kind order
    void clearSoundReferences(Uid soundUid);

    void onItemsChanged() override;
    void load(const json& j) override;
};

} // namespace evobox
