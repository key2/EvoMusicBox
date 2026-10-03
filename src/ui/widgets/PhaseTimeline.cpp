#include "ui/widgets/PhaseTimeline.h"
#include "ui/I18n.h"
#include "ui/Icons.h"
#include "ui/Theme.h"
#include "ui/widgets/Common.h"
#include "util/Strings.h"
#include "util/TimeFormat.h"
#include <algorithm>
#include <cmath>

namespace evobox
{
namespace ui
{

bool PhaseTimeline(const char* id, OscActions& actions, const PhaseTimelineState& st, ImVec2 size)
{
    const auto& c = theme::colors();
    ImGui::PushID(id);
    ImVec2 avail = ImGui::GetContentRegionAvail();
    if (size.x <= 0) size.x = avail.x;
    if (size.y <= 0) size.y = std::max(70.f, std::min(110.f, avail.y));
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImVec2 p1(p0.x + size.x, p0.y + size.y);
    ImGui::InvisibleButton("##tl", size);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p0, p1, theme::u32(c.waveformBg), 6.f);
    dl->AddRect(p0, p1, theme::u32(c.border), 6.f);

    // horizontal scale: 0 .. max(delays) * 1.15 (at least 1 s)
    int maxMs = 1000;
    for (auto& ph : actions.phases) maxMs = std::max(maxMs, std::max(0, ph->delayMs()));
    double spanMs = maxMs * 1.18 + 200;
    float left = p0.x + 24 * theme::scale(), right = p1.x - 24 * theme::scale();
    float lineY = p0.y + size.y * 0.55f;
    auto xFor = [&](double ms) { return left + (float)((ms / spanMs) * (right - left)); };

    // baseline + ticks
    dl->AddLine(ImVec2(left, lineY), ImVec2(right, lineY), theme::u32(c.textFaint), 2.f);
    double step = spanMs > 8000 ? 2000 : (spanMs > 3000 ? 1000 : (spanMs > 1200 ? 500 : 250));
    for (double ms = 0; ms <= spanMs; ms += step)
    {
        float x = xFor(ms);
        dl->AddLine(ImVec2(x, lineY - 4), ImVec2(x, lineY + 4), theme::u32(c.textFaint));
        std::string lbl = ms >= 1000 ? str::format("%.3g", ms / 1000.0) + TR("time.secondsShort") : str::format("%d", (int)ms);
        ImVec2 ts = ImGui::CalcTextSize(lbl.c_str());
        dl->AddText(ImVec2(x - ts.x * 0.5f, p1.y - ts.y - 4), theme::u32(c.textFaint), lbl.c_str());
    }

    // active progress
    if (st.active)
    {
        float xe = xFor(std::min(spanMs, st.elapsedSec * 1000.0));
        dl->AddLine(ImVec2(left, lineY), ImVec2(xe, lineY), theme::u32(c.warning), 3.f);
        dl->AddCircleFilled(ImVec2(xe, lineY), 5.f, theme::u32(c.warning));
    }

    bool changed = false;
    ImGuiIO& io = ImGui::GetIO();
    static OscPhase* s_drag = nullptr;
    static int s_dragOld = 0;
    // markers
    for (auto& php : actions.phases)
    {
        OscPhase& ph = *php;
        if (ph.anchor == Anchor::End) continue; // End is relative to playback end: shown as a label only
        bool isTimer = ph.anchor == Anchor::Timer;
        double ms = std::max(0, ph.delayMs());
        float x = xFor(ms);
        ImVec4 col = isTimer ? c.warning : c.playing;
        float fl = pulse(st.lastFire, st.now, 0.4);
        if (!isTimer && fl > 0) col = theme::lighten(col, 0.4f * fl);
        // marker
        dl->AddLine(ImVec2(x, lineY - 18), ImVec2(x, lineY + 10), theme::u32(col), 2.f);
        dl->AddCircleFilled(ImVec2(x, lineY), 6.f, theme::u32(col));
        std::string lbl = phaseLabel(ph) + "  " + str::groupThousands((long long)ms) + " " + TR("osc.ms");
        std::string cnt = std::to_string(ph.commands.items.size()) + (ph.commands.items.size() == 1 ? std::string(" ") + TR("phase.cmd") : std::string(" ") + TR("phase.cmds"));
        ImVec2 ts = ImGui::CalcTextSize(lbl.c_str());
        float lx = std::clamp(x - ts.x * 0.5f, p0.x + 4, p1.x - ts.x - 4);
        dl->AddText(ImVec2(lx, lineY - 18 - ts.y - 2), theme::u32(c.text), lbl.c_str());
        ImVec2 cs = ImGui::CalcTextSize(cnt.c_str());
        dl->AddText(ImVec2(std::clamp(x - cs.x * 0.5f, p0.x + 4, p1.x - cs.x - 4), lineY + 12), theme::u32(c.textDim), cnt.c_str());

        // drag the Timer marker
        if (isTimer)
        {
            bool hov = ImGui::IsItemHovered() && std::fabs(io.MousePos.x - x) < 10 && std::fabs(io.MousePos.y - lineY) < 16;
            if (hov) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
            if (hov && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) { s_drag = &ph; s_dragOld = ph.delayMs(); }
            if (s_drag == &ph && ImGui::IsMouseDown(ImGuiMouseButton_Left))
            {
                double nm = (io.MousePos.x - left) / (right - left) * spanMs;
                int v = (int)std::round(std::max(0.0, nm) / 50.0) * 50;
                if (v != ph.delayMs()) { ph.delayP->setValue(v); changed = true; }
            }
            if (s_drag == &ph && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
            {
                ph.delayP->recordEdit(s_dragOld, ph.delayP->value);
                s_drag = nullptr;
            }
            if (hov && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
                ImGui::SetTooltip("%s", TR("phase.dragTimer"));
        }
    }
    // End phases: text badge on the right
    float ex = right;
    for (auto& php : actions.phases)
    {
        if (php->anchor != Anchor::End) continue;
        std::string lbl = phaseLabel(*php) + "  +" + str::groupThousands((long long)std::max(0, php->delayMs())) + " " + TR("osc.ms") + TR("phase.afterEnd");
        ImVec2 ts = ImGui::CalcTextSize(lbl.c_str());
        dl->AddText(ImVec2(ex - ts.x, p0.y + 6), theme::u32(c.accent), lbl.c_str());
        ex -= ts.x + 12;
    }
    ImGui::PopID();
    return changed;
}

} // namespace ui
} // namespace evobox
