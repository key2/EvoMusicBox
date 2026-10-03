#include "ui/panels/LiveMonitorPanel.h"
#include "ui/I18n.h"
#include "ui/Icons.h"
#include "ui/Theme.h"
#include "ui/widgets/Common.h"
#include "ui/widgets/PhaseTimeline.h"
#include "ui/widgets/Tiles.h"
#include "util/Strings.h"
#include "util/TimeFormat.h"

namespace evobox
{
namespace ui
{

void LiveMonitorPanel::drawTimeline()
{
    const auto& th = theme::colors();
    GiftAction* g = app_.selectedGift();
    RoomEventAction* r = app_.selectedRoomEvent();
    Triggerable* t = g ? (Triggerable*)g : (r ? (Triggerable*)r : nullptr);
    if (!t)
    {
        EmptyState(TR("live.emptyTitle"), TR("live.emptySubtitle"), ICON_PH_TIMER);
        return;
    }
    std::string title = g ? (ICON_PH_GIFT "  " + g->displayName()) : (std::string(roomEventIcon(r->kind)) + "  " + r->niceName);
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.1f);
    ImGui::TextUnformatted(title.c_str());
    ImGui::PopFont();
    ImGui::SameLine();
    if (AccentButton((std::string(ICON_PH_PLAY " ") + TR("live.simulate")).c_str()))
    {
        if (g) app_.simulateGift(g->giftId); else app_.simulateRoomEvent(r->kind);
    }
    bool active = g ? g->rt.active : r->rt.active;
    double lastFire = g ? g->rt.lastFireTime : r->rt.lastFireTime;
    if (active)
    {
        ImGui::SameLine();
        double rem = app_.trigger.timerRemaining(*t);
        ImGui::TextColored(th.warning, (std::string(ICON_PH_TIMER " ") + TR("live.activeStopIn")).c_str(), rem < 0 ? 0.0 : rem);
    }
    PhaseTimelineState st;
    st.active = active;
    st.now = app_.now();
    st.lastFire = lastFire;
    st.elapsedSec = active && lastFire > 0 ? app_.now() - lastFire : 0;
    PhaseTimeline("tl", t->actions(), st, ImVec2(0, std::max(80.f, ImGui::GetContentRegionAvail().y - 4)));
}

void LiveMonitorPanel::drawFeed()
{
    const auto& th = theme::colors();
    // header: state · viewers · filters
    ImVec4 dot = th.textFaint;
    switch (app_.live.state)
    {
    case LiveState::Connected: dot = th.playing; break;
    case LiveState::Connecting: dot = th.warning; break;
    case LiveState::Error: dot = th.danger; break;
    default: break;
    }
    Dot(dot, 5.f);
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    TextDim(TR("live.stateLine"), liveStateLabel(app_.live.state),
            app_.live.roomUser.empty() ? "" : (TR("live.atUser") + app_.live.roomUser).c_str(),
            str::groupThousands(app_.live.viewers).c_str(), app_.live.eventsReceived, app_.live.giftsReceived);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(140 * theme::scale());
    ImGui::InputTextWithHint("##ffilter", (std::string(ICON_PH_FUNNEL " ") + TR("live.filterHint")).c_str(), filter_, sizeof(filter_));
    ImGui::SameLine();
    ToggleButton(ICON_PH_CHAT_CIRCLE, showComments_, ImVec2(ImGui::GetFrameHeight(), 0), TR("live.showComments")) ? (showComments_ = !showComments_) : false;
    ImGui::SameLine(0, 2);
    ToggleButton(ICON_PH_HEART, showLikes_, ImVec2(ImGui::GetFrameHeight(), 0), TR("live.showLikes")) ? (showLikes_ = !showLikes_) : false;
    ImGui::SameLine(0, 2);
    ToggleButton(ICON_PH_SIGN_IN, showJoins_, ImVec2(ImGui::GetFrameHeight(), 0), TR("live.showJoins")) ? (showJoins_ = !showJoins_) : false;
    ImGui::SameLine(0, 2);
    if (IconButton(ICON_PH_TRASH, TR("live.clearFeed"), ImVec2(ImGui::GetFrameHeight(), 0))) app_.live.clearFeed();

    ImGui::BeginChild("##feed", ImVec2(0, 0), ImGuiChildFlags_Borders);
    const auto& feed = app_.live.feed();
    if (feed.empty()) TextDim("%s", TR("live.noEvents"));
    for (const LiveEvent& e : feed)
    {
        if (!showComments_ && e.type == LiveEventType::Comment) continue;
        if (!showLikes_ && e.type == LiveEventType::Like) continue;
        if (!showJoins_ && e.type == LiveEventType::Join) continue;
        std::string line = e.summary();
        if (filter_[0] && !str::icontains(line, filter_) && !str::icontains(liveEventTypeName(e.type), filter_)) continue;
        ImVec4 col = th.textDim;
        const char* icon = ICON_PH_DOT_OUTLINE;
        switch (e.type)
        {
        case LiveEventType::Gift: col = th.live; icon = ICON_PH_GIFT; break;
        case LiveEventType::Comment: col = th.text; icon = ICON_PH_CHAT_CIRCLE; break;
        case LiveEventType::Like: col = th.textDim; icon = ICON_PH_HEART; break;
        case LiveEventType::Join: col = th.textFaint; icon = ICON_PH_SIGN_IN; break;
        case LiveEventType::Follow: col = th.accent; icon = ICON_PH_USER_PLUS; break;
        case LiveEventType::Share: col = th.accent; icon = ICON_PH_SHARE_NETWORK; break;
        case LiveEventType::Subscribe: col = th.warning; icon = ICON_PH_STAR; break;
        case LiveEventType::Error: col = th.danger; icon = ICON_PH_WARNING; break;
        case LiveEventType::Connect: col = th.playing; icon = ICON_PH_BROADCAST; break;
        default: break;
        }
        std::string ts = formatTime(e.time, false);
        ImGui::TextColored(th.textFaint, "%s", ts.c_str());
        ImGui::SameLine(0, 8);
        ImGui::TextColored(col, "%s", icon);
        ImGui::SameLine(0, 6);
        if (e.synthetic) { ImGui::TextColored(th.warning, "%s", TR("live.simMarker")); ImGui::SameLine(0, 4); }
        ImGui::TextColored(e.type == LiveEventType::Comment ? th.text : col, "%s", line.c_str());
    }
    if (autoScroll_ && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 40) ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();
}

void LiveMonitorPanel::draw(bool* open)
{
    ImGui::SetNextWindowSize(ImVec2(900, 260), ImGuiCond_FirstUseEver);
    if (ImGui::Begin((TR("panel.liveMonitor") + std::string("###Live Monitor")).c_str(), open))
    {
        float w = ImGui::GetContentRegionAvail().x;
        float left = std::max(260.f, w * 0.42f);
        ImGui::BeginChild("##left", ImVec2(left, 0), ImGuiChildFlags_None);
        drawTimeline();
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("##right", ImVec2(0, 0), ImGuiChildFlags_None);
        drawFeed();
        ImGui::EndChild();
    }
    ImGui::End();
}

} // namespace ui
} // namespace evobox
