#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "OrganicAudio.h"
#include "audio/AudioEngine.h"
#include <chrono>
#include <thread>

using namespace evobox;

static std::shared_ptr<const organic::AudioBuffer> clip(float seconds)
{
    return std::make_shared<organic::AudioBuffer>(organic::makeTone(seconds, 440.f));
}

TEST_CASE("null backend: play, status, stop, voice ended")
{
    AudioEngine engine;
    AudioSettings s;
    s.nullBackend = true;
    s.maxVoices = 4;
    REQUIRE(engine.init(s));
    CHECK(engine.ok());
    CHECK(engine.deviceSampleRate() > 0);

    VoiceHandle h = engine.play(clip(5.f));
    REQUIRE(h.valid());
    CHECK(engine.activeVoiceCount() == 1);
    VoiceStatus st = engine.status(h);
    CHECK(st.active);
    CHECK(st.playing);
    CHECK(st.lengthFrames == 44100 * 5);

    // the null device advances time: the cursor moves
    std::this_thread::sleep_for(std::chrono::milliseconds(120));
    VoiceStatus st2 = engine.status(h);
    CHECK(st2.cursorFrames > 0);
    CHECK(st2.progress01 > 0.f);

    engine.stop(h, 0);
    std::vector<VoiceEnded> ended;
    engine.drainEvents([&](const VoiceEnded& e) { ended.push_back(e); });
    REQUIRE(ended.size() == 1);
    CHECK(ended[0].handle == h);
    CHECK(ended[0].reason == EndReason::Stopped);
    CHECK(engine.activeVoiceCount() == 0);
    CHECK_FALSE(engine.status(h).active); // stale handle
    engine.shutdown();
}

TEST_CASE("natural end is reported and the slot is reused; stealing when the pool is full")
{
    AudioEngine engine;
    AudioSettings s;
    s.nullBackend = true;
    s.maxVoices = 2;
    REQUIRE(engine.init(s));
    VoiceHandle a = engine.play(clip(0.05f));
    REQUIRE(a.valid());
    bool natural = false;
    auto t0 = std::chrono::steady_clock::now();
    while (!natural && std::chrono::steady_clock::now() - t0 < std::chrono::seconds(3))
    {
        engine.drainEvents([&](const VoiceEnded& e) { if (e.handle == a && e.reason == EndReason::Natural) natural = true; });
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    CHECK(natural);
    CHECK(engine.activeVoiceCount() == 0);

    VoiceHandle b = engine.play(clip(5.f));
    VoiceHandle c = engine.play(clip(5.f));
    VoiceHandle d = engine.play(clip(5.f)); // steals the oldest (b)
    REQUIRE(d.valid());
    CHECK(engine.activeVoiceCount() == 2);
    std::vector<VoiceEnded> ended;
    engine.drainEvents([&](const VoiceEnded& e) { ended.push_back(e); });
    REQUIRE(ended.size() == 1);
    CHECK(ended[0].handle == b);
    CHECK(ended[0].reason == EndReason::Stolen);
    CHECK(engine.status(c).active);
    engine.stopAll(0);
    engine.drainEvents([&](const VoiceEnded&) {});
    CHECK(engine.activeVoiceCount() == 0);
    engine.shutdown();
}

TEST_CASE("preview player region + playhead")
{
    AudioEngine engine;
    AudioSettings s;
    s.nullBackend = true;
    REQUIRE(engine.init(s));
    PreviewPlayer& pv = engine.preview();
    auto src = clip(4.f);
    pv.setSource(src);
    pv.setRegion(1.0, 2.0);
    pv.setLoop(true);
    pv.play();
    CHECK(pv.isPlaying());
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    double ph = pv.playhead();
    CHECK(ph >= 1.0);
    CHECK(ph <= 2.0);
    pv.stop();
    CHECK_FALSE(pv.isPlaying());
    pv.setSource(nullptr);
    engine.shutdown();
}

TEST_CASE("engine without a device is silent but safe")
{
    AudioEngine engine;
    CHECK_FALSE(engine.ok());
    VoiceHandle h = engine.play(clip(1.f));
    CHECK_FALSE(h.valid());
    engine.stop(h);
    engine.stopAll();
    engine.drainEvents([](const VoiceEnded&) { FAIL("no events expected"); });
    engine.preview().play();
}
