// PlaybackController policy: "Music stops previous music" (default) vs effects that stack,
// plus the v1 -> v2 settings migration. Null audio backend, fake OSC scheduler.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "FakeScheduler.h"
#include "OrganicAudio.h"
#include "app/PlaybackController.h"
#include "app/TriggerController.h"
#include "audio/AudioEngine.h"
#include "model/Project.h"
#include "osc/OscService.h"
#include <chrono>
#include <thread>

using namespace evobox;

namespace
{

struct Fixture
{
    Project project;
    AudioEngine audio;
    FakeScheduler* sched = nullptr;
    OscService osc;
    TriggerController trigger{ project, osc };
    PlaybackController playback{ project, audio, trigger };

    Fixture()
    {
        auto s = std::make_unique<FakeScheduler>();
        sched = s.get();
        osc.setScheduler(std::move(s));
        AudioSettings as;
        as.nullBackend = true;
        as.maxVoices = 16;
        REQUIRE(audio.init(as));
    }
    ~Fixture() { audio.shutdown(); }

    Sound* sound(const char* name, bool effect)
    {
        auto s = std::make_unique<Sound>();
        s->setNiceName(name);
        s->isEffectP->setValue(effect, false);
        s->rt.clip = std::make_shared<organic::AudioBuffer>(organic::makeTone(5.f, 440.f));
        s->rt.mediaStatus = MediaStatus::Ok;
        return project.sounds.addSoundUndoable(std::move(s));
    }
    // lets the (null) audio thread finish the 10 ms fade-outs and report the ended voices
    void settle()
    {
        for (int i = 0; i < 6; i++)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(15));
            playback.tick(0.0);
        }
    }
};

} // namespace

TEST_CASE("default policy: a new music stops the playing music, effects stack on top")
{
    Fixture f;
    CHECK(f.project.settings.playbackPolicy() == PlaybackPolicy::MusicExclusive);
    Sound* musicA = f.sound("Intro", false);
    Sound* musicB = f.sound("Chorus", false);
    Sound* fx = f.sound("Airhorn", true);

    REQUIRE(f.playback.play(*musicA, TriggerSource::Tile));
    f.settle();
    CHECK(f.playback.isPlaying(musicA->uid));

    // three presses on the effect = three overlapping voices, the music keeps playing
    for (int i = 0; i < 3; i++) REQUIRE(f.playback.play(*fx, TriggerSource::Tile));
    f.settle();
    CHECK(fx->rt.activeVoices == 3);
    CHECK(f.playback.isPlaying(musicA->uid));
    CHECK(f.playback.playingCount() == 4);

    // toggling a playing effect fires it again instead of stopping it
    f.playback.toggle(fx->uid, TriggerSource::Shortcut);
    f.settle();
    CHECK(fx->rt.activeVoices == 4);

    // another music replaces the first one; the effects are untouched
    REQUIRE(f.playback.play(*musicB, TriggerSource::Tile));
    f.settle();
    CHECK_FALSE(f.playback.isPlaying(musicA->uid));
    CHECK(f.playback.isPlaying(musicB->uid));
    CHECK(fx->rt.activeVoices == 4);

    // pressing the same music again restarts it (never two voices of one music)
    REQUIRE(f.playback.play(*musicB, TriggerSource::Tile));
    f.settle();
    CHECK(musicB->rt.activeVoices == 1);

    // toggling a playing music stops it
    f.playback.toggle(musicB->uid, TriggerSource::Shortcut);
    f.settle();
    CHECK_FALSE(f.playback.isPlaying(musicB->uid));
    CHECK(fx->rt.activeVoices == 4);
}

