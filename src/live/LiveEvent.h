// LiveEvent.h — our own copy of a TikTok LIVE event (no ttlive dependency so the router and
// the Simulate path compile without EVOBOX_WITH_TIKTOK).
#pragma once

#include <cstdint>
#include <string>

namespace evobox
{

enum class LiveEventType
{
    Connect, Disconnect, Comment, Gift, Like, Join, Follow, Share, Subscribe,
    RoomUserSeq, Control, LiveEnd, Unknown, Error, Info
};
const char* liveEventTypeName(LiveEventType t);

struct LiveUser
{
    std::string uniqueId;
    std::string nickname;
    std::string avatarUrl;
    std::string display() const { return nickname.empty() ? (uniqueId.empty() ? "someone" : "@" + uniqueId) : nickname; }
};

struct LiveEvent
{
    LiveEventType type = LiveEventType::Unknown;
    std::string method;         // raw Webcast method name
    LiveUser user;
    std::string comment;

    int64_t giftId = 0;
    std::string giftName;
    int repeatCount = 1;
    bool giftStreaking = false;
    int diamondCount = 0;
    int giftType = 0;
    std::string giftIconUrl;

    int likeCount = 0;
    int64_t totalLikes = 0;
    int64_t memberCount = 0;
    int64_t viewerCount = 0;
    int controlAction = 0;

    std::string message;        // Error / Info / Control text
    double time = 0;            // app seconds when received (set by the router)
    bool synthetic = false;     // Simulate

    static LiveEvent syntheticGift(int64_t giftId, int repeat = 1, bool streaking = false,
                                   const std::string& name = "", int diamonds = 0,
                                   const std::string& iconUrl = "", int giftType = 0);
    static LiveEvent syntheticRoomEvent(LiveEventType type, int likeCount = 1);

    // one-line description for the feed / logger
    std::string summary() const;
};

} // namespace evobox
