#include "media/ClipRenderer.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace evobox
{

float ClipRenderer::dbToLinear(float db) { return std::pow(10.f, db / 20.f); }

float ClipRenderer::peakAbs(const organic::AudioBuffer& b, size_t f0, size_t f1)
{
    float peak = 0.f;
    size_t ch = (size_t)std::max(1, b.channels);
    f1 = std::min(f1, (size_t)b.frames());
    for (size_t i = f0 * ch; i < f1 * ch; i++) peak = std::max(peak, std::fabs(b.samples[i]));
    return peak;
}

static void frameRange(const organic::AudioBuffer& src, const RenderParams& p, size_t& f0, size_t& f1)
{
    size_t total = (size_t)src.frames();
    double start = std::max(0.0, p.trimStartSec);
    double end = p.trimEndSec > p.trimStartSec ? p.trimEndSec : src.duration();
    f0 = std::min(total, (size_t)std::llround(start * src.sampleRate));
    f1 = std::min(total, (size_t)std::llround(end * src.sampleRate));
    if (f1 < f0) std::swap(f0, f1);
}

float ClipRenderer::normalizeGain(const organic::AudioBuffer& src, const RenderParams& p)
{
    size_t f0, f1;
    frameRange(src, p, f0, f1);
    float g = dbToLinear(p.gainDb);
    float peak = peakAbs(src, f0, f1) * g;
    if (peak <= 1e-6f) return 1.f;
    return dbToLinear(-1.f) / peak;
}

organic::AudioBuffer ClipRenderer::render(const organic::AudioBuffer& src, const RenderParams& p)
{
    organic::AudioBuffer out;
    out.sampleRate = src.sampleRate;
    out.channels = std::max(1, src.channels);
    size_t f0, f1;
    frameRange(src, p, f0, f1);
    size_t frames = f1 - f0;
    size_t ch = (size_t)out.channels;
    out.samples.assign(src.samples.begin() + (long)(f0 * ch), src.samples.begin() + (long)(f1 * ch));

    float gain = dbToLinear(p.gainDb);
    if (p.normalize) gain *= normalizeGain(src, p) ;
    if (gain != 1.f) for (float& s : out.samples) s *= gain;

    size_t fadeIn = std::min(frames, (size_t)std::llround(std::max(0.f, p.fadeInMs) * 0.001 * src.sampleRate));
    size_t fadeOut = std::min(frames, (size_t)std::llround(std::max(0.f, p.fadeOutMs) * 0.001 * src.sampleRate));
    for (size_t f = 0; f < fadeIn; f++)
    {
        float g = (float)f / (float)fadeIn;
        g = g * g; // gentle curve
        for (size_t c = 0; c < ch; c++) out.samples[f * ch + c] *= g;
    }
    for (size_t k = 0; k < fadeOut; k++)
    {
        size_t f = frames - 1 - k;
        float g = (float)k / (float)fadeOut;
        g = g * g;
        for (size_t c = 0; c < ch; c++) out.samples[f * ch + c] *= g;
    }
    // hard clip safety
    for (float& s : out.samples) s = std::max(-1.f, std::min(1.f, s));
    return out;
}

// ---------------------------------------------------------------- WAV float32
static void w32(FILE* f, uint32_t v) { uint8_t b[4] = { (uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24) }; fwrite(b, 1, 4, f); }
static void w16(FILE* f, uint16_t v) { uint8_t b[2] = { (uint8_t)v, (uint8_t)(v >> 8) }; fwrite(b, 1, 2, f); }

bool saveWavFloat32(const std::string& path, const organic::AudioBuffer& buf)
{
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return false;
    uint32_t channels = (uint32_t)std::max(1, buf.channels);
    uint32_t rate = (uint32_t)std::max(1, buf.sampleRate);
    uint32_t dataBytes = (uint32_t)(buf.samples.size() * sizeof(float));
    uint32_t blockAlign = channels * 4;
    fwrite("RIFF", 1, 4, f);
    w32(f, 4 + (8 + 18) + (8 + 4) + (8 + dataBytes)); // WAVE + fmt(18) + fact + data
    fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f);
    w32(f, 18);
    w16(f, 3);            // WAVE_FORMAT_IEEE_FLOAT
    w16(f, (uint16_t)channels);
    w32(f, rate);
    w32(f, rate * blockAlign);
    w16(f, (uint16_t)blockAlign);
    w16(f, 32);
    w16(f, 0);            // cbSize
    fwrite("fact", 1, 4, f);
    w32(f, 4);
    w32(f, (uint32_t)buf.frames());
    fwrite("data", 1, 4, f);
    w32(f, dataBytes);
    // little-endian float write (host is assumed little endian on all supported platforms)
    if (!buf.samples.empty() && fwrite(buf.samples.data(), 1, dataBytes, f) != dataBytes) { fclose(f); return false; }
    bool ok = fflush(f) == 0;
    fclose(f);
    return ok;
}

} // namespace evobox
