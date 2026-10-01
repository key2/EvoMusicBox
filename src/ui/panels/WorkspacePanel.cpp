#include "ui/panels/WorkspacePanel.h"
#include "ui/Icons.h"
#include "ui/Theme.h"

namespace evobox
{
namespace ui
{

WorkspacePanel::WorkspacePanel(Application& app)
    : app_(app), soundboard_(app)
#ifdef EVOBOX_WITH_TIKTOK
      , gallery_(app)
#endif
{
}

void WorkspacePanel::draw(bool* open)
{
    ImGui::SetNextWindowSize(ImVec2(900, 600), ImGuiCond_FirstUseEver);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    if (ImGui::Begin("Workspace", open, flags))
    {
        if (app_.performanceMode || !app_.tiktokAvailable())
        {
            app_.prefs.activeTab = WorkspaceTab::Sounds;
            soundboard_.draw();
        }
        else if (ImGui::BeginTabBar("workspace", ImGuiTabBarFlags_NoCloseWithMiddleMouseButton))
        {
            ImGuiTabItemFlags sf = 0, gf = 0;
            static WorkspaceTab lastTab = app_.prefs.activeTab;
            if (lastTab != app_.prefs.activeTab)
            {
                // external change (menu / shortcut): force the tab
                if (app_.prefs.activeTab == WorkspaceTab::Sounds) sf = ImGuiTabItemFlags_SetSelected; else gf = ImGuiTabItemFlags_SetSelected;
                lastTab = app_.prefs.activeTab;
            }
            (void)gf;
            if (ImGui::BeginTabItem(ICON_PH_SQUARES_FOUR "  Sounds", nullptr, sf))
            {
                app_.prefs.activeTab = lastTab = WorkspaceTab::Sounds;
                soundboard_.draw();
                ImGui::EndTabItem();
            }
#ifdef EVOBOX_WITH_TIKTOK
            std::string giftLabel = std::string(ICON_PH_GIFT "  Gifts");
            if (app_.live.state == LiveState::Connected) giftLabel += "  " ICON_PH_BROADCAST;
            if (ImGui::BeginTabItem(giftLabel.c_str(), nullptr, gf))
            {
                app_.prefs.activeTab = lastTab = WorkspaceTab::Gifts;
                gallery_.draw();
                ImGui::EndTabItem();
            }
#endif
            ImGui::EndTabBar();
        }
    }
    ImGui::End();
}

} // namespace ui
} // namespace evobox
