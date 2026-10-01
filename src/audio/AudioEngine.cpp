#include "audio/AudioEngine.h"
#include "util/SpscRing.h"
#include "OrganicCore.h"
#include <algorithm>
#include <cmath>

// The implementation lives in liborganic (OrganicAudioEngine.cpp, compiled with MA_NO_ENCODING).
// Keep the same configuration macros so declarations match.
#define MA_NO_ENCODING
#include "miniaudio.h"

namespace evobox
{

// ---------------------------------------------------------------- voice slot
struct Voice
{
    ma_sound sound{};
    ma_audio_buffer_ref ref{};
    bool inited = false;      // ma_sound initialised
    bool inUse = false;
    uint32_t generation = 0;
    uint64_t startOrder = 0;  // for voice stealing (oldest first)
    std::shared_ptr<const organic::AudioBuffer> clip;
    struct AudioEngine::Impl* owner = nullptr;
    int index = 0;
};

struct EndedMsg
{
    int index;
    uint32_t generation;
};

struct AudioEngine::Impl
{
    AudioSettings settings;
    ma_context context{};
    bool contextOk = false;
    ma_engine engine{};
    bool engineOk = false;
    std::vector<std::unique_ptr<Voice>> voices;
    uint64_t playCounter = 0;
    SpscRing<EndedMsg, 256> ended;   // audio thread -> main
    std::vector<VoiceEnded> pendingMain; // main-thread generated end events (stop / steal)
    std::unique_ptr<PreviewPlayer> preview;
    float masterVolume = 0.9f;

    static void endCallback(void* pUserData, ma_sound*)
    {
        auto* v = (Voice*)pUserData;
        if (v && v->owner) v->owner->ended.push(EndedMsg{ v->index, v->generation });
    }

    void freeVoice(Voice& v)
    {
        if (v.inited)
        {
            ma_sound_uninit(&v.sound);
            v.inited = false;
        }
        v.inUse = false;
        v.clip.reset();
    }

