#include "app/ProjectMerge.h"
#include "app/ProjectIO.h"
#include "media/ClipEncoder.h"
#include "model/ModelCommon.h"
#include "util/Localize.h"
#include "util/Paths.h"
#include "util/Strings.h"
#include "util/ZipFile.h"
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <unordered_map>
#include <utility>
#include <vector>

namespace fs = std::filesystem;
using nlohmann::json;

namespace evobox
{

namespace
{

struct Fail
{
    std::string* err;
    bool operator()(const std::string& m) const { if (err) *err = m; return false; }
};

const json& itemsOf(const json& project, const char* key)
{
    static const json empty = json::array();
    if (project.is_object() && project.contains(key) && project[key].is_object() && project[key].contains("items") &&
        project[key]["items"].is_array())
        return project[key]["items"];
    return empty;
}

// item["params"][name] as a string / int (Container::save keys params by short name)
std::string paramString(const json& item, const char* name, const std::string& def)
{
    if (item.contains("params") && item["params"].is_object() && item["params"].contains(name) && item["params"][name].is_string())
        return item["params"][name].get<std::string>();
    return def;
}

int paramInt(const json& item, const char* name, int def)
{
    if (item.contains("params") && item["params"].is_object() && item["params"].contains(name) && item["params"][name].is_number())
        return item["params"][name].get<int>();
    return def;
}

// every command row of item["actions"]["phases"][*]["commands"]["items"]
template <class Fn>
void forEachCommand(json& item, Fn fn)
{
    if (!item.contains("actions") || !item["actions"].is_object()) return;
    json& a = item["actions"];
    if (!a.contains("phases") || !a["phases"].is_array()) return;
    for (json& ph : a["phases"])
    {
        if (!ph.is_object() || !ph.contains("commands") || !ph["commands"].is_object()) continue;
        json& cm = ph["commands"];
        if (!cm.contains("items") || !cm["items"].is_array()) continue;
        for (json& cmd : cm["items"]) fn(cmd);
    }
}

int countCommands(const json& item)
{
    int n = 0;
    forEachCommand(const_cast<json&>(item), [&](json&) { n++; });
    return n;
}

// Rewrites the target of every command: mapped targets get their uid here, unknown ones fall back
// to the default target (0). Returns the number of commands.
int remapCommandTargets(json& item, const std::unordered_map<Uid, Uid>& targetMap)
{
    int n = 0;
    forEachCommand(item, [&](json& cmd)
    {
        Uid t = jget<Uid>(cmd, "targetUid", 0);
        auto it = targetMap.find(t);
        cmd["targetUid"] = (t != 0 && it != targetMap.end()) ? it->second : (Uid)0;
        n++;
    });
    return n;
}

// Appends the commands of `add`'s phases to the matching phases (same anchor, then same name) of
// `into` (both in OscActions JSON form). Appended rows get fresh uids when loaded ("uid": 0).
int appendCommands(json& into, const json& add)
{
    if (!into.contains("actions") || !into["actions"].is_object() || !into["actions"].contains("phases")) return 0;
    if (!add.contains("actions") || !add["actions"].is_object() || !add["actions"].contains("phases")) return 0;
    json& phases = into["actions"]["phases"];
    const json& addPhases = add["actions"]["phases"];
    if (!phases.is_array() || !addPhases.is_array()) return 0;
    int n = 0;
    std::vector<bool> used(phases.size(), false);
    for (const json& ap : addPhases)
    {
        if (!ap.is_object() || !ap.contains("commands") || !ap["commands"].is_object() || !ap["commands"].contains("items")) continue;
        std::string anchor = jget<std::string>(ap, "anchor", "");
        std::string name = jget<std::string>(ap, "name", "");
        json* target = nullptr;
        for (int pass = 0; pass < 2 && !target; pass++)
            for (size_t k = 0; k < phases.size() && !target; k++)
                if (!used[k] && jget<std::string>(phases[k], "anchor", "") == anchor &&
                    (pass == 1 || jget<std::string>(phases[k], "name", "") == name))
                { target = &phases[k]; used[k] = true; }
        if (!target) continue;
        json& cm = (*target)["commands"];
        if (!cm.is_object()) cm = json{ { "items", json::array() } };
        if (!cm.contains("items") || !cm["items"].is_array()) cm["items"] = json::array();
        for (const json& cmd : ap["commands"]["items"])
        {
            json c = cmd;
            c["uid"] = 0; // BaseManager::load assigns the next free uid
            c.erase("_index");
            cm["items"].push_back(c);
            n++;
        }
    }
    return n;
}

bool readSourceFile(const MergeSource& src, const std::string& rel, std::string& bytes, std::string* err)
{
    if (src.archive) return zipfile::readEntry(src.path, rel, bytes, err);
    std::ifstream f(fs::path(src.dir) / rel, std::ios::binary);
    if (!f.is_open()) { if (err) *err = str::format(LTR("error.cannotRead", "cannot read %s"), (fs::path(src.dir) / rel).string().c_str()); return false; }
    bytes.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}

bool writeFile(const fs::path& dst, const std::string& bytes, std::string* err)
{
    std::error_code ec;
    fs::create_directories(dst.parent_path(), ec);
    std::string tmp = dst.string() + ".merge.tmp";
    {
        std::ofstream f(tmp, std::ios::binary);
        if (!f.is_open()) { if (err) *err = str::format(LTR("error.cannotWrite", "cannot write %s"), dst.string().c_str()); return false; }
        f.write(bytes.data(), (std::streamsize)bytes.size());
        if (!f.good()) { fs::remove(tmp, ec); if (err) *err = str::format(LTR("error.writeError", "write error on %s"), dst.string().c_str()); return false; }
    }
    return paths::replaceFile(tmp, dst, err);
}

std::string clipName(Uid uid, const std::string& ext)
{
    char buf[64];
    snprintf(buf, sizeof(buf), "clips/%06llu%s", (unsigned long long)uid, ext.c_str());
    return buf;
}

// Copies the picture behind an "img:icons/<hash>.png" sticker / icon into this project's icons
// folder (content-addressed names never collide; an existing file is kept). Missing pictures are
// not an error: the tile falls back to a glyph.
void copyPicture(const MergeSource& src, const Project& p, const std::string& sticker, MergeReport& rep)
{
    if (!ProjectIO::isImageSticker(sticker)) return;
    std::string rel = sticker.substr(strlen(ProjectIO::kImageStickerPrefix)); // "icons/<hash>.png"
    fs::path dst = fs::path(ProjectIO::iconsDir(p)) / fs::path(rel).filename();
    std::error_code ec;
    if (fs::exists(dst, ec)) return;
    std::string bytes;
    if (!readSourceFile(src, rel, bytes, nullptr) || bytes.empty()) return;
    if (writeFile(dst, bytes, nullptr)) rep.pictures++;
}

// construct -> load -> add: the item is complete (uid, clipFile, ...) when the managers notify,
// so the app's lazy clip loader sees the real clip name; addItem keeps a preset uid
void addLoaded(organic::BaseManager& m, const json& j)
{
    auto item = m.createFromType(j.value("type", ""));
    if (!item) { OLOGE("Project", "Merge: unknown item type '" << j.value("type", "") << "'"); return; }
    item->load(j);
    m.addItem(std::move(item), -1);
}

// `oneFmt` / `manyFmt` carry the count ("%d sound" / "%d sounds"), so a language can put the
// number where its grammar wants it ("звуки: %d")
void plural(std::string& out, int n, const char* oneFmt, const char* manyFmt)
{
    if (n <= 0) return;
    if (!out.empty()) out += ", ";
    out += str::format(n == 1 ? oneFmt : manyFmt, n);
}

} // namespace

std::string MergeReport::summary() const
{
    // "3 sounds, 1 new category; 2 clip(s) not in the show ..." — each noun pair follows the UI
    // language through util/Localize.h (English in the tests, which assert on this text)
    std::string s;
    plural(s, sounds, LTR("merge.summary.sound", "%d sound"), LTR("merge.summary.sounds", "%d sounds"));
    plural(s, categories, LTR("merge.summary.category", "%d new category"), LTR("merge.summary.categories", "%d new categories"));
    plural(s, commands, LTR("merge.summary.command", "%d OSC command"), LTR("merge.summary.commands", "%d OSC commands"));
    plural(s, targets, LTR("merge.summary.target", "%d new target"), LTR("merge.summary.targets", "%d new targets"));
    plural(s, giftActions, LTR("merge.summary.giftAction", "%d gift action"), LTR("merge.summary.giftActions", "%d gift actions"));
    plural(s, roomEvents, LTR("merge.summary.roomEvent", "%d room event"), LTR("merge.summary.roomEvents", "%d room events"));
    if (giftsSkipped) s += (s.empty() ? "" : "; ") + str::format(LTR("merge.summary.giftsSkipped", "%d gift action(s) skipped (already configured)"), giftsSkipped);
    if (clipsMissing) s += (s.empty() ? "" : "; ") + str::format(LTR("merge.summary.clipsMissing", "%d clip(s) not in the show (re-rendered from the source when available)"), clipsMissing);
    return s.empty() ? LTR("merge.summary.nothing", "nothing to merge") : s;
}

bool ProjectMerge::inspect(const std::string& path, MergeSource& out, std::string* err)
{
    Fail fail{ err };
    std::error_code ec;
    out = MergeSource{};
    out.path = path;
    if (ProjectIO::isArchivePath(path) || (fs::is_regular_file(path, ec) && zipfile::looksLikeZip(path)))
    {
        if (!fs::is_regular_file(path, ec)) return fail(str::format(LTR("error.cannotOpen", "cannot open %s"), path.c_str()));
        if (!zipfile::looksLikeZip(path)) return fail(str::format(LTR("error.notLivFile", "%s is not a .liv show file (not a zip archive)"), path.c_str()));
        std::string text;
        if (!zipfile::readEntry(path, ProjectIO::kProjectFile, text, err)) return false;
        try { out.project = json::parse(text); }
        catch (const std::exception& ex) { return fail(str::format(LTR("error.cannotParse", "cannot parse %s: %s"), path.c_str(), ex.what())); }
        out.archive = true;
        out.title = fs::path(path).stem().string();
    }
    else
    {
        std::string dir = ProjectIO::workDirFor(path);
        if (!ProjectIO::isBundle(dir))
        {
            std::string alt = ProjectIO::bundleDirFromChoice(path);
            if (ProjectIO::isBundle(alt)) dir = alt;
            else return fail(str::format(LTR("error.notShowOrFolder", "'%s' is not an EvoMusicBox show (.liv) or project folder"), path.c_str()));
        }
        if (!ProjectIO::readJson((fs::path(dir) / ProjectIO::kProjectFile).string(), out.project, err)) return false;
        out.dir = dir;
        out.title = fs::path(dir).stem().string();
    }
    if (!out.project.is_object() || out.project.value("app", "") != "EvoMusicBox")
    {
        out.project = json();
        return fail(str::format(LTR("error.notShow", "%s is not an EvoMusicBox show"), path.c_str()));
    }
    out.sounds = (int)itemsOf(out.project, "sounds").size();
    out.categories = (int)itemsOf(out.project, "categories").size();
    for (const json& t : itemsOf(out.project, "oscTargets")) if (!jget<bool>(t, "builtin", false)) out.targets++;
    for (const json& s : itemsOf(out.project, "sounds")) out.commands += countCommands(s);
    out.giftActions = (int)itemsOf(out.project, "giftActions").size();
    for (const json& r : itemsOf(out.project, "roomEvents")) out.roomEventCommands += countCommands(r);
    return true;
}

bool ProjectMerge::merge(Project& p, const std::string& path, const MergeOptions& opts, MergeReport* report, std::string* err)
{
    MergeSource src;
    if (!inspect(path, src, err)) return false;
    return merge(p, src, opts, report, err);
}

bool ProjectMerge::merge(Project& p, const MergeSource& src, const MergeOptions& opts, MergeReport* report, std::string* err)
{
    Fail fail{ err };
    if (!src.valid()) return fail(LTR("merge.summary.nothing", "nothing to merge"));
    MergeReport rep;
    std::unordered_map<Uid, Uid> catMap, targetMap, soundMap;
    std::vector<json> newCategories, newTargets, newSounds, newGifts;
    std::vector<std::pair<Uid, json>> roomBefore, roomAfter;

    // categories: the same name is the same category (both shows start with the builtin set)
    for (const json& cj : itemsOf(src.project, "categories"))
    {
        Uid oldUid = jget<Uid>(cj, "uid", 0);
        std::string name = jget<std::string>(cj, "niceName", "");
        if (Category* existing = p.categories.findByName(name)) { catMap[oldUid] = existing->uid; continue; }
        json j = cj;
        j.erase("_index");
        j["uid"] = p.categories.nextUid++;
        j["builtin"] = false;
        catMap[oldUid] = j["uid"].get<Uid>();
        copyPicture(src, p, paramString(j, "icon", ""), rep);
        newCategories.push_back(j);
        rep.categories++;
    }

    // OSC targets: the other show's Localhost is ours; same host:port (then same name) is the same target
    if (opts.osc)
        for (const json& tj : itemsOf(src.project, "oscTargets"))
        {
            Uid oldUid = jget<Uid>(tj, "uid", 0);
            std::string host = paramString(tj, "host", "127.0.0.1");
            int port = paramInt(tj, "port", OscTarget::kDefaultPort);
            std::string name = jget<std::string>(tj, "niceName", "");
            OscTarget* match = jget<bool>(tj, "builtin", false) ? p.oscTargets.localhost() : nullptr;
            if (!match)
                for (OscTarget* t : p.oscTargets.targets())
                    if (t->host() == host && t->port() == port) { match = t; break; }
            if (!match && !name.empty())
                for (OscTarget* t : p.oscTargets.targets())
                    if (t->niceName == name) { match = t; break; }
            if (match) { targetMap[oldUid] = match->uid; continue; }
            json j = tj;
            j.erase("_index");
            j["uid"] = p.oscTargets.nextUid++;
            j["builtin"] = false;
            j["params"]["default"] = false; // this project keeps its default target
            targetMap[oldUid] = j["uid"].get<Uid>();
            newTargets.push_back(j);
            rep.targets++;
        }

    // sounds: fresh uids, clips copied under the new names (a legacy .wav stays .wav and is
    // converted like any legacy clip), pictures copied, categories / command targets remapped
    for (const json& sj : itemsOf(src.project, "sounds"))
    {
        Uid oldUid = jget<Uid>(sj, "uid", 0);
        json j = sj;
        j.erase("_index");
        Uid newUid = p.sounds.nextUid++;
        j["uid"] = newUid;
        soundMap[oldUid] = newUid;
        auto cit = catMap.find(jget<Uid>(sj, "categoryUid", 0));
        Category* custom = p.categories.customCategory();
        j["categoryUid"] = cit != catMap.end() ? cit->second : (custom ? custom->uid : (Uid)0);
        std::string oldClip = jget<std::string>(sj, "clipFile", "");
        std::string ext = oldClip.empty() ? "" : str::fileExtLower(oldClip);
        if (ext.empty()) ext = ClipEncoder::clipExtension();
        std::string newClip = clipName(newUid, ext);
        j["clipFile"] = newClip;
        std::string bytes;
        if (!oldClip.empty() && readSourceFile(src, oldClip, bytes, nullptr) && !bytes.empty())
        {
            if (!writeFile(ProjectIO::clipPathFor(p, newClip), bytes, err)) return false;
            rep.clipsCopied++;
        }
        else rep.clipsMissing++;
        copyPicture(src, p, paramString(j, "sticker", ""), rep);
        if (opts.osc) rep.commands += remapCommandTargets(j, targetMap);
        else j.erase("actions"); // sounds only: the default (empty) phases
        newSounds.push_back(j);
        rep.sounds++;
    }

    if (opts.osc)
    {
        // gift actions: one per gift — an already configured gift keeps this project's action
        for (const json& gj : itemsOf(src.project, "giftActions"))
        {
            int64_t giftId = jget<int64_t>(gj, "giftId", 0);
            if (giftId != 0 && p.giftActions.find(giftId)) { rep.giftsSkipped++; continue; }
            json j = gj;
            j.erase("_index");
            j["uid"] = p.giftActions.nextUid++;
            auto sit = soundMap.find(jget<Uid>(gj, "soundUid", 0));
            j["soundUid"] = sit != soundMap.end() ? sit->second : (Uid)0;
            remapCommandTargets(j, targetMap);
            newGifts.push_back(j);
            rep.giftActions++;
        }
        // room events are fixed (one per kind): append the other show's commands, adopt its sound
        // when this project has none
        for (const json& rj : itemsOf(src.project, "roomEvents"))
        {
            RoomEventKind kind;
            if (!roomEventKindFromKey(jget<std::string>(rj, "kind", ""), kind)) continue;
            RoomEventAction* r = p.roomEvents.find(kind);
            if (!r) continue;
            json add = rj;
            remapCommandTargets(add, targetMap);
            auto sit = soundMap.find(jget<Uid>(rj, "soundUid", 0));
            Uid newSound = sit != soundMap.end() ? sit->second : (Uid)0;
            bool adoptSound = newSound != 0 && r->soundUid == 0;
            if (countCommands(add) == 0 && !adoptSound) continue;
            json before = r->save();
            json after = before;
            int appended = appendCommands(after, add);
            if (adoptSound) after["soundUid"] = newSound;
            if (appended == 0 && !adoptSound) continue;
            rep.commands += appended;
            roomBefore.emplace_back(r->uid, before);
            roomAfter.emplace_back(r->uid, after);
            rep.roomEvents++;
        }
    }

    if (newCategories.empty() && newTargets.empty() && newSounds.empty() && newGifts.empty() && roomAfter.empty())
    {
        if (report) *report = rep;
        return true;
    }

    Project* self = &p;
    auto applyRooms = [self](const std::vector<std::pair<Uid, json>>& states)
    {
        for (const auto& [uid, state] : states)
            if (auto* r = static_cast<RoomEventAction*>(self->roomEvents.findItem(uid)))
            {
                r->load(state);
                notifyStructureChanged(r);
            }
    };
    auto doFn = [self, newCategories, newTargets, newSounds, newGifts, roomAfter, applyRooms]
    {
        for (const json& j : newCategories) addLoaded(self->categories, j);
        for (const json& j : newTargets) addLoaded(self->oscTargets, j);
        for (const json& j : newSounds) addLoaded(self->sounds, j);
        for (const json& j : newGifts) addLoaded(self->giftActions, j);
        applyRooms(roomAfter);
        self->touch();
    };
    auto undoFn = [self, newCategories, newTargets, newSounds, newGifts, roomBefore, applyRooms]
    {
        applyRooms(roomBefore);
        for (auto it = newGifts.rbegin(); it != newGifts.rend(); ++it) self->giftActions.removeItem(jget<Uid>(*it, "uid", 0));
        for (auto it = newSounds.rbegin(); it != newSounds.rend(); ++it) self->sounds.removeItem(jget<Uid>(*it, "uid", 0));
        for (auto it = newTargets.rbegin(); it != newTargets.rend(); ++it) self->oscTargets.removeItem(jget<Uid>(*it, "uid", 0));
        for (auto it = newCategories.rbegin(); it != newCategories.rend(); ++it) self->categories.removeItem(jget<Uid>(*it, "uid", 0));
        self->touch();
    };
    organic::UndoManager::get().perform(LTR("undo.mergeShow", "Merge show"), doFn, undoFn,
                                        { self, &self->sounds, &self->categories, &self->oscTargets, &self->giftActions, &self->roomEvents });
    OLOG("Project", "Merged '" << src.title << "': " << rep.summary());
    if (report) *report = rep;
    return true;
}

} // namespace evobox
