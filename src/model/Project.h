// Project.h — the document: categories, sounds, gift actions, room events, OSC targets and
// settings. Root of the Container tree (every parameter/structure change bubbles here for dirty
// tracking). Persistence: project.json inside a <name>.evobox/ bundle folder (see app/ProjectIO).
#pragma once

#include "model/ModelCommon.h"
#include "model/Category.h"
#include "model/GiftAction.h"
#include "model/OscTarget.h"
#include "model/RoomEventAction.h"
#include "model/Settings.h"
#include "model/Sound.h"

namespace evobox
{

class Project : public organic::Container, public StructureListener
{
    // Declared BEFORE the managers on purpose: their constructors add default items, which
    // notifies this listener while the Project is still being constructed. The flag starts true
    // and the hook empty so those early notifications are ignored (resetToDefaults() finishes).
    bool loading_ = true;

public:
    static constexpr int kFormatVersion = 2;

    Project();
    ~Project() override;

    // dirty tracking: revision bumps on every change; savedRevision is set by save/load
    uint64_t revision = 0;
    uint64_t savedRevision = 0;
    // observers (app layer): called on the main thread after any change
    std::function<void()> onAnyChange;

    CategoryManager        categories;
    SoundManager           sounds;
    GiftActionManager      giftActions;
    RoomEventActionManager roomEvents;
    OscTargetManager       oscTargets;
    Settings               settings;

    std::string bundleDir;            // working folder (project.json, clips/, icons/); "" until saved
    std::string archivePath;          // the <name>.liv zip this project was opened from / saved to ("" = folder bundle)
    int  formatVersion = kFormatVersion;
    std::string title() const;        // archive / bundle folder stem or "Untitled"
    bool isArchive() const { return !archivePath.empty(); }

    bool dirty() const { return revision != savedRevision; }
    void markSaved() { savedRevision = revision; }
    void touch() { if (!loading_) revision++; }
    bool loading() const { return loading_; }

    void resetToDefaults();           // new project: default categories, Localhost, fixed room events

    // ---- cross-reference maintenance (single undo steps)
    std::vector<Triggerable*> triggerables();
    Triggerable* findTriggerable(Uid triggerableUid); // any real item by its kind-tagged Triggerable::triggerableUid()
    int  retargetAll(Uid fromTarget, Uid toTarget);   // commands referencing a target
    void deleteSoundsUndoable(const std::vector<Sound*>& s);     // clears gift/room soundUid refs
    void deleteCategoryUndoable(Category* c);                    // moves its sounds to Custom
    void deleteTargetUndoable(OscTarget* t);                     // re-points commands to default
    bool categoryDeletable(const Category* c) const;

    // ---- persistence (json only; files are handled by ProjectIO)
    json save() const override;
    void load(const json& j) override;

    // StructureListener / Container notifications -> dirty tracking
    void onStructureChanged(organic::Container* source) override;
    void onChildParamChanged(organic::Container* c, organic::Parameter* p) override;
    void onParamChanged(organic::Parameter* p) override;

private:
    void migrate(json& j) const;
};

} // namespace evobox
