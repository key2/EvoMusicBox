// ImageDecoder.h — file bytes -> RGBA8 through libavformat/libavcodec (image2 / webp_pipe /
// png_pipe demuxers) + libswscale. Used for TikTok gift icons (frequently WebP, which stb_image
// cannot read) with a maximum edge so thumbnails stay small.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace evobox
{

struct RgbaImage
{
    int width = 0;
    int height = 0;
    std::vector<uint8_t> pixels; // width*height*4, row-major, top-left origin
    bool empty() const { return pixels.empty(); }
};

class ImageDecoder
{
public:
    // maxEdge <= 0 keeps the original size.
    static bool decodeFile(const std::string& path, RgbaImage& out, int maxEdge = 128, std::string* err = nullptr);
    // Decodes from memory (mime hint optional, e.g. "webp").
    static bool decodeBytes(const std::vector<uint8_t>& bytes, RgbaImage& out, int maxEdge = 128, std::string* err = nullptr);
};

} // namespace evobox
