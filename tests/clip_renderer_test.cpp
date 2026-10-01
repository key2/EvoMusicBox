#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "OrganicAudio.h"
#include "media/ClipEncoder.h"
#include "media/ClipRenderer.h"
#include "media/FFmpegDecoder.h"
#include "model/MediaRef.h"
#include "util/Paths.h"
#include <cmath>
#include <filesystem>
#include <fstream>

using namespace evobox;

static organic::AudioBuffer constant(float value, float seconds, int rate = 1000, int channels = 1)
{
    organic::AudioBuffer b;
    b.sampleRate = rate;
    b.channels = channels;
    b.samples.assign((size_t)(seconds * rate) * channels, value);
    return b;
}

// A stereo test signal: two different tones (440 Hz left, 660 Hz right) with a slow envelope so
// every part of the clip carries signal and the channels are distinguishable.
static constexpr double kPi = 3.14159265358979323846;

static organic::AudioBuffer tones(int rate, int frames, int channels = 2)
{
    organic::AudioBuffer b;
    b.sampleRate = rate;
    b.channels = channels;
    b.samples.reserve((size_t)frames * channels);
    for (int i = 0; i < frames; i++)
    {
        double t = (double)i / rate;
        float env = 0.6f + 0.3f * (float)std::sin(2 * kPi * 1.5 * t);
        for (int c = 0; c < channels; c++)
            b.samples.push_back(env * (float)std::sin(2 * kPi * (440.0 + 220.0 * c) * t));
    }
    return b;
}

static std::filesystem::path testDir()
{
    std::filesystem::path dir = std::filesystem::path(EVOBOX_TEST_DIR) / "renderer";
    std::filesystem::create_directories(dir);
    return dir;
}

// RMS of (a - b) over the overlapping frames of one channel, relative to the RMS of a.
static double relativeError(const organic::AudioBuffer& a, const organic::AudioBuffer& b, int channel)
{
    int frames = std::min(a.frames(), b.frames());
    double num = 0, den = 0;
    for (int i = 0; i < frames; i++)
    {
        double x = a.samples[(size_t)i * a.channels + channel];
        double y = b.samples[(size_t)i * b.channels + channel];
        num += (x - y) * (x - y);
        den += x * x;
    }
    return den > 0 ? std::sqrt(num / den) : 0.0;
}

static std::string head(const std::string& path, size_t n)
{
    std::ifstream f(path, std::ios::binary);
    std::string s(n, '\0');
    f.read(&s[0], (std::streamsize)n);
    s.resize((size_t)f.gcount());
    return s;
}

// ClipEncoder writes "<clip>.<n>.tmp" and renames it into place: no temp file may survive a call.
static bool noTempFiles(const std::filesystem::path& dir)
{
    for (auto& e : std::filesystem::directory_iterator(dir))
        if (e.path().extension() == ".tmp") return false;
    return true;
}

// Only libmp3lame reports its encoder delay/padding (LAME tag) so the decoded length is exact;
// other MP3 encoders of an FFmpeg build (libshine, mp3_mf) come back a few hundred frames longer.
static bool gaplessMp3() { return ClipEncoder::mp3EncoderName() == "libmp3lame"; }

TEST_CASE("trim selects frames")
{
    organic::AudioBuffer src = constant(0.5f, 2.0f);
    RenderParams p; p.trimStartSec = 0.5; p.trimEndSec = 1.25;
    organic::AudioBuffer out = ClipRenderer::render(src, p);
    CHECK(out.frames() == 750);
    CHECK(out.sampleRate == 1000);
    CHECK(out.samples[100] == doctest::Approx(0.5f));
    // trimEnd <= trimStart means "to the end"
    RenderParams whole; whole.trimStartSec = 1.0;
    CHECK(ClipRenderer::render(src, whole).frames() == 1000);
    // out of range clamps
    RenderParams over; over.trimStartSec = 1.5; over.trimEndSec = 9.0;
    CHECK(ClipRenderer::render(src, over).frames() == 500);
}

