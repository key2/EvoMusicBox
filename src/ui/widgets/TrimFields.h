// TrimFields.h — Start / End editable as m:ss.mmm, Duration computed (UI.md §28).
#pragma once

#include "imgui.h"

namespace evobox
{
namespace ui
{

struct TrimFieldsResult
{
    bool changed = false;    // start/end edited (committed)
};

// Edits start/end in place; values are clamped to [0, duration]. Returns true when committed.
TrimFieldsResult TrimFields(const char* id, double& start, double& end, double duration, float fieldWidth = 0.f);

// A single time field "m:ss.mmm" (commits on Enter / focus loss). Returns true when committed.
bool TimeField(const char* label, double& seconds, double minV, double maxV, float width = 0.f);

} // namespace ui
} // namespace evobox
