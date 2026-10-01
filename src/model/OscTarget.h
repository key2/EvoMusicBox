// OscTarget.h — the OSC "address book": named host/port destinations. Localhost is always
// present (builtin, not removable) and exactly one target is the default at any time.
#pragma once

#include "model/ModelCommon.h"

namespace evobox
{

class OscTarget : public organic::BaseItem
{
public:
    static constexpr const char* kType = "OscTarget";
    static constexpr int kDefaultPort = 8000;

    OscTarget();

    organic::Parameter* hostP = nullptr;      // "127.0.0.1" / hostname
    organic::Parameter* portP = nullptr;      // 1..65535 (8000 default)
    organic::Parameter* isDefaultP = nullptr; // exactly one true per manager
    bool builtin = false;                     // Localhost

    std::string host() const { return hostP->stringValue(); }
    int  port() const { return portP->intValue(); }
    bool isDefault() const { return isDefaultP->boolValue(); }
    std::string displayName() const { return niceName; }

    // runtime: bumped whenever host/port change so OscService re-resolves lazily
    uint32_t endpointRevision = 1;

    void onParamChanged(organic::Parameter* p) override;
    std::string inspectableTypeName() const override { return "OSC target"; }
    json save() const override;
    void load(const json& j) override;
};

class OscTargetManager : public organic::BaseManager
{
public:
    explicit OscTargetManager(organic::Container* parent = nullptr);

    OscTarget* target(size_t i) const { return static_cast<OscTarget*>(items[i].get()); }
    OscTarget* find(Uid uid) const { return static_cast<OscTarget*>(findItem(uid)); }
    OscTarget* localhost() const;
    OscTarget* defaultTarget() const;               // never nullptr after construction
    OscTarget* resolve(Uid uid) const;               // uid or default when 0 / dangling
    std::vector<OscTarget*> targets() const;

    OscTarget* addTargetUndoable(const std::string& name, const std::string& host, int port);
    void setDefaultUndoable(OscTarget* t);
    // Deletes a target and re-points every command referencing it to "default" (0) in one undo step.
    // `retargetAll` performs the re-pointing across the project and returns how many commands changed.
    void removeTargetUndoable(OscTarget* t, const std::function<int(Uid from, Uid to)>& retargetAll);

    void ensureLocalhost();
    void onDefaultToggled(OscTarget* t); // called by OscTarget when its Default flag changes
    void onItemsChanged() override;
    void load(const json& j) override;

private:
    void repairDefaultInvariant();
    bool repairing_ = false;
};

} // namespace evobox
