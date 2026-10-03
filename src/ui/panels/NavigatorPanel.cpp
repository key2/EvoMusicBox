#include "ui/panels/NavigatorPanel.h"
#include "ui/I18n.h"
#include "ui/Icons.h"
#include "ui/Theme.h"
#include "ui/widgets/StickerPicker.h"
#include "ui/widgets/Tiles.h"
#include "util/Strings.h"
#include "imgui_stdlib.h"
#include <cstdio>

namespace evobox
{
namespace ui
{

void NavigatorPanel::draw(bool* open)
{
    ImGui::SetNextWindowSize(ImVec2(240, 500), ImGuiCond_FirstUseEver);
    if (ImGui::Begin((TR("panel.navigator") + std::string("###Navigator")).c_str(), open))
    {
        if (app_.prefs.activeTab == WorkspaceTab::Gifts && app_.tiktokAvailable()) drawGiftNavigator();
        else drawCategories();
        confirm_.draw();
    }
    ImGui::End();
}

// ---------------------------------------------------------------- categories
bool NavigatorPanel::categoryRow(Category* c, bool isAll, const char* label, const char* icon, ImVec4 color, size_t count, Uid uid)
{
    const auto& th = theme::colors();
    if (isAll) ImGui::PushID("all"); else ImGui::PushID((void*)c);
    bool selected = app_.selectedCategoryUid == uid;
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    float h = ImGui::GetFrameHeight() + 6 * theme::scale();
    float w = ImGui::GetContentRegionAvail().x;
    bool clicked = false;

    // rename mode
    if (!isAll && renamingUid_ == uid)
    {
        ImGui::SetNextItemWidth(-1);
        ImGui::SetKeyboardFocusHere();
        if (ImGui::InputText("##rename", &renameBuf_, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll))
        {
            std::string oldName = c->niceName, newName = str::trim(renameBuf_);
            if (!newName.empty() && newName != oldName)
            {
                Category* cat = c;
                organic::UndoManager::get().perform(TR("navigator.renameCategory"),
                    [cat, newName] { cat->setNiceName(newName); notifyStructureChanged(cat); },
                    [cat, oldName] { cat->setNiceName(oldName); notifyStructureChanged(cat); }, { cat });
            }
            renamingUid_ = 0;
        }
        if (ImGui::IsItemDeactivated() && renamingUid_ == uid) renamingUid_ = 0;
        ImGui::PopID();
        return false;
    }

    ImGui::InvisibleButton("##row", ImVec2(w, h));
    bool hovered = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (selected) dl->AddRectFilled(p0, ImVec2(p0.x + w, p0.y + h), theme::u32(th.accent, 0.22f), 6.f);
    else if (hovered) dl->AddRectFilled(p0, ImVec2(p0.x + w, p0.y + h), theme::u32(ImVec4(1, 1, 1, 0.05f)), 6.f);
    if (selected) dl->AddRectFilled(p0, ImVec2(p0.x + 3, p0.y + h), theme::u32(th.accent), 2.f);
    float ty = p0.y + (h - ImGui::GetTextLineHeight()) * 0.5f;
    dl->AddText(ImVec2(p0.x + 12, ty), theme::u32(theme::lighten(color, 0.2f)), icon);
    dl->AddText(ImVec2(p0.x + 12 + ImGui::GetFontSize() * 1.5f, ty), theme::u32(selected ? th.text : th.textDim), label);
    std::string cnt = std::to_string(count);
    ImVec2 cs = ImGui::CalcTextSize(cnt.c_str());
    dl->AddText(ImVec2(p0.x + w - cs.x - 10, ty), theme::u32(th.textFaint), cnt.c_str());

    if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) { app_.selectedCategoryUid = uid; clicked = true; }
    if (!isAll && hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) { renamingUid_ = uid; renameBuf_ = c->niceName; }

