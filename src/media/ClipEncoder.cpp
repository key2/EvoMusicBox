#include "media/ClipEncoder.h"
#include "media/ClipRenderer.h" // saveWavFloat32 (lossless fallback)
#include "util/Paths.h"
#include "util/ScopeExit.h"
#include "util/Strings.h"
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <vector>

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/audio_fifo.h>
#include <libavutil/avutil.h>
#include <libavutil/channel_layout.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
}

namespace fs = std::filesystem;

namespace evobox
{

namespace
{

// LAME VBR quality (0 = best/biggest .. 9 = smallest); 2 is the usual "transparent" setting,
// ~190 kbit/s on dense music and far less on sparse effects / silence.
constexpr int kVbrQuality = 2;
// Bit rate for encoders without VBR (libshine) — never used with libmp3lame.
constexpr int64_t kCbrBitRate = 192000;

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

// libmp3lame first (VBR, exact gapless info); any other MP3 encoder of the build otherwise.
const AVCodec* findMp3Encoder()
{
    static const AVCodec* codec = []() -> const AVCodec*
    {
        if (const AVCodec* c = avcodec_find_encoder_by_name("libmp3lame")) return c;
        return avcodec_find_encoder(AV_CODEC_ID_MP3);
    }();
    return codec;
}

// Supported sample rates / sample formats of an encoder (empty = unconstrained).
std::vector<int> supportedRates(const AVCodec* codec)
{
    std::vector<int> out;
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(61, 13, 100)
    const int* rates = nullptr;
    int n = 0;
    if (avcodec_get_supported_config(nullptr, codec, AV_CODEC_CONFIG_SAMPLE_RATE, 0, (const void**)&rates, &n) >= 0 && rates)
        for (int i = 0; i < n; i++) out.push_back(rates[i]);
#else
    if (codec->supported_samplerates)
        for (const int* r = codec->supported_samplerates; *r; r++) out.push_back(*r);
#endif
    return out;
}

std::vector<AVSampleFormat> supportedFormats(const AVCodec* codec)
{
    std::vector<AVSampleFormat> out;
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(61, 13, 100)
    const AVSampleFormat* fmts = nullptr;
    int n = 0;
    if (avcodec_get_supported_config(nullptr, codec, AV_CODEC_CONFIG_SAMPLE_FORMAT, 0, (const void**)&fmts, &n) >= 0 && fmts)
        for (int i = 0; i < n; i++) out.push_back(fmts[i]);
#else
    if (codec->sample_fmts)
        for (const AVSampleFormat* f = codec->sample_fmts; *f != AV_SAMPLE_FMT_NONE; f++) out.push_back(*f);
#endif
    return out;
}

// Smallest supported rate >= source (no high-frequency loss), else the highest one (48 kHz).
int chooseRate(const AVCodec* codec, int sourceRate)
{
    std::vector<int> rates = supportedRates(codec);
    if (rates.empty()) return sourceRate;
    int best = 0, highest = 0;
    for (int r : rates)
    {
        highest = std::max(highest, r);
        if (r >= sourceRate && (best == 0 || r < best)) best = r;
    }
    return best ? best : highest;
}

AVSampleFormat chooseFormat(const AVCodec* codec)
{
    std::vector<AVSampleFormat> fmts = supportedFormats(codec);
    if (fmts.empty()) return AV_SAMPLE_FMT_FLTP;
    for (AVSampleFormat pref : { AV_SAMPLE_FMT_FLTP, AV_SAMPLE_FMT_S32P, AV_SAMPLE_FMT_S16P,
                                 AV_SAMPLE_FMT_FLT, AV_SAMPLE_FMT_S32, AV_SAMPLE_FMT_S16 })
        if (std::find(fmts.begin(), fmts.end(), pref) != fmts.end()) return pref;
    return fmts.front();
}

// Muxer + encoder + resampler + fifo of one MP3 write; released in the right order by the destructor.
struct Mp3Writer
{
    AVFormatContext* fmt = nullptr;
    AVCodecContext* enc = nullptr;
    AVStream* st = nullptr;
    SwrContext* swr = nullptr;
    AVAudioFifo* fifo = nullptr;
    AVFrame* frame = nullptr; // frames handed to the encoder
    AVFrame* conv = nullptr;  // resampler output
    AVPacket* pkt = nullptr;
    AVChannelLayout inLayout{};
    AVChannelLayout outLayout{};
    AVSampleFormat outFmt = AV_SAMPLE_FMT_FLTP;
    int outRate = 0;
    int64_t nextPts = 0;

    ~Mp3Writer()
    {
        if (pkt) av_packet_free(&pkt);
        if (frame) av_frame_free(&frame);
        if (conv) av_frame_free(&conv);
        if (fifo) av_audio_fifo_free(fifo);
        if (swr) swr_free(&swr);
        if (enc) avcodec_free_context(&enc);
        if (fmt)
        {
            if (fmt->pb) avio_closep(&fmt->pb);
            avformat_free_context(fmt);
        }
        av_channel_layout_uninit(&inLayout);
        av_channel_layout_uninit(&outLayout);
    }

