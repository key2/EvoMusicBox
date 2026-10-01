#include "ui/Icons.h"
#include "util/Strings.h"
#include <algorithm>
#include <unordered_map>

namespace evobox
{
namespace icons
{

static const std::unordered_map<std::string, const char*>& table()
{
    static std::unordered_map<std::string, const char*> m;
    if (m.empty())
    {
        for (const IconPhosphorEntry& e : ICON_PH_NAME_TABLE) m[e.name] = e.glyph;
    }
    return m;
}

const char* glyph(const std::string& name)
{
    auto& t = table();
    auto it = t.find(name);
    return it == t.end() ? nullptr : it->second;
}

bool exists(const std::string& name) { return glyph(name) != nullptr; }

const std::vector<std::string>& allNames()
{
    static std::vector<std::string> names;
    if (names.empty())
    {
        for (const IconPhosphorEntry& e : ICON_PH_NAME_TABLE) names.push_back(e.name);
        std::sort(names.begin(), names.end());
    }
    return names;
}

std::vector<std::string> search(const std::string& filterIn, size_t maxResults)
{
    std::vector<std::string> out;
    std::string f = str::lower(str::trim(filterIn));
    for (const std::string& n : allNames())
    {
        if (f.empty() || n.find(f) != std::string::npos) out.push_back(n);
        if (out.size() >= maxResults) break;
    }
    return out;
}

std::string stickerText(const std::string& sticker)
{
    if (str::startsWith(sticker, "ph:"))
    {
        if (const char* g = glyph(sticker.substr(3))) return g;
        return ICON_PH_SPEAKER_HIGH;
    }
    if (str::startsWith(sticker, "emoji:")) return sticker.substr(6);
    if (str::startsWith(sticker, "img:")) return ICON_PH_IMAGE;
    if (sticker.empty()) return ICON_PH_SPEAKER_HIGH;
    // bare phosphor name or literal text
    if (const char* g = glyph(sticker)) return g;
    return sticker;
}

bool stickerIsIcon(const std::string& sticker)
{
    return !str::startsWith(sticker, "emoji:");
}

std::string makePhosphorSticker(const std::string& name) { return "ph:" + name; }

} // namespace icons
} // namespace evobox
