#include "live/TikTokLiveService.h"
#include "util/Paths.h"
#include "util/Strings.h"
#include <filesystem>
#include "ttlive/client.hpp"

namespace evobox
{

namespace
{

LiveEventType convertType(ttlive::EventType t)
{
    switch (t)
    {
    case ttlive::EventType::Connect:     return LiveEventType::Connect;
    case ttlive::EventType::Disconnect:  return LiveEventType::Disconnect;
    case ttlive::EventType::Comment:     return LiveEventType::Comment;
    case ttlive::EventType::Gift:        return LiveEventType::Gift;
    case ttlive::EventType::Like:        return LiveEventType::Like;
    case ttlive::EventType::Join:        return LiveEventType::Join;
    case ttlive::EventType::Follow:      return LiveEventType::Follow;
    case ttlive::EventType::Share:       return LiveEventType::Share;
    case ttlive::EventType::Subscribe:   return LiveEventType::Subscribe;
    case ttlive::EventType::RoomUserSeq: return LiveEventType::RoomUserSeq;
    case ttlive::EventType::Control:     return LiveEventType::Control;
    case ttlive::EventType::LiveEnd:     return LiveEventType::LiveEnd;
    default:                             return LiveEventType::Unknown;
    }
}

LiveEvent convert(const ttlive::Event& e)
{
    LiveEvent o;
    o.type = convertType(e.type);
    o.method = e.method;
    o.user.uniqueId = e.user.unique_id;
    o.user.nickname = e.user.nickname;
    o.user.avatarUrl = e.user.avatar_url;
    o.comment = e.comment;
    o.giftId = e.gift_id;
    o.giftName = e.gift_name;
    o.repeatCount = e.repeat_count > 0 ? e.repeat_count : 1;
    o.giftStreaking = e.gift_streaking;
    o.diamondCount = e.diamond_count;
    o.giftType = e.gift_type;
    o.giftIconUrl = e.gift_icon_url;
    o.likeCount = e.like_count;
    o.totalLikes = e.total_likes;
    o.memberCount = e.member_count;
    o.viewerCount = e.viewer_count;
    o.controlAction = e.control_action;
    if (e.type == ttlive::EventType::Connect)
        o.message = "room " + std::to_string(e.room_id);
    return o;
}

} // namespace

TikTokLiveService::TikTokLiveService() = default;

TikTokLiveService::~TikTokLiveService() { disconnect(); }

std::string TikTokLiveService::defaultJsDir()
{
    std::error_code ec;
    std::filesystem::path p = paths::exeDir() / "tiktok-js";
    if (std::filesystem::exists(p / "hybrid-fake-dom.js", ec)) return p.string();
#ifdef EVOBOX_SOURCE_DIR
    p = std::filesystem::path(EVOBOX_SOURCE_DIR) / "third_party" / "ttlive-cpp" / "js";
    if (std::filesystem::exists(p / "hybrid-fake-dom.js", ec)) return p.string();
#endif
    return ""; // library default (compile-time path)
}

std::string TikTokLiveService::username() const
{
    std::lock_guard<std::mutex> lk(strMutex_);
    return username_;
}

std::string TikTokLiveService::lastError() const
{
    std::lock_guard<std::mutex> lk(strMutex_);
    return lastError_;
}

void TikTokLiveService::setState(LiveState s, const std::string& msg)
{
    state_.store(s);
    if (s == LiveState::Error)
    {
        std::lock_guard<std::mutex> lk(strMutex_);
        lastError_ = msg;
    }
    LiveServiceEvent ev;
    ev.kind = LiveServiceEvent::Kind::State;
    ev.state = s;
    ev.message = msg;
    events_.push(std::move(ev));
}

void TikTokLiveService::connect(const std::string& usernameIn, const LiveOptions& o)
{
    disconnect();
    std::string username = str::cleanUsername(usernameIn);
    if (username.empty())
    {
        setState(LiveState::Error, "enter a TikTok username first");
        return;
    }
    {
        std::lock_guard<std::mutex> lk(strMutex_);
        username_ = username;
        lastError_.clear();
    }
    stopRequested_ = false;
    setState(LiveState::Connecting, "@" + username);
    thread_ = std::thread([this, username, o] { run(username, o); });
}

void TikTokLiveService::run(std::string username, LiveOptions o)
{
    try
    {
        ttlive::ClientOptions co;
        co.use_websocket = o.useWebsocket;
        co.use_polling = o.usePolling;
        co.fetch_live_check = o.fetchLiveCheck;
        co.fetch_stream_info = true;
        co.fetch_gift_list = true;               // the gallery depends on it
        co.process_connect_events = o.processConnectEvents;
        co.cookies = o.cookies;
        co.js_dir = o.jsDir.empty() ? defaultJsDir() : o.jsDir;

        std::unique_ptr<ttlive::TikTokLiveClient> client;
        client = std::make_unique<ttlive::TikTokLiveClient>(username, co);
        ttlive::TikTokLiveClient* raw = client.get();
        {
            std::lock_guard<std::mutex> lk(clientMutex_);
            client_ = std::move(client);
        }
        if (stopRequested_)
        {
            std::lock_guard<std::mutex> lk(clientMutex_);
            client_.reset();
            setState(LiveState::Disconnected);
            return;
        }

        // Register every handler BEFORE run() (on() is not synchronized).
        raw->on(ttlive::EventType::Connect, [this, raw, username](const ttlive::Event& e)
        {
            LiveServiceEvent cat;
            cat.kind = LiveServiceEvent::Kind::Catalog;
            cat.roomUser = username;
            for (const ttlive::GiftInfo& g : raw->gift_list())
            {
                GiftInfo gi;
                gi.id = g.id;
                gi.name = g.name;
                gi.diamondCount = g.diamond_count;
                gi.describe = g.describe;
                gi.type = g.type;
                gi.iconUrl = g.icon_url;
                cat.catalog.push_back(std::move(gi));
            }
            events_.push(std::move(cat));
            state_.store(LiveState::Connected);
            LiveServiceEvent st;
            st.kind = LiveServiceEvent::Kind::State;
            st.state = LiveState::Connected;
            st.message = "room " + std::to_string(e.room_id);
            events_.push(std::move(st));
        });
        raw->on_any([this](const ttlive::Event& e)
        {
            if (e.type == ttlive::EventType::RoomUserSeq) viewers_.store((int)e.viewer_count);
            LiveServiceEvent ev;
            ev.kind = LiveServiceEvent::Kind::Event;
            ev.event = convert(e);
            events_.push(std::move(ev));
        });

        raw->run(); // blocks until the stream ends or disconnect()

        LiveState final = state_.load() == LiveState::Ended ? LiveState::Ended : LiveState::Disconnected;
        setState(final);
    }
    catch (const std::exception& ex)
    {
        setState(LiveState::Error, ex.what());
    }
    catch (...)
    {
        setState(LiveState::Error, "unknown error");
    }
}

void TikTokLiveService::disconnect()
{
    stopRequested_ = true;
    {
        std::lock_guard<std::mutex> lk(clientMutex_);
        if (client_) client_->disconnect();
    }
    if (thread_.joinable()) thread_.join();
    {
        std::lock_guard<std::mutex> lk(clientMutex_);
        client_.reset(); // never destroy during run(): joined above
    }
    if (state_.load() == LiveState::Connecting || state_.load() == LiveState::Connected)
        setState(LiveState::Disconnected);
    viewers_ = 0;
}

size_t TikTokLiveService::drainEvents(const std::function<void(const LiveServiceEvent&)>& fn)
{
    return events_.drain([&](LiveServiceEvent& e)
    {
        if (e.kind == LiveServiceEvent::Kind::Event && e.event.type == LiveEventType::LiveEnd)
            state_.store(LiveState::Ended);
        fn(e);
    });
}

} // namespace evobox
