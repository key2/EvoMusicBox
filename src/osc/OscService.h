// OscService.h — the OSC service facade owned by the application: endpoint resolution cache
// (resolved on edit, never on the trigger path), the scheduler thread and the sender.
// Model-agnostic: the app layer (TriggerController) turns phases into OscGroups.
#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include "osc/OscCommandParser.h"
#include "osc/OscEndpoint.h"
#include "osc/OscScheduler.h"

namespace evobox
{

class OscService
{
public:
    OscService();
    ~OscService();

    void start();
    void stop();

    IOscScheduler& scheduler() { return *sched_; }
    OscScheduler* realScheduler() { return realSched_; } // nullptr when a fake scheduler is installed
    // Tests may swap in a fake scheduler; the service then never starts a thread.
    void setScheduler(std::unique_ptr<IOscScheduler> s);

    // Endpoint cache keyed by target uid; re-resolved when `revision` changes.
    const OscEndpoint& endpoint(uint64_t targetUid, uint32_t revision, const std::string& host, int port);
    void forgetEndpoint(uint64_t targetUid);

    // Builds a packet from command text (parse + encode). Invalid text -> skip=true with reason.
    OscPacket makePacket(const std::string& text, uint64_t commandUid, uint64_t targetUid, const OscEndpoint& ep) const;

    // Fire immediately (Test button): returns the token.
    SchedToken sendNow(OscGroup group);
    // Fire at now + delayMs.
    SchedToken sendAfter(int delayMs, OscGroup group);

    size_t drainEvents(const std::function<void(const OscEvent&)>& fn) { return sched_->drainEvents(fn); }

private:
    struct CachedEndpoint { uint32_t revision = 0; OscEndpoint ep; };
    std::unordered_map<uint64_t, CachedEndpoint> endpoints_;
    std::unique_ptr<IOscScheduler> sched_;
    OscScheduler* realSched_ = nullptr; // when sched_ is the real one
};

} // namespace evobox
