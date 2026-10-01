#include "media/FFmpegDecoder.h"
#include "util/ScopeExit.h"
#include "util/Strings.h"
#include <cmath>
#include <cstring>
#include <mutex>

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/channel_layout.h>
#include <libavutil/log.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
}

namespace evobox
{

namespace
{

std::string avErr(int code)
{
    char buf[AV_ERROR_MAX_STRING_SIZE] = { 0 };
    av_strerror(code, buf, sizeof(buf));
    return buf;
}

struct Fail
{
    std::string* err;
    bool operator()(const std::string& m) const { if (err) *err = m; return false; }
};

std::function<void(int, const std::string&)>& logSink()
{
    static std::function<void(int, const std::string&)> s;
    return s;
}
std::mutex& logMutex() { static std::mutex m; return m; }

void avLogCallback(void* ptr, int level, const char* fmt, va_list vl)
{
    if (level > AV_LOG_WARNING) return;
    char line[1024];
    int printPrefix = 1;
    av_log_format_line(ptr, level, fmt, vl, line, sizeof(line), &printPrefix);
    std::string s(line);
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
    std::lock_guard<std::mutex> lk(logMutex());
    if (logSink()) logSink()(level, s);
}

// avformat_open_input with a fallback: when content probing cannot decide (very short MP3 clips —
// a handful of frames — score no better than raw-video probes, and a tie means "unknown format"),
// retry with the demuxer named after the file extension. A mislabelled file still fails, inside
// that demuxer.
int openInput(AVFormatContext** fmt, const std::string& path)
{
    int rc = avformat_open_input(fmt, path.c_str(), nullptr, nullptr);
    if (rc != AVERROR_INVALIDDATA) return rc;
    std::string ext = str::fileExtLower(path);
    if (ext.size() < 2) return rc;
    const AVInputFormat* byExt = av_find_input_format(ext.c_str() + 1); // "mp3", "wav", "flac", "mp4"...
    if (!byExt) return rc;
    *fmt = nullptr; // the failed call freed the context
    int rc2 = avformat_open_input(fmt, path.c_str(), byExt, nullptr);
    return rc2 < 0 ? rc : rc2;
}

// Opens the container + best audio stream + decoder. Everything is released by the guard.
struct AudioReader
{
    AVFormatContext* fmt = nullptr;
    AVCodecContext* ctx = nullptr;
    SwrContext* swr = nullptr;
    AVPacket* pkt = nullptr;
    AVFrame* frame = nullptr;
    int streamIndex = -1;
    int outChannels = 0;
    int outRate = 0;
    AVChannelLayout outLayout{};
    // swr input configuration (from the first decoded frame)
    bool swrReady = false;
    int inRate = 0;
    AVSampleFormat inFmt = AV_SAMPLE_FMT_NONE;
    AVChannelLayout inLayout{};

    ~AudioReader()
    {
        if (swr) swr_free(&swr);
        if (frame) av_frame_free(&frame);
        if (pkt) av_packet_free(&pkt);
        if (ctx) avcodec_free_context(&ctx);
        if (fmt) avformat_close_input(&fmt);
        av_channel_layout_uninit(&outLayout);
        av_channel_layout_uninit(&inLayout);
    }

    bool open(const std::string& path, std::string* err)
    {
        Fail fail{ err };
        int rc = openInput(&fmt, path);
        if (rc < 0) return fail("cannot open '" + path + "': " + avErr(rc));
        rc = avformat_find_stream_info(fmt, nullptr);
        if (rc < 0) return fail("cannot read stream info: " + avErr(rc));
        const AVCodec* codec = nullptr;
        streamIndex = av_find_best_stream(fmt, AVMEDIA_TYPE_AUDIO, -1, -1, &codec, 0);
        if (streamIndex < 0 || !codec) return fail("no audio stream found");
        AVStream* st = fmt->streams[streamIndex];
        ctx = avcodec_alloc_context3(codec);
        if (!ctx) return fail("cannot allocate decoder");
        rc = avcodec_parameters_to_context(ctx, st->codecpar);
        if (rc < 0) return fail("cannot configure decoder: " + avErr(rc));
        ctx->pkt_timebase = st->time_base;
        rc = avcodec_open2(ctx, codec, nullptr);
        if (rc < 0) return fail("cannot open decoder: " + avErr(rc));
        pkt = av_packet_alloc();
        frame = av_frame_alloc();
        if (!pkt || !frame) return fail("out of memory");

        int inCh = ctx->ch_layout.nb_channels;
        if (inCh <= 0) inCh = st->codecpar->ch_layout.nb_channels;
        outChannels = inCh <= 1 ? 1 : 2;
        outRate = ctx->sample_rate > 0 ? ctx->sample_rate : (st->codecpar->sample_rate > 0 ? st->codecpar->sample_rate : 48000);
        av_channel_layout_default(&outLayout, outChannels);
        return true;
    }

    double duration() const
    {
        if (!fmt) return 0;
        AVStream* st = fmt->streams[streamIndex];
        if (st->duration > 0 && st->duration != AV_NOPTS_VALUE)
            return (double)st->duration * av_q2d(st->time_base);
        if (fmt->duration > 0 && fmt->duration != AV_NOPTS_VALUE)
            return (double)fmt->duration / AV_TIME_BASE;
        return 0;
    }