TEST_CASE("the effect tile's stop button: stop(uid) ends every stacked voice of that effect only")
{
    Fixture f;
    Sound* music = f.sound("music", false);
    Sound* fx = f.sound("fx", true);
    Sound* fx2 = f.sound("fx2", true);
    f.playback.play(*music, TriggerSource::Tile);
    for (int i = 0; i < 3; i++) f.playback.play(*fx, TriggerSource::Tile);
    f.playback.play(*fx2, TriggerSource::Tile);
    f.settle();
    REQUIRE(fx->rt.activeVoices == 3);
    CHECK(fx->playing()); // -> the tile shows its stop circle

    f.playback.stop(fx->uid);
    f.settle();
    CHECK(fx->rt.activeVoices == 0);
    CHECK_FALSE(fx->playing()); // -> the stop circle disappears
    CHECK_FALSE(f.playback.isPlaying(fx->uid));
    // the music and the other effect are untouched
    CHECK(f.playback.isPlaying(music->uid));
    CHECK(fx2->rt.activeVoices == 1);
    // pressing play again starts a fresh session (and the stop button comes back)
    f.playback.play(*fx, TriggerSource::Tile);
    f.settle();
    CHECK(fx->rt.activeVoices == 1);
    CHECK(fx->playing());
    // stopping an effect that is not playing is a no-op
    f.playback.stop(fx2->uid);
    f.playback.stop(fx2->uid);
    f.settle();
    CHECK(fx2->rt.activeVoices == 0);
    CHECK(fx->rt.activeVoices == 1);
}

TEST_CASE("Clip Editor playback = the tile's voices: per-voice progress, start inside the clip, loop")
{
    Fixture f;
    Sound* fx = f.sound("fx", true); // 5 s tone
    PlaybackController::PlayOptions fromMiddle;
    fromMiddle.startSec = 2.5;
    REQUIRE(f.playback.play(*fx, TriggerSource::Tile, fromMiddle));
    REQUIRE(f.playback.play(*fx, TriggerSource::Tile)); // from the start
    f.settle();
    REQUIRE(fx->rt.voiceProgress.size() == 2);      // one playhead per voice, start order
    CHECK(fx->rt.voiceProgress[0].progress01 >= 0.5f); // started halfway: progress is clip-relative
    CHECK(fx->rt.voiceProgress[0].progress01 < 0.6f);
    CHECK(fx->rt.voiceProgress[1].progress01 < 0.1f);
    CHECK(fx->rt.progress01 == fx->rt.voiceProgress[1].progress01); // the tile follows the latest voice
    // the clip of the fixture starts at source 0 s: source position = position in the clip
    CHECK(fx->rt.voiceProgress[0].sourceSec >= 2.5);
    CHECK(fx->rt.voiceProgress[0].sourceSec < 3.0);
    CHECK(fx->rt.voiceProgress[1].sourceSec < 0.5);
    f.playback.stop(fx->uid);
    f.settle();
    CHECK(fx->rt.voiceProgress.empty());
    CHECK(fx->rt.activeVoices == 0);

    // a looping voice (editor loop toggle) keeps playing past the clip end until stopped
    auto make = [&](const char* name)
    {
        auto s = std::make_unique<Sound>();
        s->setNiceName(name);
        s->rt.clip = std::make_shared<organic::AudioBuffer>(organic::makeTone(0.1f, 440.f)); // 100 ms
        s->rt.mediaStatus = MediaStatus::Ok;
        return f.project.sounds.addSoundUndoable(std::move(s));
    };
    Sound* once = make("once");
    Sound* looped = make("looped");
    PlaybackController::PlayOptions loop;
    loop.loop = true;
    REQUIRE(f.playback.play(*once, TriggerSource::Tile));
    REQUIRE(f.playback.play(*looped, TriggerSource::Tile, loop));
    for (int i = 0; i < 4; i++) f.settle(); // ~360 ms
    CHECK_FALSE(f.playback.isPlaying(once->uid));
    CHECK(f.playback.isPlaying(looped->uid));
    REQUIRE(looped->rt.voiceProgress.size() == 1);
    CHECK(looped->rt.voiceProgress[0].progress01 >= 0.f);
    CHECK(looped->rt.voiceProgress[0].progress01 <= 1.f);
    CHECK(looped->rt.voiceProgress[0].sourceSec >= 0.0);
    CHECK(looped->rt.voiceProgress[0].sourceSec <= 0.1); // wraps inside the clip, never past it
    f.playback.stop(looped->uid);
    f.settle();
    CHECK_FALSE(f.playback.isPlaying(looped->uid));
}

