// main.cpp — platform layer: GLFW window + OpenGL 3, ImGui/ImPlot contexts, theme + fonts,
// services, OS file drop, GL texture upload hook, and the frame loop described in
// docs/architecture.md §6.5.
#include "app/Application.h"
#include "app/Demo.h"
#include "ui/Fonts.h"
#include "ui/I18n.h"
#include "ui/Shell.h"
#include "ui/Theme.h"
#include "util/CrashHandler.h"
#include "util/Paths.h"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "implot.h"
#if defined(_WIN32)
// before GLFW (APIENTRY) and GL/gl.h, which needs the Win32 calling-convention macros
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <io.h>
#endif
#include <GLFW/glfw3.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#if defined(__APPLE__)
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F // GL 1.2 token; mingw's GL/gl.h stops at GL 1.1
#endif
// the stb_image_write implementation is compiled once in media/ImageWriter.cpp (evobox_core)
#include "stb_image_write.h"

using namespace evobox;

namespace
{

Application* g_app = nullptr;

void glfwErrorCb(int e, const char* d) { fprintf(stderr, "GLFW error %d: %s\n", e, d); }

void dropCallback(GLFWwindow* window, int count, const char** paths)
{
    if (!g_app) return;
    std::vector<std::string> files;
    for (int i = 0; i < count; i++) files.emplace_back(paths[i]);
    double mx = 0, my = 0;
    glfwGetCursorPos(window, &mx, &my);
    // convert to ImGui screen space (viewports: window pos + cursor). Wayland cannot report the
    // window position (GLFW leaves the outputs untouched and reports an error): keep 0,0 so the
    // drop still lands in the main viewport instead of at a garbage coordinate.
    int wx = 0, wy = 0;
    glfwGetWindowPos(window, &wx, &wy);
    ImVec2 pos((float)(mx + wx), (float)(my + wy));
    if (!(ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable)) pos = ImVec2((float)mx, (float)my);
    g_app->drops.push(std::move(files), pos);
}

TextureHandle uploadRgba(const RgbaImage& img)
{
    TextureHandle h;
    if (img.empty()) return h;
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, img.width, img.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, img.pixels.data());
    h.glId = tex;
    h.width = img.width;
    h.height = img.height;
    return h;
}

void releaseTexture(const TextureHandle& h)
{
    if (h.glId) { GLuint t = (GLuint)h.glId; glDeleteTextures(1, &t); }
}

// Window-system platform choice (Linux). GLFW <= 3.4 crashes inside its Wayland drag & drop
// "enter" handler when a drag crosses a non-GLFW surface such as the window decorations
// (fixed in GLFW 3.5.1). When we run against such a system library on a Wayland session we
// prefer the X11 platform (XWayland) so file drops are safe. EVOBOX_PLATFORM=wayland|x11 forces
// either; the vendored GLFW build keeps native Wayland (the fix is compiled in).
void choosePlatform()
{
#if defined(__linux__) && defined(GLFW_PLATFORM)
    const char* force = std::getenv("EVOBOX_PLATFORM");
    if (force && (strcmp(force, "x11") == 0 || strcmp(force, "X11") == 0))
    {
        glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);
        return;
    }
    if (force && (strcmp(force, "wayland") == 0 || strcmp(force, "Wayland") == 0))
    {
        glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_WAYLAND);
        return;
    }
    int major = 0, minor = 0, rev = 0;
    glfwGetVersion(&major, &minor, &rev);
    bool dndFixed = (major > 3) || (major == 3 && (minor > 5 || (minor == 5 && rev >= 1)));
    bool waylandSession = std::getenv("WAYLAND_DISPLAY") != nullptr;
    bool x11Available = std::getenv("DISPLAY") != nullptr && glfwPlatformSupported(GLFW_PLATFORM_X11);
    if (!dndFixed && waylandSession && x11Available)
    {
        fprintf(stderr, "GLFW %d.%d.%d has a Wayland drag & drop crash (fixed in 3.5.1): using X11 (XWayland). "
                        "Set EVOBOX_PLATFORM=wayland to override.\n", major, minor, rev);
        glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);
    }
#endif
}

