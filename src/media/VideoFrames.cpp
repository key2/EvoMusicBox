#include "media/VideoFrames.h"
#include "util/ScopeExit.h"
#include <algorithm>
#include <cmath>

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
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

struct VideoReader
{
    AVFormatContext* fmt = nullptr;
    AVCodecContext* ctx = nullptr;
    AVPacket* pkt = nullptr;
    AVFrame* frame = nullptr;
    SwsContext* sws = nullptr;
    int swsW = 0, swsH = 0, swsFmt = -1, dstW = 0, dstH = 0;
    int streamIndex = -1;

    ~VideoReader()
    {
        if (sws) sws_freeContext(sws);
        if (frame) av_frame_free(&frame);
        if (pkt) av_packet_free(&pkt);
        if (ctx) avcodec_free_context(&ctx);
        if (fmt) avformat_close_input(&fmt);
    }

    bool open(const std::string& path, std::string* err)
    {
        auto fail = [&](const std::string& m) { if (err) *err = m; return false; };
        int rc = avformat_open_input(&fmt, path.c_str(), nullptr, nullptr);
        if (rc < 0) return fail("cannot open '" + path + "': " + avErr(rc));
        rc = avformat_find_stream_info(fmt, nullptr);
        if (rc < 0) return fail("cannot read stream info: " + avErr(rc));
        const AVCodec* codec = nullptr;
        streamIndex = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, &codec, 0);
        if (streamIndex < 0 || !codec) return fail("no video stream found");
        AVStream* st = fmt->streams[streamIndex];
        // cover art / attached pictures are not movies
        if (st->disposition & AV_DISPOSITION_ATTACHED_PIC) return fail("no video stream found (attached picture only)");
        ctx = avcodec_alloc_context3(codec);
        if (!ctx) return fail("cannot allocate video decoder");
        rc = avcodec_parameters_to_context(ctx, st->codecpar);
        if (rc < 0) return fail("cannot configure video decoder: " + avErr(rc));
        ctx->pkt_timebase = st->time_base;
        ctx->thread_count = 2;
        rc = avcodec_open2(ctx, codec, nullptr);
        if (rc < 0) return fail("cannot open video decoder: " + avErr(rc));
        pkt = av_packet_alloc();
        frame = av_frame_alloc();
        if (!pkt || !frame) return fail("out of memory");
        return true;
    }

    double duration() const
    {
        AVStream* st = fmt->streams[streamIndex];
        if (st->duration > 0 && st->duration != AV_NOPTS_VALUE) return (double)st->duration * av_q2d(st->time_base);
        if (fmt->duration > 0 && fmt->duration != AV_NOPTS_VALUE) return (double)fmt->duration / AV_TIME_BASE;
        return 0;
    }

    double frameTime(const AVFrame* f) const
    {
        AVStream* st = fmt->streams[streamIndex];
        int64_t pts = f->best_effort_timestamp != AV_NOPTS_VALUE ? f->best_effort_timestamp : f->pts;
        if (pts == AV_NOPTS_VALUE) return -1;
        double t = (double)pts * av_q2d(st->time_base);
        if (st->start_time != AV_NOPTS_VALUE) t -= (double)st->start_time * av_q2d(st->time_base);
        return t;
    }

    bool seekTo(double t)
    {
        AVStream* st = fmt->streams[streamIndex];
        double base = st->start_time != AV_NOPTS_VALUE ? (double)st->start_time * av_q2d(st->time_base) : 0.0;
        int64_t ts = (int64_t)std::llround((t + base) / av_q2d(st->time_base));
        int rc = avformat_seek_file(fmt, streamIndex, INT64_MIN, ts, ts, AVSEEK_FLAG_BACKWARD);
        if (rc < 0) rc = av_seek_frame(fmt, streamIndex, ts, AVSEEK_FLAG_BACKWARD | AVSEEK_FLAG_ANY);
        avcodec_flush_buffers(ctx);
        return rc >= 0;
    }

    // Decodes forward until a frame at or after `target` (or the first decodable frame when
    // `target` < 0). Returns false at EOF / error.
    bool nextFrameAtOrAfter(double target, const CancelToken* cancel)
    {
        int guard = 0;
        while (true)
        {
            if (cancel && cancel->cancelled()) return false;
            int rc = avcodec_receive_frame(ctx, frame);
            if (rc == 0)
            {
                double t = frameTime(frame);
                if (target < 0 || t < 0 || t + 1e-3 >= target) return true;
                av_frame_unref(frame);
                if (++guard > 2000) return false; // runaway protection (broken timestamps)
                continue;
            }
            if (rc != AVERROR(EAGAIN)) return false; // EOF or error
            rc = av_read_frame(fmt, pkt);
            bool eof = rc == AVERROR_EOF;
            if (rc < 0 && !eof) return false;
            if (!eof && pkt->stream_index != streamIndex) { av_packet_unref(pkt); continue; }
            rc = avcodec_send_packet(ctx, eof ? nullptr : pkt);
            if (!eof) av_packet_unref(pkt);
            if (rc < 0 && rc != AVERROR(EAGAIN) && rc != AVERROR_EOF) return false;
            if (eof && rc == AVERROR_EOF) return false;
        }
    }

    bool toRgba(RgbaImage& out, int maxEdge, std::string* err)
    {
        int sw = frame->width, sh = frame->height;
        if (sw <= 0 || sh <= 0) { if (err) *err = "empty frame"; return false; }
        int dw = sw, dh = sh;
        if (maxEdge > 0 && (sw > maxEdge || sh > maxEdge))
        {
            double s = (double)maxEdge / std::max(sw, sh);
            dw = std::max(1, (int)(sw * s + 0.5));
            dh = std::max(1, (int)(sh * s + 0.5));
        }
        if (!sws || swsW != sw || swsH != sh || swsFmt != frame->format || dstW != dw || dstH != dh)
        {
            if (sws) sws_freeContext(sws);
            sws = sws_getContext(sw, sh, (AVPixelFormat)frame->format, dw, dh, AV_PIX_FMT_RGBA,
                                 SWS_BICUBIC | SWS_ACCURATE_RND, nullptr, nullptr, nullptr);
            swsW = sw; swsH = sh; swsFmt = frame->format; dstW = dw; dstH = dh;
        }
        if (!sws) { if (err) *err = "cannot create scaler"; return false; }
        out.width = dw;
        out.height = dh;
        out.pixels.assign((size_t)dw * dh * 4, 0);
        uint8_t* dst[4] = { out.pixels.data(), nullptr, nullptr, nullptr };
        int dstStride[4] = { dw * 4, 0, 0, 0 };
        sws_scale(sws, frame->data, frame->linesize, 0, sh, dst, dstStride);
        return true;
    }
};

} // namespace

