// Strings.h — small string helpers shared by every layer.
#pragma once

#include <string>
#include <vector>

namespace evobox
{
namespace str
{

std::string lower(std::string s);
std::string trim(const std::string& s);
bool icontains(const std::string& haystack, const std::string& needle);
bool iequals(const std::string& a, const std::string& b);
bool startsWith(const std::string& s, const std::string& prefix);
bool endsWith(const std::string& s, const std::string& suffix);
std::vector<std::string> split(const std::string& s, char sep, bool keepEmpty = false);
std::string join(const std::vector<std::string>& parts, const std::string& sep);
std::string replaceAll(std::string s, const std::string& from, const std::string& to);

// printf-style formatting into a std::string
std::string format(const char* fmt, ...);

// "1 234" style thousands grouping (UI.md uses "3 000 ms")
std::string groupThousands(long long v);
// 1.2 kB / 3.4 MB
std::string humanBytes(unsigned long long bytes);
// "@name" -> "name", trims whitespace
std::string cleanUsername(const std::string& s);
// file name without directory
std::string fileName(const std::string& path);
std::string fileStem(const std::string& path);
std::string fileExtLower(const std::string& path); // ".mp3"

} // namespace str
} // namespace evobox
