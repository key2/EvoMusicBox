// ClipRenderer.h — renders the trimmed / gained / normalized / faded clip of a source buffer; the
// result is written to the bundle by ClipEncoder (MP3, or the float32 WAV writer below as the
// lossless fallback) so tiles play without their source media.
#pragma once

#include <string>
#include "OrganicAudio.h"

namespace evobox
{

struct RenderParams
{
    double trimStartSec = 0;
    double trimEndSec = 0;     // <= trimStart means "to the end"
    float  gainDb = 0.f;
    bool   normalize = false;  // peak to -1 dBFS after gain
    float  fadeInMs = 0.f;
    float  fadeOutMs = 0.f;
};

class ClipRenderer
{
public:
    static organic::AudioBuffer render(const organic::AudioBuffer& source, const RenderParams& p);
    // The gain (linear) normalize would apply to this region — used by the preview player so
    // "Normalize" sounds the same before and after rendering.
    static float normalizeGain(const organic::AudioBuffer& source, const RenderParams& p);
    static float dbToLinear(float db);
    static float peakAbs(const organic::AudioBuffer& b, size_t frame0, size_t frame1);
};

// float32 WAV writer (IEEE float, WAVE_FORMAT_IEEE_FLOAT). Returns false on I/O error. Used by
// ClipEncoder for ".wav" paths (FFmpeg builds without an MP3 encoder) and by tests.
bool saveWavFloat32(const std::string& path, const organic::AudioBuffer& buf);

} // namespace evobox
