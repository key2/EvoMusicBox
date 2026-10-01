// AudioEngine.h — facade over miniaudio's ma_engine (the implementation is compiled inside
// liborganic; we include miniaudio.h without MINIAUDIO_IMPLEMENTATION). One device owned by
// the app, a fixed pool of voices (ma_sound over ma_audio_buffer_ref into the clip's float PCM),
// and a preview voice for the clip editor. All calls happen on the main thread; miniaudio's
// control API is thread-safe with respect to its own audio thread.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include "OrganicAudio.h"

namespace evobox
{

struct VoiceHandle
{
    int index = -1;
    uint32_t generation = 0;
    bool valid() const { return index >= 0; }
    bool operator==(const VoiceHandle& o) const { return index == o.index && generation == o.generation; }
};

struct PlayParams
{
    float gain = 1.f;
    int fadeInMs = 0;
    bool loop = false;
    uint64_t startFrame = 0;  // sub-range of the clip (0/0 = whole clip): cursor/length/loop are relative to it
    uint64_t endFrame = 0;
    uint64_t seekFrame = 0;   // start position inside the whole clip: progress stays clip-relative, a loop wraps to 0
};

struct VoiceStatus
{
    bool active = false;      // slot allocated
    bool playing = false;
    uint64_t cursorFrames = 0;
    uint64_t lengthFrames = 0;
    float progress01 = 0.f;
};

enum class EndReason { Natural = 0, Stopped = 1, Stolen = 2, Shutdown = 3 };

struct VoiceEnded
{
    VoiceHandle handle;
    EndReason reason = EndReason::Natural;
};

struct AudioDeviceInfo
{
    std::string name;
    bool isDefault = false;
};

struct AudioSettings
{
    std::string deviceName;      // "" = system default
    int periodSizeInFrames = 256;
    float masterVolume = 0.9f;
    bool nullBackend = false;    // tests / CI
    int maxVoices = 32;
};

class PreviewPlayer;

class AudioEngine
{
public:
    AudioEngine();
    ~AudioEngine();
    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    bool init(const AudioSettings& settings);
    void shutdown();
    bool ok() const;
    std::string deviceName() const;
    int deviceSampleRate() const;
    const AudioSettings& settings() const;
    std::vector<AudioDeviceInfo> enumerateDevices() const;
    // Re-open the device (device change / hot-plug). Stops every voice.
    bool reinit(const AudioSettings& settings);

    VoiceHandle play(std::shared_ptr<const organic::AudioBuffer> clip, const PlayParams& p = PlayParams());
    void stop(VoiceHandle h, int fadeOutMs = 5);
    void stopAll(int fadeOutMs = 20);
    VoiceStatus status(VoiceHandle h) const;
    bool isPlaying(VoiceHandle h) const { return status(h).playing; }
    int activeVoiceCount() const;

    void setMasterVolume(float v);
    float masterVolume() const;

    // Main thread, once per frame: releases finished voices and reports them.
    void drainEvents(const std::function<void(const VoiceEnded&)>& fn);

    PreviewPlayer& preview();

    struct Impl; // implementation detail (public so the voice slots can reference it)

private:
    std::unique_ptr<Impl> impl_;
    friend class PreviewPlayer;
};

// One dedicated voice over the *source* asset for the clip editor: region + loop follow the
// trim handles live; the cursor feeds the playhead.
class PreviewPlayer
{
public:
    explicit PreviewPlayer(AudioEngine& engine);
    ~PreviewPlayer();

    void setSource(std::shared_ptr<const organic::AudioBuffer> source); // stops playback
    std::shared_ptr<const organic::AudioBuffer> source() const { return source_; }
    void setRegion(double t0, double t1);      // seconds in the source; applies live
    void setLoop(bool loop);
    void setGain(float linear);
    void play();                               // from the region start
    void playFrom(double tSec);                // inside the region
    void stop();
    bool isPlaying() const;
    double playhead() const;                   // seconds in the source
    double regionStart() const { return t0_; }
    double regionEnd() const { return t1_; }

private:
    struct Impl;
    AudioEngine& engine_;
    std::unique_ptr<Impl> impl_;
    std::shared_ptr<const organic::AudioBuffer> source_;
    double t0_ = 0, t1_ = 0;
    bool loop_ = false;
    float gain_ = 1.f;
    void applyRegion();
};

} // namespace evobox
