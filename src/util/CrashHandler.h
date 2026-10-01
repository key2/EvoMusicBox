// CrashHandler.h — on SIGSEGV/SIGABRT/SIGFPE/SIGILL/SIGBUS writes a symbolized backtrace and the
// last Logger lines to <configDir>/crash-<epoch>.log (and stderr), then re-raises the default
// action so the OS still produces a core dump. POSIX only (no-op elsewhere).
#pragma once

#include <string>

namespace evobox
{
namespace crash
{
// `logDir` receives crash-*.log files. `buildInfo` is written at the top of the report.
void install(const std::string& logDir, const std::string& buildInfo);
// Path of the crash report written by this process, "" when none.
std::string lastReportPath();
} // namespace crash
} // namespace evobox