    // drop target: move sounds here (UI.md §35)
    if (ImGui::BeginDragDropTarget())
    {
        if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("EVOBOX_SOUND"))
        {
            Uid dragged = *(const Uid*)pl->Data;
            if (!isAll)
                if (Sound* s = app_.project.sounds.find(dragged)) app_.project.sounds.moveToCategoryUndoable({ s }, uid);
        }
        if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("EVOBOX_CATEGORY"))
        {
            Uid dragged = *(const Uid*)pl->Data;
            if (!isAll && dragged != uid)
            {
                Category* from = app_.project.categories.find(dragged);
                int fi = from ? app_.project.categories.indexOf(from) : -1;
                int ti = app_.project.categories.indexOf(c);
                if (fi >= 0 && ti >= 0) app_.project.categories.undoableMove(fi, ti);
            }
        }
        ImGui::EndDragDropTarget();
    }
    // drag source: reorder categories
    if (!isAll && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceNoDisableHover))
    {
        ImGui::SetDragDropPayload("EVOBOX_CATEGORY", &uid, sizeof(uid));
        ImGui::TextUnformatted(label);
        ImGui::EndDragDropSource();
    }
    // context menu
    if (!isAll && ImGui::BeginPopupContextItem("##catctx"))
    {
        if (ImGui::MenuItem((std::string(ICON_PH_PENCIL_SIMPLE "  ") + TR("navigator.ctx.rename")).c_str())) { renamingUid_ = uid; renameBuf_ = c->niceName; }
        if (ImGui::BeginMenu((std::string(ICON_PH_SMILEY "  ") + TR("navigator.ctx.icon")).c_str()))
        {
            std::string icn = c->icon();
            if (StickerPickerBody("caticon", icn, c->color())) c->iconP->setUndoable(icn);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu((std::string(ICON_PH_PALETTE "  ") + TR("navigator.ctx.colour")).c_str()))
        {
            static ImVec4 s_oldCol;
            static Category* s_colCat = nullptr;
            ImVec4 col = c->color();
            float f[4] = { col.x, col.y, col.z, col.w };
            if (ImGui::ColorPicker3("##catcolor", f, ImGuiColorEditFlags_NoSidePreview | ImGuiColorEditFlags_NoInputs))
            {
                if (s_colCat != c) { s_colCat = c; s_oldCol = col; }
                c->colorP->setValue(ImVec4(f[0], f[1], f[2], 1.f));
            }
            if (ImGui::IsItemActivated()) { s_colCat = c; s_oldCol = col; }
            if (ImGui::IsItemDeactivatedAfterEdit() && s_colCat == c) { c->colorP->recordEdit(s_oldCol, c->colorP->value); s_colCat = nullptr; }
            ImGui::EndMenu();
        }
        ImGui::Separator();
        int idx = app_.project.categories.indexOf(c);
        if (ImGui::MenuItem((std::string(ICON_PH_CARET_UP "  ") + TR("navigator.ctx.moveUp")).c_str(), nullptr, false, idx > 0)) app_.project.categories.undoableMove(idx, idx - 1);
        if (ImGui::MenuItem((std::string(ICON_PH_CARET_DOWN "  ") + TR("navigator.ctx.moveDown")).c_str(), nullptr, false, idx + 1 < (int)app_.project.categories.items.size())) app_.project.categories.undoableMove(idx, idx + 1);
        ImGui::Separator();
        bool deletable = app_.project.categoryDeletable(c);
        if (ImGui::MenuItem((std::string(ICON_PH_TRASH "  ") + TR("navigator.ctx.deleteCategory")).c_str(), nullptr, false, deletable))
        {
            size_t n = app_.project.sounds.countInCategory(uid);
            Category* cat = c;
            if (n == 0) app_.project.deleteCategoryUndoable(cat);
            else
            {
                std::string moveTarget = app_.project.categories.customCategory() ? app_.project.categories.customCategory()->niceName : TR("navigator.customCategory");
                char bodyBuf[256];
                std::snprintf(bodyBuf, sizeof(bodyBuf), TR("navigator.deleteCategory.body"), (int)n, moveTarget.c_str());
                confirm_.open(evobox::trFmt("navigator.deleteCategory.title", c->niceName),
                              std::string(bodyBuf),
                              [this, cat] { app_.project.deleteCategoryUndoable(cat); if (app_.selectedCategoryUid == cat->uid) app_.selectedCategoryUid = 0; });
            }
        }
        ImGui::EndPopup();
    }
    ImGui::PopID();
    return clicked;
}

void NavigatorPanel::drawAddCategory()
{
    const auto& th = theme::colors();
    if (!addingCategory_)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, th.textDim);
        if (GhostButton((std::string(ICON_PH_PLUS "  ") + TR("navigator.addCategory")).c_str(), ImVec2(-1, 0)))
        {
            addingCategory_ = true;
            newName_[0] = 0;
            newIcon_ = "ph:folder";
        }
        ImGui::PopStyleColor();
        return;
    }
    ImGui::BeginChild("##addcat", ImVec2(0, 0), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Borders);
    TextDim("%s", TR("navigator.newCategory"));
    StickerButton("newicon", newIcon_, newColor_, ImVec2(ImGui::GetFrameHeight() * 1.6f, ImGui::GetFrameHeight() * 1.6f));
    ImGui::SameLine();
    float f[3] = { newColor_.x, newColor_.y, newColor_.z };
    if (ImGui::ColorEdit3("##newcol", f, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel)) newColor_ = ImVec4(f[0], f[1], f[2], 1.f);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);
    if (!ImGui::IsAnyItemActive() && !ImGui::IsMouseClicked(0)) ImGui::SetKeyboardFocusHere();
    bool enter = ImGui::InputTextWithHint("##newname", TR("navigator.categoryNameHint"), newName_, sizeof(newName_), ImGuiInputTextFlags_EnterReturnsTrue);
    bool create = enter || AccentButton(TR("navigator.create"), ImVec2(90 * theme::scale(), 0));
    ImGui::SameLine();
    if (ImGui::Button(TR("dialog.cancel")) || ImGui::IsKeyPressed(ImGuiKey_Escape)) addingCategory_ = false;
    if (create)
    {
        std::string name = str::trim(newName_);
        if (!name.empty())
        {
            Category* c = app_.project.categories.addCategoryUndoable(name, newIcon_, newColor_);
            if (c) app_.selectedCategoryUid = c->uid;
            addingCategory_ = false;
        }
    }
    ImGui::EndChild();
}

