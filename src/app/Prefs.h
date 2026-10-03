// Prefs.h — machine-specific preferences (JSON in the user config dir): window geometry, recent
// projects, audio device, tile size, view mode, active workspace tab, dock state, TikTok username.
#pragma once

#include <string>
#include <vector>
#include "json.hpp"

namespace evobox
{

enum class TileSize { Compact = 0, Standard = 1, Large = 2 };
enum class ViewMode { Grid = 0, List = 1 };
enum class WorkspaceTab { Sounds = 0, Gifts = 1 };

struct Prefs
{
    // window
    int windowX = -1, windowY = -1, windowW = 1600, windowH = 940;
    bool maximized = false;
    // project
    std::vector<std::string> recentProjects; // most recent first, max 10
    std::string lastProject;
    bool reopenLastProject = true;
    // audio
    std::string audioDevice;
    int periodSizeInFrames = 256;
    // view
    TileSize tileSize = TileSize::Standard;
    ViewMode viewMode = ViewMode::Grid;
    WorkspaceTab activeTab = WorkspaceTab::Sounds;
    bool performanceMode = false;
    float uiScale = 1.f;
    // UI language code ("en", "ru", "zh"); matches a file in assets/lang/<code>.json
    std::string language = "en";
    // tiktok
    std::string tiktokUsername;
    bool tiktokAutoConnect = false;
    bool tiktokPolling = false;
    // dock manager state (panel open flags)
    nlohmann::json dockState;

    void addRecent(const std::string& path);
    bool load(const std::string& path);
    bool save(const std::string& path) const;
    nlohmann::json toJson() const;
    void fromJson(const nlohmann::json& j);
};

} // namespace evobox
