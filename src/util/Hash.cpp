#include "util/Hash.h"
#include <cstdio>
#if !defined(_WIN32)
#include <sys/types.h> // off_t for fseeko/ftello
#endif
#include <cstring>
#include <vector>

namespace evobox
{

// ---------------------------------------------------------------- SHA-1 (FIPS 180-1)
namespace
{
struct Sha1
{
    uint32_t h[5] = { 0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u };
    uint64_t total = 0;
    uint8_t  block[64];
    size_t   blockLen = 0;

    static uint32_t rol(uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }

    void processBlock(const uint8_t* p)
    {
        uint32_t w[80];
        for (int i = 0; i < 16; i++)
            w[i] = (uint32_t)p[i * 4] << 24 | (uint32_t)p[i * 4 + 1] << 16 |
                   (uint32_t)p[i * 4 + 2] << 8 | (uint32_t)p[i * 4 + 3];
        for (int i = 16; i < 80; i++) w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
        for (int i = 0; i < 80; i++)
        {
            uint32_t f, k;
            if (i < 20)      { f = (b & c) | (~b & d);           k = 0x5A827999u; }
            else if (i < 40) { f = b ^ c ^ d;                    k = 0x6ED9EBA1u; }
            else if (i < 60) { f = (b & c) | (b & d) | (c & d);  k = 0x8F1BBCDCu; }
            else             { f = b ^ c ^ d;                    k = 0xCA62C1D6u; }
            uint32_t t = rol(a, 5) + f + e + k + w[i];
            e = d; d = c; c = rol(b, 30); b = a; a = t;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
    }

    void update(const uint8_t* data, size_t len)
    {
        total += len;
        while (len > 0)
        {
            size_t n = std::min(len, 64 - blockLen);
            memcpy(block + blockLen, data, n);
            blockLen += n; data += n; len -= n;
            if (blockLen == 64) { processBlock(block); blockLen = 0; }
        }
    }

    std::string finishHex()
    {
        uint64_t bits = total * 8;
        uint8_t pad = 0x80;
        update(&pad, 1);
        uint8_t zero = 0;
        while (blockLen != 56) update(&zero, 1);
        uint8_t lenBytes[8];
        for (int i = 7; i >= 0; i--) { lenBytes[i] = (uint8_t)(bits & 0xFF); bits >>= 8; }
        update(lenBytes, 8);
        char out[41];
        for (int i = 0; i < 5; i++) snprintf(out + i * 8, 9, "%08x", h[i]);
        return std::string(out, 40);
    }
};
} // namespace

std::string sha1Hex(const void* data, size_t len)
{
    Sha1 s;
    s.update((const uint8_t*)data, len);
    return s.finishHex();
}

std::string sha1Hex(const std::string& str) { return sha1Hex(str.data(), str.size()); }

// ---------------------------------------------------------------- FNV-1a
uint64_t fnv1a64(const void* data, size_t len, uint64_t seed)
{
    const uint8_t* p = (const uint8_t*)data;
    uint64_t h = seed;
    for (size_t i = 0; i < len; i++) { h ^= p[i]; h *= 1099511628211ull; }
    return h;
}

std::string toHex(uint64_t v)
{
    char buf[17];
    snprintf(buf, sizeof(buf), "%016llx", (unsigned long long)v);
    return buf;
}

// ---------------------------------------------------------------- content hash
std::string fileContentHash(const std::string& path)
{
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return "";
    // 64-bit offsets: `long` is 32 bits on Windows, media files can exceed 2 GB
#if defined(_WIN32)
    auto seek = [](FILE* fp, long long off, int whence) { return _fseeki64(fp, off, whence); };
    auto tell = [](FILE* fp) -> long long { return _ftelli64(fp); };
#else
    auto seek = [](FILE* fp, long long off, int whence) { return fseeko(fp, (off_t)off, whence); };
    auto tell = [](FILE* fp) -> long long { return (long long)ftello(fp); };
#endif
    seek(f, 0, SEEK_END);
    long long size = tell(f);
    if (size < 0) { fclose(f); return ""; }
    const size_t window = 1024 * 1024;
    std::vector<uint8_t> buf(window);
    uint64_t h = fnv1a64(&size, sizeof(size));
    seek(f, 0, SEEK_SET);
    size_t n = fread(buf.data(), 1, std::min<size_t>(window, (size_t)size), f);
    h = fnv1a64(buf.data(), n, h);
    if ((size_t)size > window)
    {
        seek(f, size - (long long)window, SEEK_SET);
        n = fread(buf.data(), 1, window, f);
        h = fnv1a64(buf.data(), n, h);
    }
    fclose(f);
    return toHex(h);
}

} // namespace evobox
