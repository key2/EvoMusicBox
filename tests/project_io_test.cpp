// ProjectIO: folder bundle vs .liv show container (zip) round trips, icons/ handling, archive
// working folders, and the zip helper itself.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "app/ProjectIO.h"
#include "media/ClipEncoder.h"
#include "media/ImageWriter.h"
#include "util/ZipFile.h"
#include <chrono>
#include <filesystem>
#include <fstream>

using namespace evobox;
namespace fs = std::filesystem;

namespace
{

fs::path tmpRoot()
{
    fs::path p = fs::path(EVOBOX_TEST_DIR) / "project_io_tmp";
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
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

Sound* addSound(Project& p, const char* name)
{
    auto s = std::make_unique<Sound>();
    s->setNiceName(name);
    s->categoryUid = p.categories.customCategory()->uid;
    s->source.path = std::string("/nonexistent/") + name + ".mp4";
    s->source.durationSec = 3.0;
    s->setTrim(0.0, 3.0, false);
    Sound* raw = p.sounds.addSoundUndoable(std::move(s));
    raw->clipFile = ProjectIO::clipFileName(*raw);
    // a fake rendered clip in the scratch folder, where the renderer would have put it
    writeText(fs::path(ProjectIO::clipsDir(p)) / fs::path(raw->clipFile).filename(), std::string("RIFF-fake-") + name);
    return raw;
}

} // namespace

TEST_CASE("zip helper: pack a folder, list, read, extract; unsafe names are rejected")
{
    fs::path root = tmpRoot() / "zip";
    std::error_code ec;
    fs::remove_all(root, ec);
    writeText(root / "src" / "project.json", "{\"a\":1}");
    writeText(root / "src" / "clips" / "000001.wav", std::string(5000, 'x'));
    writeText(root / "src" / "clips" / "000002.mp3", std::string(7000, 'm'));
    writeText(root / "src" / "icons" / "abc.png", "png");
    writeText(root / "src" / "media" / "movie.mp4", "not included");
    std::string zip = (root / "out.liv").string();
    std::string err;
    REQUIRE_MESSAGE(zipfile::writeDirectory(zip, (root / "src").string(), { "project.json" }, { "clips", "icons" }, { ".mp3", ".wav" }, &err), err);
    CHECK(zipfile::looksLikeZip(zip));
    // both clip formats are stored, not deflated (their bytes sit verbatim in the archive)
    CHECK(fs::file_size(zip) > 12000);
    std::vector<zipfile::Entry> entries;
    REQUIRE(zipfile::list(zip, entries, &err));
    std::vector<std::string> names;
    for (auto& e : entries) names.push_back(e.name);
    std::sort(names.begin(), names.end());
    CHECK(names == std::vector<std::string>{ "clips/000001.wav", "clips/000002.mp3", "icons/abc.png", "project.json" });
    std::string content;
    REQUIRE(zipfile::readEntry(zip, "project.json", content, &err));
    CHECK(content == "{\"a\":1}");
    CHECK_FALSE(zipfile::readEntry(zip, "media/movie.mp4", content, &err));
    REQUIRE(zipfile::extractTo(zip, (root / "dst").string(), &err));
    CHECK(readText(root / "dst" / "clips" / "000001.wav").size() == 5000);
    CHECK(readText(root / "dst" / "clips" / "000002.mp3").size() == 7000);
    CHECK(readText(root / "dst" / "icons" / "abc.png") == "png");
    CHECK_FALSE(fs::exists(root / "dst" / "media"));
    CHECK_FALSE(fs::exists(zip + ".tmp"));
    CHECK_FALSE(zipfile::looksLikeZip((root / "src" / "project.json").string()));
}

TEST_CASE("clip file names follow the clip format; save() adopts converted clips and garbage-collects safely")
{
    if (!ClipEncoder::mp3Available()) { MESSAGE("no MP3 encoder: legacy conversion cases skipped"); return; }
    fs::path root = tmpRoot() / "gc";
    std::error_code ec;
    fs::remove_all(root, ec);
    Project p;
    Sound* a = addSound(p, "New");
    Sound* legacy = addSound(p, "Old");
    // newly rendered clips use the current extension (.mp3 when an MP3 encoder is available)
    CHECK(fs::path(a->clipFile).extension().string() == ClipEncoder::clipExtension());
    CHECK(a->clipFile == ProjectIO::clipFileName(a->uid));
    CHECK(ProjectIO::clipPathFor(p, a->clipFile) == ProjectIO::clipPath(p, *a));
    CHECK(fs::path(ProjectIO::clipPathFor(p, "clips/000099.mp3")).filename() == "000099.mp3");
    // a sound from a legacy show references its .wav (same 6-digit uid stem, other extension)
    std::string legacyName = fs::path(ProjectIO::clipFileName(*legacy)).stem().string(); // "000002"
    legacy->clipFile = "clips/" + legacyName + ".wav";
    fs::path scratch = ProjectIO::clipsDir(p);
    fs::remove(scratch / (legacyName + ".mp3"), ec); // addSound wrote a fake current-format clip
    writeText(scratch / (legacyName + ".wav"), "RIFF-legacy");
    // junk in the scratch clips folder: unreferenced clips of both formats + an interrupted encode
    writeText(scratch / "000500.mp3", "junk");
    writeText(scratch / "000501.wav", "junk");
    writeText(scratch / "000502.mp3.7.tmp", "junk");

    std::string bundle = (root / "Show.evobox").string();
    std::string err;
    REQUIRE_MESSAGE(ProjectIO::save(p, bundle, &err), err);
    fs::path clips = fs::path(bundle) / "clips";
    CHECK(fs::exists(clips / fs::path(a->clipFile).filename()));
    CHECK(legacy->clipFile == "clips/" + legacyName + ".wav");         // nothing to adopt yet
    CHECK(readText(clips / (legacyName + ".wav")) == "RIFF-legacy");   // referenced legacy clip survives
    CHECK_FALSE(fs::exists(clips / "000500.mp3"));                     // junk is never copied

    // GC inside the bundle: stale clips of either format and orphaned temp files go, other files
    // stay, and so does everything a live sound may be writing right now (its current-format name
    // and temp files of both of its names)
    writeText(clips / "000600.mp3", "stale");
    writeText(clips / "000601.wav", "stale");
    writeText(clips / "000602.mp3.3.tmp", "stale");
    writeText(clips / "readme.txt", "keep");
    writeText(clips / (legacyName + ".mp3.5.tmp"), "conversion in flight");
    writeText(clips / (fs::path(a->clipFile).filename().string() + ".9.tmp"), "render in flight");
    REQUIRE_MESSAGE(ProjectIO::save(p, bundle, &err), err);
    CHECK_FALSE(fs::exists(clips / "000600.mp3"));
    CHECK_FALSE(fs::exists(clips / "000601.wav"));
    CHECK_FALSE(fs::exists(clips / "000602.mp3.3.tmp"));
    CHECK(fs::exists(clips / "readme.txt"));
    CHECK(fs::exists(clips / (legacyName + ".wav")));
    CHECK(fs::exists(clips / (legacyName + ".mp3.5.tmp")));
    CHECK(fs::exists(clips / (fs::path(a->clipFile).filename().string() + ".9.tmp")));
    fs::remove(clips / (legacyName + ".mp3.5.tmp"), ec);
    fs::remove(clips / (fs::path(a->clipFile).filename().string() + ".9.tmp"), ec);

    // the conversion finished (a newer current-format file exists): save() adopts it, writes
    // project.json with the new name, then drops the .wav
    // explicit timestamps rather than sleeps: Windows stat() resolves seconds only
    auto stamp = [&](const fs::path& f, int secondsAgo)
    {
        std::error_code e;
        fs::last_write_time(f, fs::file_time_type::clock::now() - std::chrono::seconds(secondsAgo), e);
        REQUIRE_FALSE(e);
    };
    writeText(clips / (legacyName + ".mp3"), "converted");
    stamp(clips / (legacyName + ".wav"), 120); // the legacy clip predates its conversion
    stamp(clips / (legacyName + ".mp3"), 0);
    CHECK(ProjectIO::convertedClipFor(p, legacy->uid, legacy->clipFile) == "clips/" + legacyName + ".mp3");
    CHECK(ProjectIO::convertedClipFor(p, a->uid, a->clipFile).empty());    // already current
    REQUIRE_MESSAGE(ProjectIO::save(p, bundle, &err), err);
    CHECK(legacy->clipFile == "clips/" + legacyName + ".mp3");
    CHECK_FALSE(fs::exists(clips / (legacyName + ".wav")));
    CHECK(fs::exists(clips / (legacyName + ".mp3")));
    nlohmann::json saved;
    REQUIRE(ProjectIO::readJson((fs::path(bundle) / ProjectIO::kProjectFile).string(), saved, &err));
    bool jsonHasMp3 = false;
    for (auto& item : saved["sounds"]["items"]) if (item["uid"] == legacy->uid) jsonHasMp3 = item["clipFile"] == legacy->clipFile;
    CHECK(jsonHasMp3);

    // an OLDER current-format file next to a newer legacy clip is stale (written for an earlier
    // state by another build): the legacy clip stays referenced and the stale file is kept for
    // the conversion that will overwrite it
    Sound* other = addSound(p, "Stale");
    std::string otherName = fs::path(ProjectIO::clipFileName(*other)).stem().string();
    other->clipFile = "clips/" + otherName + ".wav";
    fs::remove(scratch / (otherName + ".mp3"), ec);
    writeText(clips / (otherName + ".mp3"), "old conversion");
    writeText(clips / (otherName + ".wav"), "RIFF-newer");
    stamp(clips / (otherName + ".mp3"), 120);
    stamp(clips / (otherName + ".wav"), 0);
    CHECK(ProjectIO::convertedClipFor(p, other->uid, other->clipFile).empty());
    REQUIRE_MESSAGE(ProjectIO::save(p, bundle, &err), err);
    CHECK(other->clipFile == "clips/" + otherName + ".wav");
    CHECK(fs::exists(clips / (otherName + ".wav")));
    CHECK(fs::exists(clips / (otherName + ".mp3")));

    // load(): a conversion done in an earlier, unsaved session is adopted before the model loads
    // (listeners resolving clips see the .mp3), without dirtying the project
    writeText(clips / (otherName + ".mp3"), "new conversion");
    stamp(clips / (otherName + ".wav"), 120);
    stamp(clips / (otherName + ".mp3"), 0);
    Project q;
    int notified = 0;
    q.onAnyChange = [&]
    {
        notified++;
        Sound* s = q.sounds.find(other->uid);
        if (s) CHECK(s->clipFile == "clips/" + otherName + ".mp3");
    };
    REQUIRE_MESSAGE(ProjectIO::open(q, bundle, &err), err);
    q.onAnyChange = nullptr;
    CHECK(notified >= 1);
    CHECK(q.sounds.find(other->uid)->clipFile == "clips/" + otherName + ".mp3");
    CHECK(q.sounds.find(legacy->uid)->clipFile == "clips/" + legacyName + ".mp3");
    CHECK_FALSE(q.dirty());
    CHECK(fs::exists(clips / (otherName + ".wav"))); // only save() deletes it
    REQUIRE_MESSAGE(ProjectIO::save(q, bundle, &err), err);
    CHECK_FALSE(fs::exists(clips / (otherName + ".wav")));
    // both clip formats are in the zip "store" list
    CHECK(ProjectIO::clipExtensions() == std::vector<std::string>{ ".mp3", ".wav" });
}

TEST_CASE("archive path helpers")
{
    CHECK(ProjectIO::isArchivePath("/x/Show.liv"));
    CHECK(ProjectIO::isArchivePath("Show.LIV"));
    CHECK_FALSE(ProjectIO::isArchivePath("/x/Show.evobox"));
    CHECK(ProjectIO::archivePathFromChoice("/x/Show") == "/x/Show.liv");
    CHECK(ProjectIO::archivePathFromChoice("/x/Show.liv") == "/x/Show.liv");
    CHECK(ProjectIO::archivePathFromChoice("/x/Show.evobox") == "/x/Show.liv");
    std::string wd = ProjectIO::archiveWorkDir("/x/Show.liv");
    CHECK(wd == ProjectIO::archiveWorkDir("/x/Show.liv"));           // stable
    CHECK(wd != ProjectIO::archiveWorkDir("/y/Show.liv"));           // per path
    CHECK(fs::path(wd).extension().string() == ProjectIO::kBundleExt);
    CHECK(ProjectIO::workDirFor("/x/Show.liv") == wd);
    CHECK(ProjectIO::workDirFor("/x/Show.evobox/project.json") == "/x/Show.evobox");
    CHECK(ProjectIO::isImageSticker("img:icons/abc.png"));
    CHECK_FALSE(ProjectIO::isImageSticker("ph:rocket"));
    CHECK(ProjectIO::imageStickerFor("abc.png") == "img:icons/abc.png");
}

TEST_CASE(".liv round trip keeps project.json, clips and picture stickers; media is left out")
{
    fs::path root = tmpRoot() / "liv";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root, ec);
    std::string livPath = (root / "MyShow.liv").string();
    fs::remove_all(ProjectIO::archiveWorkDir(livPath), ec);