    bool ensureSwr(const AVFrame* f, std::string* err)
    {
        Fail fail{ err };
        bool same = swrReady && f->sample_rate == inRate && (AVSampleFormat)f->format == inFmt &&
                    av_channel_layout_compare(&f->ch_layout, &inLayout) == 0;
        if (same) return true;
        if (swr) swr_free(&swr);
        av_channel_layout_uninit(&inLayout);
        av_channel_layout_copy(&inLayout, &f->ch_layout);
        if (inLayout.nb_channels <= 0) av_channel_layout_default(&inLayout, 1);
        inRate = f->sample_rate > 0 ? f->sample_rate : outRate;
        inFmt = (AVSampleFormat)f->format;
        int rc = swr_alloc_set_opts2(&swr, &outLayout, AV_SAMPLE_FMT_FLT, outRate,
                                     &inLayout, inFmt, inRate, 0, nullptr);
        if (rc < 0 || !swr) return fail("cannot create resampler: " + avErr(rc));
        rc = swr_init(swr);
        if (rc < 0) return fail("cannot init resampler: " + avErr(rc));
        swrReady = true;
        return true;
    }

    // Converts one decoded frame (or flushes with nullptr) into `out`.
    bool convert(const AVFrame* f, std::vector<float>& out, std::string* err)
    {
        if (f && !ensureSwr(f, err)) return false;
        if (!swr) return true;
        int inSamples = f ? f->nb_samples : 0;
        int outCount = swr_get_out_samples(swr, inSamples);
        if (outCount <= 0) return true;
        size_t before = out.size();
        out.resize(before + (size_t)outCount * outChannels);
        uint8_t* outPtr = (uint8_t*)(out.data() + before);
        int got = swr_convert(swr, &outPtr, outCount, f ? (const uint8_t**)f->extended_data : nullptr, inSamples);
        if (got < 0) { out.resize(before); Fail fail{ err }; return fail("resample error: " + avErr(got)); }
        out.resize(before + (size_t)got * outChannels);
        return true;
    }

    // Decode loop. `stopAt` seconds (<0 = whole file). Returns false on error (not on EOF).
    bool decode(std::vector<float>& out, std::string* err, const CancelToken* cancel, std::atomic<float>* progress,
                double startAt, double stopAt, uint64_t maxSamples)
    {
        Fail fail{ err };
        AVStream* st = fmt->streams[streamIndex];
        double total = duration();
        bool done = false;
        int64_t skipFrames = 0; // frames to drop at the start (seek lands on a keyframe before startAt)
        bool firstFrame = true;
        std::string localErr;
        // Pulls every frame the decoder has ready. Returns false on error; sets `done` on EOF /
        // stop time / budget overflow.
        auto drainFrames = [&]() -> bool
        {
            while (true)
            {
                int rc = avcodec_receive_frame(ctx, frame);
                if (rc == AVERROR(EAGAIN)) return true;
                if (rc == AVERROR_EOF) { done = true; return true; }
                if (rc < 0) return fail("decode error: " + avErr(rc));
                if (firstFrame && startAt > 0)
                {
                    double pts = frame->pts != AV_NOPTS_VALUE ? frame->pts * av_q2d(st->time_base) : startAt;
                    if (pts < startAt) skipFrames = (int64_t)std::llround((startAt - pts) * outRate);
                    firstFrame = false;
                }
                bool ok = convert(frame, out, err);
                av_frame_unref(frame);
                if (!ok) return false;
                double decodedSec = (double)out.size() / outChannels / outRate;
                if (progress && total > 0) progress->store((float)std::min(1.0, (decodedSec + startAt) / total));
                if (stopAt >= 0 && decodedSec >= (stopAt - startAt) + (double)skipFrames / outRate) { done = true; return true; }
                if (maxSamples && out.size() > maxSamples) return fail("media exceeds the decode budget");
            }
        };
        while (!done)
        {
            if (cancel && cancel->cancelled()) return fail("cancelled");
            int rc = av_read_frame(fmt, pkt);
            bool eof = rc == AVERROR_EOF;
            if (rc < 0 && !eof) return fail("read error: " + avErr(rc));
            if (!eof && pkt->stream_index != streamIndex) { av_packet_unref(pkt); continue; }
            // send (retrying after a drain when the decoder's input queue is full)
            for (int attempt = 0; attempt < 64; attempt++)
            {
                rc = avcodec_send_packet(ctx, eof ? nullptr : pkt);
                if (rc == AVERROR(EAGAIN)) { if (!drainFrames()) { av_packet_unref(pkt); return false; } continue; }
                break;
            }
            if (!eof) av_packet_unref(pkt);
            if (rc < 0 && rc != AVERROR_EOF && rc != AVERROR(EAGAIN)) return fail("decode error: " + avErr(rc));
            if (!drainFrames()) return false;
            if (eof) done = true;
        }
        // flush resampler
        convert(nullptr, out, err);
        if (skipFrames > 0)
        {
            size_t skip = std::min(out.size(), (size_t)skipFrames * outChannels);
            out.erase(out.begin(), out.begin() + (long)skip);
        }
        if (stopAt >= 0)
        {
            size_t maxOut = (size_t)std::llround((stopAt - startAt) * outRate) * outChannels;
            if (out.size() > maxOut) out.resize(maxOut);
        }
        return true;
    }
};

} // namespace

// ---------------------------------------------------------------- public API
bool FFmpegDecoder::probe(const std::string& path, MediaRef& out, std::string* err)
{
    Fail fail{ err };
    AVFormatContext* fmt = nullptr;
    int rc = openInput(&fmt, path);
    if (rc < 0) return fail("cannot open '" + path + "': " + avErr(rc));
    EVOBOX_SCOPE_EXIT(avformat_close_input(&fmt));
    rc = avformat_find_stream_info(fmt, nullptr);
    if (rc < 0) return fail("cannot read stream info: " + avErr(rc));
    const AVCodec* codec = nullptr;
    int idx = av_find_best_stream(fmt, AVMEDIA_TYPE_AUDIO, -1, -1, &codec, 0);
    if (idx < 0) return fail("no audio stream found");
    AVStream* st = fmt->streams[idx];
    out.path = path;
    out.container = fmt->iformat ? fmt->iformat->name : "";
    out.codec = codec ? codec->name : "";
    out.sampleRate = st->codecpar->sample_rate;
    out.channels = st->codecpar->ch_layout.nb_channels;
    if (st->duration > 0 && st->duration != AV_NOPTS_VALUE) out.durationSec = (double)st->duration * av_q2d(st->time_base);
    else if (fmt->duration > 0 && fmt->duration != AV_NOPTS_VALUE) out.durationSec = (double)fmt->duration / AV_TIME_BASE;
    out.hasVideo = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0) >= 0;
    if (fmt->pb) { int64_t sz = avio_size(fmt->pb); if (sz > 0) out.sizeBytes = (uint64_t)sz; }
    return true;
}