    // Sends one frame (nullptr = flush) and writes every packet the encoder returns.
    bool encodeFrame(const AVFrame* f, std::string* err)
    {
        Fail fail{ err };
        int rc = avcodec_send_frame(enc, f);
        if (rc < 0 && rc != AVERROR_EOF) return fail("MP3 encode error: " + avErr(rc));
        while (true)
        {
            rc = avcodec_receive_packet(enc, pkt);
            if (rc == AVERROR(EAGAIN) || rc == AVERROR_EOF) return true;
            if (rc < 0) return fail("MP3 encode error: " + avErr(rc));
            av_packet_rescale_ts(pkt, enc->time_base, st->time_base);
            pkt->stream_index = st->index;
            rc = av_interleaved_write_frame(fmt, pkt); // takes the packet's reference
            if (rc < 0) return fail("cannot write MP3 data: " + avErr(rc));
        }
    }

    // Pops `count` samples from the fifo into a frame and encodes them. A count below the
    // encoder's frame size is only legal for the last frame (libavcodec pads it when the
    // encoder cannot take short frames).
    bool encodeFromFifo(int count, std::string* err)
    {
        Fail fail{ err };
        av_frame_unref(frame);
        frame->nb_samples = count;
        frame->format = outFmt;
        frame->sample_rate = outRate;
        if (av_channel_layout_copy(&frame->ch_layout, &outLayout) < 0) return fail("out of memory");
        int rc = av_frame_get_buffer(frame, 0);
        if (rc < 0) return fail("cannot allocate audio frame: " + avErr(rc));
        if (av_audio_fifo_read(fifo, (void**)frame->data, count) < count) return fail("audio fifo underrun");
        frame->pts = nextPts;
        nextPts += count;
        return encodeFrame(frame, err);
    }

    // Encodes every complete frame in the fifo and, when `all`, the remainder as a short last frame.
    bool drainFifo(bool all, std::string* err)
    {
        int frameSize = enc->frame_size > 0 ? enc->frame_size : 1152;
        while (av_audio_fifo_size(fifo) >= frameSize)
            if (!encodeFromFifo(frameSize, err)) return false;
        if (all && av_audio_fifo_size(fifo) > 0)
            if (!encodeFromFifo(av_audio_fifo_size(fifo), err)) return false;
        return true;
    }

