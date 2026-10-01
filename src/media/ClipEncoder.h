// ClipEncoder.h — writes a rendered clip buffer to disk. Clips are MP3 (libavcodec's MP3 encoder,
// libmp3lame preferred, VBR quality 2 ≈ 190 kbit/s) so shows stay small: a 3-minute stereo song is
// ~4 MB instead of ~70 MB of float32 PCM. The float32 WAV writer (ClipRenderer.h) remains the
// lossless fallback when the FFmpeg build has no MP3 encoder, and legacy `.wav` clips still load
// (FFmpegDecoder reads any container).
//
// MP3 constraints handled here: 1 or 2 channels (more are downmixed), one of the MPEG sample
// rates (others are resampled to the nearest supported rate, at most 48 kHz), planar input for
// the encoder (libswresample). FFmpeg's mp3 muxer writes the Xing/LAME gapless tag so the decoder
// trims the encoder delay and padding: a clip comes back with exactly the frames that went in
// (at the MP3 sample rate). Contexts are per call — safe to use from several worker threads.
#pragma once

#include <string>
#include "OrganicAudio.h"

namespace evobox
{

class ClipEncoder
{
public:
    static constexpr const char* kMp3Ext = ".mp3";
    static constexpr const char* kWavExt = ".wav";

    // True when libavcodec offers an MP3 encoder (libmp3lame or libshine). Cached after the first call.
    static bool mp3Available();
    // Name of the MP3 encoder that would be used ("libmp3lame", "libshine", or "" when none).
    static std::string mp3EncoderName();
    // Extension newly rendered clips get: ".mp3" when an encoder is available, ".wav" otherwise.
    static const char* clipExtension();
    // A clip file stored in the previous lossless format while MP3 clips are in use — the app
    // re-encodes it to MP3 the first time it is loaded.
    static bool isLegacyClip(const std::string& clipFile);

    // Writes `buf` to `path`; the format follows the extension: ".mp3" -> MP3, anything else ->
    // float32 WAV. Writes through a temporary file (path + ".<n>.tmp", unique per call) renamed
    // into place, so a crash never leaves a truncated clip behind. Returns false with `err`.
    static bool encode(const std::string& path, const organic::AudioBuffer& buf, std::string* err = nullptr);
    // MP3 only (any extension). Fails when no MP3 encoder is compiled into libavcodec.
    static bool encodeMp3(const std::string& path, const organic::AudioBuffer& buf, std::string* err = nullptr);

    // Sample rate an MP3 clip rendered from a `sourceRate` buffer will have: the smallest rate the
    // encoder supports that is >= sourceRate, else its highest (48 kHz for LAME). Returns
    // sourceRate when no encoder is available.
    static int mp3SampleRate(int sourceRate);
    // Channel count an MP3 clip of a `sourceChannels` buffer will have (1 or 2).
    static int mp3Channels(int sourceChannels) { return sourceChannels <= 1 ? 1 : 2; }
};

} // namespace evobox
