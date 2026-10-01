#include "util/Strings.h"
#include <algorithm>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <filesystem>

namespace evobox
{
namespace str
{

std::string lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return s;
}

std::string trim(const std::string& s)
{
    size_t a = 0, b = s.size();
    while (a < b && std::isspace((unsigned char)s[a])) a++;
    while (b > a && std::isspace((unsigned char)s[b - 1])) b--;
    return s.substr(a, b - a);
}

bool icontains(const std::string& h, const std::string& n)
{
    if (n.empty()) return true;
    return lower(h).find(lower(n)) != std::string::npos;
}

bool iequals(const std::string& a, const std::string& b) { return lower(a) == lower(b); }

bool startsWith(const std::string& s, const std::string& p)
{
    return s.size() >= p.size() && s.compare(0, p.size(), p) == 0;
}

bool endsWith(const std::string& s, const std::string& suf)
{
    return s.size() >= suf.size() && s.compare(s.size() - suf.size(), suf.size(), suf) == 0;
}

std::vector<std::string> split(const std::string& s, char sep, bool keepEmpty)
{
    std::vector<std::string> out;
    std::string cur;
    for (char c : s)
    {
        if (c == sep)
        {
            if (keepEmpty || !cur.empty()) out.push_back(cur);
            cur.clear();
        }
        else cur.push_back(c);
    }
    if (keepEmpty || !cur.empty()) out.push_back(cur);
    return out;
}

std::string join(const std::vector<std::string>& parts, const std::string& sep)
{
    std::string out;
    for (size_t i = 0; i < parts.size(); i++)
    {
        if (i) out += sep;
        out += parts[i];
    }
    return out;
}

std::string replaceAll(std::string s, const std::string& from, const std::string& to)
{
    if (from.empty()) return s;
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos)
    {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
    return s;
}

std::string format(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    va_list ap2;
    va_copy(ap2, ap);
    int n = vsnprintf(nullptr, 0, fmt, ap);
    va_end(ap);
    std::string out;
    if (n > 0)
    {
        out.resize((size_t)n + 1);
        vsnprintf(out.data(), out.size(), fmt, ap2);
        out.resize((size_t)n);
    }
    va_end(ap2);
    return out;
}

std::string groupThousands(long long v)
{
    bool neg = v < 0;
    unsigned long long u = neg ? (unsigned long long)(-v) : (unsigned long long)v;
    std::string digits = std::to_string(u);
    std::string out;
    int count = 0;
    for (int i = (int)digits.size() - 1; i >= 0; i--)
    {
        out.insert(out.begin(), digits[(size_t)i]);
        if (++count % 3 == 0 && i > 0) out.insert(out.begin(), ' ');
    }
    if (neg) out.insert(out.begin(), '-');
    return out;
}

std::string humanBytes(unsigned long long b)
{
    const char* units[] = { "B", "kB", "MB", "GB", "TB" };
    double v = (double)b;
    int u = 0;
    while (v >= 1000.0 && u < 4) { v /= 1000.0; u++; }
    if (u == 0) return format("%llu B", b);
    return format(v < 10 ? "%.2f %s" : (v < 100 ? "%.1f %s" : "%.0f %s"), v, units[u]);
}

std::string cleanUsername(const std::string& s)
{
    std::string t = trim(s);
    if (!t.empty() && t[0] == '@') t.erase(0, 1);
    // strip a pasted URL: https://www.tiktok.com/@user/live
    size_t at = t.find("/@");
    if (at != std::string::npos)
    {
        t = t.substr(at + 2);
        size_t slash = t.find('/');
        if (slash != std::string::npos) t = t.substr(0, slash);
    }
    return t;
}

std::string fileName(const std::string& path) { return std::filesystem::path(path).filename().string(); }
std::string fileStem(const std::string& path) { return std::filesystem::path(path).stem().string(); }
std::string fileExtLower(const std::string& path) { return lower(std::filesystem::path(path).extension().string()); }

} // namespace str
} // namespace evobox
