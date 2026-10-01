// ImageWriter.h — RGBA8 -> PNG file (stb_image_write, compiled in this TU) plus a high quality
// downscale (libswscale). Used for image stickers stored in <bundle>/icons.
#pragma once

#include <string>
#include "media/ImageDecoder.h"

namespace evobox
{

class ImageWriter
{
public:
    static bool writePng(const std::string& path, const RgbaImage& img, std::string* err = nullptr);
    // Returns `in` scaled so its longer edge is <= maxEdge (unchanged copy when already smaller).
    static RgbaImage fitToEdge(const RgbaImage& in, int maxEdge);
    // Square crop (centered) then fit — tiles are square-ish, a centered crop reads better than
    // a letterboxed frame. Non-destructive: returns a new image.
    static RgbaImage squareCrop(const RgbaImage& in);
};

} // namespace evobox
