#include "ui/panels/InspectorPanel.h"
#include "model/InspectorHooks.h"
#include "ui/Fonts.h"
#include "ui/Icons.h"
#include "ui/Theme.h"
#include "ui/widgets/OscEditor.h"
#include "ui/widgets/StickerPicker.h"
#include "ui/widgets/Tiles.h"
#include "util/Strings.h"
#include "util/TimeFormat.h"
#include "imgui_stdlib.h"

namespace evobox
{
namespace ui
{

InspectorPanel::InspectorPanel(Application& app) : app_(app) {}

void InspectorPanel::installHooks()
{
    InspectorHooks::sound = [this](Sound& s) { drawSound(s); };
    InspectorHooks::gift = [this](GiftAction& g) { drawGift(g); };
    InspectorHooks::roomEvent = [this](RoomEventAction& r) { drawRoomEvent(r); };
}

void InspectorPanel::draw(bool* open)
{
    ImGui::SetNextWindowSize(ImVec2(360, 600), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Inspector", open))
    {
        auto& sel = organic::Selection::get();
        if (sel.items.empty())
            EmptyState("Select a sound or a gift", "Its OSC targets, commands and timing appear here", ICON_PH_CURSOR_CLICK);
        else if (sel.items.size() == 1)
            sel.items[0]->inspectorGui();
        else
            drawMulti(sel.items);
        confirm_.draw();
    }
    ImGui::End();
}

void InspectorPanel::drawMulti(const std::vector<organic::Inspectable*>& items)
{
    TextDim("%zu items selected", items.size());
    auto sounds = organic::Selection::get().getAs<Sound>();
    if (sounds.size() == items.size())
    {
        if (DangerButton(ICON_PH_TRASH " Delete selected"))
        {
            std::vector<Sound*> v = sounds;
            confirm_.open("Delete " + std::to_string(v.size()) + " sounds?", "The tiles and their OSC commands are removed (undo with Ctrl+Z).",
                          [this, v] { app_.deleteSounds(v); });
        }
        if (ImGui::BeginCombo("Move to category", "..."))
        {
            for (Category* c : app_.project.categories.categories())
                if (ImGui::Selectable(c->niceName.c_str())) app_.project.sounds.moveToCategoryUndoable(sounds, c->uid);
            ImGui::EndCombo();
        }
    }
}

void InspectorPanel::soundCombo(const char* label, Uid& soundUid, std::function<void(Uid)> onChange)
{
    Sound* cur = soundUid ? app_.project.sounds.find(soundUid) : nullptr;
    std::string lbl = cur ? icons::stickerText(cur->sticker()) + "  " + cur->niceName : std::string("(none)");
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo(label, lbl.c_str()))
    {
        if (ImGui::Selectable("(none)", soundUid == 0) && soundUid != 0) onChange(0);
        for (Sound* s : app_.project.sounds.sounds())
        {
            ImGui::PushID((void*)s);
            std::string l = icons::stickerText(s->sticker()) + "  " + s->niceName;
            if (ImGui::Selectable(l.c_str(), soundUid == s->uid) && soundUid != s->uid) onChange(s->uid);
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
}

// ---------------------------------------------------------------- Sound
void InspectorPanel::drawSound(Sound& s)
{
    const auto& th = theme::colors();
    ImGui::PushID((void*)&s);
    // header card
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    float w = ImGui::GetContentRegionAvail().x;
    float h = ImGui::GetFrameHeight() * 3.6f + 16;
    RoundedPanel(p0, ImVec2(p0.x + w, p0.y + h), th.panelBg, &th.tileBorder);
    ImGui::SetCursorScreenPos(ImVec2(p0.x + 8, p0.y + 8));
    ImGui::BeginGroup();
    std::string sticker = s.sticker();
    if (StickerButton("sticker", sticker, s.color(), ImVec2(ImGui::GetFrameHeight() * 2.4f, ImGui::GetFrameHeight() * 2.4f), &s))
        s.stickerP->setUndoable(sticker);
    ImGui::SameLine();
    ImGui::BeginGroup();
    ImGui::SetNextItemWidth(w - ImGui::GetFrameHeight() * 2.4f - 30 - ImGui::GetFrameHeight());
    organic::UndoableInputText("##name", s.niceName, &s, [&s](const std::string&, const std::string& n) { s.setNiceName(n); notifyStructureChanged(&s); });
    ImGui::SameLine();
    // play button (a playing effect keeps "play": it stacks; Stop is offered next to the checkbox)
    bool playing = s.playing();
    bool stopGlyph = playing && !s.isEffect();
    ImGui::PushStyleColor(ImGuiCol_Text, playing ? th.playing : th.accent);
    if (GhostButton(stopGlyph ? ICON_PH_STOP : ICON_PH_PLAY, ImVec2(ImGui::GetFrameHeight(), 0)))
    {
        if (stopGlyph) app_.playback.stop(s.uid); else app_.playback.play(s, TriggerSource::Tile);
    }
    ImGui::PopStyleColor();
    // duration · category combo · colour
    TextDim("%s", formatTime(s.clipDuration(), true).c_str());
    ImGui::SameLine();
    Category* cat = app_.project.categories.find(s.categoryUid);
    ImGui::SetNextItemWidth(140 * theme::scale());
    if (ImGui::BeginCombo("##cat", cat ? cat->niceName.c_str() : "(no category)"))
    {
        for (Category* c : app_.project.categories.categories())
            if (ImGui::Selectable(c->niceName.c_str(), c == cat) && c != cat) app_.project.sounds.moveToCategoryUndoable({ &s }, c->uid);
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    {
        static ImVec4 s_old; static Sound* s_colSound = nullptr;
        ImVec4 col = s.color();
        float f[3] = { col.x, col.y, col.z };
        if (ImGui::ColorEdit3("##color", f, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel))
        {
            if (s_colSound != &s) { s_colSound = &s; s_old = col; }
            s.colorP->setValue(ImVec4(f[0], f[1], f[2], 1.f));
        }
        if (ImGui::IsItemActivated()) { s_colSound = &s; s_old = col; }
        if (ImGui::IsItemDeactivatedAfterEdit() && s_colSound == &s) { s.colorP->recordEdit(s_old, s.colorP->value); s_colSound = nullptr; }
    }
    // effect / music
    {
        bool effect = s.isEffect();
        if (ImGui::Checkbox(ICON_PH_SPARKLE " Effect", &effect)) s.isEffectP->setUndoable(effect);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("Effect: plays on top of everything and stacks when pressed again\n"
                              "(press 3 times = 3 overlapping plays).\nMusic (off): starting it stops the music that was playing.");
        ImGui::SameLine();
        if (playing && effect)
        {
            // the header play button keeps stacking; this is the way to stop a running effect
            ImGui::PushStyleColor(ImGuiCol_Text, th.playing);
            std::string lbl = s.rt.activeVoices > 1 ? str::format(ICON_PH_STOP " Stop x%d", s.rt.activeVoices) : std::string(ICON_PH_STOP " Stop");
            if (GhostButton(lbl.c_str(), ImVec2(0, 0))) app_.playback.stop(s.uid);
            ImGui::PopStyleColor();
        }
        else if (effect) TextDim("stacks · plays over music");
        else TextDim("music · one at a time");
    }
    ImGui::EndGroup();
    ImGui::EndGroup();
    ImGui::SetCursorScreenPos(ImVec2(p0.x, p0.y + h + 6));
    // status line
    switch (s.rt.mediaStatus)
    {
    case MediaStatus::SourceMissing:
        ImGui::TextColored(th.warning, ICON_PH_WARNING " Source media not found: %s", s.source.fileName().c_str());
        if (ImGui::Button(ICON_PH_FOLDER_OPEN " Relink media...")) app_.relinkRequestUid = s.uid;
        break;
    case MediaStatus::Decoding: TextDim(ICON_PH_CIRCLE_NOTCH " Rendering clip..."); break;
    case MediaStatus::ClipMissing: if (!s.hasClip()) ImGui::TextColored(th.warning, ICON_PH_WARNING " No rendered clip"); break;
    default: break;
    }
    if (!s.rt.lastError.empty()) { ImGui::TextColored(th.danger, ICON_PH_X_CIRCLE " %s", ellipsize(s.rt.lastError, w).c_str()); }

    Spacer(4);
    OscEditorContext ctx;
    ctx.project = &app_.project;
    ctx.owner = &s;
    ctx.trigger = &app_.trigger;
    ctx.now = app_.now();
    DrawOscActionsEditor(s, ctx);

    Spacer(8);
    if (ImGui::CollapsingHeader("Clip settings"))
    {
        ImGui::Indent(4);
        organic::DrawParamWidget(*s.gainDbP);
        organic::DrawParamWidget(*s.normalizeP);
        organic::DrawParamWidget(*s.fadeInMsP);
        organic::DrawParamWidget(*s.fadeOutMsP);
        organic::DrawParamWidget(*s.hotkeyP);
        TextDim("Source: %s", s.source.fileName().c_str());
        if (!s.source.path.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) ImGui::SetTooltip("%s", s.source.path.c_str());
        ImGui::Unindent(4);
    }
    ImGui::PopID();
}

// ---------------------------------------------------------------- Gift
void InspectorPanel::drawGift(GiftAction& g)
{
    const auto& th = theme::colors();
    ImGui::PushID((void*)&g);
    GiftInfo* gi = app_.catalog.find(g.giftId);
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    float w = ImGui::GetContentRegionAvail().x;
    float h = ImGui::GetFrameHeight() * 2.8f + 12;
    RoundedPanel(p0, ImVec2(p0.x + w, p0.y + h), th.panelBg, &th.tileBorder);
    // icon
    float edge = ImGui::GetFrameHeight() * 2.4f;
    ImVec2 ic(p0.x + 8, p0.y + 8);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (gi && gi->icon.valid())
        dl->AddImageRounded(ImTextureRef((ImTextureID)(intptr_t)gi->icon.glId), ic, ImVec2(ic.x + edge, ic.y + edge), ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, 6.f);
    else
    {
        if (gi) app_.requestGiftIcon(*gi);
        dl->AddText(Fonts::icons, edge * 0.8f, ImVec2(ic.x + edge * 0.1f, ic.y + edge * 0.1f), theme::u32(th.textDim), ICON_PH_GIFT);
    }
    ImGui::SetCursorScreenPos(ImVec2(ic.x + edge + 10, p0.y + 8));
    ImGui::BeginGroup();
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.2f);
    ImGui::TextUnformatted(g.displayName().c_str());
    ImGui::PopFont();
    int diamonds = gi ? gi->diamondCount : g.cachedDiamonds;
    ImGui::TextColored(th.diamond, ICON_PH_DIAMOND " %s", str::groupThousands(diamonds).c_str());
    ImGui::SameLine();
    TextDim("· id %lld", (long long)g.giftId);
    if (g.isStreakable() || (gi && gi->streakable())) { ImGui::SameLine(); ImGui::TextColored(th.warning, ICON_PH_LIGHTNING " streak"); }
    if (g.rt.transient) TextDim("Not saved yet — the first edit adds it to the project");
    else TextDim("Received %d this session", g.rt.receivedCount);
    ImGui::EndGroup();
    // Simulate + active state on the right
    float bw = ImGui::CalcTextSize(ICON_PH_PLAY " Simulate").x + ImGui::GetStyle().FramePadding.x * 2;
    ImGui::SetCursorScreenPos(ImVec2(p0.x + w - bw - 8, p0.y + 8));
    if (AccentButton(ICON_PH_PLAY " Simulate", ImVec2(bw, 0))) app_.simulateGift(g.giftId);
    if (g.rt.active)
    {
        double rem = app_.trigger.timerRemaining(g);
        ImGui::SetCursorScreenPos(ImVec2(p0.x + w - bw - 8, p0.y + 8 + ImGui::GetFrameHeight() + 4));
        ImGui::TextColored(th.warning, ICON_PH_TIMER " %.1f s", rem < 0 ? 0.0 : rem);
    }
    ImGui::SetCursorScreenPos(ImVec2(p0.x, p0.y + h + 6));

    OscEditorContext ctx;
    ctx.project = &app_.project;
    ctx.owner = &g;
    ctx.trigger = &app_.trigger;
    ctx.now = app_.now();
    DrawOscActionsEditor(g, ctx);

    Spacer(8);
    if (ImGui::CollapsingHeader("Gift behaviour", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::Indent(4);
        organic::DrawParamWidget(*g.enabledP);
        organic::DrawParamWidget(*g.streakModeP);
        organic::DrawParamWidget(*g.retriggerP);
        organic::DrawParamWidget(*g.cooldownMsP);
        organic::DrawParamWidget(*g.minDiamondsP);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Also play sound");
        soundCombo("##gsound", g.soundUid, [&g](Uid u) { g.setSoundUidUndoable(u); });
        ImGui::Unindent(4);
    }
    if (!g.rt.transient)
    {
        Spacer(6);
        ImGui::PushStyleColor(ImGuiCol_Text, th.textDim);
        if (GhostButton(ICON_PH_TRASH " Clear this gift's actions"))
        {
            GiftAction* gp = &g;
            confirm_.open("Clear actions of '" + g.displayName() + "'?", "The gift's OSC commands and sound are removed from the project.",
                          [this, gp] { app_.project.giftActions.undoableRemove({ gp }); }, "Clear");
        }
        ImGui::PopStyleColor();
    }
    ImGui::PopID();
}

// ---------------------------------------------------------------- Room event
void InspectorPanel::drawRoomEvent(RoomEventAction& r)
{
    const auto& th = theme::colors();
    ImGui::PushID((void*)&r);
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    float w = ImGui::GetContentRegionAvail().x;
    float h = ImGui::GetFrameHeight() * 2.4f + 12;
    RoundedPanel(p0, ImVec2(p0.x + w, p0.y + h), th.panelBg, &th.tileBorder);
    ImGui::SetCursorScreenPos(ImVec2(p0.x + 10, p0.y + 8));
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.8f);
    ImGui::TextColored(th.live, "%s", roomEventIcon(r.kind));
    ImGui::PopFont();
    ImGui::SameLine();
    ImGui::BeginGroup();
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.2f);
    ImGui::TextUnformatted(r.niceName.c_str());
    ImGui::PopFont();
    if (r.kind == RoomEventKind::Like) TextDim("Fires every %d likes · %d received this session", r.threshold(), r.rt.receivedCount);
    else TextDim("Fires on every %s · %d this session", str::lower(r.niceName).c_str(), r.rt.receivedCount);
    ImGui::EndGroup();
    float bw = ImGui::CalcTextSize(ICON_PH_PLAY " Simulate").x + ImGui::GetStyle().FramePadding.x * 2;
    ImGui::SetCursorScreenPos(ImVec2(p0.x + w - bw - 8, p0.y + 8));
    if (AccentButton(ICON_PH_PLAY " Simulate", ImVec2(bw, 0))) app_.simulateRoomEvent(r.kind);
    if (r.rt.active)
    {
        double rem = app_.trigger.timerRemaining(r);
        ImGui::SetCursorScreenPos(ImVec2(p0.x + w - bw - 8, p0.y + 8 + ImGui::GetFrameHeight() + 4));
        ImGui::TextColored(th.warning, ICON_PH_TIMER " %.1f s", rem < 0 ? 0.0 : rem);
    }
    ImGui::SetCursorScreenPos(ImVec2(p0.x, p0.y + h + 6));

    OscEditorContext ctx;
    ctx.project = &app_.project;
    ctx.owner = &r;
    ctx.trigger = &app_.trigger;
    ctx.now = app_.now();
    DrawOscActionsEditor(r, ctx);

    Spacer(8);
    if (ImGui::CollapsingHeader("Behaviour", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::Indent(4);
        organic::DrawParamWidget(*r.enabledP);
        if (r.kind == RoomEventKind::Like) organic::DrawParamWidget(*r.thresholdP);
        organic::DrawParamWidget(*r.cooldownMsP);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Also play sound");
        soundCombo("##rsound", r.soundUid, [&r](Uid u) { r.setSoundUidUndoable(u); });
        ImGui::Unindent(4);
    }
    ImGui::PopID();
}

} // namespace ui
} // namespace evobox
