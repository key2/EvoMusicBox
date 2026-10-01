// project_merge_test — File > Merge show...: another show's sounds / categories / pictures (and
// optionally its OSC setup) are added to the current project as one undo step.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "app/ProjectIO.h"
#include "app/ProjectMerge.h"
#include <filesystem>
#include <fstream>

using namespace evobox;
namespace fs = std::filesystem;

namespace
{

fs::path tmpRoot()
{
    fs::path p = fs::path(EVOBOX_TEST_DIR) / "project_merge_tmp";
    std::error_code ec;
    fs::create_directories(p, ec);
    return p;
}

void writeText(const fs::path& p, const std::string& s)
{
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    std::ofstream f(p, std::ios::binary);
    f << s;
}

std::string readText(const fs::path& p)
{
    std::ifstream f(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}

// a sound with a fake rendered clip (content = its name) in the project's clips folder
Sound* addSound(Project& p, const char* name, Uid category, const char* ext = nullptr)
{
    auto s = std::make_unique<Sound>();
    s->setNiceName(name);
    s->categoryUid = category;
    s->source.path = std::string("/nonexistent/") + name + ".mp4";
    s->source.durationSec = 3.0;
    s->setTrim(0.0, 3.0, false);
    Sound* raw = p.sounds.addSoundUndoable(std::move(s));
    raw->clipFile = ProjectIO::clipFileName(*raw);
    if (ext) raw->clipFile = fs::path(raw->clipFile).replace_extension(ext).generic_string();
    writeText(fs::path(ProjectIO::clipsDir(p)) / fs::path(raw->clipFile).filename(), std::string("CLIP:") + name);
    return raw;
}

Sound* soundNamed(const Project& p, const std::string& name)
{
    for (Sound* s : p.sounds.sounds()) if (s->niceName == name) return s;
    return nullptr;
}

OscTarget* targetNamed(const Project& p, const std::string& name)
{
    for (OscTarget* t : p.oscTargets.targets()) if (t->niceName == name) return t;
    return nullptr;
}

int commandCount(const OscActions& a, Anchor anchor)
{
    const OscPhase* ph = const_cast<OscActions&>(a).phase(anchor);
    return ph ? (int)ph->commands.items.size() : 0;
}

// uid counters only ever grow (also across an undo): compare item JSON without them
json withoutCounters(json j)
{
    if (j.is_object())
    {
        j.erase("nextUid");
        for (auto& [k, v] : j.items()) v = withoutCounters(v);
    }
    else if (j.is_array())
        for (auto& v : j) v = withoutCounters(v);
    return j;
}

// the unsaved projects of one process share the scratch folder: give the target its own bundle
void ownBundle(Project& b, const fs::path& dir)
{
    std::string err;
    REQUIRE_MESSAGE(ProjectIO::save(b, dir.string(), &err), err);
}

// Show A: two custom things of everything, saved as a .liv (or a folder bundle when `folder`).
std::string buildShowA(Project& a, const fs::path& root, bool folder)
{
    std::string err;
    Category* stingers = a.categories.addCategoryUndoable("Stingers", "ph:lightning", ImVec4(1, 0, 0, 1));
    Category* music = a.categories.findByName("Music");
    REQUIRE(music);
    OscTarget* lighting = a.oscTargets.addTargetUndoable("Lighting PC", "192.168.0.10", 9000);
    OscTarget* video = a.oscTargets.addTargetUndoable("Video server", "10.0.0.5", 7000);
    Sound* kick = addSound(a, "Kick", stingers->uid);
    kick->actions().phase(Anchor::Start)->commands.addCommandUndoable("/light/flash 1", lighting->uid);
    kick->actions().phase(Anchor::End)->commands.addCommandUndoable("/video/stop", video->uid);
    kick->actions().phase(Anchor::End)->commands.addCommandUndoable("/default/target", 0);
    Sound* anthem = addSound(a, "Anthem", music->uid);
    writeText(fs::path(ProjectIO::iconsDir(a)) / "aaaa0000bbbb1111.png", "PNG-fake-anthem");
    anthem->stickerP->setValue(std::string("img:icons/aaaa0000bbbb1111.png"), false);
    anthem->isEffectP->setValue(true, false);
    addSound(a, "Legacy", stingers->uid, ".wav"); // a float32 clip of an older show
    GiftAction transient;
    transient.seedFromCatalog(5487, "Galaxy", 1000, "", 1);
    GiftAction* gift = a.giftActions.addFromTransientUndoable(transient);
    gift->setSoundUidUndoable(kick->uid);
    gift->actions().phase(Anchor::Start)->commands.addCommandUndoable("/gift/galaxy", lighting->uid);
    GiftAction transient2;
    transient2.seedFromCatalog(1001, "Rose", 1, "", 1);
    a.giftActions.addFromTransientUndoable(transient2)->setSoundUidUndoable(anthem->uid);
    RoomEventAction* like = a.roomEvents.find(RoomEventKind::Like);
    REQUIRE(like);
    like->actions().phase(Anchor::Start)->commands.addCommandUndoable("/room/like", lighting->uid);
    like->setSoundUidUndoable(anthem->uid);
    if (folder)
    {
        std::string dir = (root / "A.evobox").string();
        REQUIRE_MESSAGE(ProjectIO::save(a, dir, &err), err);
        return dir;
    }
    std::string liv = (root / "A.liv").string();
    REQUIRE_MESSAGE(ProjectIO::saveArchive(a, liv, &err), err);
    return liv;
}

} // namespace

TEST_CASE("merge a .liv with its OSC: remapped uids, copied files, reused categories / targets, one undo step")
{
    fs::path root = tmpRoot() / "liv";
    std::error_code ec;
    fs::remove_all(root, ec);
    // both projects exist before the merge: constructing a Project clears the undo history
    Project a, b;
    std::string liv = buildShowA(a, root, false);

    ownBundle(b, root / "B.evobox");
    Sound* existing = addSound(b, "Existing", b.categories.customCategory()->uid);
    OscTarget* bLights = b.oscTargets.addTargetUndoable("Lights", "192.168.0.10", 9000); // same endpoint as A's "Lighting PC"
    GiftAction transient;
    transient.seedFromCatalog(5487, "Galaxy", 1000, "", 1);
    GiftAction* bGalaxy = b.giftActions.addFromTransientUndoable(transient); // already configured here
    size_t categoriesBefore = b.categories.items.size();
    size_t targetsBefore = b.oscTargets.items.size();
    json likeBefore = b.roomEvents.find(RoomEventKind::Like)->save();
    b.markSaved();

    std::string err;
    MergeSource src;
    REQUIRE_MESSAGE(ProjectMerge::inspect(liv, src, &err), err);
    CHECK(src.archive);
    CHECK(src.title == "A");
    CHECK(src.sounds == 3);
    CHECK(src.categories == (int)a.categories.items.size());
    CHECK(src.targets == 2);            // without the builtin Localhost
    CHECK(src.commands == 3);           // the sounds' commands
    CHECK(src.giftActions == 2);
    CHECK(src.roomEventCommands == 1);

    MergeReport rep;
    MergeOptions opts;
    opts.osc = true;
    REQUIRE_MESSAGE(ProjectMerge::merge(b, src, opts, &rep, &err), err);
    CHECK(rep.sounds == 3);
    CHECK(rep.categories == 1);         // "Stingers"; the builtin names already exist
    CHECK(rep.targets == 1);            // "Video server"; "Lighting PC" == "Lights" (host:port)
    CHECK(rep.commands == 3 + 1);       // sounds + the room event's
    CHECK(rep.giftActions == 1);        // Rose
    CHECK(rep.giftsSkipped == 1);       // Galaxy is configured here
    CHECK(rep.roomEvents == 1);
    CHECK(rep.clipsCopied == 3);
    CHECK(rep.pictures == 1);
    CHECK(b.dirty());

    // sounds: fresh uids, clips under the new names with the old content, legacy stays .wav
    REQUIRE(b.sounds.items.size() == 4);
    Sound* kick = soundNamed(b, "Kick");
    Sound* anthem = soundNamed(b, "Anthem");
    Sound* legacy = soundNamed(b, "Legacy");
    REQUIRE(kick); REQUIRE(anthem); REQUIRE(legacy);
    CHECK(kick->uid != existing->uid);
    CHECK(kick->clipFile == ProjectIO::clipFileName(*kick));
    CHECK(readText(ProjectIO::clipPath(b, *kick)) == "CLIP:Kick");
    CHECK(readText(ProjectIO::clipPath(b, *anthem)) == "CLIP:Anthem");
    CHECK(fs::path(legacy->clipFile).extension() == ".wav");
    CHECK(fs::path(legacy->clipFile).stem() == fs::path(ProjectIO::clipFileName(*legacy)).stem());
    CHECK(readText(ProjectIO::clipPath(b, *legacy)) == "CLIP:Legacy");
    CHECK(anthem->isEffect());
    // categories: Stingers created (not builtin), Music reused
    Category* stingers = b.categories.findByName("Stingers");
    REQUIRE(stingers);
    CHECK_FALSE(stingers->builtin);
    CHECK(kick->categoryUid == stingers->uid);
    CHECK(legacy->categoryUid == stingers->uid);
    CHECK(anthem->categoryUid == b.categories.findByName("Music")->uid);
    CHECK(b.categories.items.size() == categoriesBefore + 1);
    // picture copied, sticker string unchanged
    CHECK(anthem->sticker() == "img:icons/aaaa0000bbbb1111.png");
    CHECK(readText(ProjectIO::stickerImagePath(b, anthem->sticker())) == "PNG-fake-anthem");
    // targets: one new, the other mapped onto the same endpoint; this project's default kept
    CHECK(b.oscTargets.items.size() == targetsBefore + 1);
    OscTarget* video = targetNamed(b, "Video server");
    REQUIRE(video);
    CHECK_FALSE(video->isDefault());
    CHECK(b.oscTargets.defaultTarget() == b.oscTargets.localhost());
    // commands remapped
    REQUIRE(commandCount(kick->actions(), Anchor::Start) == 1);
    CHECK(kick->actions().phase(Anchor::Start)->commands.commands()[0]->targetUid == bLights->uid);
    REQUIRE(commandCount(kick->actions(), Anchor::End) == 2);
    CHECK(kick->actions().phase(Anchor::End)->commands.commands()[0]->targetUid == video->uid);
    CHECK(kick->actions().phase(Anchor::End)->commands.commands()[1]->targetUid == 0);
    // gifts: Rose added with its sound remapped, Galaxy kept as configured here
    GiftAction* rose = b.giftActions.find(1001);
    REQUIRE(rose);
    CHECK(rose->soundUid == anthem->uid);
    CHECK(b.giftActions.find(5487) == bGalaxy);
    CHECK(bGalaxy->soundUid == 0);
    // room event: command appended (remapped), sound adopted
    RoomEventAction* like = b.roomEvents.find(RoomEventKind::Like);
    REQUIRE(commandCount(like->actions(), Anchor::Start) == 1);
    CHECK(like->actions().phase(Anchor::Start)->commands.commands()[0]->targetUid == bLights->uid);
    CHECK(like->soundUid == anthem->uid);
    CHECK(b.roomEvents.items.size() == (size_t)RoomEventKind::Count);

    // one undo step restores everything
    CHECK(organic::UndoManager::get().undoName() == "Merge show");
    organic::UndoManager::get().undo();
    CHECK(b.sounds.items.size() == 1);
    CHECK(b.categories.items.size() == categoriesBefore);
    CHECK(b.oscTargets.items.size() == targetsBefore);
    CHECK(b.giftActions.find(1001) == nullptr);
    CHECK(withoutCounters(b.roomEvents.find(RoomEventKind::Like)->save()) == withoutCounters(likeBefore));
    CHECK(commandCount(b.roomEvents.find(RoomEventKind::Like)->actions(), Anchor::Start) == 0);
    // redo brings it back with the same uids (files were left in place)
    organic::UndoManager::get().redo();
    Sound* kick2 = soundNamed(b, "Kick");
    REQUIRE(kick2);
    CHECK(kick2->uid == kick->uid);
    CHECK(fs::exists(ProjectIO::clipPath(b, *kick2)));
    CHECK(b.roomEvents.find(RoomEventKind::Like)->soundUid == soundNamed(b, "Anthem")->uid);
    CHECK(b.giftActions.find(1001) != nullptr);
    // merging the same show again adds the sounds again but no second Stingers / Video server
    REQUIRE_MESSAGE(ProjectMerge::merge(b, src, opts, &rep, &err), err);
    CHECK(rep.categories == 0);
    CHECK(rep.targets == 0);
    CHECK(rep.giftsSkipped == 2);
    CHECK(b.sounds.items.size() == 7);
    CHECK(b.categories.items.size() == categoriesBefore + 1);
}

TEST_CASE("merge sounds only from a folder bundle: no OSC touched")
{
    fs::path root = tmpRoot() / "folder";
    std::error_code ec;
    fs::remove_all(root, ec);
    Project a, b;
    std::string dir = buildShowA(a, root, true);
    ownBundle(b, root / "B.evobox");
    size_t targetsBefore = b.oscTargets.items.size();
    json likeBefore = b.roomEvents.find(RoomEventKind::Like)->save();

    std::string err;
    MergeSource src;
    REQUIRE_MESSAGE(ProjectMerge::inspect((fs::path(dir) / ProjectIO::kProjectFile).string(), src, &err), err);
    CHECK_FALSE(src.archive);
    CHECK(src.title == "A");
    CHECK(src.sounds == 3);
    MergeReport rep;
    MergeOptions opts;
    opts.osc = false;
    REQUIRE_MESSAGE(ProjectMerge::merge(b, src, opts, &rep, &err), err);
    CHECK(rep.sounds == 3);
    CHECK(rep.categories == 1);
    CHECK(rep.commands == 0);
    CHECK(rep.targets == 0);
    CHECK(rep.giftActions == 0);
    CHECK(rep.roomEvents == 0);
    Sound* kick = soundNamed(b, "Kick");
    REQUIRE(kick);
    CHECK(kick->actions().commandCount() == 0);
    CHECK(readText(ProjectIO::clipPath(b, *kick)) == "CLIP:Kick");
    CHECK(b.oscTargets.items.size() == targetsBefore);
    CHECK(b.giftActions.items.empty());
    CHECK(withoutCounters(b.roomEvents.find(RoomEventKind::Like)->save()) == withoutCounters(likeBefore));
    CHECK(readText(ProjectIO::stickerImagePath(b, soundNamed(b, "Anthem")->sticker())) == "PNG-fake-anthem");
    CHECK(rep.pictures == 1);
    CHECK(rep.summary().find("3 sounds") != std::string::npos);
}

TEST_CASE("merge: bad inputs and an empty show")
{
    fs::path root = tmpRoot() / "bad";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root, ec);
    Project b;
    std::string err;
    MergeSource src;
    CHECK_FALSE(ProjectMerge::inspect((root / "missing.liv").string(), src, &err));
    CHECK_FALSE(err.empty());
    writeText(root / "notes.txt", "hello");
    CHECK_FALSE(ProjectMerge::inspect((root / "notes.txt").string(), src, &err));
    writeText(root / "other.json", "{\"app\":\"Other\"}");
    CHECK_FALSE(ProjectMerge::inspect((root / "other.json").string(), src, &err));
    CHECK_FALSE(src.valid());
    // an empty show merges to nothing (and is not an undo step)
    Project empty;
    Project target;
    std::string liv = (root / "Empty.liv").string();
    REQUIRE_MESSAGE(ProjectIO::saveArchive(empty, liv, &err), err);
    target.markSaved();
    MergeReport rep;
    REQUIRE_MESSAGE(ProjectMerge::merge(target, liv, MergeOptions{}, &rep, &err), err);
    CHECK(rep.sounds == 0);
    CHECK(rep.summary() == "nothing to merge");
    CHECK_FALSE(target.dirty());
    CHECK_FALSE(organic::UndoManager::get().canUndo());
}
