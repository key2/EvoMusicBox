#include "util/CrashHandler.h"
#include "OrganicCore.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>

#if !defined(_WIN32)
#include <csignal>
#include <execinfo.h>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace evobox
{
namespace crash
{

namespace
{
std::string g_logDir;
std::string g_buildInfo;
std::string g_lastReport;
std::atomic<bool> g_handling{ false };

#if !defined(_WIN32)
void writeStr(int fd, const char* s) { if (fd >= 0) { ssize_t r = ::write(fd, s, strlen(s)); (void)r; } }

const char* signalName(int sig)
{
    switch (sig)
    {
    case SIGSEGV: return "SIGSEGV (segmentation fault)";
    case SIGABRT: return "SIGABRT (abort)";
    case SIGFPE:  return "SIGFPE (arithmetic error)";
    case SIGILL:  return "SIGILL (illegal instruction)";
    case SIGBUS:  return "SIGBUS (bus error)";
    default:      return "signal";
    }
}

void handler(int sig, siginfo_t* info, void*)
{
    // one report per process; a crash inside the handler falls through to the default action
    if (g_handling.exchange(true)) { signal(sig, SIG_DFL); raise(sig); return; }

    char path[1024];
    snprintf(path, sizeof(path), "%s/crash-%lld.log", g_logDir.c_str(), (long long)time(nullptr));
    int fd = ::open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    g_lastReport = fd >= 0 ? path : "";

    char line[512];
    snprintf(line, sizeof(line), "EvoMusicBox crash report — %s\n%s\n", signalName(sig), g_buildInfo.c_str());
    writeStr(2, "\n"); writeStr(2, line); writeStr(fd, line);
    if (info)
    {
        snprintf(line, sizeof(line), "fault address: %p  code: %d\n", info->si_addr, info->si_code);
        writeStr(2, line); writeStr(fd, line);
    }

    // backtrace (raw + symbolized; run through addr2line / gdb for lines)
    void* frames[64];
    int n = backtrace(frames, 64);
    writeStr(2, "backtrace:\n"); writeStr(fd, "backtrace:\n");
    backtrace_symbols_fd(frames, n, 2);
    if (fd >= 0) backtrace_symbols_fd(frames, n, fd);

    // last logger lines (best effort: the logger is a plain vector on the main thread)
    const auto& entries = organic::Logger::get().entries;
    size_t count = entries.size();
    size_t from = count > 40 ? count - 40 : 0;
    writeStr(fd, "\nlast log lines:\n");
    for (size_t i = from; i < count; i++)
    {
        const organic::LogEntry& e = entries[i];
        snprintf(line, sizeof(line), "[%8.3f] %s %-8s %.300s\n", e.time,
                 e.level == organic::LogLevel::Error ? "ERROR" : (e.level == organic::LogLevel::Warning ? "WARN " : "INFO "),
                 e.source.c_str(), e.message.c_str());
        writeStr(fd, line);
    }
    snprintf(line, sizeof(line), "\ncrash report written to %s\n", g_lastReport.c_str());
    writeStr(2, line);
    if (fd >= 0) ::close(fd);

    signal(sig, SIG_DFL);
    raise(sig);
}
#endif
} // namespace

void install(const std::string& logDir, const std::string& buildInfo)
{
    g_logDir = logDir;
    g_buildInfo = buildInfo;
#if !defined(_WIN32)
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = handler;
    sa.sa_flags = SA_SIGINFO | SA_RESETHAND;
    sigemptyset(&sa.sa_mask);
    for (int sig : { SIGSEGV, SIGABRT, SIGFPE, SIGILL, SIGBUS }) sigaction(sig, &sa, nullptr);
    // warm up backtrace() so its lazy initialisation does not allocate inside the handler
    void* frames[4];
    backtrace(frames, 4);
#endif
}

std::string lastReportPath() { return g_lastReport; }

} // namespace crash
} // namespace evobox
