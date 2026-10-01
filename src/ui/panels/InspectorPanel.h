// InspectorPanel.h — the generic right sidebar: draws inspectorGui() of whatever is selected
// (sound, gift, room event). Also installs the InspectorHooks that give each model type its
// themed editor (header card · OSC targets · phases · type-specific extras).
#pragma once

#include "app/Application.h"
#include "ui/widgets/Common.h"

namespace evobox
{
namespace ui
{

class InspectorPanel
{
public:
    explicit InspectorPanel(Application& app);
    void draw(bool* open);
    // Registers the model drawing hooks (call once at startup).
    void installHooks();

private:
    Application& app_;
    ConfirmPopup confirm_{ "ConfirmInspector" };

    void drawSound(Sound& s);
    void drawGift(GiftAction& g);
    void drawRoomEvent(RoomEventAction& r);
    void soundCombo(const char* label, Uid& soundUid, std::function<void(Uid)> onChange);
    void drawMulti(const std::vector<organic::Inspectable*>& items);
};

} // namespace ui
} // namespace evobox