// Mirrors new organic Logger entries to stderr (--verbose / smoke runs).
void mirrorLog()
{
    static size_t printed = 0;
    auto& entries = organic::Logger::get().entries;
    for (; printed < entries.size(); printed++)
    {
        const organic::LogEntry& e = entries[printed];
        const char* lvl = e.level == organic::LogLevel::Error ? "ERROR" : (e.level == organic::LogLevel::Warning ? "WARN " : "INFO ");
        fprintf(stderr, "[%8.3f] %s %-8s %s\n", e.time, lvl, e.source.c_str(), e.message.c_str());
    }
}

// Dumps the current framebuffer to a PNG (used by --screenshot for UI verification).
void screenshot(GLFWwindow* window, const std::string& path)
{
    int w, h;
    glfwGetFramebufferSize(window, &w, &h);
    std::vector<unsigned char> px((size_t)w * h * 4);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    // flip vertically
    std::vector<unsigned char> row((size_t)w * 4);
    for (int y = 0; y < h / 2; y++)
    {
        unsigned char* a = px.data() + (size_t)y * w * 4;
        unsigned char* b = px.data() + (size_t)(h - 1 - y) * w * 4;
        memcpy(row.data(), a, row.size()); memcpy(a, b, row.size()); memcpy(b, row.data(), row.size());
    }
    stbi_write_png(path.c_str(), w, h, 4, px.data(), w * 4);
    fprintf(stderr, "screenshot written to %s (%dx%d)\n", path.c_str(), w, h);
}

bool hasArg(int argc, char** argv, const char* flag)
{
    for (int i = 1; i < argc; i++) if (strcmp(argv[i], flag) == 0) return true;
    return false;
}

} // namespace

#if defined(_WIN32)
// evobox.exe is a GUI-subsystem program (no console window of its own). When it is started from
// a console (cmd / PowerShell / `wine evobox.exe` in a terminal) attach to it so the smoke flags
// and --verbose print like on Linux; without a parent console stdout/stderr stay unconnected.
static void attachParentConsole()
{
    if (!AttachConsole(ATTACH_PARENT_PROCESS)) return;
    FILE* f = nullptr;
    if (freopen_s(&f, "CONOUT$", "w", stdout) == 0) setvbuf(stdout, nullptr, _IONBF, 0);
    if (freopen_s(&f, "CONOUT$", "w", stderr) == 0) setvbuf(stderr, nullptr, _IONBF, 0);
}
#endif