bool VideoFrames::extract(const std::string& path, int count, int maxEdge, std::vector<VideoFrame>& out,
                          std::string* err, const CancelToken* cancel)
{
    out.clear();
    VideoReader r;
    if (!r.open(path, err)) return false;
    count = std::clamp(count, 1, 64);
    double dur = r.duration();
    std::vector<double> targets;
    if (dur <= 0.05)
    {
        targets.push_back(-1); // unknown duration: first frame(s) sequentially
    }
    else
    {
        double t0 = dur * 0.05, t1 = dur * 0.95;
        if (count == 1) targets.push_back(dur * 0.5);
        else for (int i = 0; i < count; i++) targets.push_back(t0 + (t1 - t0) * i / (count - 1));
    }
    double lastTime = -1;
    for (double t : targets)
    {
        if (cancel && cancel->cancelled()) break;
        bool seeked = t >= 0 && r.seekTo(t);
        if (!seeked && t > 0 && lastTime > t) continue; // cannot go back without seeking
        if (!r.nextFrameAtOrAfter(seeked ? t : -1, cancel))
        {
            av_frame_unref(r.frame);
            continue;
        }
        VideoFrame vf;
        vf.time = std::max(0.0, r.frameTime(r.frame));
        if (vf.time < 0) vf.time = std::max(0.0, t);
        bool dup = !out.empty() && std::fabs(out.back().time - vf.time) < 1e-3;
        if (!dup && r.toRgba(vf.image, maxEdge, err)) { lastTime = vf.time; out.push_back(std::move(vf)); }
        av_frame_unref(r.frame);
    }
    if (out.empty())
    {
        // seeking may be unsupported (e.g. raw streams): read the first frames sequentially
        r.seekTo(0);
        for (int i = 0; i < count && (!cancel || !cancel->cancelled()); i++)
        {
            if (!r.nextFrameAtOrAfter(-1, cancel)) break;
            VideoFrame vf;
            vf.time = std::max(0.0, r.frameTime(r.frame));
            if (r.toRgba(vf.image, maxEdge, err)) out.push_back(std::move(vf));
            av_frame_unref(r.frame);
        }
    }
    if (out.empty())
    {
        if (err && err->empty()) *err = "no video frame decoded";
        return false;
    }
    return true;
}

} // namespace evobox
