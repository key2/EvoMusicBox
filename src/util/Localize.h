// Localize.h — one hook so that text born below the UI layer (default item names, undo labels,
// status-bar and error messages in model/, app/, live/, osc/) follows the Language menu without
// those layers depending on the UI's I18n. Every call site keeps its English text as the fallback:
//
//     LTR("undo.trimClip", "Trim clip")
//
// ui/I18n.cpp installs the translator at init; the tests install nothing and keep seeing English
// (several assert on default names such as "Music" or "nothing to merge"). The returned pointer is
// either `english` itself or catalogue-owned storage that stays valid until the next language
// switch, so it can be used immediately or copied into a std::string.
#pragma once

namespace evobox
{

using LocalizeFn = const char* (*)(const char* key, const char* english);

void setLocalizer(LocalizeFn fn);                        // nullptr restores the English fallback
const char* localize(const char* key, const char* english);

} // namespace evobox

#define LTR(key, english) ::evobox::localize(key, english)
