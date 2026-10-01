// TimeFormat.h — the one time formatter shared by tiles, fields and ruler labels
// (UI.md §28: "m:ss.mmm"). Compatible with organic::formatTime.
#pragma once

#include <chrono>
#include <string>

namespace evobox
{

using Clock = std::chrono::steady_clock;
using TimePoint = Clock::time_point;
using Millis = std::chrono::milliseconds;

// 0:01.230 (withMs) / 0:01 (without). Hours appear as h:mm:ss when >= 1 h.
std::string formatTime(double seconds, bool withMs = true);
// Short tile label: "0:02" (rounded to the nearest second, <1 s shows "0:01" for non-zero clips)
std::string formatTileDuration(double seconds);
// Parses "m:ss.mmm", "ss.mmm", "h:mm:ss.mmm" or a plain number of seconds. Returns false when unparsable.
bool parseTime(const std::string& text, double& outSeconds);
// "1 234 ms"
std::string formatMs(long long ms);
// "12 min ago" / "just now"
std::string formatAgo(double secondsAgo);

double nowSeconds(); // steady clock, seconds since first call

} // namespace evobox