    bool findDevice(const std::string& name, ma_device_id& outId)
    {
        if (!contextOk || name.empty()) return false;
        ma_device_info* infos = nullptr;
        ma_uint32 count = 0;
        if (ma_context_get_devices(&context, &infos, &count, nullptr, nullptr) != MA_SUCCESS) return false;
        for (ma_uint32 i = 0; i < count; i++)
            if (name == infos[i].name) { outId = infos[i].id; return true; }
        return false;
    }
};

// ---------------------------------------------------------------- AudioEngine
AudioEngine::AudioEngine() : impl_(std::make_unique<Impl>()) {}

AudioEngine::~AudioEngine() { shutdown(); }

bool AudioEngine::init(const AudioSettings& s)
{
    if (impl_->engineOk) return true;
    impl_->settings = s;
    impl_->masterVolume = s.masterVolume;

    ma_context_config cc = ma_context_config_init();
    ma_result r;
    if (s.nullBackend)
    {
        ma_backend backends[] = { ma_backend_null };
        r = ma_context_init(backends, 1, &cc, &impl_->context);
    }
    else r = ma_context_init(nullptr, 0, &cc, &impl_->context);
    if (r != MA_SUCCESS)
    {
        OLOGW("Audio", "ma_context_init failed (" << r << ") - running silent");
        return false;
    }
    impl_->contextOk = true;

    ma_engine_config ec = ma_engine_config_init();
    ec.pContext = &impl_->context;
    ec.periodSizeInFrames = (ma_uint32)std::max(64, s.periodSizeInFrames);
    ec.channels = 2;
    ma_device_id devId;
    if (impl_->findDevice(s.deviceName, devId)) ec.pPlaybackDeviceID = &devId;
    r = ma_engine_init(&ec, &impl_->engine);
    if (r != MA_SUCCESS && ec.pPlaybackDeviceID)
    {
        OLOGW("Audio", "Device '" << s.deviceName << "' unavailable, falling back to the default device");
        ec.pPlaybackDeviceID = nullptr;
        r = ma_engine_init(&ec, &impl_->engine);
    }
    if (r != MA_SUCCESS)
    {
        OLOGW("Audio", "ma_engine_init failed (" << r << ") - running silent");
        ma_context_uninit(&impl_->context);
        impl_->contextOk = false;
        return false;
    }
    impl_->engineOk = true;
    ma_engine_set_volume(&impl_->engine, impl_->masterVolume);

    impl_->voices.clear();
    int n = std::max(1, s.maxVoices);
    for (int i = 0; i < n; i++)
    {
        auto v = std::make_unique<Voice>();
        v->owner = impl_.get();
        v->index = i;
        impl_->voices.push_back(std::move(v));
    }
    OLOG("Audio", "Playback device: " << deviceName() << " @ " << deviceSampleRate() << " Hz, period "
                  << ec.periodSizeInFrames << " frames, " << n << " voices");
    return true;
}

void AudioEngine::shutdown()
{
    if (!impl_) return;
    impl_->preview.reset();
    if (impl_->engineOk)
    {
        for (auto& v : impl_->voices)
            if (v->inUse) impl_->freeVoice(*v);
        ma_engine_uninit(&impl_->engine);
        impl_->engineOk = false;
    }
    if (impl_->contextOk)
    {
        ma_context_uninit(&impl_->context);
        impl_->contextOk = false;
    }
    impl_->voices.clear();
}

bool AudioEngine::reinit(const AudioSettings& s)
{
    shutdown();
    return init(s);
}

bool AudioEngine::ok() const { return impl_->engineOk; }

std::string AudioEngine::deviceName() const
{
    if (!impl_->engineOk) return "";
    ma_device* dev = ma_engine_get_device(&impl_->engine);
    if (!dev) return "";
    char name[MA_MAX_DEVICE_NAME_LENGTH + 1] = { 0 };
    ma_device_get_name(dev, ma_device_type_playback, name, sizeof(name), nullptr);
    return name;
}

int AudioEngine::deviceSampleRate() const
{
    if (!impl_->engineOk) return 0;
    return (int)ma_engine_get_sample_rate(&impl_->engine);
}

const AudioSettings& AudioEngine::settings() const { return impl_->settings; }

std::vector<AudioDeviceInfo> AudioEngine::enumerateDevices() const
{
    std::vector<AudioDeviceInfo> out;
    if (!impl_->contextOk) return out;
    ma_device_info* infos = nullptr;
    ma_uint32 count = 0;
    if (ma_context_get_devices(&impl_->context, &infos, &count, nullptr, nullptr) != MA_SUCCESS) return out;
    for (ma_uint32 i = 0; i < count; i++) out.push_back({ infos[i].name, infos[i].isDefault != 0 });
    return out;
}

VoiceHandle AudioEngine::play(std::shared_ptr<const organic::AudioBuffer> clip, const PlayParams& p)
{
    VoiceHandle h;
    if (!impl_->engineOk || !clip || clip->frames() <= 0 || clip->channels <= 0) return h;

    // pick a free slot, else steal the oldest
    Voice* slot = nullptr;
    for (auto& v : impl_->voices) if (!v->inUse) { slot = v.get(); break; }
    if (!slot)
    {
        for (auto& v : impl_->voices)
            if (!slot || v->startOrder < slot->startOrder) slot = v.get();
        if (slot)
        {
            impl_->pendingMain.push_back({ VoiceHandle{ slot->index, slot->generation }, EndReason::Stolen });
            impl_->freeVoice(*slot);
        }
    }
    if (!slot) return h;

    uint64_t frames = (uint64_t)clip->frames();
    uint64_t f0 = std::min(p.startFrame, frames);
    uint64_t f1 = (p.endFrame > f0 && p.endFrame <= frames) ? p.endFrame : frames;

    if (ma_audio_buffer_ref_init(ma_format_f32, (ma_uint32)clip->channels, clip->samples.data(), frames, &slot->ref) != MA_SUCCESS)
        return h;
    slot->ref.sampleRate = (ma_uint32)clip->sampleRate;
    if (f0 > 0 || f1 < frames) ma_data_source_set_range_in_pcm_frames(&slot->ref, f0, f1);
    ma_uint32 flags = MA_SOUND_FLAG_NO_PITCH | MA_SOUND_FLAG_NO_SPATIALIZATION;
    if (ma_sound_init_from_data_source(&impl_->engine, &slot->ref, flags, nullptr, &slot->sound) != MA_SUCCESS)
        return h;
    slot->inited = true;
    slot->inUse = true;
    slot->generation++;
    slot->startOrder = ++impl_->playCounter;
    slot->clip = std::move(clip);
    ma_sound_set_end_callback(&slot->sound, &Impl::endCallback, slot);
    ma_sound_set_looping(&slot->sound, p.loop ? MA_TRUE : MA_FALSE);
    ma_sound_set_volume(&slot->sound, std::max(0.f, p.gain));
    if (p.fadeInMs > 0) ma_sound_set_fade_in_milliseconds(&slot->sound, 0.f, std::max(0.f, p.gain), (ma_uint64)p.fadeInMs);
    if (p.seekFrame > 0 && p.seekFrame < f1 - f0) ma_sound_seek_to_pcm_frame(&slot->sound, p.seekFrame);
    ma_sound_start(&slot->sound);
    h.index = slot->index;
    h.generation = slot->generation;
    return h;
}

void AudioEngine::stop(VoiceHandle h, int fadeOutMs)
{
    if (!impl_->engineOk || !h.valid() || h.index >= (int)impl_->voices.size()) return;
    Voice& v = *impl_->voices[(size_t)h.index];
    if (!v.inUse || v.generation != h.generation) return;
    if (fadeOutMs > 0 && ma_sound_is_playing(&v.sound))
    {
        // the slot is released by drainEvents once the fade completes (sound no longer playing)
        ma_sound_stop_with_fade_in_milliseconds(&v.sound, (ma_uint64)fadeOutMs);
    }
    else
    {
        ma_sound_stop(&v.sound);
        impl_->pendingMain.push_back({ h, EndReason::Stopped });
        impl_->freeVoice(v);
    }
}

void AudioEngine::stopAll(int fadeOutMs)
{
    if (!impl_->engineOk) return;
    for (auto& v : impl_->voices)
        if (v->inUse) stop(VoiceHandle{ v->index, v->generation }, fadeOutMs);
}

VoiceStatus AudioEngine::status(VoiceHandle h) const
{
    VoiceStatus s;
    if (!impl_->engineOk || !h.valid() || h.index >= (int)impl_->voices.size()) return s;
    const Voice& v = *impl_->voices[(size_t)h.index];
    if (!v.inUse || v.generation != h.generation) return s;
    s.active = true;
    s.playing = ma_sound_is_playing(&v.sound) != 0;
    ma_uint64 cur = 0, len = 0;
    ma_sound_get_cursor_in_pcm_frames(&v.sound, &cur);
    ma_sound_get_length_in_pcm_frames(&v.sound, &len);
    s.cursorFrames = cur;
    s.lengthFrames = len;
    s.progress01 = len > 0 ? std::min(1.f, (float)((double)cur / (double)len)) : 0.f;
    return s;
}

int AudioEngine::activeVoiceCount() const
{
    int n = 0;
    for (auto& v : impl_->voices) if (v->inUse) n++;
    return n;
}

void AudioEngine::setMasterVolume(float vol)
{
    impl_->masterVolume = std::max(0.f, std::min(1.f, vol));
    if (impl_->engineOk) ma_engine_set_volume(&impl_->engine, impl_->masterVolume);
}

float AudioEngine::masterVolume() const { return impl_->masterVolume; }

void AudioEngine::drainEvents(const std::function<void(const VoiceEnded&)>& fn)
{
    // 1. main-thread generated (stop / steal)
    for (auto& e : impl_->pendingMain) fn(e);
    impl_->pendingMain.clear();
    if (!impl_->engineOk) return;
    // 2. natural ends from the audio thread
    EndedMsg m;
    while (impl_->ended.pop(m))
    {
        if (m.index < 0 || m.index >= (int)impl_->voices.size()) continue;
        Voice& v = *impl_->voices[(size_t)m.index];
        if (!v.inUse || v.generation != m.generation) continue;
        VoiceHandle h{ v.index, v.generation };
        impl_->freeVoice(v);
        fn({ h, EndReason::Natural });
    }
    // 3. safety net: voices no longer playing (fade-stop completed, or a missed callback)
    for (auto& vp : impl_->voices)
    {
        Voice& v = *vp;
        if (!v.inUse) continue;
        if (!ma_sound_is_playing(&v.sound))
        {
            bool atEnd = ma_sound_at_end(&v.sound) != 0;
            VoiceHandle h{ v.index, v.generation };
            impl_->freeVoice(v);
            fn({ h, atEnd ? EndReason::Natural : EndReason::Stopped });
        }
    }
}

PreviewPlayer& AudioEngine::preview()
{
    if (!impl_->preview) impl_->preview = std::make_unique<PreviewPlayer>(*this);
    return *impl_->preview;
}

// ---------------------------------------------------------------- PreviewPlayer
struct PreviewPlayer::Impl
{
    ma_sound sound{};
    ma_audio_buffer_ref ref{};
    bool inited = false;
    uint64_t f0 = 0, f1 = 0;
};

PreviewPlayer::PreviewPlayer(AudioEngine& e) : engine_(e), impl_(std::make_unique<Impl>()) {}

PreviewPlayer::~PreviewPlayer()
{
    if (impl_->inited) ma_sound_uninit(&impl_->sound);
}

void PreviewPlayer::setSource(std::shared_ptr<const organic::AudioBuffer> src)
{
    if (impl_->inited)
    {
        ma_sound_uninit(&impl_->sound);
        impl_->inited = false;
    }
    source_ = std::move(src);
    if (!source_ || source_->frames() <= 0 || !engine_.impl_->engineOk) return;
    if (ma_audio_buffer_ref_init(ma_format_f32, (ma_uint32)source_->channels, source_->samples.data(),
                                 (ma_uint64)source_->frames(), &impl_->ref) != MA_SUCCESS) return;
    impl_->ref.sampleRate = (ma_uint32)source_->sampleRate;
    ma_uint32 flags = MA_SOUND_FLAG_NO_PITCH | MA_SOUND_FLAG_NO_SPATIALIZATION;
    if (ma_sound_init_from_data_source(&engine_.impl_->engine, &impl_->ref, flags, nullptr, &impl_->sound) != MA_SUCCESS) return;
    impl_->inited = true;
    ma_sound_set_volume(&impl_->sound, gain_);
    ma_sound_set_looping(&impl_->sound, loop_ ? MA_TRUE : MA_FALSE);
    if (t1_ <= t0_) { t0_ = 0; t1_ = source_->duration(); }
    applyRegion();
}

void PreviewPlayer::applyRegion()
{
    if (!impl_->inited || !source_) return;
    uint64_t frames = (uint64_t)source_->frames();
    uint64_t f0 = std::min(frames, (uint64_t)std::max(0.0, std::llround(t0_ * source_->sampleRate) * 1.0));
    uint64_t f1 = std::min(frames, (uint64_t)std::max(0.0, std::llround(t1_ * source_->sampleRate) * 1.0));
    if (f1 <= f0) f1 = std::min(frames, f0 + 1);
    impl_->f0 = f0;
    impl_->f1 = f1;
    ma_data_source_set_range_in_pcm_frames(&impl_->ref, f0, f1);
    ma_data_source_set_loop_point_in_pcm_frames(&impl_->ref, 0, f1 - f0);
}

void PreviewPlayer::setRegion(double t0, double t1)
{
    if (t1 < t0) std::swap(t0, t1);
    t0_ = t0;
    t1_ = t1;
    applyRegion();
}

void PreviewPlayer::setLoop(bool loop)
{
    loop_ = loop;
    if (impl_->inited) ma_sound_set_looping(&impl_->sound, loop ? MA_TRUE : MA_FALSE);
}

void PreviewPlayer::setGain(float g)
{
    gain_ = std::max(0.f, g);
    if (impl_->inited) ma_sound_set_volume(&impl_->sound, gain_);
}

void PreviewPlayer::play()
{
    if (!impl_->inited) return;
    ma_sound_stop(&impl_->sound);
    ma_sound_seek_to_pcm_frame(&impl_->sound, 0);
    ma_sound_start(&impl_->sound);
}

void PreviewPlayer::playFrom(double tSec)
{
    if (!impl_->inited || !source_) return;
    double rel = std::max(0.0, tSec - t0_);
    uint64_t f = std::min(impl_->f1 - impl_->f0, (uint64_t)std::llround(rel * source_->sampleRate));
    ma_sound_stop(&impl_->sound);
    ma_sound_seek_to_pcm_frame(&impl_->sound, f);
    ma_sound_start(&impl_->sound);
}

void PreviewPlayer::stop()
{
    if (impl_->inited) ma_sound_stop(&impl_->sound);
}

bool PreviewPlayer::isPlaying() const
{
    return impl_->inited && ma_sound_is_playing(&impl_->sound) && !ma_sound_at_end(&impl_->sound);
}

double PreviewPlayer::playhead() const
{
    if (!impl_->inited || !source_ || source_->sampleRate <= 0) return t0_;
    ma_uint64 cur = 0;
    ma_sound_get_cursor_in_pcm_frames(&impl_->sound, &cur);
    return (double)(impl_->f0 + cur) / source_->sampleRate;
}

} // namespace evobox
