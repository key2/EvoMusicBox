#include "ui/Shell.h"
#include "OrganicPanels.h"
#include "app/ProjectIO.h"
#include "ui/Icons.h"
#include "ui/Theme.h"
#include "ui/widgets/StickerPicker.h"
#include "util/Paths.h"
#include "util/Strings.h"
#include "util/TimeFormat.h"
#include "ImGuiFileDialog.h"
#include "imgui_internal.h"
#include <filesystem>

namespace evobox
{
namespace ui
{

using organic::DockZone;

Shell::Shell(Application& app)
    : app_(app), navigator_(app), workspace_(app), inspector_(app), clipEditor_(app), settings_(app)
#ifdef EVOBOX_WITH_TIKTOK
      , liveMonitor_(app)
#endif
{
}

void Shell::init()
{
    inspector_.installHooks();
    // picture stickers: widgets resolve textures / ask for files through the application
    StickerHooks::texture = [this](const std::string& sticker) { return app_.stickerTexture(sticker); };
    StickerHooks::pickImageFile = [this](Sound& s) { app_.stickerImageRequestUid = s.uid; };
    StickerHooks::pickVideoFrames = [this](Sound& s) { app_.requestStickerFrames(s); app_.select(&s); };
    organic::DockManager& dock = app_.dock;
    dock.addPanel("Navigator",    DockZone::Left,   [this](bool* o) { navigator_.draw(o); });
    dock.addPanel("Workspace",    DockZone::Center, [this](bool* o) { workspace_.draw(o); });
    dock.addPanel("Inspector",    DockZone::Right,  [this](bool* o) { inspector_.draw(o); });
    dock.addPanel("Clip Editor",  DockZone::Bottom, [this](bool* o) { clipEditor_.draw(o); });
#ifdef EVOBOX_WITH_TIKTOK
    dock.addPanel("Live Monitor", DockZone::Bottom, [this](bool* o) { liveMonitor_.draw(o); }, false);
#endif
    dock.addPanel("Log",          DockZone::Bottom, [](bool* o) { organic::LoggerPanel(o); }, false);
    dock.addPanel("Settings",     DockZone::Right,  [this](bool* o) { settings_.draw(o); }, false);
    dock.loadState(app_.prefs.dockState);
    app_.performanceMode = false; // always start in editing mode (F11 toggles per session)
}

void Shell::shutdown()
{
    if (app_.performanceMode)
    {
        app_.performanceMode = false;
        for (const char* n : { "Navigator", "Inspector", "Clip Editor", "Workspace" })
            if (auto* p = app_.dock.find(n)) p->open = true;
    }
}

// ---------------------------------------------------------------- project actions
void Shell::doNew()
{
    app_.newProject();
    app_.selectedCategoryUid = 0;
}

void Shell::doOpen(const std::string& path)
{
    std::string err;
    if (!app_.openProject(path, &err))
    {
        OLOGE("Project", err);
        confirm_.danger = false;
        confirm_.open("Cannot open project", err, {}, "OK");
    }
    app_.selectedCategoryUid = 0;
    // autosave recovery: offer the autosave when it is newer than the saved show (.liv) / project file
    std::error_code ec;
    std::string autosave = ProjectIO::autosavePath(app_.project);
    std::string projectFile = app_.project.isArchive() ? app_.project.archivePath
                                                       : (std::filesystem::path(app_.project.bundleDir) / ProjectIO::kProjectFile).string();
    if (app_.hasBundle() && std::filesystem::exists(autosave, ec) && std::filesystem::exists(projectFile, ec) &&
        std::filesystem::last_write_time(autosave, ec) > std::filesystem::last_write_time(projectFile, ec))
    {
        recoverAutosavePath_ = autosave;
        confirm_.danger = false;
        confirm_.open("Recover autosave?", "An autosave newer than the saved project was found. Load it instead? (You can still undo or save over it.)",
                      [this]
                      {
                          nlohmann::json j;
                          std::string e;
                          if (ProjectIO::readJson(recoverAutosavePath_, j, &e))
                          {
                              std::string bundle = app_.project.bundleDir, archive = app_.project.archivePath;
                              app_.project.load(j);
                              app_.project.bundleDir = bundle;
                              app_.project.archivePath = archive;
                              app_.project.touch(); // dirty: the recovered state is not on disk as project.json
                              app_.setStatus("Recovered autosave");
                              // clips of the recovered sounds not already requested by the lazy loader
                              app_.loadAllClips();
                          }
                          else OLOGE("Project", e);
                      }, "Recover");
    }
}

bool Shell::doSave()
{
    if (!app_.hasBundle()) { saveAsDialog_ = true; return false; }
    std::string err;
    if (!app_.saveProject(&err)) { OLOGE("Project", err); return false; }
    return true;
}

void Shell::promptSaveThen(AfterSave next)
{
    afterSave_ = next;
    ImGui::OpenPopup("Unsaved changes");
}

void Shell::requestQuit()
{
    if (app_.project.dirty()) promptSaveThen(AfterSave::Quit);
    else app_.quitConfirmed = true;
}

void Shell::applyPendingActions()
{
    if (newRequested_)
    {
        newRequested_ = false;
        if (app_.project.dirty()) promptSaveThen(AfterSave::New);
        else doNew();
    }
    if (!pendingOpenPath_.empty())
    {
        std::string p = pendingOpenPath_;
        if (app_.project.dirty()) { promptSaveThen(AfterSave::Open); }
        else { pendingOpenPath_.clear(); doOpen(p); }
    }
    if (app_.wantQuit) { app_.wantQuit = false; requestQuit(); }
}

// ---------------------------------------------------------------- performance mode
void Shell::setPerformanceMode(bool on)
{
    if (on == app_.performanceMode) return;
    app_.performanceMode = on;
    organic::DockManager& dock = app_.dock;
    if (on)
    {
        dock.saveLayoutToFile("editing");
        for (const char* n : { "Navigator", "Inspector", "Clip Editor", "Live Monitor", "Log", "Settings" })
            if (auto* p = dock.find(n)) p->open = false;
        if (auto* p = dock.find("Workspace")) p->open = true;
        app_.prefs.activeTab = WorkspaceTab::Sounds;
        OLOG("App", "Performance mode ON (F11 to leave)");
    }
    else
    {
        if (std::filesystem::exists(paths::layoutsDir() / "editing.ini")) dock.requestLoadLayout("editing");
        else
        {
            for (const char* n : { "Navigator", "Inspector", "Clip Editor", "Workspace" })
                if (auto* p = dock.find(n)) p->open = true;
        }
        OLOG("App", "Performance mode OFF");
    }
}

// ---------------------------------------------------------------- menu bar
void Shell::menuBar()
{
    if (!ImGui::BeginMainMenuBar()) return;
    auto& um = organic::UndoManager::get();
    if (ImGui::BeginMenu("File"))
    {
        if (ImGui::MenuItem(ICON_PH_FILE_PLUS "  New", "Ctrl+N")) newRequested_ = true;
        if (ImGui::MenuItem(ICON_PH_FOLDER_OPEN "  Open...", "Ctrl+O")) openDialog_ = true;
        if (ImGui::BeginMenu("Open Recent", !app_.prefs.recentProjects.empty()))
        {
            for (const std::string& r : app_.prefs.recentProjects)
                if (ImGui::MenuItem(r.c_str())) pendingOpenPath_ = r;
            ImGui::Separator();
            if (ImGui::MenuItem("Clear list")) app_.prefs.recentProjects.clear();
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem(ICON_PH_ARROWS_MERGE "  Merge show...", "Ctrl+Shift+O")) mergeDialog_ = true;
        ImGui::Separator();
        if (ImGui::MenuItem(ICON_PH_FLOPPY_DISK "  Save", "Ctrl+S")) doSave();
        if (ImGui::MenuItem("Save As...", "Ctrl+Shift+S")) saveAsDialog_ = true;
        ImGui::Separator();
        if (ImGui::MenuItem(ICON_PH_UPLOAD_SIMPLE "  Import media...", "Ctrl+I")) importDialog_ = true;
        ImGui::Separator();
        if (ImGui::MenuItem("Quit", "Ctrl+Q")) app_.wantQuit = true;
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Edit"))
    {
        std::string u = um.canUndo() ? "Undo " + um.undoName() : "Undo";
        std::string r = um.canRedo() ? "Redo " + um.redoName() : "Redo";
        if (ImGui::MenuItem(u.c_str(), "Ctrl+Z", false, um.canUndo())) um.undo();
        if (ImGui::MenuItem(r.c_str(), "Ctrl+Shift+Z", false, um.canRedo())) um.redo();
        ImGui::Separator();
        Sound* sel = app_.selectedSound();
        if (ImGui::MenuItem(ICON_PH_COPY "  Duplicate", "Ctrl+D", false, sel != nullptr)) app_.importer.duplicateSound(*sel);
        if (ImGui::MenuItem(ICON_PH_TRASH "  Delete", "Del", false, sel != nullptr))
        {
            Sound* sp = sel;
            confirm_.danger = true;
            confirm_.open("Delete '" + sel->niceName + "'?", "The tile and its OSC commands are removed (undo with Ctrl+Z).", [this, sp] { app_.deleteSounds({ sp }); });
        }
        ImGui::Separator();
        if (ImGui::MenuItem(ICON_PH_STOP "  Stop All", "Esc")) { app_.playback.stopAll(); app_.trigger.cancelAll(); }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("View"))
    {
        if (ImGui::MenuItem(ICON_PH_CORNERS_OUT "  Performance Mode", "F11", app_.performanceMode)) setPerformanceMode(!app_.performanceMode);
        if (ImGui::BeginMenu("Tile size"))
        {
            const char* sizes[] = { "Compact", "Standard", "Large" };
            for (int i = 0; i < 3; i++)
                if (ImGui::MenuItem(sizes[i], nullptr, (int)app_.prefs.tileSize == i)) app_.prefs.tileSize = (TileSize)i;
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("Grid view", nullptr, app_.prefs.viewMode == ViewMode::Grid)) app_.prefs.viewMode = ViewMode::Grid;
        if (ImGui::MenuItem("List view", nullptr, app_.prefs.viewMode == ViewMode::List)) app_.prefs.viewMode = ViewMode::List;
        ImGui::Separator();
        if (app_.tiktokAvailable())
        {
            if (ImGui::MenuItem("Sounds tab", "Ctrl+G toggles", app_.prefs.activeTab == WorkspaceTab::Sounds)) app_.prefs.activeTab = WorkspaceTab::Sounds;
            if (ImGui::MenuItem("Gifts tab", "Ctrl+G toggles", app_.prefs.activeTab == WorkspaceTab::Gifts)) app_.prefs.activeTab = WorkspaceTab::Gifts;
            ImGui::Separator();
        }
        ImGui::MenuItem("Debug parameter inspector", nullptr, &showDebugInspector_);
        ImGui::MenuItem("ImGui demo", nullptr, &showImGuiDemo_);
        ImGui::EndMenu();
    }
    app_.dock.viewMenu();
    app_.dock.panelsMenu();
#ifdef EVOBOX_WITH_TIKTOK
    if (ImGui::BeginMenu("Live"))
    {
        bool connected = app_.live.state == LiveState::Connected || app_.live.state == LiveState::Connecting;
        if (ImGui::MenuItem(ICON_PH_BROADCAST "  Connect", nullptr, false, !connected && !app_.prefs.tiktokUsername.empty())) app_.liveConnect(app_.prefs.tiktokUsername);
        if (ImGui::MenuItem(ICON_PH_WIFI_SLASH "  Disconnect", nullptr, false, connected)) app_.liveDisconnect();
        TextDim("  @%s", app_.prefs.tiktokUsername.empty() ? "(set in the Gifts tab)" : app_.prefs.tiktokUsername.c_str());
        ImGui::Separator();
        if (ImGui::MenuItem(ICON_PH_ARROWS_CLOCKWISE "  Refresh gift catalog", nullptr, false, !app_.prefs.tiktokUsername.empty())) app_.liveRefreshCatalog();
        GiftAction* g = app_.selectedGift();
        RoomEventAction* r = app_.selectedRoomEvent();
        if (ImGui::MenuItem(ICON_PH_PLAY "  Simulate selected", "Ctrl+Space", false, g || r))
        {
            if (g) app_.simulateGift(g->giftId); else if (r) app_.simulateRoomEvent(r->kind);
        }
        ImGui::EndMenu();
    }
#endif
    if (ImGui::BeginMenu("Help"))
    {
        if (ImGui::MenuItem("About EvoMusicBox")) aboutPopup_ = true;
        if (ImGui::MenuItem("Open config folder"))
        {
            std::string cmd;
#if defined(_WIN32)
            cmd = "explorer \"" + paths::configDir().string() + "\"";
#elif defined(__APPLE__)
            cmd = "open \"" + paths::configDir().string() + "\"";
#else
            cmd = "xdg-open \"" + paths::configDir().string() + "\" &";
#endif
            int rc = system(cmd.c_str());
            (void)rc;
        }
        ImGui::EndMenu();
    }
    // right side: playing count + dirty
    {
        std::string s = app_.project.title() + (app_.project.dirty() ? " *" : "");
        int playing = app_.playback.playingCount();
        if (playing) s += str::format("   " ICON_PH_SPEAKER_HIGH " %d", playing);
        float w = ImGui::CalcTextSize(s.c_str()).x + 16;
        ImGui::SameLine(ImGui::GetWindowWidth() - w);
        TextDim("%s", s.c_str());
    }
    ImGui::EndMainMenuBar();
}

// ---------------------------------------------------------------- status bar
void Shell::statusBar()
{
    const auto& th = theme::colors();
    ImGuiViewport* vp = ImGui::GetMainViewport();
    float h = ImGui::GetFrameHeight();
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_MenuBar;
    if (ImGui::BeginViewportSideBar("##EvoboxStatus", vp, ImGuiDir_Down, h, flags))
    {
        if (ImGui::BeginMenuBar())
        {
            int playing = app_.playback.playingCount();
            ImGui::TextColored(playing ? th.playing : th.textDim, playing ? ICON_PH_SPEAKER_HIGH " %d playing" : ICON_PH_SPEAKER_SLASH " idle", playing);
            ImGui::Separator();
            TextDim(ICON_PH_PAPER_PLANE_TILT " %llu OSC sent%s", (unsigned long long)app_.trigger.sentCount,
                    app_.trigger.errorCount ? str::format(", %llu failed", (unsigned long long)app_.trigger.errorCount).c_str() : "");
            ImGui::Separator();
            TextDim(app_.audio.ok() ? ICON_PH_WAVEFORM " %s" : ICON_PH_WARNING " no audio device", app_.audio.deviceName().c_str());
#ifdef EVOBOX_WITH_TIKTOK
            ImGui::Separator();
            ImVec4 lc = app_.live.state == LiveState::Connected ? th.playing : (app_.live.state == LiveState::Error ? th.danger : th.textDim);
            ImGui::TextColored(lc, ICON_PH_BROADCAST " %s", liveStateName(app_.live.state));
#endif
            if (app_.media.activeJobs() > 0)
            {
                ImGui::Separator();
                TextDim(ICON_PH_CIRCLE_NOTCH " %d media job(s)", app_.media.activeJobs());
            }
            if (!app_.statusText.empty() && app_.now() - app_.statusTime < 6.0)
            {
                ImGui::Separator();
                ImGui::TextUnformatted(app_.statusText.c_str());
            }
            auto& um = organic::UndoManager::get();
            std::string undo = um.canUndo() ? "undo: " + um.undoName() : "";
            char fps[32];
            snprintf(fps, sizeof(fps), "%.0f fps", ImGui::GetIO().Framerate);
            float w = ImGui::CalcTextSize(fps).x + ImGui::CalcTextSize(undo.c_str()).x + 40;
            ImGui::SameLine(ImGui::GetWindowWidth() - w);
            TextDim("%s", undo.c_str());
            ImGui::SameLine();
            TextDim("%s", fps);
            ImGui::EndMenuBar();
        }
    }
    ImGui::End();
}

// ---------------------------------------------------------------- dialogs
void Shell::dialogs()
{
    IGFD::FileDialog* fd = ImGuiFileDialog::Instance();
    ImVec2 minSize(720 * theme::scale(), 460 * theme::scale());
    if (importDialog_)
    {
        importDialog_ = false;
        IGFD::FileDialogConfig cfg;
        cfg.path = importDialogStartPath.empty() ? "." : importDialogStartPath;
        importDialogStartPath.clear();
        cfg.countSelectionMax = 0;
        cfg.flags = ImGuiFileDialogFlags_Modal | ImGuiFileDialogFlags_CaseInsensitiveExtentionFiltering | ImGuiFileDialogFlags_DontShowHiddenFiles;
        fd->OpenDialog("ImportMedia", ICON_PH_UPLOAD_SIMPLE " Add Sound — choose media files", ImportController::mediaFilters(), cfg);
    }
    if (openDialog_)
    {
        openDialog_ = false;
        IGFD::FileDialogConfig cfg;
        cfg.path = lastProjectDir_.empty() ? "." : lastProjectDir_;
        cfg.flags = ImGuiFileDialogFlags_Modal | ImGuiFileDialogFlags_CaseInsensitiveExtentionFiltering | ImGuiFileDialogFlags_DontShowHiddenFiles;
        fd->OpenDialog("OpenProject", ICON_PH_FOLDER_OPEN " Open show — a .liv file (or a legacy .evobox folder's project.json)",
                       "Show files{.liv},Legacy project{.json},.*", cfg);
    }
    if (mergeDialog_)
    {
        mergeDialog_ = false;
        IGFD::FileDialogConfig cfg;
        cfg.path = lastProjectDir_.empty() ? "." : lastProjectDir_;
        cfg.flags = ImGuiFileDialogFlags_Modal | ImGuiFileDialogFlags_CaseInsensitiveExtentionFiltering | ImGuiFileDialogFlags_DontShowHiddenFiles;
        fd->OpenDialog("MergeProject", ICON_PH_ARROWS_MERGE " Merge show — add another .liv (or .evobox project.json) to this show",
                       "Show files{.liv},Legacy project{.json},.*", cfg);
    }
    if (saveAsDialog_)
    {
        saveAsDialog_ = false;
        IGFD::FileDialogConfig cfg;
        std::string loc = app_.projectLocation();
        cfg.path = !loc.empty() ? std::filesystem::path(loc).parent_path().string() : (lastProjectDir_.empty() ? "." : lastProjectDir_);
        cfg.fileName = app_.hasBundle() ? app_.project.title() + ProjectIO::kArchiveExt : std::string("MyShow") + ProjectIO::kArchiveExt;
        cfg.flags = ImGuiFileDialogFlags_Modal | ImGuiFileDialogFlags_ConfirmOverwrite | ImGuiFileDialogFlags_DontShowHiddenFiles;
        fd->OpenDialog("SaveProject", ICON_PH_FLOPPY_DISK " Save show as — one <name>.liv file (sounds, pictures, OSC setup)",
                       "Show files{.liv}", cfg);
    }
    if (app_.relinkRequestUid)
    {
        Sound* rs = app_.project.sounds.find(app_.relinkRequestUid);
        app_.relinkRequestUid = 0;
        if (rs)
        {
            IGFD::FileDialogConfig cfg;
            cfg.path = std::filesystem::path(rs->source.path).parent_path().string();
            if (cfg.path.empty() || !std::filesystem::exists(cfg.path)) cfg.path = ".";
            cfg.fileName = rs->source.fileName();
            cfg.userDatas = (IGFD::UserDatas)(intptr_t)rs->uid;
            cfg.flags = ImGuiFileDialogFlags_Modal | ImGuiFileDialogFlags_DontShowHiddenFiles;
            fd->OpenDialog("RelinkMedia", ICON_PH_LINK " Relink media — locate the moved source file", ImportController::mediaFilters(), cfg);
        }
    }
    if (app_.stickerImageRequestUid)
    {
        Sound* ss = app_.project.sounds.find(app_.stickerImageRequestUid);
        app_.stickerImageRequestUid = 0;
        if (ss)
        {
            IGFD::FileDialogConfig cfg;
            cfg.path = lastImageDir_.empty() ? "." : lastImageDir_;
            cfg.userDatas = (IGFD::UserDatas)(intptr_t)ss->uid;
            cfg.flags = ImGuiFileDialogFlags_Modal | ImGuiFileDialogFlags_CaseInsensitiveExtentionFiltering | ImGuiFileDialogFlags_DontShowHiddenFiles;
            fd->OpenDialog("StickerImage", ICON_PH_IMAGE " Tile picture — choose an image file", ImportController::imageFilters(), cfg);
        }
    }
    if (fd->Display("RelinkMedia", ImGuiWindowFlags_NoCollapse, minSize))
    {
        if (fd->IsOk())
        {
            Uid uid = (Uid)(intptr_t)fd->GetUserDatas();
            if (Sound* rs = app_.project.sounds.find(uid)) app_.relinkSource(*rs, fd->GetFilePathName());
        }
        fd->Close();
    }
    if (fd->Display("StickerImage", ImGuiWindowFlags_NoCollapse, minSize))
    {
        if (fd->IsOk())
        {
            Uid uid = (Uid)(intptr_t)fd->GetUserDatas();
            lastImageDir_ = fd->GetCurrentPath();
            std::string err;
            if (Sound* ss = app_.project.sounds.find(uid))
                if (!app_.setStickerFromImage(*ss, fd->GetFilePathName(), &err))
                {
                    OLOGE("Media", err);
                    confirm_.danger = false;
                    confirm_.open("Cannot use this image", err, {}, "OK");
                }
        }
        fd->Close();
    }
    if (fd->Display("ImportMedia", ImGuiWindowFlags_NoCollapse, minSize))
    {
        if (fd->IsOk())
        {
            std::vector<std::string> files;
            for (auto& [name, path] : fd->GetSelection()) files.push_back(path);
            std::sort(files.begin(), files.end());
            app_.importer.importFiles(files, app_.selectedCategoryUid);
        }
        fd->Close();
    }
    if (fd->Display("OpenProject", ImGuiWindowFlags_NoCollapse, minSize))
    {
        if (fd->IsOk())
        {
            std::string p = fd->GetFilePathName();
            if (p.empty() || !std::filesystem::exists(p)) p = fd->GetCurrentPath();
            lastProjectDir_ = fd->GetCurrentPath();
            pendingOpenPath_ = p;
        }
        fd->Close();
    }
    if (fd->Display("MergeProject", ImGuiWindowFlags_NoCollapse, minSize))
    {
        if (fd->IsOk())
        {
            std::string p = fd->GetFilePathName();
            if (p.empty() || !std::filesystem::exists(p)) p = fd->GetCurrentPath();
            lastProjectDir_ = fd->GetCurrentPath();
            std::string err;
            if (ProjectMerge::inspect(p, mergeSource_, &err)) mergeOptionsOpen_ = true;
            else
            {
                OLOGE("Project", err);
                confirm_.danger = false;
                confirm_.open("Cannot merge show", err, {}, "OK");
            }
        }
        fd->Close();
    }
    if (fd->Display("SaveProject", ImGuiWindowFlags_NoCollapse, minSize))
    {
        if (fd->IsOk())
        {
            std::string dir = fd->GetFilePathName(IGFD_ResultMode_AddIfNoFileExt);
            lastProjectDir_ = fd->GetCurrentPath();
            std::string err;
            if (app_.saveProjectAs(dir, &err))
            {
                switch (afterSave_)
                {
                case AfterSave::New: doNew(); break;
                case AfterSave::Open: { std::string p = pendingOpenPath_; pendingOpenPath_.clear(); doOpen(p); break; }
                case AfterSave::Quit: app_.quitConfirmed = true; break;
                default: break;
                }
            }
            else { OLOGE("Project", err); confirm_.danger = false; confirm_.open("Cannot save project", err, {}, "OK"); }
            afterSave_ = AfterSave::None;
        }
        else afterSave_ = AfterSave::None;
        fd->Close();
    }

    // unsaved changes prompt
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Unsaved changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("'%s' has unsaved changes.", app_.project.title().c_str());
        ImGui::Spacing();
        float bw = 120 * theme::scale();
        if (AccentButton("Save", ImVec2(bw, 0)))
        {
            ImGui::CloseCurrentPopup();
            if (app_.hasBundle())
            {
                if (doSave())
                {
                    switch (afterSave_)
                    {
                    case AfterSave::New: doNew(); break;
                    case AfterSave::Open: { std::string p = pendingOpenPath_; pendingOpenPath_.clear(); doOpen(p); break; }
                    case AfterSave::Quit: app_.quitConfirmed = true; break;
                    default: break;
                    }
                    afterSave_ = AfterSave::None;
                }
            }
            else saveAsDialog_ = true; // afterSave_ is applied once the Save As dialog completes
        }
        ImGui::SameLine();
        if (DangerButton("Don't save", ImVec2(bw, 0)))
        {
            ImGui::CloseCurrentPopup();
            switch (afterSave_)
            {
            case AfterSave::New: doNew(); break;
            case AfterSave::Open: { std::string p = pendingOpenPath_; pendingOpenPath_.clear(); doOpen(p); break; }
            case AfterSave::Quit: app_.quitConfirmed = true; break;
            default: break;
            }
            afterSave_ = AfterSave::None;
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(bw, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape))
        {
            ImGui::CloseCurrentPopup();
            afterSave_ = AfterSave::None;
            pendingOpenPath_.clear();
        }
        ImGui::EndPopup();
    }

    // merge show: what to take from the inspected show
    if (mergeOptionsOpen_) { ImGui::OpenPopup("Merge show"); mergeOptionsOpen_ = false; }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Merge show", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        const MergeSource& m = mergeSource_;
        ImGui::Text("Add the content of '%s' to '%s'.", m.title.c_str(), app_.project.title().c_str());
        TextDim("%d sound%s in %d categor%s, %d OSC command%s, %d target%s, %d gift action%s, %d room-event command%s",
                m.sounds, m.sounds == 1 ? "" : "s", m.categories, m.categories == 1 ? "y" : "ies",
                m.commands, m.commands == 1 ? "" : "s", m.targets, m.targets == 1 ? "" : "s",
                m.giftActions, m.giftActions == 1 ? "" : "s", m.roomEventCommands, m.roomEventCommands == 1 ? "" : "s");
        ImGui::Spacing();
        if (ImGui::RadioButton("Sounds only", !mergeOsc_)) mergeOsc_ = false;
        ImGui::SameLine(0, 0);
        TextDim("  — the sounds with their clips, pictures and categories");
        if (ImGui::RadioButton("Sounds and OSC", mergeOsc_)) mergeOsc_ = true;
        ImGui::SameLine(0, 0);
        TextDim("  — plus their OSC commands, the targets they use, gift and room-event actions");
        ImGui::Spacing();
        TextDim("Categories and targets that already exist here (same name / same host:port) are reused;");
        TextDim("gifts already configured here keep their action. One undo step.");
        ImGui::Spacing();
        float bw = 120 * theme::scale();
        if (AccentButton("Merge", ImVec2(bw, 0)) || ImGui::IsKeyPressed(ImGuiKey_Enter))
        {
            ImGui::CloseCurrentPopup();
            MergeOptions opts;
            opts.osc = mergeOsc_;
            std::string err;
            if (!app_.mergeProject(mergeSource_, opts, nullptr, &err))
            {
                OLOGE("Project", err);
                confirm_.danger = false;
                confirm_.open("Cannot merge show", err, {}, "OK");
            }
            mergeSource_ = MergeSource{};
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(bw, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape))
        {
            ImGui::CloseCurrentPopup();
            mergeSource_ = MergeSource{};
        }
        ImGui::EndPopup();
    }

