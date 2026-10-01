#include "util/TimeFormat.h"
#include "util/Strings.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace evobox
{

std::string formatTime(double seconds, bool withMs)
{
    if (!(seconds >= 0)) seconds = 0; // also catches NaN
    long long totalMs = (long long)std::llround(seconds * 1000.0);
    long long ms = totalMs % 1000;
    long long totalS = totalMs / 1000;
    long long s = totalS % 60;
    long long m = (totalS / 60) % 60;
    long long h = totalS / 3600;
    char buf[64];
    if (h > 0)
    {
        if (withMs) snprintf(buf, sizeof(buf), "%lld:%02lld:%02lld.%03lld", h, m, s, ms);
        else        snprintf(buf, sizeof(buf), "%lld:%02lld:%02lld", h, m, s);
    }
    else
    {
        if (withMs) snprintf(buf, sizeof(buf), "%lld:%02lld.%03lld", m, s, ms);
        else        snprintf(buf, sizeof(buf), "%lld:%02lld", m, s);
    }
    return buf;
}

std::string formatTileDuration(double seconds)
{
    if (!(seconds > 0)) return "0:00";
    double r = std::round(seconds);
    if (r < 1) r = 1;
    return formatTime(r, false);
}

bool parseTime(const std::string& textIn, double& out)
{
    std::string text = str::trim(textIn);
    if (text.empty()) return false;
    // strip a trailing unit
    if (str::endsWith(text, "s")) text = str::trim(text.substr(0, text.size() - 1));
    auto parts = str::split(text, ':', true);
    if (parts.empty() || parts.size() > 3) return false;
    double total = 0;
    for (size_t i = 0; i < parts.size(); i++)
    {
        const std::string& p = parts[i];
        if (p.empty() && i + 1 != parts.size()) return false;
        char* end = nullptr;
        double v = p.empty() ? 0.0 : std::strtod(p.c_str(), &end);
        if (!p.empty() && (end == p.c_str() || *end != 0)) return false;
        if (v < 0) return false;
        total = total * 60.0 + v;
    }
    out = total;
    return true;
}

std::string formatMs(long long ms) { return str::groupThousands(ms) + " ms"; }

std::string formatAgo(double s)
{
    if (s < 5) return "just now";
    if (s < 60) return str::format("%d s ago", (int)s);
    if (s < 3600) return str::format("%d min ago", (int)(s / 60));
    if (s < 86400) return str::format("%d h ago", (int)(s / 3600));
    return str::format("%d d ago", (int)(s / 86400));
}

double nowSeconds()
{
    static const TimePoint t0 = Clock::now();
    return std::chrono::duration<double>(Clock::now() - t0).count();
}

} // namespace evobox