TEST_CASE("the Clip Editor playhead follows the audio, not the trim handles")
{
    // The clip is the rendered trim range [10 s, 15 s] of a longer source. While it plays, the user
    // drags the handles: the position reported for the waveform must keep tracking the audio that is
    // heard (the voice still plays the old render) instead of being rescaled onto the new selection.
    Fixture f;
    Sound* music = f.sound("song", false);          // 5 s clip
    music->setTrim(10.0, 15.0, false);
    music->rt.clipSourceStart = 10.0;               // what the render of [10, 15] reports
    PlaybackController::PlayOptions fromMiddle;
    fromMiddle.startSec = 2.5;                      // = source 12.5 s
    REQUIRE(f.playback.play(*music, TriggerSource::Tile, fromMiddle));
    f.settle();
    REQUIRE(music->rt.voiceProgress.size() == 1);
    double before = music->rt.voiceProgress[0].sourceSec;
    CHECK(before >= 12.5);
    CHECK(before < 13.0);

    // the right handle moves (nothing re-rendered yet): the audio did not change, nor may the position
    music->setTrim(10.0, 12.0, false);
    f.settle();
    double afterRight = music->rt.voiceProgress[0].sourceSec;
    CHECK(afterRight >= before);
    CHECK(afterRight < 13.5);                       // the old formula would have given ~11.0
    // the left handle moves too
    music->setTrim(11.0, 12.0, false);
    f.settle();
    double afterLeft = music->rt.voiceProgress[0].sourceSec;
    CHECK(afterLeft >= afterRight);
    CHECK(afterLeft < 13.5);                        // (old formula: ~11.5)

    // the re-render of [11, 12] lands: the running voice keeps its own buffer and position...
    music->rt.clip = std::make_shared<organic::AudioBuffer>(organic::makeTone(1.f, 440.f));
    music->rt.clipSourceStart = 11.0;
    f.settle();
    REQUIRE(music->rt.voiceProgress.size() == 1);
    CHECK(music->rt.voiceProgress[0].sourceSec >= afterLeft);
    CHECK(music->rt.voiceProgress[0].sourceSec < 13.5);
    // ...while a voice started now plays the new clip and reports inside [11, 12]
    Sound* fx = f.sound("fx", true);
    fx->rt.clip = music->rt.clip;
    fx->rt.clipSourceStart = 11.0;
    REQUIRE(f.playback.play(*fx, TriggerSource::Tile));
    f.settle();
    REQUIRE(fx->rt.voiceProgress.size() == 1);
    CHECK(fx->rt.voiceProgress[0].sourceSec >= 11.0);
    CHECK(fx->rt.voiceProgress[0].sourceSec < 11.5);
    f.playback.stopAll(0);
    f.settle();
}

TEST_CASE("Overlap and Stop others policies")
{
    Fixture f;
    Sound* a = f.sound("A", false);
    Sound* b = f.sound("B", false);
    Sound* fx = f.sound("FX", true);
    f.project.settings.playbackPolicyP->setValue((int)PlaybackPolicy::Overlap, false);
    f.playback.play(*a, TriggerSource::Tile);
    f.playback.play(*b, TriggerSource::Tile);
    f.playback.play(*a, TriggerSource::Tile);
    f.settle();
    CHECK(a->rt.activeVoices == 2);
    CHECK(b->rt.activeVoices == 1);

    f.project.settings.playbackPolicyP->setValue((int)PlaybackPolicy::StopOthers, false);
    f.playback.play(*fx, TriggerSource::Tile);
    f.settle();
    CHECK(a->rt.activeVoices == 0);
    CHECK(b->rt.activeVoices == 0);
    CHECK(fx->rt.activeVoices == 1);
    f.playback.play(*fx, TriggerSource::Tile); // same sound is not "other": stacks
    f.settle();
    CHECK(fx->rt.activeVoices == 2);
}

TEST_CASE("format v1 playback policy values are remapped")
{
    Project p;
    json j = p.save();
    j["formatVersion"] = 1;
    j["settings"]["params"]["playbackPolicy"] = 1; // v1 "Stop others"
    Project q;
    q.load(j);
    CHECK(q.settings.playbackPolicy() == PlaybackPolicy::StopOthers);
    j["settings"]["params"]["playbackPolicy"] = 0; // v1 "Overlap" (the old default) -> new default
    Project r;
    r.load(j);
    CHECK(r.settings.playbackPolicy() == PlaybackPolicy::MusicExclusive);
    CHECK(r.save()["formatVersion"] == Project::kFormatVersion);
    // the Effect flag round-trips
    auto s = std::make_unique<Sound>();
    s->isEffectP->setValue(true, false);
    Sound* raw = r.sounds.addSoundUndoable(std::move(s));
    json saved = r.save();
    Project t;
    t.load(saved);
    REQUIRE(t.sounds.find(raw->uid));
    CHECK(t.sounds.find(raw->uid)->isEffect());
}