    // about
    if (aboutPopup_) { ImGui::OpenPopup("About EvoMusicBox"); aboutPopup_ = false; }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("About EvoMusicBox", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.4f);
        ImGui::TextUnformatted(ICON_PH_MUSIC_NOTES "  EvoMusicBox");
        ImGui::PopFont();
        TextDim("Version %s", EVOBOX_VERSION);
        ImGui::Separator();
        ImGui::BulletText("Soundboard + lightweight clip trimmer + OSC trigger launcher");
        ImGui::BulletText("TikTok LIVE gift gallery: every gift can carry OSC actions");
        ImGui::BulletText("Dear ImGui (docking) · imgui_organic · FFmpeg · miniaudio · Phosphor icons");
        ImGui::Spacing();
        if (ImGui::Button("Close", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    confirm_.draw();
    if (showDebugInspector_) organic::InspectorPanel(&showDebugInspector_);
    if (showImGuiDemo_) ImGui::ShowDemoWindow(&showImGuiDemo_);
}

// ---------------------------------------------------------------- shortcuts
void Shell::shortcuts()
{
    ImGuiIO& io = ImGui::GetIO();
    auto& um = organic::UndoManager::get();
    // Esc = Stop All (panic) works even while typing
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId) && !io.WantTextInput)
    {
        app_.playback.stopAll();
        app_.trigger.cancelAll();
        app_.setStatus("Stop All");
    }
    if (io.WantTextInput) return;
    if (ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)) return;
    bool ctrl = io.KeyCtrl || io.KeySuper;
    if (ctrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z, false)) um.undo();
    if (ctrl && (ImGui::IsKeyPressed(ImGuiKey_Y, false) || (io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z, false)))) um.redo();
    if (ctrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_S, false)) doSave();
    if (ctrl && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_S, false)) saveAsDialog_ = true;
    if (ctrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_O, false)) openDialog_ = true;
    if (ctrl && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_O, false)) mergeDialog_ = true;
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_N, false)) newRequested_ = true;
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_I, false)) importDialog_ = true;
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Q, false)) app_.wantQuit = true;
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_F, false)) workspace_.soundboard().requestFocusSearch();
    if (ImGui::IsKeyPressed(ImGuiKey_F11, false)) setPerformanceMode(!app_.performanceMode);
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_G, false) && app_.tiktokAvailable() && !app_.performanceMode)
        app_.prefs.activeTab = app_.prefs.activeTab == WorkspaceTab::Sounds ? WorkspaceTab::Gifts : WorkspaceTab::Sounds;

    Sound* sel = app_.selectedSound();
    if (sel)
    {
        if (ImGui::IsKeyPressed(ImGuiKey_Space, false) && !ctrl) app_.playback.toggle(sel->uid, TriggerSource::Shortcut);
        if (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false)) app_.playback.play(*sel, TriggerSource::Shortcut);
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_D, false)) app_.importer.duplicateSound(*sel);
        if (ImGui::IsKeyPressed(ImGuiKey_Delete, false) || ImGui::IsKeyPressed(ImGuiKey_Backspace, false))
        {
            Sound* sp = sel;
            confirm_.danger = true;
            confirm_.open("Delete '" + sel->niceName + "'?", "The tile and its OSC commands are removed (undo with Ctrl+Z).", [this, sp] { app_.deleteSounds({ sp }); });
        }
    }
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Space, false))
    {
        if (GiftAction* g = app_.selectedGift()) app_.simulateGift(g->giftId);
        else if (RoomEventAction* r = app_.selectedRoomEvent()) app_.simulateRoomEvent(r->kind);
        else if (app_.selectedSound()) clipEditor_.playSelected();
    }
    if (app_.prefs.activeTab == WorkspaceTab::Sounds)
    {
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) workspace_.soundboard().moveSelection(-1, 0);
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) workspace_.soundboard().moveSelection(1, 0);
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) workspace_.soundboard().moveSelection(0, -1);
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) workspace_.soundboard().moveSelection(0, 1);
    }
}

