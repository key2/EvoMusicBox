// OscActions.h — the reusable OSC component: an ordered list of phases owned by any
// triggerable. Owners decide the phase set in their constructor; the UI never invents phases.
#pragma once

#include "model/ModelCommon.h"
#include "model/OscPhase.h"

namespace evobox
{

class OscActions : public organic::Container
{
public:
    explicit OscActions(organic::Container* parent = nullptr);

    std::vector<std::unique_ptr<OscPhase>> phases;

    OscPhase* addPhase(const std::string& name, Anchor anchor, int delayMs);
    OscPhase* phase(Anchor anchor) const;              // first phase with that anchor (or nullptr)
    OscPhase* phaseAt(size_t i) const { return i < phases.size() ? phases[i].get() : nullptr; }
    int       indexOf(const OscPhase* p) const;

    bool   hasCommands() const;
    size_t commandCount(bool enabledOnly = false) const;
    // every distinct target uid referenced by the commands (0 = default)
    std::vector<Uid> referencedTargets() const;
    int    retarget(Uid from, Uid to);

    // Copies delays + rows from another actions set with the same phase layout (clipboard / duplicate).
    void copyFrom(const OscActions& other);
    void clearCommands();

    json save() const override;
    void load(const json& j) override;
};

} // namespace evobox
