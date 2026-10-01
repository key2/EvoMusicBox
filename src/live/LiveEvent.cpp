#include "live/LiveEvent.h"
#include "util/Strings.h"

namespace evobox
{

const char* liveEventTypeName(LiveEventType t)
{
    switch (t)
    {
    case LiveEventType::Connect:     return "Connect";
    case LiveEventType::Disconnect:  return "Disconnect";
    case LiveEventType::Comment:     return "Comment";
    case LiveEventType::Gift:        return "Gift";
    case LiveEventType::Like:        return "Like";
    case LiveEventType::Join:        return "Join";
    case LiveEventType::Follow:      return "Follow";
    case LiveEventType::Share:       return "Share";
    case LiveEventType::Subscribe:   return "Subscribe";
    case LiveEventType::RoomUserSeq: return "Viewers";
    case LiveEventType::Control:     return "Control";
    case LiveEventType::LiveEnd:     return "LiveEnd";
    case LiveEventType::Unknown:     return "Unknown";
    case LiveEventType::Error:       return "Error";
    case LiveEventType::Info:        return "Info";
    }
    return "?";
}

LiveEvent LiveEvent::syntheticGift(int64_t giftId, int repeat, bool streaking, const std::string& name,
                                   int diamonds, const std::string& iconUrl, int giftType)
{
    LiveEvent e;
    e.type = LiveEventType::Gift;
    e.method = "Simulate";
    e.giftId = giftId;
    e.giftName = name;
    e.repeatCount = repeat < 1 ? 1 : repeat;
    e.giftStreaking = streaking;
    e.diamondCount = diamonds;
    e.giftIconUrl = iconUrl;
    e.giftType = giftType;
    e.user.nickname = "Simulator";
    e.user.uniqueId = "simulate";
    e.synthetic = true;
    return e;
}

LiveEvent LiveEvent::syntheticRoomEvent(LiveEventType type, int likeCount)
{
    LiveEvent e;
    e.type = type;
    e.method = "Simulate";
    e.likeCount = likeCount;
    e.user.nickname = "Simulator";
    e.user.uniqueId = "simulate";
    e.synthetic = true;
    return e;
}

std::string LiveEvent::summary() const
{
    std::string who = user.display();
    switch (type)
    {
    case LiveEventType::Gift:
        return who + " sent " + (giftName.empty() ? ("gift #" + std::to_string(giftId)) : giftName) +
               (repeatCount > 1 ? " x" + std::to_string(repeatCount) : "") +
               (giftStreaking ? " (streaking)" : "") +
               (diamondCount ? "  " + std::to_string(diamondCount * (repeatCount > 0 ? repeatCount : 1)) + " diamonds" : "");
    case LiveEventType::Comment:     return who + ": " + comment;
    case LiveEventType::Like:        return who + " liked x" + std::to_string(likeCount) + (totalLikes ? "  (total " + str::groupThousands(totalLikes) + ")" : "");
    case LiveEventType::Join:        return who + " joined";
    case LiveEventType::Follow:      return who + " followed";
    case LiveEventType::Share:       return who + " shared";
    case LiveEventType::Subscribe:   return who + " subscribed";
    case LiveEventType::RoomUserSeq: return std::to_string(viewerCount) + " viewers";
    case LiveEventType::Connect:     return "connected" + (message.empty() ? "" : " " + message);
    case LiveEventType::Disconnect:  return "disconnected";
    case LiveEventType::LiveEnd:     return "stream ended";
    case LiveEventType::Control:     return "control " + std::to_string(controlAction) + (message.empty() ? "" : " " + message);
    case LiveEventType::Error:       return "error: " + message;
    case LiveEventType::Info:        return message;
    case LiveEventType::Unknown:     return method.empty() ? "unknown message" : method;
    }
    return method;
}

} // namespace evobox
