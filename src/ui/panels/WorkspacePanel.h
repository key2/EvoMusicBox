// WorkspacePanel.h — center workspace: ImGui tab bar with Sounds (SoundboardTab, the tile grid of
// UI.md §5-§11) and Gifts (GiftGalleryTab, only with EVOBOX_WITH_TIKTOK).
#pragma once

#include <string>
#include "app/Application.h"
#include "ui/widgets/Common.h"

namespace evobox
{
namespace ui
{

class SoundboardTab
{
public:
    explicit SoundboardTab(Application& app) : app_(app) {}
    void draw();
    void requestFocusSearch() { focusSearch_ = true; }
    // keyboard navigation in the grid
    void moveSelection(int dx, int dy);
    std::vector<Sound*> visibleSounds() const;

    ConfirmPopup confirm{ "ConfirmSoundboard" };
    bool wantOpenFileDialog = false;

private:
    Application& app_;
    bool focusSearch_ = false;
    Uid contextUid_ = 0;
    Uid renamingUid_ = 0;
    std::string renameBuf_;
    int lastColumns_ = 1;

    void header(const std::vector<Sound*>& visible);
    void grid(const std::vector<Sound*>& visible);
    void list(const std::vector<Sound*>& visible);
    void contextMenu(Sound& s);
    void tileActions(Sound& s, int action);
};

#ifdef EVOBOX_WITH_TIKTOK
class GiftGalleryTab
{
public:
    explicit GiftGalleryTab(Application& app) : app_(app) {}
    void draw();
    ConfirmPopup confirm{ "ConfirmGallery" };

private:
    Application& app_;
    char username_[96] = "";
    bool usernameInit_ = false;
    std::vector<GiftInfo*> sorted_;
    uint32_t sortedRevision_ = 0;
    GiftFilter sortedFilter_ = GiftFilter::All;
    GiftSort sortedSort_ = GiftSort::Diamonds;
    std::string sortedSearch_;
    int sortedConfiguredCount_ = -1;
    int sortedReceived_ = -1;

    void header();
    void gallery();
    void rebuildSorted();
    void giftContextMenu(GiftInfo& g);
};
#endif

class WorkspacePanel
{
public:
    explicit WorkspacePanel(Application& app);
    void draw(bool* open);
    SoundboardTab& soundboard() { return soundboard_; }

private:
    Application& app_;
    SoundboardTab soundboard_;
#ifdef EVOBOX_WITH_TIKTOK
    GiftGalleryTab gallery_;
#endif
};

} // namespace ui
} // namespace evobox
