// OscScheduler.h — steady-clock scheduler thread: fires groups of pre-encoded OSC packets at
// their due time (sub-millisecond jitter, independent of the ~16 ms UI frame) and posts
// OscSent / OscError / GroupFired events for the main thread.
#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
#include "osc/OscEndpoint.h"
#include "osc/OscSender.h"
#include "util/ThreadSafeQueue.h"
#include "util/TimeFormat.h"

namespace evobox
{

using SchedToken = uint64_t;

struct OscPacket
{
    std::vector<uint8_t> bytes;
    OscEndpoint endpoint;
    uint64_t commandUid = 0;
    uint64_t targetUid = 0;
    std::string text;     // human readable, for the log
    bool skip = false;    // invalid command: reported as an error, not sent
    std::string skipReason;
};

struct OscGroup
{
    std::vector<OscPacket> packets;
    uint64_t ownerUid = 0;   // triggerable
    int phaseIndex = -1;
    uint64_t sessionId = 0;
    std::string label;       // "Explosion / At play (start)"
};

struct OscEvent
{
    enum class Kind { Sent, Error, GroupFired, GroupCancelled };
    Kind kind = Kind::Sent;
    SchedToken token = 0;
    uint64_t ownerUid = 0;
    uint64_t commandUid = 0;
    uint64_t targetUid = 0;
    int phaseIndex = -1;
    uint64_t sessionId = 0;
    std::string message;     // packet text or error
    double time = 0;         // app seconds (nowSeconds())
};

// Interface so TriggerController can be tested with a fake clock.
class IOscScheduler
{
public:
    virtual ~IOscScheduler() = default;
    virtual TimePoint now() const = 0;
    virtual SchedToken schedule(TimePoint when, OscGroup group) = 0;
    virtual bool cancel(SchedToken token) = 0;
    virtual bool reschedule(SchedToken token, TimePoint when) = 0;
    virtual bool isPending(SchedToken token) const = 0;
    virtual size_t drainEvents(const std::function<void(const OscEvent&)>& fn) = 0;
    virtual size_t pendingCount() const = 0;
};

class OscScheduler : public IOscScheduler
{
public:
    OscScheduler();
    ~OscScheduler() override;

    void start();
    void stop();
    bool running() const { return running_.load(); }

    TimePoint now() const override { return Clock::now(); }
    SchedToken schedule(TimePoint when, OscGroup group) override;
    bool cancel(SchedToken token) override;
    bool reschedule(SchedToken token, TimePoint when) override;
    bool isPending(SchedToken token) const override;
    size_t drainEvents(const std::function<void(const OscEvent&)>& fn) override;
    size_t pendingCount() const override;

    OscSender& sender() { return sender_; }

    // Fires a group synchronously on the calling thread (used by tests / shutdown flush).
    void fireNow(const OscGroup& g, SchedToken token);

private:
    struct Entry
    {
        SchedToken token;
        OscGroup group;
    };
    void run();

    OscSender sender_;
    mutable std::mutex m_;
    std::condition_variable cv_;
    std::multimap<TimePoint, Entry> queue_;
    std::unordered_map<SchedToken, std::multimap<TimePoint, Entry>::iterator> byToken_;
    std::atomic<SchedToken> nextToken_{ 1 };
    std::atomic<bool> running_{ false };
    std::atomic<bool> stopRequested_{ false };
    std::thread thread_;
    ThreadSafeQueue<OscEvent> events_;
};

} // namespace evobox
