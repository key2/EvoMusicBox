// OscEndpoint.h — a resolved UDP destination (host:port -> sockaddr). Resolution happens on
// edit (main thread / service), never on the trigger path.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace evobox
{

struct OscEndpoint
{
    std::string host;
    int port = 8000;
    bool valid = false;
    bool ipv6 = false;
    std::string error;
    std::vector<uint8_t> sockaddrBytes; // sockaddr_in / sockaddr_in6 raw storage

    std::string label() const { return host + ":" + std::to_string(port); }

    // getaddrinfo (numeric hosts are resolved without DNS). Empty host -> 127.0.0.1.
    static OscEndpoint resolve(const std::string& host, int port);
};

} // namespace evobox
