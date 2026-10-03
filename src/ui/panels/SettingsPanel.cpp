#include "ui/panels/SettingsPanel.h"
#include "ui/I18n.h"
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
    if (ImGui::Begin((TR("panel.settings") + std::string("###Settings")).c_str(), open))
    {
        SectionHeader(TR("settings.project"));
        {
            // the Settings container is drawn by organic's generic widget: give every parameter its
            // translated label / tooltip / option names first (JSON short names stay English)
            Settings& st = app_.project.settings;
            LocalizeParam(st.playbackPolicyP, "settings.param.playbackPolicy", "settings.param.playbackPolicy.desc", "settings.param.playbackPolicy");
            LocalizeParam(st.masterVolumeP, "settings.param.masterVolume", "settings.param.masterVolume.desc");
            LocalizeParam(st.copyMediaP, "settings.param.copyMedia", "settings.param.copyMedia.desc");
            LocalizeParam(st.autosaveIntervalP, "settings.param.autosaveInterval", "settings.param.autosaveInterval.desc");
            LocalizeParam(st.oscDefaultPortP, "settings.param.oscDefaultPort", "settings.param.oscDefaultPort.desc");
            LocalizeParam(st.afterPlayOnStopP, "settings.param.afterPlayOnStop", "settings.param.afterPlayOnStop.desc");
            LocalizeParam(st.stopAllFadeMsP, "settings.param.stopAllFade", "settings.param.stopAllFade.desc");
        }
        app_.project.settings.inspectorGui();

        Spacer(8);
        SectionHeader(TR("settings.audio"));
        if (devicesTime_ < 0 || app_.now() - devicesTime_ > 5.0) { devices_ = app_.audio.enumerateDevices(); devicesTime_ = app_.now(); }
        std::string cur = app_.prefs.audioDevice.empty() ? TR("settings.systemDefault") : app_.prefs.audioDevice;
        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##device", cur.c_str()))
        {
            if (ImGui::Selectable(TR("settings.systemDefault"), app_.prefs.audioDevice.empty())) { app_.prefs.audioDevice.clear(); wantReinitAudio = true; }
            for (auto& d : devices_)
            {
                std::string l = d.name + (d.isDefault ? TR("settings.defaultSuffix") : "");
                if (ImGui::Selectable(l.c_str(), app_.prefs.audioDevice == d.name)) { app_.prefs.audioDevice = d.name; wantReinitAudio = true; }
            }
            ImGui::EndCombo();
        }
        int period = app_.prefs.periodSizeInFrames;
        const int periods[] = { 128, 256, 512, 1024 };
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(TR("settings.buffer"));
        ImGui::SameLine();
        for (int p : periods)
        {
            ImGui::PushID(p);
            if (ToggleButton(std::to_string(p).c_str(), period == p) && period != p) { app_.prefs.periodSizeInFrames = p; wantReinitAudio = true; }
            ImGui::PopID();
            ImGui::SameLine(0, 2);
        }
        ImGui::NewLine();
        if (app_.audio.ok())
            TextDim(TR("settings.deviceInfo"), app_.audio.deviceName().c_str(), app_.audio.deviceSampleRate(), app_.audio.activeVoiceCount());
        else
            TextDim("%s", TR("settings.noDevice"));

        Spacer(8);
        SectionHeader(TR("settings.interface"));
        int ts = (int)app_.prefs.tileSize;
        const char* sizes[] = { TR("tileSize.compact"), TR("tileSize.standard"), TR("tileSize.large") };
        ImGui::SetNextItemWidth(160 * theme::scale());
        if (ImGui::Combo(TR("settings.tileSize"), &ts, sizes, 3)) app_.prefs.tileSize = (TileSize)ts;
        float sc = app_.prefs.uiScale;
        ImGui::SetNextItemWidth(160 * theme::scale());
        if (ImGui::SliderFloat(TR("settings.uiScale"), &sc, 0.75f, 2.0f, "%.2f")) app_.prefs.uiScale = sc;
        if (ImGui::IsItemDeactivatedAfterEdit()) wantReloadFonts = true;
        {
            I18n& i18n = I18n::get();
            const std::string& langCur = i18n.language();
            std::string curName;
            for (auto& l : i18n.available()) if (l.code == langCur) curName = l.nativeName;
            ImGui::SetNextItemWidth(160 * theme::scale());
            if (ImGui::BeginCombo(TR("settings.language"), curName.c_str()))
            {
                for (auto& l : i18n.available())
                {
                    bool sel = l.code == langCur;
                    if (ImGui::Selectable(l.nativeName.c_str(), sel) && !sel)
                    {
                        i18n.setLanguage(l.code);
                        app_.prefs.language = l.code;
                    }
                    if (sel) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
        }
        ImGui::Checkbox(TR("settings.reopenLast"), &app_.prefs.reopenLastProject);

#ifdef EVOBOX_WITH_TIKTOK
        Spacer(8);
        SectionHeader(TR("settings.tiktok"));
        ImGui::Checkbox(TR("settings.tiktokPolling"), &app_.prefs.tiktokPolling);
        ImGui::Checkbox(TR("settings.tiktokAutoConnect"), &app_.prefs.tiktokAutoConnect);
        TextDim(TR("settings.tiktokUsername"), app_.prefs.tiktokUsername.c_str());
        TextDim(TR("settings.tiktokScripts"), TikTokLiveService::defaultJsDir().empty() ? TR("settings.libraryDefault") : TikTokLiveService::defaultJsDir().c_str());
#endif

        Spacer(8);
        SectionHeader(TR("settings.about"));
        TextDim(TR("settings.aboutVersion"), EVOBOX_VERSION);
        TextDim("%s", FFmpegDecoder::versionString().c_str());
        TextDim(TR("settings.imguiVersion"), IMGUI_VERSION);
        TextDim(TR("settings.configPath"), paths::configDir().string().c_str());
        TextDim(TR("settings.cachePath"), paths::cacheDir().string().c_str());
    }
    ImGui::End();
}

} // namespace ui
} // namespace evobox