    Project p;
    Sound* a = addSound(p, "Kick");
    Sound* b = addSound(p, "Movie");
    b->isEffectP->setValue(true, false);
    // a picture sticker in the scratch icons folder
    RgbaImage img;
    img.width = img.height = 8;
    img.pixels.assign(8 * 8 * 4, 200);
    std::string err;
    fs::path iconFile = fs::path(ProjectIO::iconsDir(p)) / "deadbeef00000000.png";
    REQUIRE_MESSAGE(ImageWriter::writePng(iconFile.string(), img, &err), err);
    b->stickerP->setValue(std::string("img:icons/deadbeef00000000.png"), false);
    a->actions().phase(Anchor::Start)->commands.addCommandUndoable("/light/flash 1", 0);
    OscTarget* pc = p.oscTargets.addTargetUndoable("Lighting PC", "192.168.0.10", 9000);
    GiftAction transient;
    transient.seedFromCatalog(5487, "Galaxy", 1000, "", 1);
    transient.actions().phase(Anchor::Start)->commands.addCommandUndoable("/fx/galaxy 1", pc->uid);
    GiftAction* g = p.giftActions.addFromTransientUndoable(transient);
    g->setSoundUidUndoable(b->uid);

    REQUIRE_MESSAGE(ProjectIO::saveArchive(p, (root / "MyShow").string(), &err), err); // bare name -> .liv
    CHECK(p.archivePath == livPath);
    CHECK(p.isArchive());
    CHECK(p.title() == "MyShow");
    CHECK(p.bundleDir == ProjectIO::archiveWorkDir(livPath));
    CHECK_FALSE(p.dirty());
    REQUIRE(fs::is_regular_file(livPath));
    std::vector<zipfile::Entry> entries;
    REQUIRE(zipfile::list(livPath, entries, &err));
    std::vector<std::string> names;
    for (auto& e : entries) names.push_back(e.name);
    std::sort(names.begin(), names.end());
    CHECK(names == std::vector<std::string>{ "clips/" + fs::path(a->clipFile).filename().string(),
                                             "clips/" + fs::path(b->clipFile).filename().string(),
                                             "icons/deadbeef00000000.png", "project.json" });

