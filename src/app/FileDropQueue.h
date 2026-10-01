// FileDropQueue.h — OS drag & drop: glfwSetDropCallback stores {paths, mousePos}; on the next
// frame each drop zone checks `pos in zoneRect` and claims the drop.
#pragma once

#include <string>
#include <vector>
#include "imgui.h"

namespace evobox
{

struct FileDrop
{
    std::vector<std::string> paths;
    ImVec2 pos;         // screen position at drop time
    bool claimed = false;
};

class FileDropQueue
{
public:
    void push(std::vector<std::string> paths, ImVec2 pos) { drops_.push_back({ std::move(paths), pos, false }); }

    // Zones call this while drawing; the first zone whose rect contains the drop claims it.
    // Returns the paths (empty when nothing dropped there).
    std::vector<std::string> claim(ImVec2 rectMin, ImVec2 rectMax)
    {
        for (auto& d : drops_)
        {
            if (d.claimed) continue;
            if (d.pos.x >= rectMin.x && d.pos.x < rectMax.x && d.pos.y >= rectMin.y && d.pos.y < rectMax.y)
            {
                d.claimed = true;
                return d.paths;
            }
        }
        return {};
    }
    // Any unclaimed drop (fallback: import into the current category).
    std::vector<std::string> claimAny()
    {
        for (auto& d : drops_)
            if (!d.claimed) { d.claimed = true; return d.paths; }
        return {};
    }
    bool pending() const { for (auto& d : drops_) if (!d.claimed) return true; return false; }
    // End of frame: forget everything (claimed or not).
    void endFrame() { drops_.clear(); }

private:
    std::vector<FileDrop> drops_;
};

} // namespace evobox
