// MediaAsset.h — an in-memory decoded source: MediaRef facts + float PCM + waveform peaks.
// Immutable once published (shared between the main thread, media workers and voices).
#pragma once

#include <memory>
#include "OrganicAudio.h"
#include "model/MediaRef.h"

namespace evobox
{

struct MediaAsset
{
    MediaRef ref;
    std::shared_ptr<const organic::AudioBuffer> buffer; // interleaved float, source rate
    std::shared_ptr<const organic::Peaks> peaks;        // mono min/max per bin

    double duration() const { return buffer ? buffer->duration() : ref.durationSec; }
    int sampleRate() const { return buffer ? buffer->sampleRate : ref.sampleRate; }
    int channels() const { return buffer ? buffer->channels : ref.channels; }
};

} // namespace evobox
