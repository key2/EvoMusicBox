// InspectorHooks.h — the model implements organic::Inspectable::inspectorGui() (that is what
// makes the right sidebar generic) but must not include UI headers (ui -> app -> model).
// The UI layer installs these drawing callbacks at startup; without them the model falls back
// to organic's automatic parameter form (useful for the headless tests and the debug panel).
#pragma once

#include <functional>

namespace evobox
{

class Sound;
class GiftAction;
class RoomEventAction;

struct InspectorHooks
{
    static std::function<void(Sound&)>           sound;
    static std::function<void(GiftAction&)>      gift;
    static std::function<void(RoomEventAction&)> roomEvent;
};

} // namespace evobox
