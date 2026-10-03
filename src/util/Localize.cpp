#include "util/Localize.h"

namespace evobox
{

static LocalizeFn g_localizer = nullptr;

void setLocalizer(LocalizeFn fn) { g_localizer = fn; }

const char* localize(const char* key, const char* english)
{
    if (g_localizer && key && *key)
        if (const char* s = g_localizer(key, english)) return s;
    return english ? english : "";
}

} // namespace evobox