bool FFmpegDecoder::decodeAll(const std::string& path, organic::AudioBuffer& out, std::string* err,
                              const CancelToken* cancel, std::atomic<float>* progress, const DecodeBudget& budget)
{
    Fail fail{ err };
    AudioReader r;
    if (!r.open(path, err)) return false;
    double dur = r.duration();
    if (budget.maxDurationSec > 0 && dur > budget.maxDurationSec)
        return fail("media is longer than the decode budget (" + std::to_string((int)(budget.maxDurationSec / 60)) + " min)");
    uint64_t maxSamples = budget.maxBytes ? budget.maxBytes / sizeof(float) : 0;
    std::vector<float> samples;
    if (dur > 0) samples.reserve((size_t)std::min<double>(dur * r.outRate * r.outChannels + 4096, (double)maxSamples));
    if (!r.decode(samples, err, cancel, progress, 0.0, -1.0, maxSamples)) return false;
    if (samples.empty()) return fail("no audio decoded");
    out.sampleRate = r.outRate;
    out.channels = r.outChannels;
    out.samples = std::move(samples);
    if (progress) progress->store(1.f);
    return true;
}

bool FFmpegDecoder::decodeRange(const std::string& path, double t0, double t1, organic::AudioBuffer& out,
                                std::string* err, const CancelToken* cancel)
{
    if (t1 < t0) std::swap(t0, t1);
    AudioReader r;
    if (!r.open(path, err)) return false;
    if (t0 > 0)
    {
        AVStream* st = r.fmt->streams[r.streamIndex];
        int64_t ts = (int64_t)llround(t0 / av_q2d(st->time_base));
        int rc = avformat_seek_file(r.fmt, r.streamIndex, INT64_MIN, ts, ts, AVSEEK_FLAG_BACKWARD);
        if (rc < 0)
        {
            // fall back to decoding from the start
            t0 = 0;
        }
        else avcodec_flush_buffers(r.ctx);
    }
    std::vector<float> samples;
    if (!r.decode(samples, err, cancel, nullptr, t0, t1, 0)) return false;
    out.sampleRate = r.outRate;
    out.channels = r.outChannels;
    out.samples = std::move(samples);
    return true;
}

bool FFmpegDecoder::decodeFallback(const std::string& path, organic::AudioBuffer& out, std::string* err)
{
    return decodeAll(path, out, err);
}

void FFmpegDecoder::installLogSink(std::function<void(int, const std::string&)> sink)
{
    {
        std::lock_guard<std::mutex> lk(logMutex());
        logSink() = std::move(sink);
    }
    av_log_set_level(AV_LOG_WARNING);
    av_log_set_callback(avLogCallback);
}

std::string FFmpegDecoder::versionString()
{
    return std::string("libavformat ") + std::to_string(LIBAVFORMAT_VERSION_MAJOR) + "." + std::to_string(LIBAVFORMAT_VERSION_MINOR) +
           " / libavcodec " + std::to_string(LIBAVCODEC_VERSION_MAJOR) + "." + std::to_string(LIBAVCODEC_VERSION_MINOR);
}

} // namespace evobox
