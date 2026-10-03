// I18n.h — tiny runtime translation layer. UI strings live in assets/lang/<code>.json (flat
// key -> UTF-8 string maps), never hard-coded in three languages. tr("key") returns the string for
// the active language, falling back to English and finally to the key itself, so a missing
// translation is visible but never fatal. Adding a language later is just dropping another JSON
// file next to en.json; adding a string is one key in each file.
//
// Usage:
//   #include "ui/I18n.h"
//   ImGui::MenuItem(TR("menu.file"));                 // -> "File" / "Файл" / "文件"
//   TrFmt("status.importing", path)                    // "Importing %s" style, one %s argument
//
// ImGui takes const char*; tr() returns a reference to a std::string owned by the catalogue, so the
// pointer stays valid for the frame (the catalogue is only replaced by setLanguage(), between
// frames). The returned storage is stable until the next setLanguage().
#pragma once

#include <string>
#include <vector>

namespace evobox
{

struct LanguageInfo
{
    std::string code;        // "en", "ru", "zh"
    std::string nativeName;  // "English", "Русский", "中文" (shown in the Language menu)
};

class I18n
{
public:
    static I18n& get();

    // Scan the resource dir (fonts/.. -> lang/) for <code>.json files and load English as the
    // always-present fallback. Safe to call again; it rescans. Returns the available languages.
    void init();

    // Switch the active language by code (e.g. "ru"). Loads the file if needed. Unknown codes fall
    // back to English. No-op when already active.
    void setLanguage(const std::string& code);

    const std::string& language() const { return active_; }
    const std::vector<LanguageInfo>& available() const { return languages_; }

    // Translate a key. Returns the active string, else English, else the key itself.
    const std::string& tr(const std::string& key) const;

private:
    I18n() = default;
    struct Catalogue; // pimpl to keep nlohmann/json out of this header
    std::shared_ptr<Catalogue> loadFile(const std::string& code) const;

    std::string active_ = "en";
    std::vector<LanguageInfo> languages_;
    std::shared_ptr<Catalogue> activeCat_;   // active language
    std::shared_ptr<Catalogue> englishCat_;  // fallback
};

// Convenience: returns const char* for ImGui. Valid for the current frame.
const char* tr(const std::string& key);

// printf-style single-argument helpers (the format string is itself translated and must keep the
// same conversion specifier). The result is returned in a thread-local buffer, valid until the next
// call on the same thread — copy it if you need to keep it.
const char* trFmt(const std::string& key, const char* a);
const char* trFmt(const std::string& key, const std::string& a);
const char* trFmt(const std::string& key, int a);

#define TR(k) ::evobox::tr(k)

} // namespace evobox
