#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "OrganicAudio.h"
#include "media/ClipEncoder.h"
#include "media/FFmpegDecoder.h"
#include "media/ImageDecoder.h"
#include "media/ImageWriter.h"
#include "media/MediaService.h"
#include "media/VideoFrames.h"
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <thread>

using namespace evobox;
namespace fs = std::filesystem;

static std::string testDir()
{
    fs::path d = fs::path(EVOBOX_TEST_DIR) / "decoder_smoke";
    fs::create_directories(d);
    return d.string();
}

static std::string makeWav(const char* name, float seconds, int channels = 1)
{
    organic::AudioBuffer b = organic::makeTone(seconds, 440.f);
    if (channels == 2)
    {
        organic::AudioBuffer st;
        st.sampleRate = b.sampleRate;
        st.channels = 2;
        for (float s : b.samples) { st.samples.push_back(s); st.samples.push_back(s * 0.5f); }
        b = st;
    }
    std::string path = testDir() + "/" + name;
    REQUIRE(organic::saveWavPcm16(path, b));
    return path;
}

static bool haveFfmpegCli()
{
    static int cached = -1;
#if defined(_WIN32)
    if (cached < 0) cached = std::system("ffmpeg -version > NUL 2>&1") == 0 ? 1 : 0;
#else
    if (cached < 0) cached = std::system("ffmpeg -version > /dev/null 2>&1") == 0 ? 1 : 0;
#endif
    return cached == 1;
}

TEST_CASE("probe + decodeAll on a generated WAV")
{
    std::string wav = makeWav("tone.wav", 2.0f);
    MediaRef ref;
    std::string err;
    REQUIRE_MESSAGE(FFmpegDecoder::probe(wav, ref, &err), err);
    CHECK(ref.sampleRate == 44100);
    CHECK(ref.channels == 1);
    CHECK(ref.durationSec == doctest::Approx(2.0).epsilon(0.01));
    CHECK(ref.container.find("wav") != std::string::npos);
    CHECK(ref.codec == "pcm_s16le");
    CHECK_FALSE(ref.hasVideo);

    organic::AudioBuffer out;
    REQUIRE_MESSAGE(FFmpegDecoder::decodeAll(wav, out, &err), err);
    CHECK(out.sampleRate == 44100);
    CHECK(out.channels == 1);
    CHECK(out.frames() == 88200);
    // 440 Hz sine amplitude
    float peak = 0; for (float s : out.samples) peak = std::max(peak, std::fabs(s));
    CHECK(peak > 0.2f);
    CHECK(peak <= 1.0f);
}

TEST_CASE("stereo stays stereo; decodeRange trims")
{
    std::string wav = makeWav("stereo.wav", 3.0f, 2);
    organic::AudioBuffer out;
    std::string err;
    REQUIRE(FFmpegDecoder::decodeAll(wav, out, &err));
    CHECK(out.channels == 2);
    CHECK(out.frames() == 44100 * 3);
    organic::AudioBuffer part;
    REQUIRE_MESSAGE(FFmpegDecoder::decodeRange(wav, 1.0, 2.0, part, &err), err);
    CHECK(part.channels == 2);
    CHECK(part.frames() == doctest::Approx(44100).epsilon(0.02));
}

TEST_CASE("errors are reported, never crash")
{
    organic::AudioBuffer out;
    std::string err;
    CHECK_FALSE(FFmpegDecoder::decodeAll("/definitely/not/here.mp3", out, &err));
    CHECK_FALSE(err.empty());
    std::string junk = testDir() + "/junk.bin";
    { FILE* f = fopen(junk.c_str(), "wb"); const char z[64] = { 0 }; fwrite(z, 1, 64, f); fclose(f); }
    MediaRef ref;
    CHECK_FALSE(FFmpegDecoder::probe(junk, ref, &err));
    DecodeBudget tiny; tiny.maxDurationSec = 0.5;
    std::string wav = makeWav("long.wav", 2.0f);
    CHECK_FALSE(FFmpegDecoder::decodeAll(wav, out, &err, nullptr, nullptr, tiny));
    CHECK(err.find("budget") != std::string::npos);
}

