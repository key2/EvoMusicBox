// StickerPicker.h — fast visual sticker picker (UI.md §32): curated Phosphor groups from
// assets/stickers.json plus a search over every Phosphor icon ("ph:<name>"), and for sounds
// picture stickers ("img:icons/<hash>.png"): an image file, or a frame of the imported video.
#pragma once

#include <functional>
#include <string>
#include "imgui.h"
#include "live/GiftCatalog.h" // TextureHandle

namespace evobox
{

class Sound;

namespace ui
{

// Image stickers are resolved to GL textures by the application; the widgets only know these
// hooks (installed by the Shell at startup). An invalid handle draws the fallback glyph.
struct StickerHooks
{
    static std::function<TextureHandle(const std::string& sticker)> texture;
    static std::function<void(Sound&)> pickImageFile;    // "Picture from file..." (opens a dialog)
    static std::function<void(Sound&)> pickVideoFrames;  // "Frames from the video" (extracts thumbnails)
};

// Draws a sticker button; opens the picker popup on click. `sticker` is updated when picked.
// Returns true when the sticker changed. `forSound` enables the picture options.
bool StickerButton(const char* id, std::string& sticker, const ImVec4& tint, ImVec2 size = ImVec2(0, 0), Sound* forSound = nullptr);
// Inline picker body (used inside popups). Returns true when picked and writes `outSticker`.
bool StickerPickerBody(const char* id, std::string& outSticker, const ImVec4& tint, Sound* forSound = nullptr);

// Draws any sticker (glyph, emoji or picture) centered in the given rect on a draw list.
void DrawStickerInRect(ImDrawList* dl, const std::string& sticker, ImVec2 mn, ImVec2 mx, ImU32 glyphColor, float glyphSizePx, float rounding = 6.f);

} // namespace ui
} // namespace evobox
