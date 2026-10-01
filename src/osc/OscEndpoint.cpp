#include "osc/OscEndpoint.h"
#include "osc/Socket.h"
#include <cstring>
#include <mutex>

namespace evobox
{

void socketsInit()
{
#if defined(_WIN32)
    static std::once_flag once;
    std::call_once(once, [] { WSADATA d; WSAStartup(MAKEWORD(2, 2), &d); });
#endif
}

OscEndpoint OscEndpoint::resolve(const std::string& hostIn, int port)
{
    socketsInit();
    OscEndpoint ep;
    ep.host = hostIn.empty() ? "127.0.0.1" : hostIn;
    ep.port = (port <= 0 || port > 65535) ? 8000 : port;

    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;
    // numeric first (no DNS on the hot path when the user typed an IP)
    hints.ai_flags = AI_NUMERICHOST;
    addrinfo* res = nullptr;
    std::string portStr = std::to_string(ep.port);
    int rc = getaddrinfo(ep.host.c_str(), portStr.c_str(), &hints, &res);
    if (rc != 0)
    {
        hints.ai_flags = 0;
        rc = getaddrinfo(ep.host.c_str(), portStr.c_str(), &hints, &res);
    }
    if (rc != 0 || !res)
    {
#if defined(_WIN32)
        ep.error = "cannot resolve host";
#else
        ep.error = gai_strerror(rc);
#endif
        return ep;
    }
    // prefer IPv4
    addrinfo* pick = nullptr;
    for (addrinfo* p = res; p; p = p->ai_next) if (p->ai_family == AF_INET) { pick = p; break; }
    if (!pick) for (addrinfo* p = res; p; p = p->ai_next) if (p->ai_family == AF_INET6) { pick = p; break; }
    if (!pick) pick = res;
    ep.ipv6 = pick->ai_family == AF_INET6;
    ep.sockaddrBytes.assign((const uint8_t*)pick->ai_addr, (const uint8_t*)pick->ai_addr + pick->ai_addrlen);
    ep.valid = true;
    freeaddrinfo(res);
    return ep;
}

} // namespace evobox