int main(int argc, char** argv)
{
#if defined(_WIN32)
    attachParentConsole();
#endif
    // --frames N : run N frames then exit (UI smoke test); --no-audio : null audio backend;
    // --new : start with an empty project instead of reopening the last one; --import <file> : add a sound
    int smokeFrames = 0;
    std::string screenshotPath;
    std::string saveAsPath; // --save-as <file.liv | dir.evobox>: save the project at the end of the smoke run
    std::vector<std::string> importPaths; // --import <media file> (repeatable): added as sounds after startup
    std::string importDialogDir;          // --import-dialog <dir>: open the Add Sound dialog there (smoke/debug)
    std::vector<std::string> dropPaths;   // --drop <file|dir> (repeatable): simulate an OS file drop on frame 10
    std::string mergePath;                // --merge <show>: File > Merge show (with its OSC) on frame 10
    bool visibleSmoke = hasArg(argc, argv, "--visible");
    for (int i = 1; i + 1 < argc; i++)
    {
        if (strcmp(argv[i], "--frames") == 0) smokeFrames = atoi(argv[i + 1]);
        if (strcmp(argv[i], "--screenshot") == 0) screenshotPath = argv[i + 1];
        if (strcmp(argv[i], "--save-as") == 0) saveAsPath = argv[i + 1];
        if (strcmp(argv[i], "--import") == 0) importPaths.push_back(argv[i + 1]);
        if (strcmp(argv[i], "--import-dialog") == 0) importDialogDir = argv[i + 1];
        if (strcmp(argv[i], "--drop") == 0) dropPaths.push_back(argv[i + 1]);
        if (strcmp(argv[i], "--merge") == 0) mergePath = argv[i + 1];
    }
    bool noAudio = hasArg(argc, argv, "--no-audio");
    bool verbose = hasArg(argc, argv, "--verbose");
    bool demoMode = hasArg(argc, argv, "--demo");
    bool demoGifts = hasArg(argc, argv, "--demo-gifts");
    bool perfMode = hasArg(argc, argv, "--performance");
    std::string openPath;
    for (int i = 1; i < argc; i++)
        if (argv[i][0] != '-' && (i == 1 || (strcmp(argv[i - 1], "--frames") != 0 && strcmp(argv[i - 1], "--screenshot") != 0 &&
                                              strcmp(argv[i - 1], "--save-as") != 0 && strcmp(argv[i - 1], "--import") != 0 &&
                                              strcmp(argv[i - 1], "--import-dialog") != 0 && strcmp(argv[i - 1], "--drop") != 0 &&
                                              strcmp(argv[i - 1], "--merge") != 0))) openPath = argv[i];

    crash::install(paths::configDir().string(),
                   std::string("EvoMusicBox ") + EVOBOX_VERSION + " built " __DATE__ " " __TIME__ + " (" + IMGUI_VERSION + ")");
    glfwSetErrorCallback(glfwErrorCb);
    choosePlatform();
    if (!glfwInit()) { fprintf(stderr, "glfwInit failed\n"); return 1; }
#if defined(__APPLE__)
    // macOS's NSGL only exposes a 3.2+ Core profile (no 3.0/3.1); it must be requested
    // forward-compatible. The GL3 backend then needs the matching GLSL "#version 150".
    const char* glslVersion = "#version 150";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#else
    const char* glslVersion = "#version 130";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
#endif
    if (smokeFrames > 0 && !visibleSmoke) glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);

    Application app;
    g_app = &app;
    app.prefs.load(paths::prefsFile().string());
    // Load the UI translations and select the saved language before any string is drawn.
    I18n::get().init();
    I18n::get().setLanguage(app.prefs.language);
    int ww = app.prefs.windowW > 400 ? app.prefs.windowW : 1600;
    int wh = app.prefs.windowH > 300 ? app.prefs.windowH : 940;
    GLFWwindow* window = glfwCreateWindow(ww, wh, "EvoMusicBox", nullptr, nullptr);
    if (!window) { glfwTerminate(); fprintf(stderr, "cannot create window\n"); return 1; }
    if (app.prefs.windowX >= 0 && app.prefs.windowY >= 0) glfwSetWindowPos(window, app.prefs.windowX, app.prefs.windowY);
    if (app.prefs.maximized) glfwMaximizeWindow(window);
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);
    glfwSetDropCallback(window, dropCallback);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext(); // organic's debug panels draw with ImPlot; creating the context is cheap
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_DockingEnable;
    if (smokeFrames == 0 || visibleSmoke) io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
    io.ConfigDockingWithShift = false;
    io.ConfigDpiScaleFonts = true;
    io.ConfigDpiScaleViewports = true;
    static std::string iniPath = paths::imguiIniFile().string();
    io.IniFilename = iniPath.c_str();

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glslVersion);
    theme::apply(app.prefs.uiScale);
    Fonts::load(app.prefs.uiScale);

    // ---- application + services
    app.uploadTexture = uploadRgba;
    app.releaseTexture = releaseTexture;
    app.setWindowTitle = [window](const std::string& t) { glfwSetWindowTitle(window, t.c_str()); };
    if (noAudio)
    {
        // init() reads prefs; force the null backend by pre-initialising the engine
        AudioSettings as; as.nullBackend = true;
        app.audio.init(as);
    }
    app.init();

    ui::Shell shell(app);
    shell.init();

    // ---- last project
    if (!openPath.empty()) { std::string err; if (!app.openProject(openPath, &err)) OLOGE("Project", err); }
    else if (app.prefs.reopenLastProject && !app.prefs.lastProject.empty() && !hasArg(argc, argv, "--new"))
    {
        std::string err;
        if (!app.openProject(app.prefs.lastProject, &err)) OLOGW("Project", "Could not reopen last project: " << err);
    }
#ifdef EVOBOX_WITH_TIKTOK
    if (app.prefs.tiktokAutoConnect && !app.prefs.tiktokUsername.empty() && smokeFrames == 0) app.liveConnect(app.prefs.tiktokUsername);
