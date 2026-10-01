#include "model/OscActions.h"

namespace evobox
{

OscActions::OscActions(organic::Container* parent)
    : organic::Container("OSC actions", parent)
{
    renamable = false;
}

OscPhase* OscActions::addPhase(const std::string& name, Anchor anchor, int delayMs)
{
    phases.push_back(std::make_unique<OscPhase>(name, anchor, delayMs, this));
    return phases.back().get();
}

OscPhase* OscActions::phase(Anchor a) const
{
    for (auto& p : phases) if (p->anchor == a) return p.get();
    return nullptr;
}

int OscActions::indexOf(const OscPhase* p) const
{
    for (size_t i = 0; i < phases.size(); i++)
        if (phases[i].get() == p) return (int)i;
    return -1;
}

bool OscActions::hasCommands() const
{
    for (auto& p : phases) if (p->hasCommands()) return true;
    return false;
}

size_t OscActions::commandCount(bool enabledOnly) const
{
    size_t n = 0;
    for (auto& p : phases)
        n += enabledOnly ? p->commands.enabledCount() : p->commands.items.size();
    return n;
}

std::vector<Uid> OscActions::referencedTargets() const
{
    std::vector<Uid> out;
    for (auto& p : phases)
        for (auto* c : p->commands.commands())
            if (std::find(out.begin(), out.end(), c->targetUid) == out.end())
                out.push_back(c->targetUid);
    return out;
}

int OscActions::retarget(Uid from, Uid to)
{
    int n = 0;
    for (auto& p : phases) n += p->commands.retarget(from, to);
    return n;
}

void OscActions::copyFrom(const OscActions& other)
{
    json j = other.save();
    load(j);
}

void OscActions::clearCommands()
{
    for (auto& p : phases)
    {
        std::vector<Uid> uids;
        for (auto* c : p->commands.commands()) uids.push_back(c->uid);
        for (Uid u : uids) p->commands.removeItem(u);
    }
}

json OscActions::save() const
{
    json j;
    json arr = json::array();
    for (auto& p : phases) arr.push_back(p->save());
    j["phases"] = arr;
    return j;
}

void OscActions::load(const json& j)
{
    if (!j.is_object() || !j.contains("phases") || !j["phases"].is_array()) return;
    const json& arr = j["phases"];
    std::vector<bool> used(phases.size(), false);
    for (size_t i = 0; i < arr.size(); i++)
    {
        const json& pj = arr[i];
        Anchor a = Anchor::Start;
        bool hasAnchor = anchorFromName(jget<std::string>(pj, "anchor", ""), a);
        std::string name = jget<std::string>(pj, "name", "");
        OscPhase* target = nullptr;
        // 1. same index with matching anchor
        if (i < phases.size() && !used[i] && (!hasAnchor || phases[i]->anchor == a)) target = phases[i].get();
        // 2. by anchor + name
        if (!target)
            for (size_t k = 0; k < phases.size(); k++)
                if (!used[k] && (!hasAnchor || phases[k]->anchor == a) && (name.empty() || phases[k]->niceName == name))
                { target = phases[k].get(); break; }
        // 3. by anchor only
        if (!target && hasAnchor)
            for (size_t k = 0; k < phases.size(); k++)
                if (!used[k] && phases[k]->anchor == a) { target = phases[k].get(); break; }
        if (!target) continue;
        used[(size_t)indexOf(target)] = true;
        target->load(pj);
    }
}

} // namespace evobox
