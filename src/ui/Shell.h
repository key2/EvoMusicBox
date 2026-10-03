// Shell.h — the application shell: dock panels, menubar, status bar, file dialogs, shortcuts,
// performance mode. One instance owns every panel and is driven by main.cpp's frame loop.
#pragma once

#include <memory>
#include <string>
#include "app/Application.h"
#include "ui/panels/ClipEditorPanel.h"
#include "ui/panels/InspectorPanel.h"
#include "ui/panels/NavigatorPanel.h"
#include "ui/panels/SettingsPanel.h"
#include "ui/panels/WorkspacePanel.h"
#include "ui/widgets/Common.h"
#ifdef EVOBOX_WITH_TIKTOK
#include "ui/panels/LiveMonitorPanel.h"
#endif

namespace evobox
{
namespace ui
{

class Shell
{
public:
    explicit Shell(Application& app);

    void init();                 // registers dock panels + inspector hooks
    void shutdown();             // leaves performance mode so the saved panel state is the editing one
    void frame(double now);      // menubar · dock · panels · dialogs · shortcuts (inside NewFrame/Render)
    void setPerformanceMode(bool on);
    bool performanceMode() const { return app_.performanceMode; }

    // requests from other components
    void requestOpenDialog() { openDialog_ = true; }
    void requestSaveAsDialog() { saveAsDialog_ = true; }
    void requestImportDialog() { importDialog_ = true; }
    std::string importDialogStartPath;   // optional start folder for the next import dialog (smoke/debug)
    void requestQuit();          // asks to save when dirty
    bool wantsClose() const { return app_.quitConfirmed; }
    bool wantReloadFonts() { bool r = settings_.wantReloadFonts; settings_.wantReloadFonts = false; return r; }

private:
    Application& app_;
    NavigatorPanel navigator_;
    WorkspacePanel workspace_;
    InspectorPanel inspector_;
    ClipEditorPanel clipEditor_;
    SettingsPanel settings_;
#ifdef EVOBOX_WITH_TIKTOK
    LiveMonitorPanel liveMonitor_;
#endif
    ConfirmPopup confirm_{ "ConfirmShell" };
    bool openDialog_ = false, saveAsDialog_ = false, importDialog_ = false, newRequested_ = false;
    bool mergeDialog_ = false;          // File > Merge show...: file picker, then the options modal
    bool mergeOptionsOpen_ = false;
    bool mergeOsc_ = true;              // modal choice: also the OSC setup (targets, commands, gift / room actions)
    MergeSource mergeSource_;           // the inspected show while the modal is up
    bool aboutPopup_ = false;
    bool showDebugInspector_ = false;
    bool showImGuiDemo_ = false;
    std::string pendingOpenPath_;
    std::string savedLayoutBeforePerf_;
    std::string recoverAutosavePath_;   // newer autosave found while opening
    std::string lastImageDir_;          // sticker image dialog: last folder
    std::string lastProjectDir_;        // open / save as dialogs: last folder
    enum class AfterSave { None, New, Open, Quit } afterSave_ = AfterSave::None;

    void menuBar();
    void languageMenu();
    void statusBar();
    void dialogs();
    void shortcuts();
    void applyPendingActions();
    void doNew();
    void doOpen(const std::string& path);
    bool doSave();
    void promptSaveThen(AfterSave next);
    void focusBottomPanelIfRequested();
};

} // namespace ui
} // namespace evobox