TEST_CASE("compressed containers through the ffmpeg CLI (skipped without ffmpeg)")
{
    if (!haveFfmpegCli()) { MESSAGE("ffmpeg CLI not found: skipping mp3/mp4 smoke"); return; }
    std::string wav = makeWav("src.wav", 2.0f, 2);
    std::string mp3 = testDir() + "/src.mp3";
    std::string mp4 = testDir() + "/src.mp4";
    std::string png = testDir() + "/img.png";
    auto run = [](const std::string& cmd) { int rc = std::system(cmd.c_str()); (void)rc; };
    run("ffmpeg -y -loglevel error -i \"" + wav + "\" -codec:a libmp3lame -q:a 4 \"" + mp3 + "\"");
    run("ffmpeg -y -loglevel error -f lavfi -i color=c=blue:s=64x48:d=2 -i \"" + wav + "\" -shortest -c:v libx264 -pix_fmt yuv420p -c:a aac \"" + mp4 + "\"");
    run("ffmpeg -y -loglevel error -f lavfi -i color=c=red:s=32x16:d=1 -frames:v 1 \"" + png + "\"");
    std::string err;
    if (fs::exists(mp3))
    {
        MediaRef ref;
        REQUIRE_MESSAGE(FFmpegDecoder::probe(mp3, ref, &err), err);
        CHECK(ref.codec.find("mp3") != std::string::npos); // "mp3" or "mp3float"
        organic::AudioBuffer out;
        REQUIRE_MESSAGE(FFmpegDecoder::decodeAll(mp3, out, &err), err);
        CHECK(out.channels == 2);
        CHECK(out.duration() == doctest::Approx(2.0).epsilon(0.05));
    }
    if (fs::exists(mp4))
    {
        MediaRef ref;
        REQUIRE_MESSAGE(FFmpegDecoder::probe(mp4, ref, &err), err);
        CHECK(ref.hasVideo);
        CHECK(ref.codec == "aac");
        organic::AudioBuffer out;
        REQUIRE_MESSAGE(FFmpegDecoder::decodeAll(mp4, out, &err), err);
        CHECK(out.duration() == doctest::Approx(2.0).epsilon(0.1));
    }
    if (fs::exists(png))
    {
        RgbaImage img;
        REQUIRE_MESSAGE(ImageDecoder::decodeFile(png, img, 16, &err), err);
        CHECK(img.width == 16);
        CHECK(img.height == 8);
        CHECK(img.pixels[0] > 200);   // red
        CHECK(img.pixels[1] < 50);
        CHECK(img.pixels[3] == 255);
        // from memory too
        std::vector<uint8_t> bytes;
        { FILE* f = fopen(png.c_str(), "rb"); uint8_t buf[4096]; size_t n; while ((n = fread(buf, 1, sizeof(buf), f)) > 0) bytes.insert(bytes.end(), buf, buf + n); fclose(f); }
        RgbaImage img2;
        REQUIRE_MESSAGE(ImageDecoder::decodeBytes(bytes, img2, 0, &err), err);
        CHECK(img2.width == 32);
    }
}

