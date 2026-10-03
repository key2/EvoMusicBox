#include "ui/panels/WorkspacePanel.h"
#include "ui/I18n.h"
#include "ui/Icons.h"
#include "ui/Theme.h"
#include "ui/widgets/Common.h"
#include "ui/widgets/Tiles.h"
#include "util/Strings.h"
#include "util/TimeFormat.h"
#include <algorithm>
#include <chrono>
#include <cstring>

namespace evobox
{
namespace ui
{

void GiftGalleryTab::header()
{
    const auto& th = theme::colors();
    if (!usernameInit_)
    {
        strncpy(username_, app_.prefs.tiktokUsername.c_str(), sizeof(username_) - 1);
        usernameInit_ = true;
    }
    LiveState st = app_.live.state;
    bool connected = st == LiveState::Connected;
    bool connecting = st == LiveState::Connecting;

    // @username field
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("@");
    ImGui::SameLine(0, 2);
    ImGui::SetNextItemWidth(180 * theme::scale());
    ImGui::BeginDisabled(connected || connecting);
    bool enter = ImGui::InputTextWithHint("##user", TR("gift.usernameHint"), username_, sizeof(username_), ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (connected || connecting)
    {
        if (ImGui::Button((std::string(ICON_PH_WIFI_SLASH " ") + TR("gift.disconnect")).c_str())) app_.liveDisconnect();
    }
    else
    {
        if (AccentButton((std::string(ICON_PH_BROADCAST " ") + TR("gift.connect")).c_str()) || enter)
        {
            std::string u = str::cleanUsername(username_);
            if (!u.empty()) app_.liveConnect(u);
        }
    }
    ImGui::SameLine();
    // state dot + text
    ImVec4 dot = th.textFaint;
    std::string stateText = liveStateLabel(st);
    switch (st)
    {
    case LiveState::Connected: dot = th.playing; stateText = evobox::trFmt("gift.connected", str::groupThousands(app_.live.viewers)); break;
    case LiveState::Connecting: dot = th.warning; break;
    case LiveState::Ended: dot = th.textDim; stateText = TR("gift.streamEnded"); break;
    case LiveState::Error: dot = th.danger; stateText = evobox::trFmt("gift.errorPrefix", app_.live.errorMessage); break;
    default: break;
    }
    Dot(dot, 5.f);
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    if (st == LiveState::Error) ImGui::TextColored(th.danger, "%s", ellipsize(stateText, 320 * theme::scale()).c_str());
    else TextDim("%s", stateText.c_str());
    if (st == LiveState::Error && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) ImGui::SetTooltip("%s", app_.live.errorMessage.c_str());

    // right: catalog info + refresh
    std::string catInfo = str::format(TR("gift.catalogCount"), (size_t)app_.catalog.size());
    if (app_.catalog.lastUpdateUnix)
    {
        long long nowUnix = (long long)std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        catInfo += evobox::trFmt("gift.catalogUpdated", formatAgo((double)(nowUnix - app_.catalog.lastUpdateUnix)));
    }
    float rw = ImGui::CalcTextSize(catInfo.c_str()).x + ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x * 2;
    ImGui::SameLine(ImGui::GetContentRegionMax().x - rw);
    ImGui::AlignTextToFramePadding();
    TextDim("%s", catInfo.c_str());
    ImGui::SameLine();
    if (IconButton(ICON_PH_ARROWS_CLOCKWISE, TR("gift.refreshTooltip"), ImVec2(ImGui::GetFrameHeight(), 0), !username_[0] == false))
        app_.liveRefreshCatalog();

    // second row: search · sort · simulate selected
    ImGui::SetNextItemWidth(std::clamp(ImGui::GetContentRegionAvail().x * 0.35f, 140.f, 320.f));
    static char search[64] = "";
    if (ImGui::InputTextWithHint("##gsearch", (std::string(ICON_PH_MAGNIFYING_GLASS " ") + TR("gift.searchHint")).c_str(), search, sizeof(search)))
        app_.giftSearch = search;
    if (ImGui::IsItemActive() && ImGui::IsKeyPressed(ImGuiKey_Escape)) { search[0] = 0; app_.giftSearch.clear(); }
    ImGui::SameLine();
    const char* sorts[] = { TR("gift.sort.name"), TR("gift.sort.diamonds"), TR("gift.sort.configured"), TR("gift.sort.received") };
    int sort = (int)app_.giftSort;
    ImGui::SetNextItemWidth(170 * theme::scale());
    if (ImGui::Combo("##gsort", &sort, sorts, 4)) app_.giftSort = (GiftSort)sort;
    ImGui::SameLine();
    GiftAction* selGift = app_.selectedGift();
    ImGui::BeginDisabled(!selGift);
    if (ImGui::Button((std::string(ICON_PH_PLAY " ") + TR("gift.simulateSelected")).c_str()) && selGift) app_.simulateGift(selGift->giftId);
    ImGui::EndDisabled();
    ImGui::Separator();
}

void GiftGalleryTab::rebuildSorted()
{
    sorted_.clear();
    for (auto& [id, g] : app_.catalog.gifts)
    {
        GiftAction* a = app_.project.giftActions.find(id);
        bool configured = a && (a->actions().hasCommands() || a->soundUid);
        switch (app_.giftFilter)
        {
        case GiftFilter::Configured: if (!configured) continue; break;
        case GiftFilter::ReceivedThisSession: if (g.receivedCount == 0) continue; break;
        case GiftFilter::Tier1: if (g.diamondCount >= 100) continue; break;
        case GiftFilter::Tier2: if (g.diamondCount < 100 || g.diamondCount >= 1000) continue; break;
        case GiftFilter::Tier3: if (g.diamondCount < 1000) continue; break;
        default: break;
        }
        if (!app_.giftSearch.empty() && !str::icontains(g.name, app_.giftSearch) && std::to_string(g.id).find(app_.giftSearch) == std::string::npos) continue;
        sorted_.push_back(&g);
    }
    auto configuredRank = [&](const GiftInfo* g)
    {
        GiftAction* a = app_.project.giftActions.find(g->id);
        return a && (a->actions().hasCommands() || a->soundUid) ? 0 : 1;
    };
    std::stable_sort(sorted_.begin(), sorted_.end(), [&](const GiftInfo* a, const GiftInfo* b)
    {
        switch (app_.giftSort)
        {
        case GiftSort::Name: return str::lower(a->name) < str::lower(b->name);
        case GiftSort::Diamonds: return a->diamondCount != b->diamondCount ? a->diamondCount < b->diamondCount : str::lower(a->name) < str::lower(b->name);
        case GiftSort::ConfiguredFirst:
        {
            int ra = configuredRank(a), rb = configuredRank(b);
            if (ra != rb) return ra < rb;
            return a->diamondCount != b->diamondCount ? a->diamondCount < b->diamondCount : str::lower(a->name) < str::lower(b->name);
        }
        case GiftSort::Recent: return a->lastReceivedTime > b->lastReceivedTime;
        }
        return false;
    });
    sortedRevision_ = app_.catalog.revision;
    sortedFilter_ = app_.giftFilter;
    sortedSort_ = app_.giftSort;
    sortedSearch_ = app_.giftSearch;
    sortedConfiguredCount_ = (int)app_.project.giftActions.items.size();
    sortedReceived_ = app_.live.giftsReceived;
}

void GiftGalleryTab::giftContextMenu(GiftInfo& g)
{
    GiftAction* a = app_.project.giftActions.find(g.id);
    if (ImGui::MenuItem((std::string(ICON_PH_PLAY "  ") + TR("gift.ctx.simulate")).c_str())) app_.simulateGift(g.id);
    if (ImGui::MenuItem((std::string(ICON_PH_PLAY "  ") + TR("gift.ctx.simulateStreak")).c_str())) app_.simulateGift(g.id, 5);
    ImGui::Separator();
    static nlohmann::json s_clipboard;
    if (ImGui::MenuItem((std::string(ICON_PH_COPY "  ") + TR("gift.ctx.copyActions")).c_str(), nullptr, false, a != nullptr)) s_clipboard = a->actions().save();
    if (ImGui::MenuItem((std::string(ICON_PH_CLIPBOARD "  ") + TR("gift.ctx.pasteActions")).c_str(), nullptr, false, !s_clipboard.is_null()))
    {
        GiftAction* target = app_.selectGift(g.id);
        if (target)
        {
            nlohmann::json before = target->actions().save();
            nlohmann::json after = s_clipboard;
            GiftAction* tp = target;
            organic::UndoManager::get().perform(TR("gift.pasteActions"),
                [tp, after] { tp->actions().clearCommands(); tp->actions().load(after); notifyStructureChanged(tp); },
                [tp, before] { tp->actions().clearCommands(); tp->actions().load(before); notifyStructureChanged(tp); }, { tp });
        }
    }
    if (ImGui::MenuItem((std::string(ICON_PH_TRASH "  ") + TR("gift.ctx.clearActions")).c_str(), nullptr, false, a != nullptr))
    {
        GiftAction* ap = a;
        confirm.open(evobox::trFmt("gift.clearActions.title", g.name), TR("gift.clearActions.body"),
                     [this, ap] { app_.project.giftActions.undoableRemove({ ap }); }, TR("dialog.clear"));
    }
    if (ImGui::BeginMenu((std::string(ICON_PH_SPEAKER_HIGH "  ") + TR("gift.ctx.assignSound")).c_str()))
    {
        GiftAction* target = a;
        if (ImGui::MenuItem(TR("gift.assignNone"), nullptr, !target || target->soundUid == 0))
            if (target) target->setSoundUidUndoable(0);
        for (Sound* s : app_.project.sounds.sounds())
        {
            ImGui::PushID((void*)s);
            std::string lbl = icons::stickerText(s->sticker()) + "  " + s->niceName;
            if (ImGui::MenuItem(lbl.c_str(), nullptr, target && target->soundUid == s->uid))
            {
                GiftAction* t2 = app_.selectGift(g.id);
                if (t2) t2->setSoundUidUndoable(s->uid);
            }
            ImGui::PopID();
        }
        ImGui::EndMenu();
    }
}

void GiftGalleryTab::gallery()
{
    if (sortedRevision_ != app_.catalog.revision || sortedFilter_ != app_.giftFilter || sortedSort_ != app_.giftSort ||
        sortedSearch_ != app_.giftSearch || sortedConfiguredCount_ != (int)app_.project.giftActions.items.size() ||
        sortedReceived_ != app_.live.giftsReceived)
        rebuildSorted();

    ImGui::BeginChild("##gallery");
    if (app_.catalog.empty())
    {
        EmptyState(TR("gift.empty.noCatalog"), TR("gift.empty.connectOnce"), ICON_PH_GIFT);
        ImGui::EndChild();
        return;
    }
    if (sorted_.empty())
    {
        EmptyState(TR("gift.empty.noMatch"), TR("gift.empty.changeFilter"), ICON_PH_FUNNEL);
        ImGui::EndChild();
        return;
    }
    ImVec2 tile = tileSizeFor(app_.prefs.tileSize);
    float spacing = ImGui::GetStyle().ItemSpacing.x;
    int cols = std::max(1, (int)((ImGui::GetContentRegionAvail().x + spacing) / (tile.x + spacing)));
    int i = 0;
    double now = app_.now();
    GiftAction* selected = app_.selectedGift();
    static int64_t s_ctxGift = 0;
    for (GiftInfo* g : sorted_)
    {
        if (i % cols != 0) ImGui::SameLine();
        // lazy icon request for visible tiles only
        ImVec2 pos = ImGui::GetCursorScreenPos();
        bool visible = ImGui::IsRectVisible(pos, ImVec2(pos.x + tile.x, pos.y + tile.y));
        if (visible) app_.requestGiftIcon(*g);
        GiftAction* a = app_.project.giftActions.find(g->id);
        GiftAction* eff = a ? a : (app_.transientGift() && app_.transientGift()->giftId == g->id ? app_.transientGift() : nullptr);
        GiftTileState st;
        st.selected = selected && selected->giftId == g->id;
        st.configuredCommands = a ? (int)a->actions().commandCount() : 0;
        st.hasSound = a && a->soundUid != 0;
        st.active = eff && eff->rt.active;
        st.now = now;
        st.lastPulse = g->lastReceivedTime;
        st.receivedCount = g->receivedCount;
        TileAction act = GiftTile(*g, tile, st);
        switch (act)
        {
        case TileAction::Select: app_.selectGift(g->id); break;
        case TileAction::DoubleClick: app_.selectGift(g->id); app_.simulateGift(g->id); break;
        case TileAction::ContextMenu: app_.selectGift(g->id); s_ctxGift = g->id; ImGui::OpenPopup("##giftctx"); break;
        default: break;
        }
        i++;
    }
    if (ImGui::BeginPopup("##giftctx"))
    {
        if (GiftInfo* g = app_.catalog.find(s_ctxGift)) giftContextMenu(*g);
        else ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::EndChild();
}

void GiftGalleryTab::draw()
{
    header();
    gallery();
    confirm.draw();
}

} // namespace ui
} // namespace evobox