void Shell::focusBottomPanelIfRequested()
{
    if (app_.focusBottomPanel.empty()) return;
    // Tiles select on mouse-down. Focusing another window while the mouse button is still held
    // makes ImGui clear the active item (FocusWindow "steals active widgets"), which kills the
    // drag that may be starting from that tile (reorder / drop onto a Navigator category). Wait
    // for the release; drop the request entirely when a drag & drop is in flight.
    if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
        if (ImGui::IsDragDropActive()) app_.focusBottomPanel.clear();
        return;
    }
    std::string name = app_.focusBottomPanel;
    app_.focusBottomPanel.clear();
    if (app_.performanceMode) return;
    organic::DockManager::Panel* p = app_.dock.find(name);
    if (!p) return;
    if (!p->open) p->open = true;
    ImGui::SetWindowFocus(name.c_str());
}

// ---------------------------------------------------------------- frame
void Shell::frame(double now)
{
    (void)now;
    menuBar();
    statusBar();
    focusBottomPanelIfRequested();
    app_.dock.gui();
    app_.dock.popupsGui();
    if (!app_.performanceMode) app_.dock.shortcuts();
    shortcuts();
    dialogs();
    applyPendingActions();
    if (workspace_.soundboard().wantOpenFileDialog) { workspace_.soundboard().wantOpenFileDialog = false; importDialog_ = true; }
    // unclaimed OS drops -> current category
    auto files = app_.drops.claimAny();
    if (!files.empty()) app_.importer.importFiles(files, app_.selectedCategoryUid);
    // a dropped / imported show file (.liv, .evobox) is opened instead of imported
    if (!app_.openRequestPath.empty()) { pendingOpenPath_ = app_.openRequestPath; app_.openRequestPath.clear(); }
    if (settings_.wantReinitAudio)
    {
        settings_.wantReinitAudio = false;
        AudioSettings as;
        as.deviceName = app_.prefs.audioDevice;
        as.periodSizeInFrames = app_.prefs.periodSizeInFrames;
        as.masterVolume = app_.project.settings.masterVolume();
        app_.playback.stopAll(0);
        app_.audio.reinit(as);
    }
    organic::CommitPendingParamEdits();
}

} // namespace ui
} // namespace evobox