void NavigatorPanel::drawCategories()
{
    Project& p = app_.project;
    SectionHeader(TR("navigator.categories"));
    categoryRow(nullptr, true, TR("navigator.allSounds"), ICON_PH_SQUARES_FOUR, theme::colors().accent, p.sounds.items.size(), 0);
    for (Category* c : p.categories.categories())
        categoryRow(c, false, c->niceName.c_str(), icons::stickerText(c->icon()).c_str(), c->color(), p.sounds.countInCategory(c->uid), c->uid);
    // repair a deleted selection
    if (app_.selectedCategoryUid && !p.categories.find(app_.selectedCategoryUid)) app_.selectedCategoryUid = 0;
    Spacer(4);
    drawAddCategory();

    // whole-panel drop zone for OS files: import into the selected category
    {
        ImVec2 mn = ImGui::GetWindowPos(), mx = ImVec2(mn.x + ImGui::GetWindowSize().x, mn.y + ImGui::GetWindowSize().y);
        auto files = app_.drops.claim(mn, mx);
        if (!files.empty()) app_.importer.importFiles(files, app_.selectedCategoryUid);
    }
}

// ---------------------------------------------------------------- gifts navigator
void NavigatorPanel::drawGiftNavigator()
{
    const auto& th = theme::colors();
    SectionHeader(TR("navigator.gifts"));
    struct F { GiftFilter f; const char* icon; const char* label; };
    const F filters[] = {
        { GiftFilter::All,                 ICON_PH_GIFT,        TR("navigator.filter.allGifts") },
        { GiftFilter::Configured,          ICON_PH_LIGHTNING,   TR("navigator.filter.configured") },
        { GiftFilter::ReceivedThisSession, ICON_PH_BROADCAST,   TR("navigator.filter.received") },
        { GiftFilter::Tier1,               ICON_PH_DIAMOND,     TR("navigator.filter.tier1") },
        { GiftFilter::Tier2,               ICON_PH_DIAMOND,     TR("navigator.filter.tier2") },
        { GiftFilter::Tier3,               ICON_PH_CROWN,       TR("navigator.filter.tier3") },
    };
    for (auto& f : filters)
    {
        bool sel = app_.giftFilter == f.f;
        ImGui::PushID((int)f.f);
        if (sel) ImGui::PushStyleColor(ImGuiCol_Header, theme::withAlpha(th.accent, 0.22f));
        std::string lbl = std::string(f.icon) + "  " + f.label;
        if (ImGui::Selectable(lbl.c_str(), sel)) app_.giftFilter = f.f;
        if (sel) ImGui::PopStyleColor();
        ImGui::PopID();
    }
    Spacer(8);
    SectionHeader(TR("navigator.roomEvents"));
    for (RoomEventAction* a : app_.project.roomEvents.actions())
    {
        ImGui::PushID((void*)a);
        bool sel = a->isSelected();
        float pl = pulse(a->rt.lastPulseTime, app_.now(), 0.6);
        if (pl > 0) ImGui::PushStyleColor(ImGuiCol_Header, theme::withAlpha(th.live, 0.35f * pl));
        std::string lbl = std::string(roomEventIcon(a->kind)) + "  " + a->niceName;
        size_t n = a->actions().commandCount();
        if (n || a->soundUid) lbl += "   " + std::string(n ? std::to_string(n) : "") + (a->soundUid ? " " ICON_PH_SPEAKER_HIGH : "");
        if (a->rt.active) lbl += "  " ICON_PH_TIMER;
        if (ImGui::Selectable(lbl.c_str(), sel || pl > 0)) app_.select(a);
        if (pl > 0) ImGui::PopStyleColor();
        if (ImGui::BeginPopupContextItem("##rectx"))
        {
            if (ImGui::MenuItem(ICON_PH_PLAY "  Simulate")) app_.simulateRoomEvent(a->kind);
            ImGui::EndPopup();
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
        {
            if (a->kind == RoomEventKind::Like) ImGui::SetTooltip(TR("navigator.tooltip.likeEvery"), a->threshold(), a->rt.receivedCount);
            else ImGui::SetTooltip(TR("navigator.tooltip.received"), a->rt.receivedCount);
        }
        ImGui::PopID();
    }
    Spacer(8);
    size_t configured = app_.project.giftActions.items.size();
    TextDim(TR("navigator.giftsConfigured"), configured, configured == 1 ? TR("plural.empty") : TR("plural.s"));
}

} // namespace ui
} // namespace evobox
