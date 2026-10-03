#include "ui/I18n.h"
#include "OrganicCore.h"
#include "util/Paths.h"
#include "json.hpp"
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>

namespace fs = std::filesystem;
using nlohmann::json;

namespace evobox
{

struct I18n::Catalogue
{
    std::map<std::string, std::string> strings;
};

I18n& I18n::get()
{
    static I18n inst;
    return inst;
}

// The language files live in <resources>/lang/. findResource already searches exeDir, the macOS
// bundle's Contents/Resources, the source tree and the cwd, so reuse it: resolve lang/en.json and
// take its parent as the directory to scan.
static fs::path langDir()
{
    // Translations ship in assets/lang/ (deployed next to the exe, in the macOS bundle's
    // Contents/Resources, or in the source tree) — the same place as assets/stickers.json.
    std::string marker = paths::findResource("assets/lang/en.json");
    if (!marker.empty()) return fs::path(marker).parent_path();
    std::string dir = paths::findResource("assets/lang");
    if (!dir.empty()) return dir;
    return {};
}

static std::string nativeNameFor(const std::string& code, const std::map<std::string, std::string>& strings)
{
    auto it = strings.find("language.native");
    if (it != strings.end() && !it->second.empty()) return it->second;
    // Sensible defaults if the file omits the self-name.
    if (code == "en") return "English";
    if (code == "ru") return "\xD0\xA0\xD1\x83\xD1\x81\xD1\x81\xD0\xBA\xD0\xB8\xD0\xB9"; // Русский
    if (code == "zh") return "\xE4\xB8\xAD\xE6\x96\x87";                                 // 中文
    return code;
}

std::shared_ptr<I18n::Catalogue> I18n::loadFile(const std::string& code) const
{
    fs::path dir = langDir();
    if (dir.empty()) return nullptr;
    fs::path file = dir / (code + ".json");
    std::error_code ec;
    if (!fs::exists(file, ec)) return nullptr;
    std::ifstream f(file, std::ios::binary);
    if (!f.is_open()) return nullptr;
    json j;
    try { f >> j; }
    catch (const std::exception& e)
    {
        OLOGW("I18n", "cannot parse " << file.string() << ": " << e.what());
        return nullptr;
    }
    if (!j.is_object()) return nullptr;
    auto cat = std::make_shared<Catalogue>();
    for (auto it = j.begin(); it != j.end(); ++it)
        if (it.value().is_string()) cat->strings[it.key()] = it.value().get<std::string>();
    return cat;
}

void I18n::init()
{
    languages_.clear();
    englishCat_ = loadFile("en");
    if (!englishCat_)
    {
        OLOGW("I18n", "lang/en.json not found: UI falls back to raw keys");
        englishCat_ = std::make_shared<Catalogue>();
    }

    fs::path dir = langDir();
    std::error_code ec;
    if (!dir.empty() && fs::is_directory(dir, ec))
    {
        for (auto& e : fs::directory_iterator(dir, ec))
        {
            if (!e.is_regular_file()) continue;
            if (e.path().extension() != ".json") continue;
            std::string code = e.path().stem().string();
            auto cat = (code == "en") ? englishCat_ : loadFile(code);
            if (!cat) continue;
            languages_.push_back({ code, nativeNameFor(code, cat->strings) });
        }
    }
    // Keep English first, then the rest alphabetically by code, so the menu order is stable.
    std::sort(languages_.begin(), languages_.end(), [](const LanguageInfo& a, const LanguageInfo& b) {
        if (a.code == "en") return true;
        if (b.code == "en") return false;
        return a.code < b.code;
    });
    if (languages_.empty()) languages_.push_back({ "en", "English" });

    // Resolve the active catalogue (active_ may have been set from prefs before init()).
    setLanguage(active_);
    OLOG("I18n", "languages: " << languages_.size() << ", active '" << active_ << "'");
}

void I18n::setLanguage(const std::string& code)
{
    std::string want = code.empty() ? "en" : code;
    bool known = false;
    for (auto& l : languages_) if (l.code == want) { known = true; break; }
    if (!known && !languages_.empty()) want = languages_.front().code; // usually "en"

    active_ = want;
    if (want == "en") activeCat_ = englishCat_;
    else
    {
        activeCat_ = loadFile(want);
        if (!activeCat_) activeCat_ = englishCat_;
    }
}

const std::string& I18n::tr(const std::string& key) const
{
    if (activeCat_)
    {
        auto it = activeCat_->strings.find(key);
        if (it != activeCat_->strings.end()) return it->second;
    }
    if (englishCat_)
    {
        auto it = englishCat_->strings.find(key);
        if (it != englishCat_->strings.end()) return it->second;
    }
    return key; // last resort: show the key so a missing string is obvious, never crash
}

const char* tr(const std::string& key)
{
    return I18n::get().tr(key).c_str();
}

static const char* formatOne(const std::string& fmt, const char* arg)
{
    static thread_local std::string buf;
    // The translated format must contain exactly one %s. If it does not (bad translation), fall
    // back to "<fmt> <arg>" so nothing is lost and there is no format-string vulnerability.
    if (fmt.find("%s") == std::string::npos)
    {
        buf = fmt;
        buf += ' ';
        buf += (arg ? arg : "");
        return buf.c_str();
    }
    int need = std::snprintf(nullptr, 0, fmt.c_str(), arg ? arg : "");
    if (need < 0) { buf = fmt; return buf.c_str(); }
    buf.assign((size_t)need + 1, '\0');
    std::snprintf(&buf[0], buf.size(), fmt.c_str(), arg ? arg : "");
    buf.resize((size_t)need);
    return buf.c_str();
}

const char* trFmt(const std::string& key, const char* a) { return formatOne(I18n::get().tr(key), a); }
const char* trFmt(const std::string& key, const std::string& a) { return formatOne(I18n::get().tr(key), a.c_str()); }
const char* trFmt(const std::string& key, int a)
{
    static thread_local std::string buf;
    const std::string& fmt = I18n::get().tr(key);
    if (fmt.find("%d") == std::string::npos && fmt.find("%i") == std::string::npos)
    {
        buf = fmt;
        buf += ' ';
        buf += std::to_string(a);
        return buf.c_str();
    }
    int need = std::snprintf(nullptr, 0, fmt.c_str(), a);
    if (need < 0) { buf = fmt; return buf.c_str(); }
    buf.assign((size_t)need + 1, '\0');
    std::snprintf(&buf[0], buf.size(), fmt.c_str(), a);
    buf.resize((size_t)need);
    return buf.c_str();
}

} // namespace evobox
