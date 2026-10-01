// Icons.h — Phosphor glyph lookups and the sticker scheme ("ph:rocket", "emoji:🚀", "img:<file>").
#pragma once

#include <string>
#include <vector>
#include "IconsPhosphor.h"
#include "imgui.h"

namespace evobox
{
namespace icons
{

// UTF-8 glyph for a Phosphor icon name ("rocket"); nullptr when unknown.
const char* glyph(const std::string& phosphorName);
bool exists(const std::string& phosphorName);
// All icon names (kebab-case), sorted.
const std::vector<std::string>& allNames();
// Names containing `filter` (case-insensitive), capped.
std::vector<std::string> search(const std::string& filter, size_t maxResults = 200);

// Sticker string -> display text (glyph or emoji). Returns a fallback speaker glyph for unknown.
std::string stickerText(const std::string& sticker);
bool stickerIsIcon(const std::string& sticker); // draw with the Fill font
std::string makePhosphorSticker(const std::string& name); // "ph:" + name

} // namespace icons
} // namespace evobox
