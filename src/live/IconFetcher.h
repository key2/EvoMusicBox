// IconFetcher.h — one worker thread + one CURL* easy handle (curl-impersonate, the libcurl that
// ttlive already links — never a second libcurl) downloading gift icons into the disk cache
// <cache>/gift-icons/<sha1(url)>. Only compiled with EVOBOX_WITH_TIKTOK.
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <thread>
#include "util/ThreadSafeQueue.h"

namespace evobox
{

struct IconRequest
{
    std::string url;
    int64_t giftId = 0;
};

struct IconResult
{
    int64_t giftId = 0;
    std::string url;
    std::string path;  // cached file
    bool ok = false;
    std::string error;
    bool fromCache = false;
};

class IconFetcher
{
public:
    IconFetcher();
    ~IconFetcher();

    void start(const std::string& cacheDir);
    void stop();
    bool running() const { return thread_.joinable(); }

    void fetch(const std::string& url, int64_t giftId);
    size_t drain(const std::function<void(const IconResult&)>& fn);

    static std::string cachePathFor(const std::string& cacheDir, const std::string& url);
    int timeoutSeconds = 10;

private:
    void run();
    std::thread thread_;
    std::string cacheDir_;
    ThreadSafeQueue<IconRequest> requests_;
    ThreadSafeQueue<IconResult> results_;
};

} // namespace evobox
