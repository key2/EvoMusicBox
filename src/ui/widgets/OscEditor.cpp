#include "ui/widgets/OscEditor.h"
#include "ui/I18n.h"
#include "app/TriggerController.h"
#include "osc/OscCommandParser.h"
#include "ui/Icons.h"
#include "ui/Theme.h"
#include "ui/widgets/Common.h"
#include "util/Strings.h"
#include "imgui_stdlib.h"
#include <algorithm>

namespace evobox
{
namespace ui
{

// ---------------------------------------------------------------- DelayField
bool DelayField(const char* id, organic::Parameter& delayP, bool isTimer, bool allowNegative, float width)
{
    ImGui::PushID(id);
    int v = delayP.intValue();
    if (width > 0) ImGui::SetNextItemWidth(width);
    bool changed = false;
    static organic::Parameter* s_editing = nullptr;
    static int s_old = 0;
    if (ImGui::InputInt("##delay", &v, 0, 0, ImGuiInputTextFlags_CharsDecimal))
    {
        if (!allowNegative && v < 0) v = 0;
        if (s_editing != &delayP) { s_editing = &delayP; s_old = delayP.intValue(); }
        delayP.setValue(v);
        changed = true;
    }
    if (ImGui::IsItemActivated()) { s_editing = &delayP; s_old = delayP.intValue(); }
    if (ImGui::IsItemDeactivatedAfterEdit() && s_editing == &delayP)
    {
        delayP.recordEdit(s_old, delayP.value);
        s_editing = nullptr;
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
        ImGui::SetTooltip("%s", isTimer ? TR("osc.timerTooltip") : TR("osc.delayTooltip"));
    ImGui::SameLine(0, 4);
    TextDim("%s", TR("osc.ms"));
    ImGui::PopID();
    return changed;
}

// ---------------------------------------------------------------- TargetCombo
bool TargetCombo(const char* id, Uid& targetUid, OscTargetManager& targets, float width, std::function<void(Uid)> onChange)
{
    OscTarget* cur = targetUid ? targets.find(targetUid) : nullptr;
    OscTarget* def = targets.defaultTarget();
    std::string label;
    if (cur) label = cur->displayName();
    else label = (def ? def->displayName() : std::string(TR("osc.localhost"))) + TR("osc.defaultSuffix");
    if (width > 0) ImGui::SetNextItemWidth(width);
    bool changed = false;
    if (ImGui::BeginCombo(id, label.c_str(), ImGuiComboFlags_None))
    {
        std::string defLabel = (def ? def->displayName() : std::string(TR("osc.localhost"))) + TR("osc.defaultSuffix");
        if (ImGui::Selectable(defLabel.c_str(), targetUid == 0))
        {
            if (targetUid != 0) { targetUid = 0; changed = true; if (onChange) onChange(0); }
        }
        for (OscTarget* t : targets.targets())
        {
            ImGui::PushID((int)t->uid);
            std::string l = t->displayName() + "  ";
            bool sel = targetUid == t->uid;
            if (ImGui::Selectable(l.c_str(), sel))
            {
                if (!sel) { targetUid = t->uid; changed = true; if (onChange) onChange(t->uid); }
            }
            ImGui::SameLine();
            TextDim("%s", t->host().c_str());
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    return changed;
}

// ---------------------------------------------------------------- OscCommandRow
bool OscCommandRow(OscCommand& cmd, OscPhase& phase, OscTargetManager& targets, const OscEditorContext& ctx)
{
    const auto& c = theme::colors();
    ImGui::PushID((void*)&cmd);
    bool remove = false;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 rowMin = ImGui::GetCursorScreenPos();
    float rowH = ImGui::GetFrameHeight();
    float fullW = ImGui::GetContentRegionAvail().x;

    // feedback: row background pulse (~300 ms) after a send, red tint on error
    float p = pulse(cmd.rt.lastSentTime, ctx.now, 0.35);
    float e = pulse(cmd.rt.lastErrorTime, ctx.now, 1.2);
    std::string parseErr;
    bool valid = OscCommandParser::valid(cmd.text(), &parseErr);
    if (p > 0) dl->AddRectFilled(ImVec2(rowMin.x - 4, rowMin.y - 2), ImVec2(rowMin.x + fullW + 4, rowMin.y + rowH + 2), theme::u32(c.accent, 0.35f * p), 4.f);
    if (e > 0) dl->AddRectFilled(ImVec2(rowMin.x - 4, rowMin.y - 2), ImVec2(rowMin.x + fullW + 4, rowMin.y + rowH + 2), theme::u32(c.danger, 0.25f * e), 4.f);

    float btnW = ImGui::GetFrameHeight();
    float spacing = ImGui::GetStyle().ItemSpacing.x;
    float comboW = std::clamp(fullW * 0.32f, 96.f, 180.f);
    float textW = fullW - comboW - btnW * 3 - spacing * 4;
    if (textW < 80) textW = 80;

    // enable toggle
    bool en = cmd.enabled();
    ImGui::PushStyleColor(ImGuiCol_Text, en ? c.text : c.textFaint);
    if (GhostButton(en ? ICON_PH_CHECK_SQUARE : ICON_PH_SQUARE, ImVec2(btnW, 0)))
        cmd.enabledP->setUndoable(!en);
    ImGui::PopStyleColor();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) ImGui::SetTooltip("%s", en ? TR("osc.enabledTooltip") : TR("osc.bypassedTooltip"));
    ImGui::SameLine();

    // text
    if (!valid) ImGui::PushStyleColor(ImGuiCol_Text, c.danger);
    if (!en) ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(1, 1, 1, 0.03f));
    ImGui::SetNextItemWidth(textW);
    std::string text = cmd.text();
    static OscCommand* s_editing = nullptr;
    static std::string s_old;
    if (ImGui::InputTextWithHint("##cmd", TR("osc.addressHint"), &text))
    {
        if (s_editing != &cmd) { s_editing = &cmd; s_old = cmd.text(); }
        cmd.textP->setValue(text);
    }
    if (ImGui::IsItemActivated()) { s_editing = &cmd; s_old = cmd.text(); }
    if (ImGui::IsItemDeactivatedAfterEdit() && s_editing == &cmd)
    {
        cmd.textP->recordEdit(s_old, cmd.textP->value);
        s_editing = nullptr;
    }
    if (!en) ImGui::PopStyleColor();
    if (!valid) ImGui::PopStyleColor();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
    {
        if (!valid) ImGui::SetTooltip(TR("osc.invalidCommand"), oscErrorLabel(parseErr).c_str());
        else if (!cmd.rt.lastError.empty()) ImGui::SetTooltip(TR("osc.lastSendFailed"), cmd.rt.lastError.c_str());
        else
        {
            ParsedCommand pc = OscCommandParser::parse(cmd.text());
            ImGui::SetTooltip(TR("osc.typeTags"), pc.message.toString().c_str(), pc.message.typeTags().c_str());
        }
    }
    ImGui::SameLine();

    // target
    Uid tu = cmd.targetUid;
    TargetCombo("##target", tu, targets, comboW, [&](Uid u) { cmd.setTargetUidUndoable(u); });
    ImGui::SameLine();

    // test
    bool canTest = ctx.trigger && ctx.owner && valid;
    if (IconButton(ICON_PH_PAPER_PLANE_TILT, TR("osc.sendNow"), ImVec2(btnW, 0), canTest))
        ctx.trigger->testCommand(*ctx.owner, phase, cmd);
    ImGui::SameLine();

    // remove
    ImGui::PushStyleColor(ImGuiCol_Text, c.textDim);
    if (GhostButton(ICON_PH_X, ImVec2(btnW, 0))) remove = true;
    ImGui::PopStyleColor();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) ImGui::SetTooltip("%s", TR("osc.removeCommand"));

    // context menu
    if (ImGui::BeginPopupContextItem("##rowctx"))
    {
        if (ImGui::MenuItem((std::string(ICON_PH_PAPER_PLANE_TILT "  ") + TR("osc.ctx.test")).c_str(), nullptr, false, canTest)) ctx.trigger->testCommand(*ctx.owner, phase, cmd);
        if (ImGui::MenuItem((std::string(ICON_PH_COPY "  ") + TR("osc.ctx.duplicate")).c_str())) phase.commands.undoableDuplicate({ &cmd });
        int idx = phase.commands.indexOf(&cmd);
        if (ImGui::MenuItem((std::string(ICON_PH_CARET_UP "  ") + TR("osc.ctx.moveUp")).c_str(), nullptr, false, idx > 0)) phase.commands.undoableMove(idx, idx - 1);
        if (ImGui::MenuItem((std::string(ICON_PH_CARET_DOWN "  ") + TR("osc.ctx.moveDown")).c_str(), nullptr, false, idx >= 0 && idx + 1 < (int)phase.commands.items.size())) phase.commands.undoableMove(idx, idx + 1);
        ImGui::Separator();
        if (ImGui::MenuItem((std::string(ICON_PH_TRASH "  ") + TR("osc.ctx.remove")).c_str())) remove = true;
        ImGui::EndPopup();
    }
    ImGui::PopID();
    return remove;
}

// ---------------------------------------------------------------- OscPhaseSection
void OscPhaseSection(OscPhase& phase, OscTargetManager& targets, const OscEditorContext& ctx)
{
    const auto& c = theme::colors();
    ImGui::PushID((void*)&phase);
    Spacer(4);
    // header: name · delay · + Add
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 hmin = ImGui::GetCursorScreenPos();
    float w = ImGui::GetContentRegionAvail().x;
    float h = ImGui::GetFrameHeight() + 8.f * theme::scale();
    dl->AddRectFilled(hmin, ImVec2(hmin.x + w, hmin.y + h), theme::u32(c.headerBg), 6.f);
    // anchor accent bar
    ImVec4 bar = phase.anchor == Anchor::Start ? c.playing : (phase.anchor == Anchor::Timer ? c.warning : c.accent);
    dl->AddRectFilled(hmin, ImVec2(hmin.x + 3, hmin.y + h), theme::u32(bar), 3.f);
    // right side controls: [delay ms] [test] [+ Add OSC command] — the Add label shrinks to "+"
    // when the panel is narrow so the phase name always stays readable.
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float pad2 = ImGui::GetStyle().FramePadding.x * 2;
    float testW = ImGui::GetFrameHeight();
    float delayW = 64.f * theme::scale();
    float msW = ImGui::CalcTextSize(TR("osc.ms")).x + 4;
    std::string addLabelLong = std::string(ICON_PH_PLUS " ") + TR("osc.addCommandLong");
    std::string addLabelShort = std::string(ICON_PH_PLUS " ") + TR("osc.addCommandShort");
    std::string phaseName = phaseLabel(phase);
    float nameW = ImGui::CalcTextSize(phaseName.c_str()).x + ImGui::GetFontSize() * 1.6f + 16;
    float addW = ImGui::CalcTextSize(addLabelLong.c_str()).x + pad2;
    const std::string* addLabel = &addLabelLong;
    if (nameW + delayW + msW + testW + addW + spacing * 4 + 12 > w)
    {
        addLabel = &addLabelShort;
        addW = ImGui::CalcTextSize(addLabelShort.c_str()).x + pad2;
    }
    float controlsW = delayW + msW + testW + addW + spacing * 3;
    float right = hmin.x + w - 6;
    float controlsX = right - controlsW;
    float rowY = hmin.y + 4.f * theme::scale();

    // name (clipped to the space left of the controls)
    ImGui::SetCursorScreenPos(ImVec2(hmin.x + 10, rowY));
    ImGui::AlignTextToFramePadding();
    const char* icon = phase.anchor == Anchor::Start ? ICON_PH_PLAY : (phase.anchor == Anchor::Timer ? ICON_PH_TIMER : ICON_PH_STOP);
    ImGui::TextUnformatted(icon);
    ImGui::SameLine();
    float nameMaxW = std::max(30.f, controlsX - ImGui::GetCursorScreenPos().x - 8);
    std::string nameShown = ellipsize(phaseName, nameMaxW);
    ImGui::TextUnformatted(nameShown.c_str());
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
    {
        switch (phase.anchor)
        {
        case Anchor::Start: ImGui::SetTooltip(TR("osc.startTooltip"), phaseName.c_str()); break;
        case Anchor::End:   ImGui::SetTooltip(TR("osc.endTooltip"), phaseName.c_str()); break;
        case Anchor::Timer: ImGui::SetTooltip(TR("osc.timerAnchorTooltip"), phaseName.c_str()); break;
        }
    }
    ImGui::SameLine();
    ImGui::SetCursorScreenPos(ImVec2(controlsX, rowY));
    DelayField("delay", *phase.delayP, phase.isTimer(), ctx.allowNegativeDelay, delayW);
    ImGui::SameLine();
    bool canTest = ctx.trigger && ctx.owner && phase.hasCommands();
    if (IconButton(ICON_PH_PAPER_PLANE_TILT, TR("osc.sendPhaseNow"), ImVec2(testW, 0), canTest))
        ctx.trigger->testPhase(*ctx.owner, phase);
    ImGui::SameLine();
    if (GhostButton(addLabel->c_str(), ImVec2(addW, 0)))
    {
        phase.commands.addCommandUndoable("/", 0);
    }
    if (addLabel == &addLabelShort && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) ImGui::SetTooltip("%s", TR("osc.addCommand"));
    ImGui::SetCursorScreenPos(ImVec2(hmin.x, hmin.y + h + 4));

    // rows
    OscCommand* toRemove = nullptr;
    ImGui::Indent(6.f);
    if (phase.commands.items.empty())
    {
        ImGui::PushStyleColor(ImGuiCol_Text, c.textFaint);
        ImGui::TextUnformatted(phase.anchor == Anchor::Timer ? TR("osc.noCommandsTimer")
                                                             : TR("osc.noCommands"));
        ImGui::PopStyleColor();
    }
    for (OscCommand* cmd : phase.commands.commands())
        if (OscCommandRow(*cmd, phase, targets, ctx)) toRemove = cmd;
    ImGui::Unindent(6.f);
    if (toRemove) phase.commands.removeCommandUndoable(toRemove);
    ImGui::PopID();
}

// ---------------------------------------------------------------- OscTargetsSection
static void targetEditorPopup(OscTarget& t, Project& project, OscTargetManager& targets)
{
    const auto& c = theme::colors();
    ImGui::PushID((void*)&t);
    if (ImGui::BeginPopup("##targetEdit"))
    {
        ImGui::TextUnformatted(t.builtin ? TR("osc.localhost") : TR("osc.editTarget"));
        ImGui::Separator();
        ImGui::SetNextItemWidth(220 * theme::scale());
        if (!t.builtin)
        {
            organic::UndoableInputText(TR("osc.name"), t.niceName, &t, [&t](const std::string&, const std::string& n) { t.setNiceName(n); notifyStructureChanged(&t); });
        }
        else TextDim("%s", TR("osc.nameLocalhost"));
        {
            std::string host = t.host();
            ImGui::SetNextItemWidth(220 * theme::scale());
            static OscTarget* s_editing = nullptr;
            static std::string s_old;
            if (ImGui::InputTextWithHint(TR("osc.ipAddress"), TR("osc.ipHint"), &host))
            {
                if (s_editing != &t) { s_editing = &t; s_old = t.host(); }
                t.hostP->setValue(host);
            }
            if (ImGui::IsItemActivated()) { s_editing = &t; s_old = t.host(); }
            if (ImGui::IsItemDeactivatedAfterEdit() && s_editing == &t) { t.hostP->recordEdit(s_old, t.hostP->value); s_editing = nullptr; }
        }
        if (ImGui::TreeNodeEx(TR("osc.advanced"), ImGuiTreeNodeFlags_SpanAvailWidth))
        {
            int port = t.port();
            ImGui::SetNextItemWidth(120 * theme::scale());
            if (ImGui::InputInt(TR("osc.port"), &port, 0, 0))
                t.portP->setUndoable(std::clamp(port, 1, 65535));
            TextDim("%s", TR("osc.defaultPort"));
            ImGui::TreePop();
        }
        ImGui::Spacing();
        bool isDef = t.isDefault();
        if (ImGui::Checkbox(TR("osc.defaultTarget"), &isDef) && isDef) targets.setDefaultUndoable(&t);
        if (!t.builtin)
        {
            ImGui::Separator();
            ImGui::PushStyleColor(ImGuiCol_Text, c.danger);
            if (ImGui::Selectable((std::string(ICON_PH_TRASH "  ") + TR("osc.deleteTarget")).c_str()))
            {
                project.deleteTargetUndoable(&t);
                ImGui::CloseCurrentPopup();
            }
            ImGui::PopStyleColor();
        }
        ImGui::EndPopup();
    }
    ImGui::PopID();
}

void OscTargetsSection(OscTargetManager& targets, Project& project)
{
    const auto& c = theme::colors();
    ImGui::PushID("osc-targets");
    // header
    std::string addTargetLabel = std::string(ICON_PH_PLUS " ") + TR("osc.addTarget");
    float addW = ImGui::CalcTextSize(addTargetLabel.c_str()).x + ImGui::GetStyle().FramePadding.x * 2;
    ImGui::AlignTextToFramePadding();
    ImGui::PushStyleColor(ImGuiCol_Text, c.textDim);
    ImGui::TextUnformatted(TR("osc.targets"));
    ImGui::PopStyleColor();
    ImGui::SameLine(ImGui::GetContentRegionMax().x - addW);
    if (GhostButton(addTargetLabel.c_str(), ImVec2(addW, 0)))
    {
        OscTarget* t = targets.addTargetUndoable(TR("osc.newTargetName"), "192.168.0.10", OscTarget::kDefaultPort);
        if (t) { ImGui::PushID((void*)t); ImGui::OpenPopup("##targetEdit"); ImGui::PopID(); }
    }
    // rows
    std::vector<OscTarget*> list = targets.targets();
    for (OscTarget* t : list)
    {
        ImGui::PushID((void*)t);
        bool def = t->isDefault();
        ImGui::PushStyleColor(ImGuiCol_Text, def ? c.accent : c.textFaint);
        if (GhostButton(def ? ICON_PH_RADIO_BUTTON : ICON_PH_CIRCLE, ImVec2(ImGui::GetFrameHeight(), 0)) && !def)
            targets.setDefaultUndoable(t);
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) ImGui::SetTooltip("%s", def ? TR("osc.defaultTarget") : TR("osc.makeDefault"));
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        std::string name = t->displayName() + (def ? std::string(" ") + TR("osc.defaultSuffix") : std::string());
        ImGui::TextUnformatted(name.c_str());
        // host on the right, then edit button
        float editW = ImGui::GetFrameHeight();
        std::string host = t->host();
        if (t->port() != OscTarget::kDefaultPort) host += ":" + std::to_string(t->port());
        float hostW = ImGui::CalcTextSize(host.c_str()).x;
        ImGui::SameLine(ImGui::GetContentRegionMax().x - editW - hostW - ImGui::GetStyle().ItemSpacing.x);
        TextDim("%s", host.c_str());
        ImGui::SameLine(ImGui::GetContentRegionMax().x - editW);
        if (GhostButton(ICON_PH_DOTS_THREE, ImVec2(editW, 0))) ImGui::OpenPopup("##targetEdit");
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) ImGui::SetTooltip("%s", TR("osc.editTarget"));
        ImGui::PopID();
        targetEditorPopup(*t, project, targets);
    }
    ImGui::PopID();
}

void DrawOscActionsEditor(Triggerable& t, const OscEditorContext& ctx)
{
    if (!ctx.project) return;
    OscTargetsSection(ctx.project->oscTargets, *ctx.project);
    Spacer(6);
    for (auto& phase : t.actions().phases)
        OscPhaseSection(*phase, ctx.project->oscTargets, ctx);
}

} // namespace ui
} // namespace evobox