TEST_CASE("gain and normalize")
{
    organic::AudioBuffer src = constant(0.25f, 1.0f);
    RenderParams p; p.gainDb = 6.0206f;
    organic::AudioBuffer out = ClipRenderer::render(src, p);
    CHECK(out.samples[10] == doctest::Approx(0.5f).epsilon(0.001));
    RenderParams n; n.normalize = true;
    organic::AudioBuffer norm = ClipRenderer::render(src, n);
    float target = ClipRenderer::dbToLinear(-1.f);
    CHECK(norm.samples[10] == doctest::Approx(target).epsilon(0.001));
    CHECK(ClipRenderer::normalizeGain(src, n) == doctest::Approx(target / 0.25f).epsilon(0.001));
    // clipping safety
    RenderParams hot; hot.gainDb = 24.f;
    organic::AudioBuffer clipped = ClipRenderer::render(constant(0.9f, 0.1f), hot);
    for (float s : clipped.samples) CHECK(s <= 1.f);
}

TEST_CASE("fades ramp in and out")
{
    organic::AudioBuffer src = constant(1.0f, 1.0f, 1000, 2);
    RenderParams p; p.fadeInMs = 100; p.fadeOutMs = 200;
    organic::AudioBuffer out = ClipRenderer::render(src, p);
    REQUIRE(out.frames() == 1000);
    CHECK(out.samples[0] == doctest::Approx(0.f));
    CHECK(out.samples[50 * 2] < out.samples[99 * 2]);
    CHECK(out.samples[500 * 2] == doctest::Approx(1.f));
    CHECK(out.samples[(999) * 2] == doctest::Approx(0.f));
    CHECK(out.samples[900 * 2] < 0.5f);
}

TEST_CASE("float32 WAV writer round trips through the decoder")
{
    organic::AudioBuffer src;
    src.sampleRate = 48000;
    src.channels = 2;
    for (int i = 0; i < 4800; i++) { float v = std::sin(i * 0.01f) * 0.8f; src.samples.push_back(v); src.samples.push_back(-v); }
    std::string path = (testDir() / "f32.wav").string();
    REQUIRE(saveWavFloat32(path, src));
    organic::AudioBuffer back;
    std::string err;
    REQUIRE_MESSAGE(FFmpegDecoder::decodeAll(path, back, &err), err);
    CHECK(back.sampleRate == 48000);
    CHECK(back.channels == 2);
    REQUIRE(back.frames() == 4800);
    for (int i = 0; i < 4800; i += 97)
    {
        CHECK(back.samples[i * 2] == doctest::Approx(src.samples[i * 2]).epsilon(1e-5));
        CHECK(back.samples[i * 2 + 1] == doctest::Approx(src.samples[i * 2 + 1]).epsilon(1e-5));
    }
}

// ---------------------------------------------------------------- MP3 clips
// These need an MP3 encoder in the system libavcodec (libmp3lame); without one the app falls back
// to float32 WAV clips and the cases below are skipped.

