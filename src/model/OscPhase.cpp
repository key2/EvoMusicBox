#include "model/OscPhase.h"

namespace evobox
{

const char* anchorName(Anchor a)
{
    switch (a)
    {
    case Anchor::Start: return "start";
    case Anchor::End:   return "end";
    case Anchor::Timer: return "timer";
    }
    return "start";
}

bool anchorFromName(const std::string& s, Anchor& out)
{
    if (s == "start") { out = Anchor::Start; return true; }
    if (s == "end")   { out = Anchor::End;   return true; }
    if (s == "timer") { out = Anchor::Timer; return true; }
    return false;
}

OscPhase::OscPhase(const std::string& name, Anchor a, int delayMs, organic::Container* parent)
    : organic::Container(name, parent), anchor(a), commands(this)
{
    renamable = false;
    // Negative delays stay representable (UI.md §17 "later option"); the UI clamps at 0 in v1.
    delayP = addInt(a == Anchor::Timer ? "Timer" : "Delay", delayMs, -600000, 3600000,
                    a == Anchor::Timer ? "Milliseconds after the trigger before these commands are sent (restartable)"
                                       : "Milliseconds relative to the anchor");
    delayP->unit = "ms";
}

json OscPhase::save() const
{
    json j = Container::save();
    j["name"] = niceName;
    j["anchor"] = anchorName(anchor);
    j["commands"] = commands.save();
    return j;
}

void OscPhase::load(const json& j)
{
    // keep the owner's name/anchor: only the delay and the rows are user data
    if (j.contains("params")) Container::load(json{ { "params", j["params"] } });
    if (j.contains("commands")) commands.load(j["commands"]);
}

} // namespace evobox
