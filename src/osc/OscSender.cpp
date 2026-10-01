#include "osc/OscSender.h"
#include "osc/Socket.h"
#include <cerrno>
#include <cstring>

namespace evobox
{

OscSender::OscSender() { socketsInit(); }

OscSender::~OscSender()
{
    if (sock4_ >= 0) EVOBOX_CLOSESOCKET((evobox_socket_t)sock4_);
    if (sock6_ >= 0) EVOBOX_CLOSESOCKET((evobox_socket_t)sock6_);
}

intptr_t OscSender::socketFor(bool ipv6, std::string* err)
{
    intptr_t& s = ipv6 ? sock6_ : sock4_;
    if (s == -2)
    {
        evobox_socket_t fd = ::socket(ipv6 ? AF_INET6 : AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (fd == EVOBOX_INVALID_SOCKET) { s = -1; }
        else
        {
            int bcast = 1;
            setsockopt(fd, SOL_SOCKET, SO_BROADCAST, (const char*)&bcast, sizeof(bcast));
            s = (intptr_t)fd;
        }
    }
    if (s < 0 && err) *err = "cannot create UDP socket";
    return s;
}

bool OscSender::send(const OscEndpoint& ep, const std::vector<uint8_t>& bytes, std::string* err)
{
    if (!ep.valid) { if (err) *err = ep.error.empty() ? "unresolved target" : ep.error; return false; }
    std::lock_guard<std::mutex> lk(m_);
    intptr_t s = socketFor(ep.ipv6, err);
    if (s < 0) return false;
#if defined(_WIN32)
    int n = ::sendto((evobox_socket_t)s, (const char*)bytes.data(), (int)bytes.size(), 0,
                     (const sockaddr*)ep.sockaddrBytes.data(), (int)ep.sockaddrBytes.size());
#else
    ssize_t n = ::sendto((evobox_socket_t)s, bytes.data(), bytes.size(), 0,
                         (const sockaddr*)ep.sockaddrBytes.data(), (socklen_t)ep.sockaddrBytes.size());
#endif
    if (n < 0)
    {
        if (err)
        {
#if defined(_WIN32)
            *err = "sendto failed (" + std::to_string(WSAGetLastError()) + ")";
#else
            *err = std::string("sendto failed: ") + strerror(errno);
#endif
        }
        return false;
    }
    return true;
}

} // namespace evobox
