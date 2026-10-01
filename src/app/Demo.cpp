#include "app/Demo.h"
#include "OrganicAudio.h"
#include "app/Application.h"
#include "util/Paths.h"
#include <filesystem>

namespace fs = std::filesystem;

namespace evobox
{
namespace demo
{

std::vector<std::string> ensureDemoAudio()
{
    fs::path dir = paths::cacheDir() / "demo";
    paths::ensureDir(dir);
    std::vector<std::string> out;
    struct D { const char* name; organic::AudioBuffer (*make)(); };
    auto beat = [] { return organic::makeBeat(4.f, 120.f); };
    auto tone = [] { return organic::makeTone(2.f, 440.f); };
    auto sweep = [] { return organic::makeSweep(3.f, 80.f, 2400.f); };
    const D defs[] = { { "drum-loop.wav", beat }, { "tone-a440.wav", tone }, { "riser-sweep.wav", sweep } };
    for (auto& d : defs)
    {
        fs::path p = dir / d.name;
        std::error_code ec;
        if (!fs::exists(p, ec)) organic::saveWavPcm16(p.string(), d.make());
        out.push_back(p.string());
    }
    return out;
}

void importDemoAudio(Application& app)
{
    app.importer.importFiles(ensureDemoAudio(), app.selectedCategoryUid);
}

void seedDemoCatalog(Application& app)
{
    if (!app.catalog.empty()) return;
    struct G { int64_t id; const char* name; int diamonds; int type; };
    static const G gifts[] = {
        { 5655, "Rose", 1, 1 }, { 5827, "TikTok", 1, 1 }, { 6064, "GG", 1, 1 }, { 7934, "Heart Me", 15, 1 },
        { 5269, "Doughnut", 30, 1 }, { 6671, "Hand Hearts", 100, 1 }, { 5760, "Perfume", 20, 1 },
        { 7121, "Confetti", 100, 0 }, { 5487, "Galaxy", 1000, 0 }, { 6369, "Diamond Ring", 300, 0 },
        { 5658, "Lion", 29999, 0 }, { 7059, "Universe", 44999, 0 }, { 5879, "Money Gun", 500, 1 },
        { 6193, "Drama Queen", 5000, 0 }, { 5650, "Corgi", 299, 0 }, { 6100, "Fireworks", 1088, 0 } };
    std::vector<GiftInfo> snapshot;
    for (auto& g : gifts)
    {
        GiftInfo gi;
        gi.id = g.id; gi.name = g.name; gi.diamondCount = g.diamonds; gi.type = g.type;
        snapshot.push_back(gi);
    }
    app.catalog.merge(snapshot, "demo");
}

bool driveSmokeGifts(Application& app, int frame, int atFrame)
{
    static int step = 0;
    if (frame < atFrame) return false;
    if (step == 0)
    {
        app.prefs.activeTab = WorkspaceTab::Gifts;
        GiftAction* a = app.selectGift(5487); // Galaxy
        if (a && a->actions().phase(Anchor::Start)) a->actions().phase(Anchor::Start)->commands.addCommandUndoable("/fx/galaxy 1", 0);
        step = 1;
        return false;
    }
    if (step == 1)
    {
        // the transient became a real action on the previous frame: finish its config
        if (GiftAction* a = app.project.giftActions.find(5487))
        {
            if (a->actions().phase(Anchor::Timer) && !a->actions().phase(Anchor::Timer)->hasCommands())
                a->actions().phase(Anchor::Timer)->commands.addCommandUndoable("/fx/galaxy 0", 0);
            app.select(a);
            if (auto* p = app.dock.find("Live Monitor")) p->open = true;
            app.simulateGift(5487);
            app.simulateGift(5655, 3);
            app.simulateRoomEvent(RoomEventKind::Follow);
        }
        step = 2;
        return true;
    }
    // ~4 s later (after the 3 s Stop timer) the same gift arrives again: it must fire again
    // (regression for the uid-collision bug that used to swallow every event after the first)
    if (step == 2 && frame >= atFrame + 260)
    {
        app.simulateGift(5487);
        app.simulateRoomEvent(RoomEventKind::Follow);
        step = 3;
    }
    return true;
}

bool driveSmokeDemo(Application& app, int frame, int saveAtFrame)
{
    static bool done = false;
    static bool played = false;
    if (done)
    {
        // once the clip is rendered, play it through the audio engine so "After play" fires too
        if (!played)
            if (Sound* s = app.selectedSound())
                if (s->hasClip()) { app.playback.play(*s, TriggerSource::Tile); played = true; }
        return true;
    }
    if (frame < saveAtFrame) return false;
    // imported media becomes a sound right away: trim the first one like a user would
    if (app.project.sounds.items.empty()) return false; // still decoding
    Sound* s = app.project.sounds.sound(0);
    // a video imported with --import: pick one of its frames as the tile picture (exercises the
    // img: sticker path end to end); wait for the extraction to finish first
    for (Sound* v : app.project.sounds.sounds())
    {
        if (app.stickerFramesPending(v->uid)) return false;
        if (const auto* frames = app.stickerFrames(v->uid))
        {
            const auto& pick = (*frames)[frames->size() / 2];
            if (pick.image) { std::string err; if (!app.setStickerFromImage(*v, *pick.image, &err)) OLOGW("Demo", err); }
            v->isEffectP->setUndoable(true);
            s = v;
        }
    }
    s->setTrimUndoable(0.5, std::min(s->trimEnd(), 2.75));
    if (OscPhase* start = s->actions().phase(Anchor::Start))
    {
        start->commands.addCommandUndoable("/light/flash 1 0.5", 0);
        start->commands.addCommandUndoable("/scene/go \"Verse 1\"", 0);
    }
    if (OscPhase* end = s->actions().phase(Anchor::End))
    {
        end->setDelayMsUndoable(250);
        end->commands.addCommandUndoable("/light/flash 0", 0);
    }
    app.project.oscTargets.addTargetUndoable("Lighting PC", "192.168.0.10", 8000);
    app.select(s);
    done = true;
    return true;
}

} // namespace demo
} // namespace evobox
