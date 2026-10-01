// IMediaSource.h — what the clip editor / renderer need from a source: peaks for drawing,
// duration, and PCM for a time window. The in-memory implementation wraps a MediaAsset; a
// streaming implementation (peaks on the fly + windowed decode) is the documented upgrade for
// multi-hour media.
#pragma once

#include <memory>
#include "model/MediaAsset.h"

namespace evobox
{

class IMediaSource
{
public:
    virtual ~IMediaSource() = default;
    virtual const organic::Peaks* peaks() const = 0;
    virtual double duration() const = 0;
    virtual int sampleRate() const = 0;
    virtual int channels() const = 0;
    // PCM of [t0, t1] seconds (interleaved float at sampleRate()).
    virtual organic::AudioBuffer read(double t0, double t1) const = 0;
    // The whole decoded buffer when resident in memory (nullptr for streaming sources).
    virtual std::shared_ptr<const organic::AudioBuffer> wholeBuffer() const { return nullptr; }
};

class InMemoryMediaSource : public IMediaSource
{
public:
    explicit InMemoryMediaSource(std::shared_ptr<const MediaAsset> asset) : asset_(std::move(asset)) {}
    const organic::Peaks* peaks() const override { return asset_ && asset_->peaks ? asset_->peaks.get() : nullptr; }
    double duration() const override { return asset_ ? asset_->duration() : 0; }
    int sampleRate() const override { return asset_ ? asset_->sampleRate() : 0; }
    int channels() const override { return asset_ ? asset_->channels() : 0; }
    organic::AudioBuffer read(double t0, double t1) const override;
    std::shared_ptr<const organic::AudioBuffer> wholeBuffer() const override { return asset_ ? asset_->buffer : nullptr; }
    const std::shared_ptr<const MediaAsset>& asset() const { return asset_; }

private:
    std::shared_ptr<const MediaAsset> asset_;
};

} // namespace evobox
