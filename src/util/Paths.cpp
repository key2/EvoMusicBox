#include "util/Paths.h"
#include <cstdlib>
#include <system_error>

#if defined(_WIN32)
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <climits>
#else
#include <unistd.h>
#include <climits>
#endif

namespace fs = std::filesystem;

namespace evobox
{
namespace paths
{

static const char* kAppDirName = "EvoMusicBox";

fs::path exeDir()
{
    static fs::path cached;
    if (!cached.empty()) return cached;
#if defined(_WIN32)
    wchar_t buf[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    cached = n ? fs::path(std::wstring(buf, n)).parent_path() : fs::current_path();
#elif defined(__APPLE__)
    char buf[PATH_MAX];
    uint32_t size = sizeof(buf);
    if (_NSGetExecutablePath(buf, &size) == 0)
    {
        std::error_code ec;
        cached = fs::weakly_canonical(fs::path(buf), ec).parent_path();
    }
    else cached = fs::current_path();
#else
    char buf[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n > 0) { buf[n] = 0; cached = fs::path(buf).parent_path(); }
    else cached = fs::current_path();
#endif
    return cached;
}

static fs::path homeDir()
{
#if defined(_WIN32)
    if (const char* p = std::getenv("USERPROFILE")) return fs::path(p);
#else
    if (const char* p = std::getenv("HOME")) return fs::path(p);
#endif
    return fs::current_path();
}

bool ensureDir(const fs::path& p)
{
    std::error_code ec;
    if (fs::exists(p, ec)) return true;
    return fs::create_directories(p, ec);
}

bool replaceFile(const fs::path& tmp, const fs::path& dst, std::string* err)
{
    std::error_code ec;
    fs::rename(tmp, dst, ec);
    if (!ec) return true;
    std::string why = ec.message();
    // Windows: rename over an existing file may fail. Only then remove the destination, and only
    // while the replacement still exists — never trade a good file for a missing one.
    if (fs::is_regular_file(tmp, ec) && fs::exists(dst, ec))
    {
        fs::remove(dst, ec);
        fs::rename(tmp, dst, ec);
        if (!ec) return true;
        why = ec.message();
    }
    fs::remove(tmp, ec);
    if (err) *err = "cannot replace " + dst.string() + ": " + why;
    return false;
}

fs::path configDir()
{
    fs::path base;
#if defined(_WIN32)
    if (const char* p = std::getenv("APPDATA")) base = fs::path(p);
    else base = homeDir() / "AppData" / "Roaming";
#elif defined(__APPLE__)
    base = homeDir() / "Library" / "Application Support";
#else
    if (const char* p = std::getenv("XDG_CONFIG_HOME")) base = fs::path(p);
    else base = homeDir() / ".config";
#endif
    fs::path dir = base / kAppDirName;
    ensureDir(dir);
    return dir;
}

fs::path cacheDir()
{
    fs::path base;
#if defined(_WIN32)
    if (const char* p = std::getenv("LOCALAPPDATA")) base = fs::path(p);
    else base = homeDir() / "AppData" / "Local";
#elif defined(__APPLE__)
    base = homeDir() / "Library" / "Caches";
#else
    if (const char* p = std::getenv("XDG_CACHE_HOME")) base = fs::path(p);
    else base = homeDir() / ".cache";
#endif
    fs::path dir = base / kAppDirName;
    ensureDir(dir);
    return dir;
}

fs::path layoutsDir()      { fs::path d = configDir() / "layouts"; ensureDir(d); return d; }
fs::path prefsFile()       { return configDir() / "prefs.json"; }
fs::path imguiIniFile()    { return configDir() / "imgui.ini"; }
fs::path giftCatalogFile() { return cacheDir() / "gift-catalog.json"; }
fs::path giftIconsDir()    { fs::path d = cacheDir() / "gift-icons"; ensureDir(d); return d; }

std::string findResource(const std::string& rel)
{
    std::error_code ec;
    fs::path candidates[] = {
        exeDir() / rel,
#if defined(__APPLE__)
        // Inside an .app the executable is Contents/MacOS/evobox and resources live in
        // Contents/Resources (tools/macos/build.sh stages them there).
        exeDir().parent_path() / "Resources" / rel,
#endif
#ifdef EVOBOX_SOURCE_DIR
        fs::path(EVOBOX_SOURCE_DIR) / rel,
#endif
        fs::current_path() / rel,
    };
    for (auto& c : candidates)
        if (fs::exists(c, ec)) return c.string();
    return "";
}

std::string fontPath(const std::string& fileName)
{
    std::string p = findResource("fonts/" + fileName);
    if (!p.empty()) return p;
    p = findResource("assets/fonts/" + fileName);
    if (!p.empty()) return p;
#ifdef EVOBOX_IMGUI_FONT_DIR
    std::error_code ec;
    fs::path q = fs::path(EVOBOX_IMGUI_FONT_DIR) / fileName;
    if (fs::exists(q, ec)) return q.string();
#endif
    return "";
}

} // namespace paths
} // namespace evobox