    // Resamples `inFrames` interleaved float frames (nullptr = flush) into the fifo and encodes
    // what is complete.
    bool convert(const float* in, int inFrames, std::string* err)
    {
        Fail fail{ err };
        int outCount = swr_get_out_samples(swr, inFrames);
        if (outCount <= 0) return true;
        av_frame_unref(conv);
        conv->nb_samples = outCount;
        conv->format = outFmt;
        conv->sample_rate = outRate;
        if (av_channel_layout_copy(&conv->ch_layout, &outLayout) < 0) return fail("out of memory");
        int rc = av_frame_get_buffer(conv, 0);
        if (rc < 0) return fail("cannot allocate conversion buffer: " + avErr(rc));
        const uint8_t* inPtr = (const uint8_t*)in;
        int got = swr_convert(swr, conv->data, outCount, in ? &inPtr : nullptr, in ? inFrames : 0);
        if (got < 0) return fail("resample error: " + avErr(got));
        if (got > 0 && av_audio_fifo_write(fifo, (void**)conv->data, got) < got) return fail("out of memory");
        return drainFifo(false, err);
    }
};

bool writeMp3(const std::string& path, const organic::AudioBuffer& buf, std::string* err)
{
    Fail fail{ err };
    const AVCodec* codec = findMp3Encoder();
    if (!codec) return fail("this FFmpeg build has no MP3 encoder (libmp3lame)");
    if (buf.channels <= 0 || buf.sampleRate <= 0) return fail("empty audio buffer");

    Mp3Writer w;
    int inCh = buf.channels;
    int outCh = ClipEncoder::mp3Channels(inCh);
    w.outRate = chooseRate(codec, buf.sampleRate);
    w.outFmt = chooseFormat(codec);
    av_channel_layout_default(&w.inLayout, inCh);
    av_channel_layout_default(&w.outLayout, outCh);

    int rc = avformat_alloc_output_context2(&w.fmt, nullptr, "mp3", path.c_str());
    if (rc < 0 || !w.fmt) return fail("cannot create MP3 muxer: " + avErr(rc));
    w.st = avformat_new_stream(w.fmt, codec);
    if (!w.st) return fail("cannot create MP3 stream");
    w.enc = avcodec_alloc_context3(codec);
    if (!w.enc) return fail("cannot allocate MP3 encoder");
    w.enc->sample_rate = w.outRate;
    w.enc->sample_fmt = w.outFmt;
    if (av_channel_layout_copy(&w.enc->ch_layout, &w.outLayout) < 0) return fail("out of memory");
    w.enc->time_base = AVRational{ 1, w.outRate };
    w.enc->bit_rate = kCbrBitRate;
    if (std::string(codec->name) == "libmp3lame")
    {
        w.enc->flags |= AV_CODEC_FLAG_QSCALE; // VBR
        w.enc->global_quality = kVbrQuality * FF_QP2LAMBDA;
    }
    if (w.fmt->oformat->flags & AVFMT_GLOBALHEADER) w.enc->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    rc = avcodec_open2(w.enc, codec, nullptr);
    if (rc < 0) return fail("cannot open MP3 encoder: " + avErr(rc));
    // codecpar carries initial_padding: the muxer writes it into the LAME tag (gapless decoding)
    rc = avcodec_parameters_from_context(w.st->codecpar, w.enc);
    if (rc < 0) return fail("cannot configure MP3 stream: " + avErr(rc));
    w.st->time_base = w.enc->time_base;

    rc = avio_open(&w.fmt->pb, path.c_str(), AVIO_FLAG_WRITE);
    if (rc < 0) return fail("cannot create '" + path + "': " + avErr(rc));
    rc = avformat_write_header(w.fmt, nullptr);
    if (rc < 0) return fail("cannot write MP3 header: " + avErr(rc));

    rc = swr_alloc_set_opts2(&w.swr, &w.outLayout, w.outFmt, w.outRate, &w.inLayout, AV_SAMPLE_FMT_FLT, buf.sampleRate, 0, nullptr);
    if (rc < 0 || !w.swr) return fail("cannot create resampler: " + avErr(rc));
    rc = swr_init(w.swr);
    if (rc < 0) return fail("cannot init resampler: " + avErr(rc));
    w.fifo = av_audio_fifo_alloc(w.outFmt, outCh, 1);
    w.frame = av_frame_alloc();
    w.conv = av_frame_alloc();
    w.pkt = av_packet_alloc();
    if (!w.fifo || !w.frame || !w.conv || !w.pkt) return fail("out of memory");

    const size_t chunk = 8192; // frames per resampler call
    size_t total = (size_t)buf.frames();
    for (size_t pos = 0; pos < total; pos += chunk)
    {
        int n = (int)std::min(chunk, total - pos);
        if (!w.convert(buf.samples.data() + pos * (size_t)inCh, n, err)) return false;
    }
    if (!w.convert(nullptr, 0, err)) return false;  // flush the resampler
    if (!w.drainFifo(true, err)) return false;      // short last frame
    if (!w.encodeFrame(nullptr, err)) return false; // flush the encoder
    rc = av_write_trailer(w.fmt);                   // patches the Xing/LAME header (frame count, padding)
    if (rc < 0) return fail("cannot finish '" + path + "': " + avErr(rc));
    rc = avio_closep(&w.fmt->pb);
    if (rc < 0) return fail("cannot close '" + path + "': " + avErr(rc));
    return true;
}

} // namespace

// ---------------------------------------------------------------- public API
bool ClipEncoder::mp3Available() { return findMp3Encoder() != nullptr; }

std::string ClipEncoder::mp3EncoderName()
{
    const AVCodec* c = findMp3Encoder();
    return c ? c->name : "";
}

const char* ClipEncoder::clipExtension() { return mp3Available() ? kMp3Ext : kWavExt; }

bool ClipEncoder::isLegacyClip(const std::string& clipFile)
{
    return mp3Available() && str::fileExtLower(clipFile) == kWavExt;
}

int ClipEncoder::mp3SampleRate(int sourceRate)
{
    const AVCodec* c = findMp3Encoder();
    return c ? chooseRate(c, sourceRate) : sourceRate;
}

bool ClipEncoder::encodeMp3(const std::string& path, const organic::AudioBuffer& buf, std::string* err)
{
    return writeMp3(path, buf, err);
}

bool ClipEncoder::encode(const std::string& path, const organic::AudioBuffer& buf, std::string* err)
{
    Fail fail{ err };
    std::error_code ec;
    fs::create_directories(fs::path(path).parent_path(), ec);
    // a temp name unique to this call: two jobs writing the same clip never share a partial file
    static std::atomic<unsigned> serial{ 0 };
    std::string tmp = path + "." + std::to_string(++serial) + ".tmp";
    bool ok = str::fileExtLower(path) == kMp3Ext ? encodeMp3(tmp, buf, err)
                                                 : (saveWavFloat32(tmp, buf) || fail("cannot write " + tmp));
    if (!ok) { fs::remove(tmp, ec); return false; }
    // never removes an existing clip unless the finished replacement is there to take its place
    return paths::replaceFile(tmp, path, err);
}

} // namespace evobox
