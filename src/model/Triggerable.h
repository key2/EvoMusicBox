// Triggerable.h — the interface shared by every object that can carry OSC actions
// (Sound, GiftAction, RoomEventAction). "No matter what we click, we can set the OSC on it."
#pragma once

#include "model/ModelCommon.h"

namespace evobox
{

class OscActions;

enum class TriggerableKind { Sound, Gift, RoomEvent };

// Triggerable identities must be unique ACROSS kinds: organic BaseItem uids are per-manager
// sequences (the first sound and the first gift action are both uid 1), so the kind is encoded
// in the top bits of the identity used by TriggerController sessions and OSC feedback events.
constexpr int kTriggerableTagShift = 60;
constexpr Uid kTriggerableItemMask = (Uid(1) << kTriggerableTagShift) - 1;
constexpr Uid kTriggerableTransientTag = 4; // transient gift actions (not in the project): item = gift id

inline Uid makeTriggerableUid(TriggerableKind kind, Uid itemUid)
{
    return ((Uid)kind + 1) << kTriggerableTagShift | (itemUid & kTriggerableItemMask);
}
inline Uid makeTransientTriggerableUid(Uid giftId)
{
    return kTriggerableTransientTag << kTriggerableTagShift | (giftId & kTriggerableItemMask);
}
inline Uid triggerableTag(Uid triggerableUid) { return triggerableUid >> kTriggerableTagShift; }
inline Uid triggerableItemUid(Uid triggerableUid) { return triggerableUid & kTriggerableItemMask; }
// Kind of a real (non transient) identity; false for transient / untagged values.
inline bool triggerableKindOf(Uid triggerableUid, TriggerableKind& out)
{
    Uid tag = triggerableTag(triggerableUid);
    if (tag < 1 || tag > 3) return false;
    out = (TriggerableKind)(tag - 1);
    return true;
}

class Triggerable
{
public:
    virtual ~Triggerable() = default;

    virtual OscActions&       actions()             = 0;
    virtual const OscActions& actions() const       = 0;
    virtual std::string       displayName() const   = 0;
    virtual TriggerableKind   triggerableKind() const = 0;
    // Stable identity used by TriggerController sessions and OSC feedback: makeTriggerableUid(kind,
    // BaseItem uid) for real items, makeTransientTriggerableUid(giftId) for transient gift actions.
    virtual Uid               triggerableUid() const = 0;
    // The organic container (for undo owners / structure notifications).
    virtual organic::Container* asContainer() = 0;
};

} // namespace evobox
