// Hash.h — SHA-1 (icon cache file names), FNV-1a and the media "content hash"
// (size + first/last 1 MB) used to detect moved files.
#pragma once

#include <cstdint>
#include <string>

namespace evobox
{

std::string sha1Hex(const void* data, size_t len);
std::string sha1Hex(const std::string& s);

uint64_t fnv1a64(const void* data, size_t len, uint64_t seed = 1469598103934665603ull);

// Hash of the file size plus its first and last 1 MB; "" when the file cannot be read.
std::string fileContentHash(const std::string& path);

std::string toHex(uint64_t v);

} // namespace evobox