    // open it in a fresh project. Project::load notifies onAnyChange (the app lazily loads clips
    // from it): the working folder must already be in place at that moment, otherwise clips are
    // resolved against the previous project's folder and re-rendered / reported missing.
    Project q;
    q.bundleDir = "/previous/project.evobox";
    int notified = 0;
    q.onAnyChange = [&]
    {
        notified++;
        CHECK(q.bundleDir == ProjectIO::archiveWorkDir(livPath));
        for (Sound* s : q.sounds.sounds()) CHECK(fs::exists(ProjectIO::clipPath(q, *s)));
    };
    REQUIRE_MESSAGE(ProjectIO::open(q, livPath, &err), err);
    CHECK(notified >= 1);
    q.onAnyChange = nullptr;
    CHECK(q.isArchive());
    CHECK(q.archivePath == livPath);
    CHECK(q.title() == "MyShow");
    REQUIRE(q.sounds.items.size() == 2);
    Sound* qb = q.sounds.find(b->uid);
    REQUIRE(qb);
    CHECK(qb->isEffect());
    CHECK(qb->sticker() == "img:icons/deadbeef00000000.png");
    CHECK(fs::exists(ProjectIO::stickerImagePath(q, qb->sticker())));
    CHECK(fs::exists(ProjectIO::clipPath(q, *qb)));
    CHECK(readText(ProjectIO::clipPath(q, *qb)) == "RIFF-fake-Movie");
    CHECK(q.giftActions.find(5487)->soundUid == b->uid);
    CHECK(q.giftActions.find(5487)->actions().phase(Anchor::Start)->commands.command(0)->targetUid == pc->uid);
    CHECK(q.oscTargets.find(pc->uid)->host() == "192.168.0.10");
    CHECK(q.sounds.find(a->uid)->actions().phase(Anchor::Start)->commands.command(0)->text() == "/light/flash 1");

