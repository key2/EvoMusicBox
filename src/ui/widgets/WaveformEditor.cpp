#include "ui/widgets/WaveformEditor.h"
#include "ui/I18n.h"
#include "ui/Theme.h"
#include "util/TimeFormat.h"
#include <algorithm>
#include <cmath>

namespace evobox
{
namespace ui
{

WaveformColors defaultWaveformColors()
{
    const auto& c = theme::colors();
    WaveformColors w;
    w.bg = theme::u32(c.waveformBg);
    w.wave = theme::u32(c.waveform, 0.55f);
    w.waveSelected = theme::u32(c.waveformSel);
    w.selectionBg = theme::u32(c.selectionBg);
    w.handle = theme::u32(c.accent);
    w.handleHover = theme::u32(theme::lighten(c.accent, 0.2f));
    w.playhead = theme::u32(c.playhead);
    w.ruler = theme::u32(c.panelBg);
    w.rulerText = theme::u32(c.textDim);
    w.border = theme::u32(c.border);
    w.grid = theme::u32(ImVec4(1, 1, 1, 0.05f));
    return w;
}

// Ruler label with just enough decimals for the step: 0:01 / 0:01.2 / 0:01.25 / 0:01.250
static std::string rulerLabel(double t, double step)
{
    if (step >= 1.0) return formatTime(t, false);
    int decimals = step >= 0.1 ? 1 : (step >= 0.01 ? 2 : 3);
    std::string full = formatTime(t, true); // m:ss.mmm
    return full.substr(0, full.size() - (3 - decimals));
}

double niceTimeStep(double pps, float minLabelPx)
{
    if (pps <= 0) return 1.0;
    double minStep = minLabelPx / pps;
    static const double steps[] = { 0.001, 0.002, 0.005, 0.01, 0.02, 0.05, 0.1, 0.2, 0.25, 0.5, 1, 2, 5, 10, 15, 30, 60, 120, 300, 600, 1800, 3600 };
    for (double s : steps) if (s >= minStep) return s;
    return 7200;
}

double WaveformEditor::xToTime(const WaveformView& v, float x0, float x)
{
    return v.t0 + (x - x0) / std::max(1e-6, v.pixelsPerSecond);
}

float WaveformEditor::timeToX(const WaveformView& v, float x0, double t)
{
    return x0 + (float)((t - v.t0) * v.pixelsPerSecond);
}

void WaveformEditor::clampView(WaveformView& view, double duration, float widthPx)
{
    if (duration <= 0 || widthPx <= 0) return;
    double minPps = widthPx / duration;             // whole file fits
    double maxPps = widthPx / 0.01;                 // 10 ms across the width
    view.pixelsPerSecond = std::clamp(view.pixelsPerSecond, minPps, std::max(minPps, maxPps));
    double visible = widthPx / view.pixelsPerSecond;
    view.t0 = std::clamp(view.t0, 0.0, std::max(0.0, duration - visible));
}

void WaveformEditor::zoomAt(WaveformView& view, double factor, double anchorTime, double duration, float widthPx)
{
    double oldPps = view.pixelsPerSecond;
    view.pixelsPerSecond *= factor;
    clampView(view, duration, widthPx);
    // keep the anchor time under the same pixel
    double px = (anchorTime - view.t0) * oldPps;
    view.t0 = anchorTime - px / view.pixelsPerSecond;
    clampView(view, duration, widthPx);
}

WaveformResult WaveformEditor::draw(const char* id, const organic::Peaks* peaks, double duration,
                                    WaveformSelection& sel, WaveformView& view, double playhead, bool showPlayhead,
                                    ImVec2 size, const WaveformColors* colorsIn, const std::vector<WaveformPlayhead>* playheads)
{
    WaveformResult r;
    WaveformColors col = colorsIn ? *colorsIn : defaultWaveformColors();
    ImVec2 avail = ImGui::GetContentRegionAvail();
    if (size.x <= 0) size.x = avail.x;
    if (size.y <= 0) size.y = std::max(60.f, avail.y);
    if (size.x < 10 || size.y < 30) { ImGui::Dummy(size); return r; }

    ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImVec2 p1(p0.x + size.x, p0.y + size.y);
    float ruler = rulerHeight * theme::scale();
    ImVec2 w0(p0.x, p0.y + ruler);   // waveform body
    float bodyH = p1.y - w0.y;
    float widthPx = size.x;

    if (duration <= 0) duration = 1e-3;
    if (view.pixelsPerSecond <= 0) view.fit(duration, widthPx);
    clampView(view, duration, widthPx);

    ImGui::InvisibleButton(id, size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle | ImGuiButtonFlags_MouseButtonRight);
    bool hovered = ImGui::IsItemHovered();
    bool active = ImGui::IsItemActive();
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 mouse = io.MousePos;
    r.hovered = hovered;
    r.hoverTime = xToTime(view, p0.x, mouse.x);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->PushClipRect(p0, p1, true);
    dl->AddRectFilled(p0, p1, col.bg, 6.f);
    dl->AddRectFilled(p0, ImVec2(p1.x, w0.y), col.ruler, 6.f, ImDrawFlags_RoundCornersTop);

    // ---- ruler + grid
    double step = niceTimeStep(view.pixelsPerSecond);
    double tStart = std::floor(view.t0 / step) * step;
    for (double t = tStart; t < view.t0 + widthPx / view.pixelsPerSecond + step; t += step)
    {
        float x = timeToX(view, p0.x, t);
        if (x < p0.x - 1 || x > p1.x + 1) continue;
        dl->AddLine(ImVec2(x, w0.y), ImVec2(x, p1.y), col.grid);
        dl->AddLine(ImVec2(x, w0.y - 5), ImVec2(x, w0.y), col.rulerText);
        std::string lbl = rulerLabel(t, step);
        dl->AddText(ImVec2(x + 3, p0.y + 2), col.rulerText, lbl.c_str());
    }

    // ---- selection background
    float sx0 = timeToX(view, p0.x, sel.start);
    float sx1 = timeToX(view, p0.x, sel.end);
    if (sx1 > p0.x && sx0 < p1.x)
        dl->AddRectFilled(ImVec2(std::max(sx0, p0.x), w0.y), ImVec2(std::min(sx1, p1.x), p1.y), col.selectionBg);

    // ---- waveform (min/max per pixel column)
    if (peaks && !peaks->mins.empty())
    {
        float midY = w0.y + bodyH * 0.5f;
        float amp = bodyH * 0.47f;
        dl->AddLine(ImVec2(p0.x, midY), ImVec2(p1.x, midY), col.grid);
        int cols = (int)widthPx;
        double secPerPx = 1.0 / view.pixelsPerSecond;
        for (int i = 0; i < cols; i++)
        {
            double ta = view.t0 + i * secPerPx;
            double tb = ta + secPerPx;
            if (ta > duration) break;
            float mn, mx;
            if (!peaks->query(ta, std::min(tb, duration), mn, mx)) continue;
            float x = p0.x + (float)i + 0.5f;
            float y0 = midY - mx * amp;
            float y1 = midY - mn * amp;
            if (y1 - y0 < 1.f) { y0 -= 0.5f; y1 += 0.5f; }
            bool inSel = tb > sel.start && ta < sel.end;
            dl->AddLine(ImVec2(x, y0), ImVec2(x, y1), inSel ? col.waveSelected : col.wave, 1.f);
        }
    }
    else
    {
        const char* msg = TR("wave.noWaveform");
        ImVec2 ts = ImGui::CalcTextSize(msg);
        dl->AddText(ImVec2(p0.x + (size.x - ts.x) * 0.5f, w0.y + (bodyH - ts.y) * 0.5f), col.rulerText, msg);
    }

    // ---- handles
    float hw = handleWidth * theme::scale();
    float hit = handleHit * theme::scale();
    bool hoverStart = hovered && std::fabs(mouse.x - sx0) <= hit && mouse.y >= w0.y;
    bool hoverEnd = hovered && std::fabs(mouse.x - sx1) <= hit && mouse.y >= w0.y;
    if (hoverStart && hoverEnd) { if (std::fabs(mouse.x - sx0) > std::fabs(mouse.x - sx1)) hoverStart = false; else hoverEnd = false; }
    auto drawHandle = [&](float x, bool hov, bool isStart)
    {
        if (x < p0.x - hw || x > p1.x + hw) return;
        ImU32 c = hov ? col.handleHover : col.handle;
        dl->AddLine(ImVec2(x, w0.y), ImVec2(x, p1.y), c, hov ? 2.f : 1.5f);
        // grip: a rounded tab at the top and a triangle pointing inwards
        float dir = isStart ? 1.f : -1.f;
        dl->AddRectFilled(ImVec2(x - (isStart ? 0 : hw), w0.y), ImVec2(x + (isStart ? hw : 0), w0.y + 16 * theme::scale()), c, 3.f);
        float gy = p1.y - 10 * theme::scale();
        dl->AddTriangleFilled(ImVec2(x, gy - 6), ImVec2(x, gy + 6), ImVec2(x + dir * 8, gy), c);
    };
    drawHandle(sx0, hoverStart || drag_ == Drag::Start, true);
    drawHandle(sx1, hoverEnd || drag_ == Drag::End, false);

    // ---- playheads (the default one, then every coloured running position)
    auto drawPlayhead = [&](double t, ImU32 color)
    {
        float px = timeToX(view, p0.x, t);
        if (px < p0.x || px > p1.x) return;
        dl->AddLine(ImVec2(px, p0.y), ImVec2(px, p1.y), color, 1.5f);
        dl->AddTriangleFilled(ImVec2(px - 5, p0.y), ImVec2(px + 5, p0.y), ImVec2(px, p0.y + 7), color);
    };
    if (showPlayhead) drawPlayhead(playhead, col.playhead);
    if (playheads)
        for (const WaveformPlayhead& ph : *playheads) drawPlayhead(ph.time, ph.color);

    // ---- hover time readout
    if (hovered && drag_ == Drag::None && mouse.y >= w0.y)
    {
        std::string t = formatTime(std::clamp(r.hoverTime, 0.0, duration));
        ImVec2 ts = ImGui::CalcTextSize(t.c_str());
        ImVec2 bp(std::min(mouse.x + 10, p1.x - ts.x - 8), w0.y + 6);
        dl->AddRectFilled(ImVec2(bp.x - 4, bp.y - 2), ImVec2(bp.x + ts.x + 4, bp.y + ts.y + 2), theme::u32(ImVec4(0, 0, 0, 0.6f)), 4.f);
        dl->AddText(bp, theme::u32(theme::colors().text), t.c_str());
    }
    dl->AddRect(p0, p1, col.border, 6.f);
    dl->PopClipRect();

    // ---- interaction
    if (hovered && io.MouseWheel != 0.f)
    {
        if (io.KeyCtrl || io.KeySuper)
        {
            double factor = io.MouseWheel > 0 ? 1.25 : 0.8;
            zoomAt(view, factor, r.hoverTime, duration, widthPx);
            r.viewChanged = true;
        }
        else
        {
            view.t0 -= io.MouseWheel * (widthPx * 0.1) / view.pixelsPerSecond;
            clampView(view, duration, widthPx);
            r.viewChanged = true;
        }
    }
    if (hovered && io.MouseWheelH != 0.f)
    {
        view.t0 += io.MouseWheelH * (widthPx * 0.1) / view.pixelsPerSecond;
        clampView(view, duration, widthPx);
        r.viewChanged = true;
    }
    if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && !hoverStart && !hoverEnd)
    {
        view.fit(duration, widthPx);
        r.viewChanged = true;
    }
    if (ImGui::IsItemActivated())
    {
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Middle) || (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && io.KeyShift))
        {
            drag_ = Drag::Pan;
            dragStartT0_ = view.t0;
            dragStartX_ = mouse.x;
        }
        else if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            if (mouse.y < w0.y) { r.seekRequested = true; r.seekTime = std::clamp(r.hoverTime, 0.0, duration); }
            else if (hoverStart) { drag_ = Drag::Start; r.selectionDragBegan = true; }
            else if (hoverEnd) { drag_ = Drag::End; r.selectionDragBegan = true; }
            else if (io.KeyAlt) { r.seekRequested = true; r.seekTime = std::clamp(r.hoverTime, 0.0, duration); }
            else
            {
                // drag in the body: pan when zoomed in, otherwise draw a new selection
                double visible = widthPx / view.pixelsPerSecond;
                if (visible < duration * 0.999) { drag_ = Drag::Pan; dragStartT0_ = view.t0; dragStartX_ = mouse.x; }
                else { drag_ = Drag::Body; dragAnchorTime_ = std::clamp(r.hoverTime, 0.0, duration); r.selectionDragBegan = true; }
            }
        }
    }
    if (active && drag_ != Drag::None)
    {
        double t = std::clamp(xToTime(view, p0.x, mouse.x), 0.0, duration);
        switch (drag_)
        {
        case Drag::Start:
            sel.start = std::min(t, sel.end - minSelection);
            sel.start = std::max(0.0, sel.start);
            r.selectionChanged = true;
            break;
        case Drag::End:
            sel.end = std::max(t, sel.start + minSelection);
            sel.end = std::min(duration, sel.end);
            r.selectionChanged = true;
            break;
        case Drag::Body:
            sel.start = std::min(dragAnchorTime_, t);
            sel.end = std::max(dragAnchorTime_, t);
            if (sel.end - sel.start < minSelection) sel.end = std::min(duration, sel.start + minSelection);
            r.selectionChanged = true;
            break;
        case Drag::Pan:
            view.t0 = dragStartT0_ - (mouse.x - dragStartX_) / view.pixelsPerSecond;
            clampView(view, duration, widthPx);
            r.viewChanged = true;
            break;
        default: break;
        }
        // auto-scroll when dragging a handle past the edge
        if (drag_ == Drag::Start || drag_ == Drag::End || drag_ == Drag::Body)
        {
            if (mouse.x > p1.x) { view.t0 += (mouse.x - p1.x) * 0.02 / view.pixelsPerSecond * 60.0 * io.DeltaTime; clampView(view, duration, widthPx); r.viewChanged = true; }
            if (mouse.x < p0.x) { view.t0 -= (p0.x - mouse.x) * 0.02 / view.pixelsPerSecond * 60.0 * io.DeltaTime; clampView(view, duration, widthPx); r.viewChanged = true; }
        }
    }
    if (drag_ != Drag::None && !active)
    {
        if (drag_ == Drag::Start || drag_ == Drag::End || drag_ == Drag::Body) r.selectionDragEnded = true;
        drag_ = Drag::None;
    }
    if (hovered && (hoverStart || hoverEnd || drag_ == Drag::Start || drag_ == Drag::End))
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    return r;
}

} // namespace ui
} // namespace evobox
