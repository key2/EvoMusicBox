#include "ui/panels/WorkspacePanel.h"
#include "ui/I18n.h"
#include "ui/Icons.h"
#include "ui/Theme.h"
#include "ui/widgets/StickerPicker.h"
#include "ui/widgets/Tiles.h"
#include "util/Strings.h"
#include "util/TimeFormat.h"
#include "imgui_stdlib.h"
#include <algorithm>

namespace evobox
{
namespace ui
{

std::vector<Sound*> SoundboardTab::visibleSounds() const
{
    std::vector<Sound*> out;
    for (Sound* s : app_.project.sounds.soundsInCategory(app_.selectedCategoryUid))
        if (app_.soundSearch.empty() || str::icontains(s->niceName, app_.soundSearch)) out.push_back(s);
    return out;
}

void SoundboardTab::moveSelection(int dx, int dy)
{
    auto vis = visibleSounds();
    if (vis.empty()) return;
    Sound* cur = app_.selectedSound();
    int idx = -1;
    for (size_t i = 0; i < vis.size(); i++) if (vis[i] == cur) idx = (int)i;
    int cols = std::max(1, lastColumns_);
    int ni = idx < 0 ? 0 : idx + dx + dy * cols;
    ni = std::clamp(ni, 0, (int)vis.size() - 1);
    app_.select(vis[(size_t)ni]);
}

void SoundboardTab::header(const std::vector<Sound*>& visible)
{
    const auto& th = theme::colors();
    Project& p = app_.project;
    Category* cat = p.categories.find(app_.selectedCategoryUid);
    std::string title = cat ? cat->niceName : TR("soundboard.allSounds");
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.35f);
    ImGui::TextUnformatted(title.c_str());
    ImGui::PopFont();
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    TextDim(TR("soundboard.clips"), (size_t)visible.size(), visible.size() == 1 ? TR("plural.empty") : TR("plural.s"));

