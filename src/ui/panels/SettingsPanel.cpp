#include "ui/panels/SettingsPanel.h"
#include "media/FFmpegDecoder.h"
#include "ui/Icons.h"
#include "ui/Theme.h"
#include "ui/widgets/Common.h"
#include "util/Paths.h"

namespace evobox
{
namespace ui
{

void SettingsPanel::draw(bool* open)
{
    ImGui::SetNextWindowSize(ImVec2(420, 520), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Settings", open))
    {
        SectionHeader("Project");
        app_.project.settings.inspectorGui();

        Spacer(8);
        SectionHeader("Audio (this machine)");
        if (devicesTime_ < 0 || app_.now() - devicesTime_ > 5.0) { devices_ = app_.audio.enumerateDevices(); devicesTime_ = app_.now(); }
        std::string cur = app_.prefs.audioDevice.empty() ? "System default" : app_.prefs.audioDevice;
        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##device", cur.c_str()))
        {
            if (ImGui::Selectable("System default", app_.prefs.audioDevice.empty())) { app_.prefs.audioDevice.clear(); wantReinitAudio = true; }
            for (auto& d : devices_)
            {
                std::string l = d.name + (d.isDefault ? "  (default)" : "");
                if (ImGui::Selectable(l.c_str(), app_.prefs.audioDevice == d.name)) { app_.prefs.audioDevice = d.name; wantReinitAudio = true; }
            }
            ImGui::EndCombo();
        }
        int period = app_.prefs.periodSizeInFrames;
        const int periods[] = { 128, 256, 512, 1024 };
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Buffer");
        ImGui::SameLine();
        for (int p : periods)
        {
            ImGui::PushID(p);
            if (ToggleButton(std::to_string(p).c_str(), period == p) && period != p) { app_.prefs.periodSizeInFrames = p; wantReinitAudio = true; }
            ImGui::PopID();
            ImGui::SameLine(0, 2);
        }
        ImGui::NewLine();
        TextDim(app_.audio.ok() ? "Device: %s @ %d Hz, %d voices active" : "No audio device (silent)",
                app_.audio.deviceName().c_str(), app_.audio.deviceSampleRate(), app_.audio.activeVoiceCount());

        Spacer(8);
        SectionHeader("Interface (this machine)");
        int ts = (int)app_.prefs.tileSize;
        const char* sizes[] = { "Compact", "Standard", "Large" };
        ImGui::SetNextItemWidth(160 * theme::scale());
        if (ImGui::Combo("Tile size", &ts, sizes, 3)) app_.prefs.tileSize = (TileSize)ts;
        float sc = app_.prefs.uiScale;
        ImGui::SetNextItemWidth(160 * theme::scale());
        if (ImGui::SliderFloat("UI scale", &sc, 0.75f, 2.0f, "%.2f")) app_.prefs.uiScale = sc;
        if (ImGui::IsItemDeactivatedAfterEdit()) wantReloadFonts = true;
        ImGui::Checkbox("Reopen last project at startup", &app_.prefs.reopenLastProject);

#ifdef EVOBOX_WITH_TIKTOK
        Spacer(8);
        SectionHeader("TikTok LIVE (this machine)");
        ImGui::Checkbox("Also use HTTP polling (dual mode, more complete)", &app_.prefs.tiktokPolling);
        ImGui::Checkbox("Connect automatically at startup", &app_.prefs.tiktokAutoConnect);
        TextDim("Username: @%s", app_.prefs.tiktokUsername.c_str());
        TextDim("Scripts: %s", TikTokLiveService::defaultJsDir().empty() ? "(library default)" : TikTokLiveService::defaultJsDir().c_str());
#endif

        Spacer(8);
        SectionHeader("About");
        TextDim("EvoMusicBox %s", EVOBOX_VERSION);
        TextDim("%s", FFmpegDecoder::versionString().c_str());
        TextDim("Dear ImGui %s (docking)", IMGUI_VERSION);
        TextDim("Config: %s", paths::configDir().string().c_str());
        TextDim("Cache:  %s", paths::cacheDir().string().c_str());
    }
    ImGui::End();
}

} // namespace ui
} // namespace evobox
