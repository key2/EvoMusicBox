// Paths.h — well-known directories (config, cache, executable) with per-platform defaults.
#pragma once

#include <filesystem>
#include <string>

namespace evobox
{

namespace paths
{
std::filesystem::path exeDir();          // directory containing the running executable
std::filesystem::path configDir();       // ~/.config/EvoMusicBox (created on demand)
std::filesystem::path cacheDir();        // ~/.cache/EvoMusicBox (created on demand)
std::filesystem::path layoutsDir();      // <configDir>/layouts
std::filesystem::path prefsFile();       // <configDir>/prefs.json
std::filesystem::path imguiIniFile();    // <configDir>/imgui.ini
std::filesystem::path giftCatalogFile(); // <cacheDir>/gift-catalog.json
std::filesystem::path giftIconsDir();    // <cacheDir>/gift-icons

// Search order for a deployed resource: <exeDir>/<rel>, <source dir>/<rel>. "" when absent.
std::string findResource(const std::string& rel);
std::string fontPath(const std::string& fileName); // fonts/<file>, falls back to imgui/misc/fonts

bool ensureDir(const std::filesystem::path& p);

// Moves a completely written temporary file over `dst` (atomic on POSIX; Windows may need a
// remove + rename). The destination is only ever removed while `tmp` is intact, so a failure —
// including the temp file having disappeared — leaves any existing `dst` untouched. The temp file
// is deleted on failure. Returns false with `err`.
bool replaceFile(const std::filesystem::path& tmp, const std::filesystem::path& dst, std::string* err = nullptr);
} // namespace paths

} // namespace evobox
