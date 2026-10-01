#include "app/Prefs.h"
#include <algorithm>
#include <fstream>

namespace evobox
{

using json = nlohmann::json;

void Prefs::addRecent(const std::string& path)
{
    if (path.empty()) return;
    recentProjects.erase(std::remove(recentProjects.begin(), recentProjects.end(), path), recentProjects.end());
    recentProjects.insert(recentProjects.begin(), path);
    if (recentProjects.size() > 10) recentProjects.resize(10);
    lastProject = path;
}

json Prefs::toJson() const
{
    json j;
    j["window"] = { { "x", windowX }, { "y", windowY }, { "w", windowW }, { "h", windowH }, { "maximized", maximized } };
    j["recentProjects"] = recentProjects;
    j["lastProject"] = lastProject;
    j["reopenLastProject"] = reopenLastProject;
    j["audioDevice"] = audioDevice;
    j["periodSizeInFrames"] = periodSizeInFrames;
    j["tileSize"] = (int)tileSize;
    j["viewMode"] = (int)viewMode;
    j["activeTab"] = (int)activeTab;
    j["performanceMode"] = performanceMode;
    j["uiScale"] = uiScale;
    j["tiktokUsername"] = tiktokUsername;
    j["tiktokAutoConnect"] = tiktokAutoConnect;
    j["tiktokPolling"] = tiktokPolling;
    j["dock"] = dockState;
    return j;
}

void Prefs::fromJson(const json& j)
{
    if (!j.is_object()) return;
    if (j.contains("window") && j["window"].is_object())
    {
        const json& w = j["window"];
        windowX = w.value("x", windowX); windowY = w.value("y", windowY);
        windowW = w.value("w", windowW); windowH = w.value("h", windowH);
        maximized = w.value("maximized", maximized);
    }
    if (j.contains("recentProjects") && j["recentProjects"].is_array())
    {
        recentProjects.clear();
        for (auto& p : j["recentProjects"]) if (p.is_string()) recentProjects.push_back(p.get<std::string>());
    }
    lastProject = j.value("lastProject", lastProject);
    reopenLastProject = j.value("reopenLastProject", reopenLastProject);
    audioDevice = j.value("audioDevice", audioDevice);
    periodSizeInFrames = j.value("periodSizeInFrames", periodSizeInFrames);
    tileSize = (TileSize)std::clamp(j.value("tileSize", (int)tileSize), 0, 2);
    viewMode = (ViewMode)std::clamp(j.value("viewMode", (int)viewMode), 0, 1);
    activeTab = (WorkspaceTab)std::clamp(j.value("activeTab", (int)activeTab), 0, 1);
    performanceMode = j.value("performanceMode", performanceMode);
    uiScale = std::clamp(j.value("uiScale", uiScale), 0.5f, 3.f);
    tiktokUsername = j.value("tiktokUsername", tiktokUsername);
    tiktokAutoConnect = j.value("tiktokAutoConnect", tiktokAutoConnect);
    tiktokPolling = j.value("tiktokPolling", tiktokPolling);
    if (j.contains("dock")) dockState = j["dock"];
}

bool Prefs::load(const std::string& path)
{
    std::ifstream f(path);
    if (!f.is_open()) return false;
    try
    {
        json j;
        f >> j;
        fromJson(j);
        return true;
    }
    catch (...) { return false; }
}

bool Prefs::save(const std::string& path) const
{
    std::ofstream f(path);
    if (!f.is_open()) return false;
    f << toJson().dump(2);
    return f.good();
}

} // namespace evobox
