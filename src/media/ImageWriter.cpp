#include "media/ImageWriter.h"
#include <algorithm>
#include <cstring>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wunused-function"
#endif
#include "stb_image_write.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

extern "C"
{
#include <libswscale/swscale.h>
}

namespace evobox
{

bool ImageWriter::writePng(const std::string& path, const RgbaImage& img, std::string* err)
{
    if (img.empty() || img.width <= 0 || img.height <= 0)
    {
        if (err) *err = "empty image";
        return false;
    }
    if (!stbi_write_png(path.c_str(), img.width, img.height, 4, img.pixels.data(), img.width * 4))
    {
        if (err) *err = "cannot write " + path;
        return false;
    }
    return true;
}

RgbaImage ImageWriter::fitToEdge(const RgbaImage& in, int maxEdge)
{
    if (in.empty() || maxEdge <= 0 || (in.width <= maxEdge && in.height <= maxEdge)) return in;
    double s = (double)maxEdge / std::max(in.width, in.height);
    int dw = std::max(1, (int)(in.width * s + 0.5));
    int dh = std::max(1, (int)(in.height * s + 0.5));
    SwsContext* sws = sws_getContext(in.width, in.height, AV_PIX_FMT_RGBA, dw, dh, AV_PIX_FMT_RGBA,
                                     SWS_BICUBIC | SWS_ACCURATE_RND, nullptr, nullptr, nullptr);
    if (!sws) return in;
    RgbaImage out;
    out.width = dw;
    out.height = dh;
    out.pixels.assign((size_t)dw * dh * 4, 0);
    const uint8_t* src[4] = { in.pixels.data(), nullptr, nullptr, nullptr };
    int srcStride[4] = { in.width * 4, 0, 0, 0 };
    uint8_t* dst[4] = { out.pixels.data(), nullptr, nullptr, nullptr };
    int dstStride[4] = { dw * 4, 0, 0, 0 };
    sws_scale(sws, src, srcStride, 0, in.height, dst, dstStride);
    sws_freeContext(sws);
    return out;
}

RgbaImage ImageWriter::squareCrop(const RgbaImage& in)
{
    if (in.empty() || in.width == in.height) return in;
    int edge = std::min(in.width, in.height);
    int x0 = (in.width - edge) / 2, y0 = (in.height - edge) / 2;
    RgbaImage out;
    out.width = out.height = edge;
    out.pixels.resize((size_t)edge * edge * 4);
    for (int y = 0; y < edge; y++)
        memcpy(out.pixels.data() + (size_t)y * edge * 4, in.pixels.data() + ((size_t)(y0 + y) * in.width + x0) * 4, (size_t)edge * 4);
    return out;
}

} // namespace evobox
