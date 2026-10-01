#include "osc/OscService.h"

namespace evobox
{

OscService::OscService()
{
    auto s = std::make_unique<OscScheduler>();
    realSched_ = s.get();
    sched_ = std::move(s);
}

OscService::~OscService() { stop(); }

void OscService::start()
{
    if (realSched_) realSched_->start();
}

void OscService::stop()
{
    if (realSched_) realSched_->stop();
}

void OscService::setScheduler(std::unique_ptr<IOscScheduler> s)
{
    stop();
    realSched_ = dynamic_cast<OscScheduler*>(s.get());
    sched_ = std::move(s);
}

const OscEndpoint& OscService::endpoint(uint64_t targetUid, uint32_t revision, const std::string& host, int port)
{
    auto it = endpoints_.find(targetUid);
    if (it == endpoints_.end() || it->second.revision != revision ||
        it->second.ep.host != (host.empty() ? "127.0.0.1" : host) || it->second.ep.port != port)
    {
        CachedEndpoint c;
        c.revision = revision;
        c.ep = OscEndpoint::resolve(host, port);
        it = endpoints_.insert_or_assign(targetUid, std::move(c)).first;
    }
    return it->second.ep;
}

void OscService::forgetEndpoint(uint64_t targetUid) { endpoints_.erase(targetUid); }

OscPacket OscService::makePacket(const std::string& text, uint64_t commandUid, uint64_t targetUid, const OscEndpoint& ep) const
{
    OscPacket p;
    p.commandUid = commandUid;
    p.targetUid = targetUid;
    p.endpoint = ep;
    ParsedCommand pc = OscCommandParser::parse(text);
    if (!pc.ok)
    {
        p.skip = true;
        p.skipReason = "invalid OSC command '" + text + "': " + pc.error;
        p.text = text;
        return p;
    }
    p.text = pc.message.toString();
    p.bytes = pc.message.encode();
    if (!ep.valid)
    {
        p.skip = true;
        p.skipReason = "target " + ep.label() + " unresolved: " + ep.error;
    }
    return p;
}

SchedToken OscService::sendNow(OscGroup group)
{
    return sched_->schedule(sched_->now(), std::move(group));
}

SchedToken OscService::sendAfter(int delayMs, OscGroup group)
{
    if (delayMs < 0) delayMs = 0;
    return sched_->schedule(sched_->now() + Millis(delayMs), std::move(group));
}

} // namespace evobox
