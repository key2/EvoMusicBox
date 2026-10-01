// ScopeExit.h — run a callable when the scope ends (FFmpeg context cleanup etc.).
#pragma once

#include <utility>

namespace evobox
{

template <typename F>
class ScopeExit
{
public:
    explicit ScopeExit(F f) : f_(std::move(f)) {}
    ~ScopeExit() { if (active_) f_(); }
    ScopeExit(ScopeExit&& o) noexcept : f_(std::move(o.f_)), active_(o.active_) { o.active_ = false; }
    ScopeExit(const ScopeExit&) = delete;
    ScopeExit& operator=(const ScopeExit&) = delete;
    void release() { active_ = false; }

private:
    F f_;
    bool active_ = true;
};

template <typename F>
ScopeExit<F> makeScopeExit(F f) { return ScopeExit<F>(std::move(f)); }

} // namespace evobox

#define EVOBOX_CONCAT_(a, b) a##b
#define EVOBOX_CONCAT(a, b) EVOBOX_CONCAT_(a, b)
#define EVOBOX_SCOPE_EXIT(...) auto EVOBOX_CONCAT(_scopeExit_, __LINE__) = ::evobox::makeScopeExit([&]() { __VA_ARGS__; })
