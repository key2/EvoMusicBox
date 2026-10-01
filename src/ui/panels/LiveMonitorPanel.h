// LiveMonitorPanel.h — bottom panel for gifts: left the phase timeline of the selected gift /
// room event (live marker while active, drag the Stop marker), right the live event feed
// (last 200, filterable), viewers and Simulate.
#pragma once

#include "app/Application.h"

namespace evobox
{
namespace ui
{

class LiveMonitorPanel
{
public:
    explicit LiveMonitorPanel(Application& app) : app_(app) {}
    void draw(bool* open);

private:
    Application& app_;
    char filter_[64] = "";
    bool showComments_ = true;
    bool showLikes_ = true;
    bool showJoins_ = true;
    bool autoScroll_ = true;
    void drawTimeline();
    void drawFeed();
};

} // namespace ui
} // namespace evobox