TEST_CASE("video frames as sticker candidates + PNG writer (skipped without ffmpeg)")
{
    if (!haveFfmpegCli()) { MESSAGE("ffmpeg CLI not found: skipping video frame extraction"); return; }
    std::string mp4 = testDir() + "/frames.mp4";
    auto run = [](const std::string& cmd) { int rc = std::system(cmd.c_str()); (void)rc; };
    // testsrc2 changes every frame, so distinct times give distinct pictures
    run("ffmpeg -y -loglevel error -f lavfi -i testsrc2=size=160x120:rate=25:duration=4 -f lavfi -i sine=frequency=440:duration=4 "
        "-shortest -c:v libx264 -pix_fmt yuv420p -preset ultrafast -c:a aac \"" + mp4 + "\"");
    if (!fs::exists(mp4)) { MESSAGE("could not generate the test video"); return; }
    std::vector<VideoFrame> frames;
    std::string err;
    REQUIRE_MESSAGE(VideoFrames::extract(mp4, 12, 64, frames, &err), err);
    CHECK(frames.size() >= 10);
    CHECK(frames.size() <= 12);
    for (size_t i = 0; i < frames.size(); i++)
    {
        CHECK(frames[i].image.width == 64);
        CHECK(frames[i].image.height == 48);
        CHECK(frames[i].image.pixels.size() == 64u * 48u * 4u);
        if (i) CHECK(frames[i].time > frames[i - 1].time); // time ordered, no duplicates
    }
    CHECK(frames.front().time < 0.5);
    CHECK(frames.back().time > 3.0);
    // frames differ (the pattern moves)
    CHECK(frames.front().image.pixels != frames.back().image.pixels);

    // through the worker pool
    MediaService svc;
    svc.start(1);
    svc.requestVideoFrames(mp4, 0x4200, 5, 32);
    std::vector<MediaEvent> events;
    auto t0 = std::chrono::steady_clock::now();
    while (events.empty() && std::chrono::steady_clock::now() - t0 < std::chrono::seconds(15))
    {
        svc.drain([&](const MediaEvent& e) { if (e.kind == MediaEvent::Kind::Frames) events.push_back(e); });
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    svc.stop();
    REQUIRE(events.size() == 1);
    REQUIRE_MESSAGE(events[0].ok, events[0].error);
    CHECK(events[0].requestId == 0x4200);
    REQUIRE(events[0].frames);
    CHECK(events[0].frames->size() == 5);
    CHECK(events[0].frames->front().image.width == 32);

    // PNG round trip of a frame (what setStickerFromImage stores in icons/)
    RgbaImage small = ImageWriter::fitToEdge(frames.front().image, 16);
    CHECK(small.width == 16);
    CHECK(small.height == 12);
    RgbaImage sq = ImageWriter::squareCrop(frames.front().image);
    CHECK(sq.width == sq.height);
    std::string png = testDir() + "/frame.png";
    REQUIRE_MESSAGE(ImageWriter::writePng(png, small, &err), err);
    RgbaImage back;
    REQUIRE_MESSAGE(ImageDecoder::decodeFile(png, back, 0, &err), err);
    CHECK(back.width == 16);
    CHECK(back.height == 12);
    CHECK(back.pixels == small.pixels);
    // audio-only files have no frames
    std::string wav = makeWav("noframes.wav", 1.0f);
    frames.clear();
    CHECK_FALSE(VideoFrames::extract(wav, 4, 64, frames, &err));
    CHECK(frames.empty());
}

TEST_CASE("MediaService: decode + render + clip load through the worker pool")
{
    std::string wav = makeWav("svc.wav", 1.0f);
    MediaService svc;
    svc.start(2);
    svc.requestDecode(wav, 77);
    std::vector<MediaEvent> events;
    auto t0 = std::chrono::steady_clock::now();
    while (events.empty() && std::chrono::steady_clock::now() - t0 < std::chrono::seconds(10))
    {
        svc.drain([&](const MediaEvent& e) { if (e.kind == MediaEvent::Kind::Decoded) events.push_back(e); });
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    REQUIRE(events.size() == 1);
    const MediaEvent& d = events[0];
    REQUIRE_MESSAGE(d.ok, d.error);
    REQUIRE(d.asset);
    CHECK(d.asset->buffer->frames() == 44100);
    REQUIRE(d.asset->peaks);
    CHECK_FALSE(d.asset->peaks->mins.empty());
    CHECK_FALSE(d.asset->ref.contentHash.empty());
    CHECK(d.asset->ref.sizeBytes > 0);
    std::shared_ptr<const organic::AudioBuffer> source = d.asset->buffer; // `d` dies with events.clear()

    // events[0] = the next event of `kind`; events of other kinds stay queued for later waits
    // (a render posts Rendered and ClipEncoded back to back). Requires success unless told otherwise.
    std::vector<MediaEvent> pending;
    auto waitFor = [&](MediaEvent::Kind kind, bool expectOk = true)
    {
        events.clear();
        t0 = std::chrono::steady_clock::now();
        while (events.empty() && std::chrono::steady_clock::now() - t0 < std::chrono::seconds(10))
        {
            svc.drain([&](const MediaEvent& e) { pending.push_back(e); });
            for (auto it = pending.begin(); it != pending.end(); ++it)
                if (it->kind == kind) { events.push_back(*it); pending.erase(it); break; }
            if (events.empty()) std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        REQUIRE(events.size() == 1);
        if (expectOk) REQUIRE_MESSAGE(events[0].ok, events[0].error);
    };
    auto noTempFiles = [&]
    {
        for (auto& e : fs::directory_iterator(testDir() + "/clips"))
            if (e.path().extension() == ".tmp") return false;
        return true;
    };
    // only libmp3lame writes the gapless (LAME) tag that makes the decoded length exact
    bool gapless = ClipEncoder::mp3EncoderName() == "libmp3lame" || !ClipEncoder::mp3Available();

    // render -> two events: the buffer as soon as it is rendered, then the file write (current
    // clip format: .mp3 with an MP3 encoder, else float32 .wav)
    RenderParams rp; rp.trimStartSec = 0.25; rp.trimEndSec = 0.75;
    std::string clip = testDir() + "/clips/000001" + ClipEncoder::clipExtension();
    svc.requestRender(source, rp, clip, 1);
    waitFor(MediaEvent::Kind::Rendered);
    CHECK(events[0].buffer->frames() == 22050); // the in-memory render (lossless)
    CHECK(events[0].path == clip);
    waitFor(MediaEvent::Kind::ClipEncoded);
    CHECK(events[0].requestId == 1);
    CHECK(events[0].path == clip);
    REQUIRE(fs::exists(clip));
    CHECK(noTempFiles());
    if (ClipEncoder::mp3Available()) CHECK(fs::file_size(clip) < 20 * 1024); // vs 176 KB of float PCM

    svc.requestClipLoad(clip, 1);
    waitFor(MediaEvent::Kind::ClipLoaded);
    if (gapless) CHECK(events[0].buffer->frames() == 22050); // same length after the file round trip
    else CHECK(events[0].buffer->frames() >= 22050);
    CHECK(events[0].buffer->sampleRate == 44100);
    CHECK(events[0].buffer->channels == 1);

    // a render without an output path only produces the buffer event
    svc.requestRender(source, rp, "", 9);
    waitFor(MediaEvent::Kind::Rendered);
    CHECK(events[0].requestId == 9);
    CHECK(events[0].path.empty());

    // legacy migration job: an already decoded buffer written as a new clip file
    std::string legacy = testDir() + "/clips/000002.wav";
    REQUIRE(ClipEncoder::encode(legacy, *source));
    svc.requestClipLoad(legacy, 2);
    waitFor(MediaEvent::Kind::ClipLoaded);
    std::string migrated = testDir() + "/clips/000002" + ClipEncoder::clipExtension();
    svc.requestClipEncode(events[0].buffer, migrated, 2);
    waitFor(MediaEvent::Kind::ClipEncoded);
    CHECK(events[0].requestId == 2);
    CHECK(events[0].path == migrated);
    CHECK(fs::exists(migrated));
    CHECK(noTempFiles());
    svc.requestClipLoad(migrated, 2);
    waitFor(MediaEvent::Kind::ClipLoaded);
    if (gapless) CHECK(events[0].buffer->frames() == 44100); else CHECK(events[0].buffer->frames() >= 44100);
    // a missing buffer fails without crashing
    svc.requestClipEncode(nullptr, migrated, 3);
    waitFor(MediaEvent::Kind::ClipEncoded, false);
    CHECK_FALSE(events[0].ok);
    CHECK_FALSE(events[0].error.empty());
    svc.stop();
}