    // right side controls
    std::string addLabel = std::string(ICON_PH_PLUS " ") + TR("soundboard.addSound");
    float addW = ImGui::CalcTextSize(addLabel.c_str()).x + ImGui::GetStyle().FramePadding.x * 2;
    float toggleW = ImGui::GetFrameHeight() * 2 + 2;
    float searchW = std::clamp(ImGui::GetContentRegionAvail().x * 0.35f, 140.f, 320.f);
    float spacing = ImGui::GetStyle().ItemSpacing.x;
    float right = ImGui::GetContentRegionMax().x;
    ImGui::SameLine(right - addW - toggleW - searchW - spacing * 2);
    if (focusSearch_) { ImGui::SetKeyboardFocusHere(); focusSearch_ = false; }
    ImGui::SetNextItemWidth(searchW);
    ImGui::InputTextWithHint("##search", (std::string(ICON_PH_MAGNIFYING_GLASS " ") + TR("soundboard.searchHint")).c_str(), &app_.soundSearch);
    if (ImGui::IsItemActive() && ImGui::IsKeyPressed(ImGuiKey_Escape)) app_.soundSearch.clear();
    ImGui::SameLine();
    bool grid = app_.prefs.viewMode == ViewMode::Grid;
    if (ToggleButton(ICON_PH_SQUARES_FOUR, grid, ImVec2(ImGui::GetFrameHeight(), 0), TR("soundboard.gridView"))) app_.prefs.viewMode = ViewMode::Grid;
    ImGui::SameLine(0, 2);
    if (ToggleButton(ICON_PH_LIST, !grid, ImVec2(ImGui::GetFrameHeight(), 0), TR("soundboard.listView"))) app_.prefs.viewMode = ViewMode::List;
    ImGui::SameLine();
    if (AccentButton(addLabel.c_str(), ImVec2(addW, 0))) wantOpenFileDialog = true;
    (void)th;
    ImGui::Separator();
}

void SoundboardTab::tileActions(Sound& s, int action)
{
    switch ((TileAction)action)
    {
    case TileAction::Select: app_.select(&s); break;
    case TileAction::Play: app_.select(&s); app_.playback.play(s, TriggerSource::Tile); break;
    case TileAction::Stop: app_.playback.stop(s.uid); break;
    case TileAction::DoubleClick: app_.select(&s); app_.playback.play(s, TriggerSource::Tile); break;
    case TileAction::ContextMenu: app_.select(&s); contextUid_ = s.uid; ImGui::OpenPopup("##tilectx"); break;
    default: break;
    }
}

void SoundboardTab::contextMenu(Sound& s)
{
    Project& p = app_.project;
    if (ImGui::MenuItem((std::string(ICON_PH_PLAY "  ") + TR("soundboard.ctx.play")).c_str())) app_.playback.play(s, TriggerSource::Tile);
    if (s.playing() && ImGui::MenuItem((std::string(ICON_PH_STOP "  ") + TR("soundboard.ctx.stop")).c_str())) app_.playback.stop(s.uid);
    bool effect = s.isEffect();
    if (ImGui::MenuItem((std::string(ICON_PH_SPARKLE "  ") + TR("soundboard.ctx.effect")).c_str(), nullptr, effect)) s.isEffectP->setUndoable(!effect);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
        ImGui::SetTooltip("%s", TR("soundboard.effectTooltip"));
    if (ImGui::MenuItem((std::string(ICON_PH_SCISSORS "  ") + TR("soundboard.ctx.editClip")).c_str())) { app_.select(&s); app_.focusBottomPanel = "Clip Editor"; }
    if (ImGui::MenuItem((std::string(ICON_PH_PENCIL_SIMPLE "  ") + TR("soundboard.ctx.rename")).c_str())) { renamingUid_ = s.uid; renameBuf_ = s.niceName; }
    if (ImGui::BeginMenu((std::string(ICON_PH_SMILEY "  ") + TR("soundboard.ctx.changeSticker")).c_str()))
    {
        std::string st = s.sticker();
        if (StickerPickerBody("tilesticker", st, s.color(), &s)) s.stickerP->setUndoable(st);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu((std::string(ICON_PH_PALETTE "  ") + TR("soundboard.ctx.colour")).c_str()))
    {
        static const ImVec4 palette[] = {
            ImVec4(0.36f, 0.56f, 0.95f, 1), ImVec4(0.30f, 0.72f, 0.62f, 1), ImVec4(0.95f, 0.62f, 0.25f, 1),
            ImVec4(0.93f, 0.33f, 0.36f, 1), ImVec4(0.85f, 0.35f, 0.55f, 1), ImVec4(0.62f, 0.55f, 0.90f, 1),
            ImVec4(0.55f, 0.75f, 0.30f, 1), ImVec4(0.95f, 0.85f, 0.30f, 1), ImVec4(0.55f, 0.60f, 0.68f, 1) };
        for (int i = 0; i < 9; i++)
        {
            ImGui::PushID(i);
            if (i % 3) ImGui::SameLine();
            if (ImGui::ColorButton("##c", palette[i], ImGuiColorEditFlags_NoTooltip, ImVec2(28, 28))) s.colorP->setUndoable(palette[i]);
            ImGui::PopID();
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu((std::string(ICON_PH_FOLDER "  ") + TR("soundboard.ctx.moveToCategory")).c_str()))
    {
        for (Category* c : p.categories.categories())
        {
            bool cur = s.categoryUid == c->uid;
            std::string lbl = icons::stickerText(c->icon()) + "  " + c->niceName;
            if (ImGui::MenuItem(lbl.c_str(), nullptr, cur) && !cur) p.sounds.moveToCategoryUndoable({ &s }, c->uid);
        }
        ImGui::EndMenu();
    }
    ImGui::Separator();
    static nlohmann::json s_oscClipboard;
    if (ImGui::MenuItem((std::string(ICON_PH_COPY "  ") + TR("soundboard.ctx.copyOsc")).c_str(), nullptr, false, s.actions().hasCommands())) s_oscClipboard = s.actions().save();
    if (ImGui::MenuItem((std::string(ICON_PH_CLIPBOARD "  ") + TR("soundboard.ctx.pasteOsc")).c_str(), nullptr, false, !s_oscClipboard.is_null()))
    {
        nlohmann::json before = s.actions().save(), after = s_oscClipboard;
        Sound* sp = &s;
        organic::UndoManager::get().perform(TR("soundboard.ctx.pasteOsc"),
            [sp, after] { sp->actions().clearCommands(); sp->actions().load(after); notifyStructureChanged(sp); },
            [sp, before] { sp->actions().clearCommands(); sp->actions().load(before); notifyStructureChanged(sp); }, { sp });
    }
    ImGui::Separator();
    if (ImGui::MenuItem((std::string(ICON_PH_COPY "  ") + TR("soundboard.ctx.duplicate")).c_str())) app_.importer.duplicateSound(s);
    if (ImGui::MenuItem((std::string(ICON_PH_TRASH "  ") + TR("soundboard.ctx.delete")).c_str()))
    {
        Sound* sp = &s;
        confirm.open(evobox::trFmt("inspector.deleteSound.title", s.niceName), TR("inspector.deleteSound.body"),
                     [this, sp] { app_.deleteSounds({ sp }); });
    }
}

void SoundboardTab::grid(const std::vector<Sound*>& visible)
{
    ImVec2 tile = tileSizeFor(app_.performanceMode ? TileSize::Large : app_.prefs.tileSize);
    float spacing = ImGui::GetStyle().ItemSpacing.x;
    float avail = ImGui::GetContentRegionAvail().x;
    int cols = std::max(1, (int)((avail + spacing) / (tile.x + spacing)));
    lastColumns_ = cols;
    int i = 0;
    Uid dropUid = 0;
    bool dropAfter = false;
    Sound* dropTarget = nullptr;
    double now = app_.now();
    for (Sound* s : visible)
    {
        if (i % cols != 0) ImGui::SameLine();
        SoundTileState st;
        st.selected = s->isSelected();
        st.playing = s->playing();
        st.progress01 = s->rt.progress01;
        st.missingMedia = s->rt.mediaStatus == MediaStatus::SourceMissing || (s->rt.mediaStatus == MediaStatus::ClipMissing && !s->hasClip());
        st.decoding = s->rt.mediaStatus == MediaStatus::Decoding;
        st.now = now;
        st.lastOscPulse = s->rt.lastOscPulseTime;
        st.lastTrigger = s->rt.lastTriggerTime;
        st.oscCommandCount = s->actions().commandCount();
        st.performanceMode = app_.performanceMode;
        st.effect = s->isEffect();
        st.voices = s->rt.activeVoices;
        Uid du = 0; bool da = false;
        TileAction a = SoundTile(*s, tile, st, &du, &da);
        if (du) { dropUid = du; dropAfter = da; dropTarget = s; }
        if (a != TileAction::None) tileActions(*s, (int)a);
        // inline rename popup anchored to the tile
        if (renamingUid_ == s->uid)
        {
            ImGui::SetNextWindowPos(ImVec2(ImGui::GetItemRectMin().x, ImGui::GetItemRectMax().y + 4));
            ImGui::OpenPopup("##rename");
        }
        i++;
    }
    if (!app_.performanceMode)
    {
        if (i % cols != 0) ImGui::SameLine();
        bool highlight = app_.drops.pending();
        ImVec2 addMin = ImGui::GetCursorScreenPos();
        if (AddSoundTile(tile, highlight)) wantOpenFileDialog = true;
        auto files = app_.drops.claim(addMin, ImVec2(addMin.x + tile.x, addMin.y + tile.y));
        if (!files.empty()) app_.importer.importFiles(files, app_.selectedCategoryUid);
    }
    if (dropUid && dropTarget)
        if (Sound* dragged = app_.project.sounds.find(dropUid))
        {
            if (dragged->categoryUid != dropTarget->categoryUid && app_.selectedCategoryUid != 0)
                app_.project.sounds.moveToCategoryUndoable({ dragged }, dropTarget->categoryUid);
            app_.project.sounds.reorderUndoable(dragged, dropTarget, dropAfter);
        }
    // rename popup
    if (renamingUid_)
    {
        Sound* rs = app_.project.sounds.find(renamingUid_);
        if (!rs) renamingUid_ = 0;
        else if (ImGui::BeginPopup("##rename"))
        {
            ImGui::SetNextItemWidth(220 * theme::scale());
            if (!ImGui::IsAnyItemActive()) ImGui::SetKeyboardFocusHere();
            if (ImGui::InputText("##rn", &renameBuf_, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll))
            {
                std::string oldName = rs->niceName, newName = str::trim(renameBuf_);
                if (!newName.empty() && newName != oldName)
                    organic::UndoManager::get().perform(TR("soundboard.renameSound"),
                        [rs, newName] { rs->setNiceName(newName); notifyStructureChanged(rs); },
                        [rs, oldName] { rs->setNiceName(oldName); notifyStructureChanged(rs); }, { rs });
                renamingUid_ = 0;
                ImGui::CloseCurrentPopup();
            }
            if (ImGui::IsKeyPressed(ImGuiKey_Escape)) { renamingUid_ = 0; ImGui::CloseCurrentPopup(); }
            ImGui::EndPopup();
        }
        else renamingUid_ = 0;
    }
}

void SoundboardTab::list(const std::vector<Sound*>& visible)
{
    const auto& th = theme::colors();
    ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable;
    if (ImGui::BeginTable("##list", 6, flags))
    {
        ImGui::TableSetupScrollFreeze(0, 1);
        // play + (effects while playing) stop
        ImGui::TableSetupColumn("##play", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFrameHeight() * 2 + ImGui::GetStyle().ItemSpacing.x);
        ImGui::TableSetupColumn(TR("soundboard.col.name"), ImGuiTableColumnFlags_WidthStretch, 3.f);
        ImGui::TableSetupColumn(TR("soundboard.col.category"), ImGuiTableColumnFlags_WidthStretch, 1.5f);
        ImGui::TableSetupColumn(TR("soundboard.col.duration"), ImGuiTableColumnFlags_WidthFixed, 80.f);
        ImGui::TableSetupColumn(TR("soundboard.col.osc"), ImGuiTableColumnFlags_WidthFixed, 50.f);
        ImGui::TableSetupColumn(TR("soundboard.col.targets"), ImGuiTableColumnFlags_WidthStretch, 2.f);
        ImGui::TableHeadersRow();
        for (Sound* s : visible)
        {
            ImGui::PushID((void*)s);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            bool playing = s->playing();
            bool effect = s->isEffect();
            // music toggles; an effect keeps its play button (press again = stack) and shows a
            // stop button next to it while any of its voices plays (stops all of them)
            bool stopGlyph = playing && !effect;
            ImGui::PushStyleColor(ImGuiCol_Text, playing ? th.playing : th.accent);
            if (GhostButton(stopGlyph ? ICON_PH_STOP : ICON_PH_PLAY, ImVec2(ImGui::GetFrameHeight(), 0)))
            {
                if (stopGlyph) app_.playback.stop(s->uid); else app_.playback.play(*s, TriggerSource::Tile);
            }
            if (effect && playing)
            {
                ImGui::SameLine();
                if (GhostButton(ICON_PH_STOP "##stop", ImVec2(ImGui::GetFrameHeight(), 0))) app_.playback.stop(s->uid);
                if (ImGui::IsItemHovered())
                {
                    if (s->rt.activeVoices > 1) ImGui::SetTooltip(TR("soundboard.stopAllPlays"), s->rt.activeVoices);
                    else ImGui::SetTooltip("%s", TR("soundboard.stopThisEffect"));
                }
            }
            ImGui::PopStyleColor();
            ImGui::TableNextColumn();
            std::string lbl = icons::stickerText(s->sticker()) + "  " + s->niceName + (s->isEffect() ? "  " ICON_PH_SPARKLE : "");
            if (ImGui::Selectable(lbl.c_str(), s->isSelected(), ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap))
                app_.select(s);
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) app_.playback.play(*s, TriggerSource::Tile);
            if (ImGui::BeginPopupContextItem("##lctx")) { contextMenu(*s); ImGui::EndPopup(); }
            ImGui::TableNextColumn();
            Category* c = app_.project.categories.find(s->categoryUid);
            TextDim("%s", c ? c->niceName.c_str() : TR("soundboard.noCategoryDash"));
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(formatTime(s->clipDuration(), true).c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%zu", s->actions().commandCount());
            ImGui::TableNextColumn();
            std::string targets;
            for (Uid tu : s->actions().referencedTargets())
            {
                OscTarget* t = app_.project.oscTargets.resolve(tu);
                std::string n = t ? t->displayName() : TR("soundboard.unknownTarget");
                if (targets.find(n) == std::string::npos) targets += (targets.empty() ? "" : ", ") + n;
            }
            TextDim("%s", targets.c_str());
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}

void SoundboardTab::draw()
{
    Project& p = app_.project;
    auto visible = visibleSounds();
    if (!app_.performanceMode) header(visible);
    else
    {
        Category* cat = p.categories.find(app_.selectedCategoryUid);
        ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.6f);
        TextCentered(cat ? cat->niceName.c_str() : TR("soundboard.allSoundsPerf"));
        ImGui::PopFont();
        // quick category switcher
        if (ImGui::BeginTabBar("##perfcats", ImGuiTabBarFlags_FittingPolicyScroll))
        {
            if (ImGui::BeginTabItem(TR("soundboard.perfAll"))) { app_.selectedCategoryUid = 0; ImGui::EndTabItem(); }
            for (Category* c : p.categories.categories())
            {
                ImGui::PushID((void*)c);
                if (ImGui::BeginTabItem(c->niceName.c_str())) { app_.selectedCategoryUid = c->uid; ImGui::EndTabItem(); }
                ImGui::PopID();
            }
            ImGui::EndTabBar();
        }
    }

    ImVec2 bodyMin = ImGui::GetCursorScreenPos();
    ImGui::BeginChild("##board", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollWithMouse * 0);
    ImVec2 bodyMax = ImVec2(bodyMin.x + ImGui::GetWindowSize().x, bodyMin.y + ImGui::GetWindowSize().y);
    if (visible.empty() && app_.performanceMode)
        EmptyState(TR("soundboard.empty.noneInCategory"), nullptr, ICON_PH_SPEAKER_SLASH);
    else if (visible.empty() && p.sounds.items.empty() && app_.soundSearch.empty())
    {
        EmptyState(TR("soundboard.empty.noneYet"), TR("soundboard.empty.dropHere"), ICON_PH_UPLOAD_SIMPLE);
        ImGui::Spacing();
        ImVec2 tile = tileSizeFor(app_.prefs.tileSize);
        ImGui::SetCursorPosX((ImGui::GetContentRegionAvail().x - tile.x) * 0.5f);
        if (AddSoundTile(tile, app_.drops.pending())) wantOpenFileDialog = true;
    }
    else if (visible.empty() && !app_.soundSearch.empty())
        EmptyState(TR("soundboard.empty.noMatch"), evobox::trFmt("soundboard.empty.nothingNamed", app_.soundSearch), ICON_PH_MAGNIFYING_GLASS);
    else if (app_.prefs.viewMode == ViewMode::List && !app_.performanceMode) list(visible);
    else grid(visible);

    // context menu for tiles
    if (ImGui::BeginPopup("##tilectx"))
    {
        if (Sound* s = p.sounds.find(contextUid_)) contextMenu(*s);
        else ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    // click on empty space deselects nothing (keep selection); right-click empty space: paste later
    ImGui::EndChild();

    // whole-body OS drop zone (import into the current category)
    auto files = app_.drops.claim(bodyMin, bodyMax);
    if (!files.empty()) app_.importer.importFiles(files, app_.selectedCategoryUid);
    // drop-target for tiles dragged onto empty space: move to current category (no-op otherwise)
    confirm.draw();
}

} // namespace ui
} // namespace evobox
