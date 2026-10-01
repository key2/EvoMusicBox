#include "media/ImageDecoder.h"
#include "util/ScopeExit.h"
#include <algorithm>
#include <cstring>

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavformat/avio.h>
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

struct MemReader
{
    const std::vector<uint8_t>* data;
    size_t pos = 0;
};

int memRead(void* opaque, uint8_t* buf, int size)
{
    auto* r = (MemReader*)opaque;
    size_t left = r->data->size() - r->pos;
    if (left == 0) return AVERROR_EOF;
    size_t n = std::min(left, (size_t)size);
    memcpy(buf, r->data->data() + r->pos, n);
    r->pos += n;
    return (int)n;
}

int64_t memSeek(void* opaque, int64_t offset, int whence)
{
    auto* r = (MemReader*)opaque;
    if (whence == AVSEEK_SIZE) return (int64_t)r->data->size();
    size_t base = whence == SEEK_SET ? 0 : whence == SEEK_CUR ? r->pos : r->data->size();
    int64_t np = (int64_t)base + offset;
    if (np < 0 || np > (int64_t)r->data->size()) return AVERROR(EINVAL);
    r->pos = (size_t)np;
    return np;
}

bool decodeFormat(AVFormatContext* fmt, RgbaImage& out, int maxEdge, std::string* err)
{
    auto fail = [&](const std::string& m) { if (err) *err = m; return false; };
    int rc = avformat_find_stream_info(fmt, nullptr);
    if (rc < 0) return fail("cannot read image info: " + avErr(rc));
    const AVCodec* codec = nullptr;
    int idx = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, &codec, 0);
    if (idx < 0 || !codec) return fail("no image stream found");
    AVCodecContext* ctx = avcodec_alloc_context3(codec);
    if (!ctx) return fail("cannot allocate image decoder");
    EVOBOX_SCOPE_EXIT(avcodec_free_context(&ctx));
    rc = avcodec_parameters_to_context(ctx, fmt->streams[idx]->codecpar);
    if (rc < 0) return fail("cannot configure image decoder: " + avErr(rc));
    rc = avcodec_open2(ctx, codec, nullptr);
    if (rc < 0) return fail("cannot open image decoder: " + avErr(rc));

    AVPacket* pkt = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    EVOBOX_SCOPE_EXIT(av_packet_free(&pkt); av_frame_free(&frame));
    bool got = false;
    while (!got)
    {
        rc = av_read_frame(fmt, pkt);
        bool eof = rc == AVERROR_EOF;
        if (rc < 0 && !eof) return fail("read error: " + avErr(rc));
        if (!eof && pkt->stream_index != idx) { av_packet_unref(pkt); continue; }
        rc = avcodec_send_packet(ctx, eof ? nullptr : pkt);
        if (!eof) av_packet_unref(pkt);
        if (rc < 0 && rc != AVERROR(EAGAIN) && rc != AVERROR_EOF) return fail("image decode error: " + avErr(rc));
        rc = avcodec_receive_frame(ctx, frame);
        if (rc == 0) got = true;
        else if (rc == AVERROR_EOF || (eof && rc == AVERROR(EAGAIN))) break;
        else if (rc != AVERROR(EAGAIN)) return fail("image decode error: " + avErr(rc));
    }
    if (!got) return fail("no image frame decoded");

    int sw = frame->width, sh = frame->height;
    if (sw <= 0 || sh <= 0) return fail("empty image");
    int dw = sw, dh = sh;
    if (maxEdge > 0 && (sw > maxEdge || sh > maxEdge))
    {
        double s = (double)maxEdge / std::max(sw, sh);
        dw = std::max(1, (int)(sw * s + 0.5));
        dh = std::max(1, (int)(sh * s + 0.5));
    }
    SwsContext* sws = sws_getContext(sw, sh, (AVPixelFormat)frame->format, dw, dh, AV_PIX_FMT_RGBA,
                                     SWS_BILINEAR | SWS_ACCURATE_RND, nullptr, nullptr, nullptr);
    if (!sws) return fail("cannot create scaler");
    EVOBOX_SCOPE_EXIT(sws_freeContext(sws));
    out.width = dw;
    out.height = dh;
    out.pixels.assign((size_t)dw * dh * 4, 0);
    uint8_t* dst[4] = { out.pixels.data(), nullptr, nullptr, nullptr };
    int dstStride[4] = { dw * 4, 0, 0, 0 };
    sws_scale(sws, frame->data, frame->linesize, 0, sh, dst, dstStride);
    return true;
}

} // namespace

bool ImageDecoder::decodeFile(const std::string& path, RgbaImage& out, int maxEdge, std::string* err)
{
    AVFormatContext* fmt = nullptr;
    int rc = avformat_open_input(&fmt, path.c_str(), nullptr, nullptr);
    if (rc < 0) { if (err) *err = "cannot open image '" + path + "': " + avErr(rc); return false; }
    EVOBOX_SCOPE_EXIT(avformat_close_input(&fmt));
    return decodeFormat(fmt, out, maxEdge, err);
}

bool ImageDecoder::decodeBytes(const std::vector<uint8_t>& bytes, RgbaImage& out, int maxEdge, std::string* err)
{
    if (bytes.empty()) { if (err) *err = "empty image data"; return false; }
    MemReader reader{ &bytes, 0 };
    const int bufSize = 64 * 1024;
    uint8_t* buf = (uint8_t*)av_malloc(bufSize);
    AVIOContext* io = avio_alloc_context(buf, bufSize, 0, &reader, memRead, nullptr, memSeek);
    if (!io) { av_free(buf); if (err) *err = "cannot allocate io context"; return false; }
    AVFormatContext* fmt = avformat_alloc_context();
    fmt->pb = io;
    int rc = avformat_open_input(&fmt, nullptr, nullptr, nullptr);
    if (rc < 0)
    {
        // avformat_open_input frees fmt on failure but not the custom io
        av_freep(&io->buffer);
        avio_context_free(&io);
        if (err) *err = "cannot probe image data: " + avErr(rc);
        return false;
    }
    bool ok = decodeFormat(fmt, out, maxEdge, err);
    avformat_close_input(&fmt);
    av_freep(&io->buffer);
    avio_context_free(&io);
    return ok;
}

} // namespace evobox
