// WaveformEditor.h — waveform + timeline labels + playhead + accent-highlighted selection with two
// draggable handles (generous hit areas), Ctrl+wheel zoom at the mouse, drag to pan, double-click
// to fit. NO app dependencies: input = peaks/duration/selection/playhead/view, output = change
// flags, so it can be upstreamed into imgui_organic later.
#pragma once

#include <vector>
#include "OrganicAudio.h"
#include "imgui.h"

namespace evobox
{
namespace ui
{

struct WaveformView
{
    double t0 = 0;              // left edge time (s)
    double pixelsPerSecond = 0; // 0 = fit on first draw
    void fit(double duration, float widthPx) { t0 = 0; pixelsPerSecond = duration > 0 ? widthPx / duration : 100.0; }
};

struct WaveformSelection
{
    double start = 0;
    double end = 0;
    double length() const { return end > start ? end - start : 0; }
};

struct WaveformResult
{
    bool selectionChanged = false;   // while dragging (live)
    bool selectionDragBegan = false;
    bool selectionDragEnded = false; // drag released -> push one undo step
    bool viewChanged = false;
    bool seekRequested = false;      // click in the ruler / alt-click in the body
    double seekTime = 0;
    bool hovered = false;
    double hoverTime = 0;
};

struct WaveformColors
{
    ImU32 bg, wave, waveSelected, selectionBg, handle, handleHover, playhead, ruler, rulerText, border, grid;
};
WaveformColors defaultWaveformColors();

// One running position on the waveform (seconds in the source) with its own colour: a music sound
// has one, a stacked effect one per playing voice.
struct WaveformPlayhead
{
    double time = 0;
    ImU32 color = 0;
};

class WaveformEditor
{
public:
    // Draws in a region of `size` at the cursor (size.x <= 0 = available width).
    // `playhead`/`showPlayhead`: one position in the default playhead colour; `playheads`: any
    // number of additional coloured positions.
    WaveformResult draw(const char* id, const organic::Peaks* peaks, double duration,
                        WaveformSelection& sel, WaveformView& view, double playhead, bool showPlayhead,
                        ImVec2 size = ImVec2(0, 0), const WaveformColors* colors = nullptr,
                        const std::vector<WaveformPlayhead>* playheads = nullptr);

    // view helpers
    static void zoomAt(WaveformView& view, double factor, double anchorTime, double duration, float widthPx);
    static void clampView(WaveformView& view, double duration, float widthPx);
    static double xToTime(const WaveformView& v, float x0, float x);
    static float timeToX(const WaveformView& v, float x0, double t);

    float rulerHeight = 18.f;
    float handleWidth = 10.f;   // visual width
    float handleHit = 14.f;     // hit area (each side)
    double minSelection = 0.005;

private:
    enum class Drag { None, Start, End, Pan, Body };
    Drag drag_ = Drag::None;
    double dragStartT0_ = 0;
    float dragStartX_ = 0;
    double dragAnchorTime_ = 0;
};

// Draws a time ruler label set: returns a "nice" step in seconds for the given px/s.
double niceTimeStep(double pixelsPerSecond, float minLabelPx = 70.f);

} // namespace ui
} // namespace evobox
