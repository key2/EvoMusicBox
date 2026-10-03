// IGFDGlue.h — lets the vendored ImGuiFileDialog follow the Language menu without patching it.
// Its texts are compile-time macros (see IGFDConfig.h); some are even concatenated with "##id"
// literals inside the library, so they cannot expand to runtime calls. Instead the config routes
// every button through igfd::button() / igfd::toggleButton() and every plain text through
// igfd::text(), which translate the English part at draw time through a function the application
// installs (ui/I18n.cpp). Compiled into the ImGuiFileDialog target: it depends on ImGui only.
#pragma once

#include "imgui.h"

namespace evobox
{
namespace igfd
{

// Returns the UI-language text for one of the dialog's English strings, or nullptr to keep it.
// The pointer must stay valid until the next language switch (catalogue-owned storage).
using Translate = const char* (*)(const char* english);
void setTranslator(Translate fn);

// Translation of `english` (leading / trailing blanks preserved); `english` itself when unknown.
const char* text(const char* english);
// ImGui::Button whose visible part ("<icon> Cancel" of "<icon> Cancel##id") is translated; the
// "##id" suffix is kept so the widget identity does not change with the language.
bool button(const char* label, const ImVec2& size = ImVec2(0, 0));
// The same for ImGuiFileDialog's toggle button (highlighted while *toggled).
bool toggleButton(const char* label, bool* toggled);

} // namespace igfd
} // namespace evobox
