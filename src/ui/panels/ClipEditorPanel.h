// ClipEditorPanel.h — bottom panel (UI.md §23-§33): media info, transport, waveform with trim
// handles, Start/End/Duration fields and clip details. Imported media becomes a Sound at once,
// so the editor always edits the selected sound in place (its saved trim region, re-rendered on
// change). Video sources additionally show a strip of frames to pick the tile picture from.
#pragma once

#include "app/Application.h"
#include "ui/widgets/Common.h"
#include "ui/widgets/WaveformEditor.h"

namespace evobox
{
namespace ui
{

class ClipEditorPanel
{
public:
    explicit ClipEditorPanel(Application& app) : app_(app) {}
    void draw(bool* open);
    // Ctrl+Space: the same Play/Stop as the panel's button (= the tile's). Music toggles, an
    // effect stacks another voice.
    void playSelected();

private:
    Application& app_;
    WaveformEditor editor_;
    WaveformView view_;
    bool loop_ = false;              // voices started from this panel loop the selection
    const void* target_ = nullptr;   // the sound the view (zoom/scroll) belongs to
    double dragStart_ = 0, dragEnd_ = 0; // trim before a handle drag (undo)
    ConfirmPopup confirm_{ "ConfirmClipEditor" };

    void drawSound(Sound& s);
    float stickerFrameStrip(Sound& s);
    void transport(Sound& s, double duration, const organic::AudioBuffer* src);
    void mediaInfo(const MediaRef& ref, double duration, int rate, int channels);
    // The panel plays through PlaybackController like the tile does (one voice, never a second
    // "preview" copy of the sound); `fromSourceSec` < 0 = from the start of the selection.
    void play(Sound& s, double fromSourceSec = -1.0);
    void togglePlay(Sound& s);
};

} // namespace ui
} // namespace evobox
