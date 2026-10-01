// NavigatorPanel.h — left sidebar. Sounds tab: categories list (UI.md §4, §35) with inline
// "+ Add Category" editor, rename on double-click, context menu, drop target for EVOBOX_SOUND
// payloads and drag-reorder. Gifts tab: gift filters + the five room events.
#pragma once

#include <string>
#include "app/Application.h"
#include "ui/widgets/Common.h"

namespace evobox
{
namespace ui
{

class NavigatorPanel
{
public:
    explicit NavigatorPanel(Application& app) : app_(app) {}
    void draw(bool* open);

private:
    Application& app_;
    ConfirmPopup confirm_{ "ConfirmNavigator" };
    bool addingCategory_ = false;
    char newName_[64] = "";
    std::string newIcon_ = "ph:folder";
    ImVec4 newColor_ = ImVec4(0.45f, 0.55f, 0.70f, 1.f);
    Uid renamingUid_ = 0;
    std::string renameBuf_;

    void drawCategories();
    void drawGiftNavigator();
    bool categoryRow(Category* c, bool isAll, const char* label, const char* icon, ImVec4 color, size_t count, Uid uid);
    void drawAddCategory();
};

} // namespace ui
} // namespace evobox