TEST_CASE("MP3 clip encoder: gapless round trip keeps the exact frame count and the waveform")
{
    if (!ClipEncoder::mp3Available()) { MESSAGE("no MP3 encoder in this FFmpeg build, skipped"); return; }
    if (!gaplessMp3()) MESSAGE("MP3 encoder is " << ClipEncoder::mp3EncoderName() << ": exact-length checks relaxed");
    CHECK(std::string(ClipEncoder::clipExtension()) == ".mp3");

    organic::AudioBuffer src = tones(48000, 48000 * 3 / 2); // 1.5 s stereo, not a multiple of the MP3 frame
    std::string path = (testDir() / "clip.mp3").string();
    std::string err;
    REQUIRE_MESSAGE(ClipEncoder::encode(path, src, &err), err);
    CHECK(noTempFiles(testDir()));
    // an MP3, and far smaller than the 576 KB of float PCM
    MediaRef ref;
    REQUIRE_MESSAGE(FFmpegDecoder::probe(path, ref, &err), err);
    CHECK(ref.container == "mp3");
    CHECK(ref.codec.find("mp3") != std::string::npos);
    CHECK(std::filesystem::file_size(path) < 80 * 1024);

    organic::AudioBuffer back;
    REQUIRE_MESSAGE(FFmpegDecoder::decodeAll(path, back, &err), err);
    CHECK(back.sampleRate == 48000);
    CHECK(back.channels == 2);
    if (!gaplessMp3()) { CHECK(back.frames() >= src.frames()); return; }
    // the Xing/LAME tag lets the decoder drop the encoder delay and padding: same length as the input
    CHECK(back.frames() == src.frames());
    // lossy but close, and the channels are not swapped / time shifted
    CHECK(relativeError(src, back, 0) < 0.05);
    CHECK(relativeError(src, back, 1) < 0.05);
    // a time shift of a single MP3 granule would make the 440 Hz channel disagree badly
    organic::AudioBuffer shifted = back;
    shifted.samples.erase(shifted.samples.begin(), shifted.samples.begin() + 2 * 24);
    CHECK(relativeError(src, shifted, 0) > 0.2);
}

TEST_CASE("MP3 clip encoder: short and mono clips")
{
    if (!ClipEncoder::mp3Available()) return;
    std::string err;
    // shorter than one MP3 frame (1152 samples)
    organic::AudioBuffer tiny = tones(44100, 500, 1);
    std::string path = (testDir() / "tiny.mp3").string();
    REQUIRE_MESSAGE(ClipEncoder::encodeMp3(path, tiny, &err), err);
    organic::AudioBuffer back;
    REQUIRE_MESSAGE(FFmpegDecoder::decodeAll(path, back, &err), err);
    CHECK(back.channels == 1);
    CHECK(back.sampleRate == 44100);
    if (gaplessMp3()) CHECK(back.frames() == 500); else CHECK(back.frames() >= 500);
    // more than two channels are downmixed to stereo
    organic::AudioBuffer wide = tones(48000, 4800, 4);
    path = (testDir() / "wide.mp3").string();
    REQUIRE_MESSAGE(ClipEncoder::encodeMp3(path, wide, &err), err);
    REQUIRE_MESSAGE(FFmpegDecoder::decodeAll(path, back, &err), err);
    CHECK(back.channels == 2);
    if (gaplessMp3()) CHECK(back.frames() == 4800); else CHECK(back.frames() >= 4800);
}

TEST_CASE("MP3 clip encoder: unsupported sample rates are resampled to the nearest MPEG rate")
{
    if (!ClipEncoder::mp3Available()) return;
    CHECK(ClipEncoder::mp3SampleRate(44100) == 44100);
    CHECK(ClipEncoder::mp3SampleRate(48000) == 48000);
    CHECK(ClipEncoder::mp3SampleRate(22050) == 22050);
    CHECK(ClipEncoder::mp3SampleRate(8000) == 8000);
    CHECK(ClipEncoder::mp3SampleRate(96000) == 48000);  // above the highest: downsample
    CHECK(ClipEncoder::mp3SampleRate(37800) == 44100);  // in between: the next rate up, no HF loss
    CHECK(ClipEncoder::mp3Channels(1) == 1);
    CHECK(ClipEncoder::mp3Channels(6) == 2);

    organic::AudioBuffer src = tones(96000, 96000 / 2); // 0.5 s at 96 kHz
    std::string path = (testDir() / "hires.mp3").string();
    std::string err;
    REQUIRE_MESSAGE(ClipEncoder::encode(path, src, &err), err);
    organic::AudioBuffer back;
    REQUIRE_MESSAGE(FFmpegDecoder::decodeAll(path, back, &err), err);
    CHECK(back.sampleRate == 48000);
    CHECK(back.channels == 2);
    if (!gaplessMp3()) return;
    CHECK(back.frames() == doctest::Approx(48000 / 2).epsilon(0.001)); // resampler rounding only
    CHECK(back.duration() == doctest::Approx(src.duration()).epsilon(0.001));
    // same tones at the new rate: compare against a 48 kHz rendering of the signal
    organic::AudioBuffer ref = tones(48000, 48000 / 2);
    CHECK(relativeError(ref, back, 0) < 0.08);
}

