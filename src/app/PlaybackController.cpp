#include "app/PlaybackController.h"
#include "util/TimeFormat.h"
#include <algorithm>
#include <cmath>

namespace evobox
{

PlaybackController::PlaybackController(Project& project, AudioEngine& audio, TriggerController& trigger)
    : project_(project), audio_(audio), trigger_(trigger)
{
}

bool PlaybackController::play(Uid soundUid, TriggerSource src)
{
    Sound* s = project_.sounds.find(soundUid);
    if (!s) return false;
    return play(*s, src);
}

bool PlaybackController::play(Sound& s, TriggerSource src, const PlayOptions& opts)
{
    double now = nowSeconds();
    s.rt.lastTriggerTime = now;
    // policy (effects always play on top and stack; only "Stop others" interrupts them)
    switch (project_.settings.playbackPolicy())
    {
    case PlaybackPolicy::StopOthers:
        for (auto& v : voices_)
            if (v.soundUid != s.uid) audio_.stop(v.handle, 10);
        break;
    case PlaybackPolicy::MusicExclusive:
        if (!s.isEffect())
        {
            // a new music replaces the music that was playing — including an earlier voice of
            // itself, so pressing a music tile again restarts it; effects keep playing
            for (auto& v : voices_)
            {
                Sound* other = project_.sounds.find(v.soundUid);
                if (!other || !other->isEffect()) audio_.stop(v.handle, 10);
            }
        }
        break;
    case PlaybackPolicy::Overlap:
        break;
    }
    if (!s.hasClip())
    {
        OLOGW("Audio", "'" << s.niceName << "' has no rendered clip yet (" <<
              (s.rt.mediaStatus == MediaStatus::Decoding ? "decoding" : "missing media") << ")");
        // OSC still fires so the trigger is not lost: begin + immediate end
        SessionId sid = trigger_.begin(s, src);
        trigger_.end(sid, SessionEndReason::Finished);
        return false;
    }
    PlayParams p;
    p.gain = 1.f;
    p.loop = opts.loop;
    if (opts.startSec > 0 && s.rt.clip->sampleRate > 0)
    {
        uint64_t frames = (uint64_t)s.rt.clip->frames();
        uint64_t f0 = (uint64_t)std::llround(opts.startSec * s.rt.clip->sampleRate);
        if (f0 < frames) p.seekFrame = f0; // progress stays relative to the whole clip
    }
    VoiceHandle h = audio_.play(s.rt.clip, p);
    SessionId sid = trigger_.begin(s, src);
    if (!h.valid())
    {
        // no audio device: behave as a zero-length sound
        trigger_.end(sid, SessionEndReason::Finished);
        return false;
    }
    voices_.push_back({ h, s.uid, sid, now, s.rt.clip, s.rt.clipSourceStart });
    s.rt.activeVoices++;
    s.rt.progress01 = 0.f;
    return true;
}

void PlaybackController::stop(Uid soundUid, int fadeOutMs)
{
    for (auto& v : voices_)
        if (v.soundUid == soundUid) audio_.stop(v.handle, fadeOutMs);
}

void PlaybackController::toggle(Uid soundUid, TriggerSource src)
{
    // effects stack: "toggle" on a playing effect fires it again (stop via Stop All / context menu)
    Sound* s = project_.sounds.find(soundUid);
    if (isPlaying(soundUid) && !(s && s->isEffect())) stop(soundUid);
    else play(soundUid, src);
}

void PlaybackController::stopAll(int fadeOutMs)
{
    if (fadeOutMs < 0) fadeOutMs = project_.settings.stopAllFadeMs();
    audio_.stopAll(fadeOutMs);
    audio_.preview().stop();
}

bool PlaybackController::isPlaying(Uid soundUid) const
{
    for (auto& v : voices_) if (v.soundUid == soundUid) return true;
    return false;
}

void PlaybackController::onSoundRemoved(Uid soundUid)
{
    for (auto it = voices_.begin(); it != voices_.end();)
    {
        if (it->soundUid == soundUid)
        {
            audio_.stop(it->handle, 0);
            trigger_.end(it->session, SessionEndReason::Cancelled, false);
            it = voices_.erase(it);
        }
        else ++it;
    }
}

void PlaybackController::tick(double now)
{
    audio_.drainEvents([&](const VoiceEnded& e)
    {
        for (auto it = voices_.begin(); it != voices_.end(); ++it)
        {
            if (!(it->handle == e.handle)) continue;
            ActiveVoice v = *it;
            voices_.erase(it);
            if (Sound* s = project_.sounds.find(v.soundUid))
            {
                s->rt.activeVoices = std::max(0, s->rt.activeVoices - 1);
                if (s->rt.activeVoices == 0) s->rt.progress01 = 0.f;
            }
            bool manual = e.reason == EndReason::Stopped || e.reason == EndReason::Stolen;
            bool fireEnd = !manual || project_.settings.afterPlayOnStop();
            if (e.reason == EndReason::Shutdown) fireEnd = false;
            trigger_.end(v.session, manual ? SessionEndReason::Stopped : SessionEndReason::Finished, fireEnd);
            break;
        }
    });
    // progress per voice (start order) for the Clip Editor's playheads; the most recent one is the
    // tile's progress. The source position comes from the voice's OWN clip (cursor within the
    // buffer it plays + where that buffer starts in the source), never from the live trim handles.
    for (Sound* s : project_.sounds.sounds())
        if (!s->rt.voiceProgress.empty()) s->rt.voiceProgress.clear();
    for (auto& v : voices_)
    {
        Sound* s = project_.sounds.find(v.soundUid);
        if (!s) continue;
        VoiceStatus st = audio_.status(v.handle);
        if (!st.active) continue;
        Sound::VoiceProgress vp;
        vp.progress01 = st.progress01;
        double inClip = 0.0;
        if (v.clip && v.clip->sampleRate > 0)
            inClip = std::clamp((double)st.cursorFrames / (double)v.clip->sampleRate, 0.0, v.clip->duration());
        vp.sourceSec = v.clipSourceStart + inClip;
        s->rt.voiceProgress.push_back(vp);
        s->rt.progress01 = st.progress01;
    }
    (void)now;
}

} // namespace evobox
