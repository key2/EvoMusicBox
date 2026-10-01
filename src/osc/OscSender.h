// OscSender.h — UDP transmit: one socket per address family, shared by the scheduler thread.
#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>
#include "osc/OscEndpoint.h"

namespace evobox
{

class OscSender
{
public:
    OscSender();
    ~OscSender();
    OscSender(const OscSender&) = delete;
    OscSender& operator=(const OscSender&) = delete;

    // Returns false and fills err on failure. Thread-safe.
    bool send(const OscEndpoint& ep, const std::vector<uint8_t>& bytes, std::string* err = nullptr);
    bool ok() const { return sock4_ != -2; }

private:
    std::mutex m_;
    intptr_t sock4_ = -2; // -2 = not created, -1 = failed
    intptr_t sock6_ = -2;
    intptr_t socketFor(bool ipv6, std::string* err);
};

} // namespace evobox