TEST_CASE("ClipEncoder::encode picks the container from the extension")
{
    organic::AudioBuffer src = tones(44100, 4410);
    std::string err;
    std::string wav = (testDir() / "byext.wav").string();
    REQUIRE_MESSAGE(ClipEncoder::encode(wav, src, &err), err);
    CHECK(head(wav, 4) == "RIFF");
    CHECK(noTempFiles(testDir()));
    organic::AudioBuffer back;
    REQUIRE_MESSAGE(FFmpegDecoder::decodeAll(wav, back, &err), err);
    CHECK(back.frames() == 4410); // lossless path unchanged
    if (ClipEncoder::mp3Available())
    {
        std::string mp3 = (testDir() / "byext.mp3").string();
        REQUIRE_MESSAGE(ClipEncoder::encode(mp3, src, &err), err);
        CHECK(head(mp3, 4) != "RIFF");
        CHECK(std::filesystem::file_size(mp3) < std::filesystem::file_size(wav) / 4);
        // Regression: a 0.1 s stereo clip is only ~6 MP3 frames — FFmpeg's content probe scores
        // it no better than a raw video probe and refuses to guess. The decoder must still open
        // it (retry with the demuxer named after the extension).
        REQUIRE_MESSAGE(FFmpegDecoder::decodeAll(mp3, back, &err), err);
        if (gaplessMp3()) CHECK(back.frames() == 4410); else CHECK(back.frames() >= 4410);
        CHECK(back.channels == 2);
        CHECK(noTempFiles(testDir()));
        MediaRef ref;
        REQUIRE_MESSAGE(FFmpegDecoder::probe(mp3, ref, &err), err);
        CHECK(ref.container == "mp3");
        // ...while garbage with an .mp3 name is still refused
        std::string fake = (testDir() / "garbage.mp3").string();
        std::ofstream(fake, std::ios::binary) << std::string(3000, 'q');
        CHECK_FALSE(FFmpegDecoder::decodeAll(fake, back, &err));
        CHECK(ClipEncoder::isLegacyClip("clips/000001.wav"));
        CHECK(ClipEncoder::isLegacyClip("clips/000001.WAV"));
        CHECK_FALSE(ClipEncoder::isLegacyClip("clips/000001.mp3"));
        CHECK_FALSE(ClipEncoder::isLegacyClip(""));
    }
    // an unwritable location fails cleanly without leaving files behind
    std::string bad = (testDir() / "no-such-dir-parent-file.txt" / "x.mp3").string();
    std::ofstream(testDir() / "no-such-dir-parent-file.txt") << "not a directory";
    CHECK_FALSE(ClipEncoder::encode(bad, src, &err));
    CHECK_FALSE(err.empty());
}

TEST_CASE("replaceFile never trades an existing file for a missing replacement")
{
    namespace fs = std::filesystem;
    fs::path dst = testDir() / "keep.bin";
    std::ofstream(dst, std::ios::binary) << "good clip";
    std::string err;
    // the temp file vanished (e.g. cleaned up by somebody else) -> failure, destination untouched
    CHECK_FALSE(paths::replaceFile(testDir() / "gone.tmp", dst, &err));
    CHECK_FALSE(err.empty());
    CHECK(head(dst.string(), 9) == "good clip");
    // the normal path replaces atomically and removes the temp file
    fs::path tmp = testDir() / "keep.bin.1.tmp";
    std::ofstream(tmp, std::ios::binary) << "new clip";
    REQUIRE(paths::replaceFile(tmp, dst, &err));
    CHECK(head(dst.string(), 8) == "new clip");
    CHECK_FALSE(fs::exists(tmp));
}
