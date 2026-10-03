#include "ui/widgets/TrimFields.h"
#include "ui/I18n.h"
#include "ui/Theme.h"
#include "ui/widgets/Common.h"
#include "util/TimeFormat.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace evobox
{
namespace ui
{

bool TimeField(const char* label, double& seconds, double minV, double maxV, float width)
{
    ImGui::PushID(label);
    static char s_buf[64];
    static ImGuiID s_activeId = 0;
    ImGuiID id = ImGui::GetID("##time");
    bool committed = false;
    char buf[64];
    std::string cur = formatTime(seconds, true);
    if (s_activeId == id) { strncpy(buf, s_buf, sizeof(buf) - 1); buf[sizeof(buf) - 1] = 0; }
    else { strncpy(buf, cur.c_str(), sizeof(buf) - 1); buf[sizeof(buf) - 1] = 0; }
    if (width > 0) ImGui::SetNextItemWidth(width);
    ImGui::InputText("##time", buf, sizeof(buf), ImGuiInputTextFlags_AutoSelectAll);
    if (ImGui::IsItemActivated()) { s_activeId = id; strncpy(s_buf, cur.c_str(), sizeof(s_buf) - 1); s_buf[sizeof(s_buf) - 1] = 0; }
    if (ImGui::IsItemActive() && s_activeId == id) { strncpy(s_buf, buf, sizeof(s_buf) - 1); s_buf[sizeof(s_buf) - 1] = 0; }
    if (ImGui::IsItemDeactivated() && s_activeId == id)
    {
        double v;
        if (parseTime(buf, v))
        {
            v = std::clamp(v, minV, maxV);
            if (std::fabs(v - seconds) > 1e-6) { seconds = v; committed = true; }
        }
        s_activeId = 0;
    }
    if (label[0] != '#')
    {
        ImGui::SameLine();
        ImGui::TextUnformatted(label);
    }
    ImGui::PopID();
    return committed;
}

TrimFieldsResult TrimFields(const char* id, double& start, double& end, double duration, float fieldWidth)
{
    TrimFieldsResult r;
    ImGui::PushID(id);
    if (fieldWidth <= 0) fieldWidth = 96.f * theme::scale();
    ImGui::AlignTextToFramePadding();
    TextDim("%s", TR("trim.start"));
    ImGui::SameLine(64 * theme::scale());
    double s = start, e = end;
    if (TimeField("##start", s, 0.0, std::max(0.0, e - 0.005), fieldWidth)) { start = s; r.changed = true; }
    ImGui::SameLine();
    TextDim("%s", TR("trim.end"));
    ImGui::SameLine();
    if (TimeField("##end", e, std::min(duration, s + 0.005), duration, fieldWidth)) { end = e; r.changed = true; }
    ImGui::SameLine();
    TextDim("%s", TR("trim.duration"));
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Text, theme::colors().accent);
    ImGui::TextUnformatted(formatTime(std::max(0.0, end - start), true).c_str());
    ImGui::PopStyleColor();
    ImGui::PopID();
    return r;
}

} // namespace ui
} // namespace evobox
