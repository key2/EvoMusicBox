// SettingsPanel.h — project settings (Settings container via DrawParamWidget) + machine prefs
// (audio device, tile size, UI scale, TikTok options).
#pragma once

#include "app/Application.h"

namespace evobox
{
namespace ui
{

class SettingsPanel
{
public:
    explicit SettingsPanel(Application& app) : app_(app) {}
    void draw(bool* open);
    bool wantReloadFonts = false;   // UI scale changed
    bool wantReinitAudio = false;

private:
    Application& app_;
    std::vector<AudioDeviceInfo> devices_;
    double devicesTime_ = -1;
};

} // namespace ui
} // namespace evobox
