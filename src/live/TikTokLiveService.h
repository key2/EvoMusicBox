// TikTokLiveService.h — ttlive-cpp wrapped in a service thread. Callbacks copy events into a
// queue and nothing else; the Connect callback additionally posts one immutable gift catalog
// snapshot. Only compiled with EVOBOX_WITH_TIKTOK.
#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "live/GiftCatalog.h"
#include "live/LiveEvent.h"
#include "live/LiveEventRouter.h"
#include "util/ThreadSafeQueue.h"

namespace ttlive { class TikTokLiveClient; }

namespace evobox
{

struct LiveOptions
{
    bool useWebsocket = true;
    bool usePolling = false;
    bool fetchLiveCheck = true;
    bool processConnectEvents = false; // backlog comments would fire triggers
    std::string cookies;
    std::string jsDir;                 // <exe>/tiktok-js by default
};

struct LiveServiceEvent
{
    enum class Kind { Event, Catalog, State };
    Kind kind = Kind::Event;
    LiveEvent event;
    std::vector<GiftInfo> catalog;   // Kind::Catalog
    std::string roomUser;
    LiveState state = LiveState::Disconnected; // Kind::State
    std::string message;
};

class TikTokLiveService
{
public:
    TikTokLiveService();
    ~TikTokLiveService();

    void connect(const std::string& username, const LiveOptions& o);
    void disconnect();          // client->disconnect(); join; reset
    bool busy() const { return thread_.joinable(); }

    LiveState state() const { return state_.load(); }
    int viewers() const { return viewers_.load(); }
    std::string username() const;
    std::string lastError() const;

    size_t drainEvents(const std::function<void(const LiveServiceEvent&)>& fn);

    static std::string defaultJsDir();

private:
    void run(std::string username, LiveOptions o);
    void setState(LiveState s, const std::string& msg = "");

    std::thread thread_;
    std::unique_ptr<ttlive::TikTokLiveClient> client_;
    std::mutex clientMutex_;
    std::atomic<LiveState> state_{ LiveState::Disconnected };
    std::atomic<int> viewers_{ 0 };
    std::atomic<bool> stopRequested_{ false };
    mutable std::mutex strMutex_;
    std::string username_;
    std::string lastError_;
    ThreadSafeQueue<LiveServiceEvent> events_;
};

} // namespace evobox