    // save again over the same archive (working folder reused), the sticker survives GC
    q.sounds.find(a->uid)->setNiceName("Kick 2");
    REQUIRE_MESSAGE(ProjectIO::saveArchive(q, livPath, &err), err);
    Project r;
    REQUIRE(ProjectIO::open(r, livPath, &err));
    CHECK(r.sounds.find(a->uid)->niceName == "Kick 2");
    CHECK(fs::exists(ProjectIO::stickerImagePath(r, r.sounds.find(b->uid)->sticker())));

    // a folder bundle Save As from an archive project drops the archive binding
    std::string folder = (root / "Legacy.evobox").string();
    REQUIRE(ProjectIO::save(r, folder, &err));
    CHECK_FALSE(r.isArchive());
    CHECK(r.title() == "Legacy");
    CHECK(fs::exists(fs::path(folder) / "icons" / "deadbeef00000000.png"));
    Project s;
    REQUIRE(ProjectIO::open(s, folder, &err));
    CHECK(s.sounds.items.size() == 2);
    // garbage: a non-zip .liv is refused with a clear error
    writeText(root / "Bogus.liv", "hello");
    Project t;
    CHECK_FALSE(ProjectIO::open(t, (root / "Bogus.liv").string(), &err));
    CHECK(err.find("not a .liv show file") != std::string::npos);
}
