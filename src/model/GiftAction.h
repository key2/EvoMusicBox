// GiftAction.h — what happens when a TikTok gift arrives: OSC {On gift, Stop (timer)}, an
// optional sound, and the streak / retrigger / cooldown / min-diamonds policy. One item per
// gift the user configured (never one per catalog entry).
#pragma once

#include "model/ModelCommon.h"
#include "model/OscActions.h"
#include "model/Triggerable.h"

namespace evobox
{

enum class StreakMode { OnceAtStreakEnd = 0, EveryEvent = 1, PerRepeat = 2 };
enum class RetriggerPolicy { Restart = 0, Ignore = 1, Queue = 2 };

class GiftAction : public organic::BaseItem, public Triggerable
{
public:
    static constexpr const char* kType = "GiftAction";
    static constexpr int kDefaultStopMs = 3000;

    GiftAction();

    int64_t     giftId = 0;
    std::string cachedName;      // copied from the catalog so offline projects still display
    int         cachedDiamonds = 0;
    std::string cachedIconUrl;
    int         cachedType = 0;  // 1 = streakable

    organic::Parameter* streakModeP = nullptr;
    organic::Parameter* retriggerP = nullptr;
    organic::Parameter* cooldownMsP = nullptr;
    organic::Parameter* minDiamondsP = nullptr;
    Uid soundUid = 0;            // optional "Also play sound"

    OscActions oscActions;

    struct Runtime
    {
        bool     active = false;           // Start fired, Timer pending
        uint64_t sessionId = 0;            // TriggerController session while active
        double   lastFireTime = -1e9;      // cooldown reference (app seconds)
        double   lastPulseTime = -1.0;     // gallery tile feedback (received)
        double   lastOscPulseTime = -1.0;
        int      pendingStreakCount = 0;
        int      receivedCount = 0;        // this session
        int      queued = 0;               // Retrigger = Queue
        bool     transient = false;        // shown in the Inspector but not yet in the project
    } rt;

    StreakMode      streakMode() const { return (StreakMode)streakModeP->intValue(); }
    RetriggerPolicy retrigger() const { return (RetriggerPolicy)retriggerP->intValue(); }
    int  cooldownMs() const { return cooldownMsP->intValue(); }
    int  minDiamonds() const { return minDiamondsP->intValue(); }
    int  stopTimerMs() const;
    bool isStreakable() const { return cachedType == 1; }
    void setSoundUidUndoable(Uid uid);
    void seedFromCatalog(int64_t id, const std::string& name, int diamonds, const std::string& iconUrl, int type);

    // Triggerable
    OscActions& actions() override { return oscActions; }
    const OscActions& actions() const override { return oscActions; }
    std::string displayName() const override { return cachedName.empty() ? niceName : cachedName; }
    TriggerableKind triggerableKind() const override { return TriggerableKind::Gift; }
    Uid triggerableUid() const override;
    organic::Container* asContainer() override { return this; }

    std::string inspectableTypeName() const override { return "Gift"; }
    std::string inspectableLabel() const override { return displayName(); }
    void inspectorGui() override;

    json save() const override;
    void load(const json& j) override;
};

class GiftActionManager : public organic::BaseManager
{
public:
    explicit GiftActionManager(organic::Container* parent = nullptr);

    GiftAction* action(size_t i) const { return static_cast<GiftAction*>(items[i].get()); }
    GiftAction* find(int64_t giftId) const;
    GiftAction* findByUid(Uid uid) const { return static_cast<GiftAction*>(findItem(uid)); }
    std::vector<GiftAction*> actions() const;

    // Promotes a transient action into the project (one undo step; keeps its data).
    GiftAction* addFromTransientUndoable(const GiftAction& transient);
    void clearSoundReferences(Uid soundUid);

    void onItemsChanged() override;
};

} // namespace evobox
