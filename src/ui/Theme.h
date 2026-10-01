// Theme.h — the dark charcoal/navy palette (UI.md §3): one function applies the ImGui style and
// exposes the named colours used by every custom drawing routine.
#pragma once

#include "imgui.h"

namespace evobox
{
namespace theme
{

struct Colors
{
    ImVec4 accent       = ImVec4(0.27f, 0.62f, 1.00f, 1.f); // neon blue
    ImVec4 accentGlow   = ImVec4(0.27f, 0.62f, 1.00f, 0.35f);
    ImVec4 accentDim    = ImVec4(0.27f, 0.62f, 1.00f, 0.18f);
    ImVec4 windowBg     = ImVec4(0.094f, 0.106f, 0.133f, 1.f);
    ImVec4 panelBg      = ImVec4(0.118f, 0.133f, 0.165f, 1.f);
    ImVec4 headerBg     = ImVec4(0.141f, 0.157f, 0.196f, 1.f);
    ImVec4 tileBg       = ImVec4(0.160f, 0.180f, 0.224f, 1.f);
    ImVec4 tileBgHover  = ImVec4(0.200f, 0.224f, 0.275f, 1.f);
    ImVec4 tileBorder   = ImVec4(1.f, 1.f, 1.f, 0.07f);
    ImVec4 border       = ImVec4(1.f, 1.f, 1.f, 0.08f);
    ImVec4 text         = ImVec4(0.92f, 0.93f, 0.95f, 1.f);
    ImVec4 textDim      = ImVec4(0.58f, 0.62f, 0.70f, 1.f);
    ImVec4 textFaint    = ImVec4(0.40f, 0.44f, 0.52f, 1.f);
    ImVec4 playing      = ImVec4(0.30f, 0.86f, 0.55f, 1.f);
    ImVec4 success      = ImVec4(0.30f, 0.80f, 0.50f, 1.f);
    ImVec4 warning      = ImVec4(0.98f, 0.72f, 0.25f, 1.f);
    ImVec4 danger       = ImVec4(0.93f, 0.33f, 0.36f, 1.f);
    ImVec4 diamond      = ImVec4(0.55f, 0.78f, 1.00f, 1.f);
    ImVec4 live         = ImVec4(0.95f, 0.30f, 0.45f, 1.f);
    ImVec4 waveform     = ImVec4(0.45f, 0.55f, 0.72f, 1.f);
    ImVec4 waveformSel  = ImVec4(0.27f, 0.62f, 1.00f, 1.f);
    ImVec4 waveformBg   = ImVec4(0.075f, 0.085f, 0.110f, 1.f);
    ImVec4 selectionBg  = ImVec4(0.27f, 0.62f, 1.00f, 0.13f);
    ImVec4 playhead     = ImVec4(1.00f, 0.90f, 0.40f, 1.f);
};

const Colors& colors();
void apply(float scale = 1.f);         // full style + palette; call at startup and on DPI change
float scale();                         // current UI scale
float rounding();                      // 8 px * scale

ImU32 u32(const ImVec4& c, float alphaMul = 1.f);
ImVec4 withAlpha(const ImVec4& c, float a);
ImVec4 lighten(const ImVec4& c, float amount);
ImVec4 mix(const ImVec4& a, const ImVec4& b, float t);
// Readable text colour (dark or light) for a given background.
ImVec4 contrastText(const ImVec4& bg);

} // namespace theme
} // namespace evobox