#endif
    if (demoMode) demo::importDemoAudio(app);
    if (demoGifts) demo::seedDemoCatalog(app);
    if (!importPaths.empty()) app.importer.importFiles(importPaths, app.selectedCategoryUid);
    if (!importDialogDir.empty()) { shell.importDialogStartPath = importDialogDir; shell.requestImportDialog(); }
    if (perfMode) shell.setPerformanceMode(true);
    if (hasArg(argc, argv, "--crash-test")) { volatile int* p = nullptr; *p = 42; } // verifies the crash reporter
    {
        int gmaj = 0, gmin = 0, grev = 0;
        glfwGetVersion(&gmaj, &gmin, &grev);
        const char* platform = "unknown";
#ifdef GLFW_PLATFORM
        switch (glfwGetPlatform())
        {
        case GLFW_PLATFORM_WAYLAND: platform = "Wayland"; break;
        case GLFW_PLATFORM_X11: platform = "X11"; break;
        case GLFW_PLATFORM_WIN32: platform = "Win32"; break;
        case GLFW_PLATFORM_COCOA: platform = "Cocoa"; break;
        default: break;
        }
#endif
        OLOG("App", "GLFW " << gmaj << "." << gmin << "." << grev << (EVOBOX_GLFW_VENDORED ? " (vendored)" : " (system)") << ", platform " << platform);
    }
    OLOG("App", "Ready. Click a tile to select, its play button to trigger. Esc = Stop All, F11 = performance mode.");

    // ---- frame loop
    int frame = 0;
    while (!glfwWindowShouldClose(window) && !shell.wantsClose())
    {
        glfwPollEvents();                        // fills FileDropQueue via callback
        if (frame == 10 && !dropPaths.empty())
        {
            std::vector<const char*> ptrs;
            for (auto& d : dropPaths) ptrs.push_back(d.c_str());
            dropCallback(window, (int)ptrs.size(), ptrs.data());
        }
        if (frame == 10 && !mergePath.empty())
        {
            MergeSource src;
            std::string err;
            if (!ProjectMerge::inspect(mergePath, src, &err) || !app.mergeProject(src, MergeOptions{}, nullptr, &err))
                OLOGE("Project", "--merge: " << err);
        }
        if (glfwGetWindowAttrib(window, GLFW_ICONIFIED) && smokeFrames == 0) { glfwWaitEventsTimeout(0.1); }
        app.drainAll();                          // audio/media/osc/live events -> controllers (main thread)
        app.dock.preNewFrame();
        if (shell.wantReloadFonts())
        {
            theme::apply(app.prefs.uiScale);
            Fonts::load(app.prefs.uiScale);
        }
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        double now = nowSeconds();
        app.tick(now);                           // tile progress, debounced renders, autosave
        if (demoMode && smokeFrames > 0) demo::driveSmokeDemo(app, frame, smokeFrames / 2);
        if (demoGifts && smokeFrames > 0) demo::driveSmokeGifts(app, frame, smokeFrames * 2 / 3);
        shell.frame(now);                        // menubar · dock · panels · dialogs · shortcuts · commit undo

        ImGui::Render();
        int dw, dh;
        glfwGetFramebufferSize(window, &dw, &dh);
        glViewport(0, 0, dw, dh);
        const ImVec4& bg = theme::colors().windowBg;
        glClearColor(bg.x, bg.y, bg.z, 1.f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
        {
            GLFWwindow* backup = glfwGetCurrentContext();
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
            glfwMakeContextCurrent(backup);
        }
        if (verbose || smokeFrames > 0) mirrorLog();
        if (!screenshotPath.empty() && smokeFrames > 0 && frame + 1 == smokeFrames) screenshot(window, screenshotPath);
        glfwSwapBuffers(window);
        app.endFrame();

        if (glfwWindowShouldClose(window) && !shell.wantsClose())
        {
            glfwSetWindowShouldClose(window, GLFW_FALSE);
            shell.requestQuit();                 // asks to save when dirty
        }
        if (smokeFrames > 0)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(16)); // hidden window: no vsync
            if (++frame >= smokeFrames) break;
        }
    }

    if (!saveAsPath.empty())
    {
        std::string err;
        if (app.saveProjectAs(saveAsPath, &err)) fprintf(stderr, "saved project to %s\n", app.projectLocation().c_str());
        else fprintf(stderr, "save failed: %s\n", err.c_str());
    }

    // ---- shutdown
    {
        int x, y, w, h;
        glfwGetWindowPos(window, &x, &y);
        glfwGetWindowSize(window, &w, &h);
        app.prefs.maximized = glfwGetWindowAttrib(window, GLFW_MAXIMIZED) != 0;
        if (!app.prefs.maximized) { app.prefs.windowX = x; app.prefs.windowY = y; app.prefs.windowW = w; app.prefs.windowH = h; }
    }
    shell.shutdown();
    app.shutdown();
    if (verbose || smokeFrames > 0) mirrorLog();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    g_app = nullptr;
    return 0;
}
