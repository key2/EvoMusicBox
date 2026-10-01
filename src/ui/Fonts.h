// Fonts.h — UI font (Roboto-Medium from imgui/misc/fonts) with Phosphor Regular + Fill merged
// (ImFontConfig::MergeMode). ImGui >= 1.92 rasterises glyphs on demand, so no glyph-range tables.
#pragma once

#include "imgui.h"

namespace evobox
{

struct Fonts
{
    static ImFont* ui;        // 16 px base, Phosphor merged
    static ImFont* icons;     // Phosphor Fill (for stickers) — falls back to ui when missing
    static float   baseSize;  // unscaled base size (style.FontSizeBase)

    static bool load(float scale = 1.f);
    static bool hasIcons();

    // Convenience: push the UI font at a multiple of the base size.
    static void pushSize(float mul);
    static void pop();
};

} // namespace evobox
