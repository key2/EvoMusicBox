#include "ui/IGFDGlue.h"
#include <map>
#include <string>

namespace evobox
{
namespace igfd
{

namespace
{
Translate g_translate = nullptr;

// Splits "<icons> text##id" into its three parts. Phosphor glyphs are 3-byte UTF-8 sequences in
// the private-use area (lead byte 0xEE / 0xEF); they and the blanks after them form the prefix.
struct Parts { std::string prefix, text, id; };

Parts split(const char* label)
{
    Parts p;
    std::string s = label ? label : "";
    size_t hash = s.find("##");
    if (hash != std::string::npos) { p.id = s.substr(hash); s.erase(hash); }
    size_t i = 0;
    while (i < s.size())
    {
        unsigned char c = (unsigned char)s[i];
        if (c == 0xEE || c == 0xEF) { i += 3; continue; }  // one PUA glyph
        if (c == ' ') { i++; continue; }
        break;
    }
    p.prefix = s.substr(0, i);
    p.text = s.substr(i);
    return p;
}

// Translation with the original leading / trailing blanks (table headers are " Name" etc.).
std::string translated(const std::string& english)
{
    size_t b = english.find_first_not_of(' ');
    if (b == std::string::npos || !g_translate) return english;
    size_t e = english.find_last_not_of(' ');
    std::string core = english.substr(b, e - b + 1);
    const char* t = g_translate(core.c_str());
    if (!t || !*t) return english;
    return english.substr(0, b) + t + english.substr(e + 1);
}
} // namespace

void setTranslator(Translate fn) { g_translate = fn; }

const char* text(const char* english)
{
    if (!english) return "";
    // one stable string per English text: ImGui consumes the pointer within the call, but the
    // library also keeps some (table headers) across frames
    static std::map<std::string, std::string> cache;
    std::string& slot = cache[english];
    slot = translated(english);
    return slot.c_str();
}

bool button(const char* label, const ImVec2& size)
{
    Parts p = split(label);
    std::string shown = p.prefix + translated(p.text) + p.id;
    return ImGui::Button(shown.c_str(), size);
}

bool toggleButton(const char* label, bool* toggled)
{
    // same look as ImGuiFileDialog's own inToggleButton(): inverted colours while active
    bool pressed = false;
    if (toggled && *toggled)
    {
        ImVec4 active = ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive);
        ImVec4 textCol = ImGui::GetStyleColorVec4(ImGuiCol_Text);
        ImGui::PushStyleColor(ImGuiCol_Button, textCol);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, textCol);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, textCol);
        ImGui::PushStyleColor(ImGuiCol_Text, active);
    }
    pressed = button(label);
    if (toggled && *toggled) ImGui::PopStyleColor(4);
    if (toggled && pressed) *toggled = !*toggled;
    return pressed;
}

} // namespace igfd
} // namespace evobox
