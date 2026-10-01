#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "osc/OscScheduler.h"
#include "osc/OscService.h"
#include "osc/Socket.h"
#include <chrono>
#include <thread>

using namespace evobox;

static OscGroup group(uint64_t owner, int phase, const std::string& text = "")
{
    OscGroup g;
    g.ownerUid = owner;
    g.phaseIndex = phase;
    if (!text.empty())
    {
        OscPacket p;
        p.text = text;
        p.skip = true; // no network in unit tests: reported as an error event, still ordered
        p.skipReason = "test";
        g.packets.push_back(p);
    }
    return g;
}

static std::vector<OscEvent> waitEvents(OscScheduler& s, size_t count, int timeoutMs = 2000)
{
    std::vector<OscEvent> out;
    auto t0 = Clock::now();
    while (out.size() < count && Clock::now() - t0 < Millis(timeoutMs))
    {
        s.drainEvents([&](const OscEvent& e) { out.push_back(e); });
        std::this_thread::sleep_for(Millis(1));
    }
    return out;
}

TEST_CASE("groups fire in due order with small jitter")
{
    OscScheduler s;
    s.start();
    TimePoint t0 = s.now();
    SchedToken late = s.schedule(t0 + Millis(60), group(1, 0));
    SchedToken early = s.schedule(t0 + Millis(20), group(2, 0));
    CHECK(s.pendingCount() == 2);
    auto ev = waitEvents(s, 2);
    REQUIRE(ev.size() == 2);
    CHECK(ev[0].kind == OscEvent::Kind::GroupFired);
    CHECK(ev[0].token == early);
    CHECK(ev[1].token == late);
    // jitter: the second group fired ~40 ms after the first
    double dt = ev[1].time - ev[0].time;
    CHECK(dt > 0.030);
    CHECK(dt < 0.070);
    CHECK(s.pendingCount() == 0);
    s.stop();
}

TEST_CASE("cancel and reschedule")
{
    OscScheduler s;
    s.start();
    TimePoint t0 = s.now();
    SchedToken a = s.schedule(t0 + Millis(30), group(1, 1));
    SchedToken b = s.schedule(t0 + Millis(30), group(2, 1));
    CHECK(s.isPending(a));
    CHECK(s.cancel(a));
    CHECK_FALSE(s.isPending(a));
    CHECK_FALSE(s.cancel(a));
    CHECK(s.reschedule(b, t0 + Millis(80)));
    auto ev = waitEvents(s, 2);
    REQUIRE(ev.size() == 2);
    CHECK(ev[0].kind == OscEvent::Kind::GroupCancelled);
    CHECK(ev[0].token == a);
    CHECK(ev[1].kind == OscEvent::Kind::GroupFired);
    CHECK(ev[1].token == b);
    double elapsed = std::chrono::duration<double>(Clock::now() - t0).count();
    CHECK(elapsed >= 0.079);
    s.stop();
}

TEST_CASE("packets produce Sent/Error events before the GroupFired marker")
{
    OscScheduler s;
    s.start();
    OscGroup g = group(5, 0, "/skipped 1");
    OscEndpoint ep = OscEndpoint::resolve("127.0.0.1", 39123); // nothing listens: UDP send still succeeds
    OscMessage m; m.address = "/real"; m.args.push_back(OscArg::Int(1));
    OscPacket real; real.bytes = m.encode(); real.endpoint = ep; real.text = m.toString(); real.commandUid = 77;
    g.packets.push_back(real);
    SchedToken tok = s.schedule(s.now(), g);
    auto ev = waitEvents(s, 3);
    REQUIRE(ev.size() == 3);
    CHECK(ev[0].kind == OscEvent::Kind::Error);
    CHECK(ev[1].kind == OscEvent::Kind::Sent);
    CHECK(ev[1].commandUid == 77);
    CHECK(ev[2].kind == OscEvent::Kind::GroupFired);
    CHECK(ev[2].token == tok);
    s.stop();
}

TEST_CASE("OscService: endpoint cache + packet building")
{
    OscService svc;
    const OscEndpoint& e1 = svc.endpoint(1, 1, "127.0.0.1", 8000);
    CHECK(e1.valid);
    CHECK_FALSE(e1.ipv6);
    CHECK(e1.port == 8000);
    const OscEndpoint& e2 = svc.endpoint(1, 1, "127.0.0.1", 8000);
    CHECK(&e1 == &e2); // cached
    const OscEndpoint& e3 = svc.endpoint(1, 2, "127.0.0.1", 9000); // revision bump -> re-resolved
    CHECK(e3.port == 9000);
    const OscEndpoint& bad = svc.endpoint(2, 1, "no.such.host.invalid", 8000);
    CHECK_FALSE(bad.valid);
    OscPacket ok = svc.makePacket("/a 1", 10, 1, e3);
    CHECK_FALSE(ok.skip);
    CHECK(ok.bytes.size() == 12); // "/a\0\0" + ",i\0\0" + int32
    OscPacket invalid = svc.makePacket("a 1", 10, 1, e3);
    CHECK(invalid.skip);
    OscPacket unresolved = svc.makePacket("/a 1", 10, 2, bad);
    CHECK(unresolved.skip);
}

TEST_CASE("OscEndpoint resolves IPv6 literals")
{
    OscEndpoint ep = OscEndpoint::resolve("::1", 8000);
    if (ep.valid) CHECK(ep.ipv6);
    OscEndpoint def = OscEndpoint::resolve("", 0);
    CHECK(def.valid);
    CHECK(def.host == "127.0.0.1");
    CHECK(def.port == 8000);
}
