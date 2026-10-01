#include "ui/panels/ClipEditorPanel.h"
#include "media/ClipRenderer.h"
#include "ui/Icons.h"
#include "ui/Theme.h"
#include "ui/widgets/StickerPicker.h"
#include "ui/widgets/TrimFields.h"
#include "util/Strings.h"
#include "util/TimeFormat.h"
#include "imgui_stdlib.h"
#include <algorithm>
#include <cmath>

namespace evobox
{
namespace ui
{

namespace
{
// Stacked effect voices each get their own playhead colour (start order): the first one is the
// standard playhead (like music), the next ones cycle through five distinct hues.
ImVec4 voiceColor(int index)
{
    static const ImVec4 palette[] = {
        ImVec4(1.00f, 0.62f, 0.20f, 1.f), // orange
        ImVec4(0.35f, 0.75f, 1.00f, 1.f), // sky
        ImVec4(0.95f, 0.40f, 0.75f, 1.f), // pink
        ImVec4(0.30f, 0.85f, 0.45f, 1.f), // green
        ImVec4(0.70f, 0.55f, 1.00f, 1.f), // violet
    };
    if (index <= 0) return theme::colors().playhead;
    return palette[(index - 1) % 5];
}
} // namespace

// ---------------------------------------------------------------- playback (= the tile's)
void ClipEditorPanel::play(Sound& s, double fromSourceSec)
{
    PlaybackController::PlayOptions o;
    o.loop = loop_;
    if (fromSourceSec >= 0)
    {
        // a click starts there: the offset is measured inside the clip that will actually play
        // (the last render, which lags behind freshly moved handles), not from the live trim start
        double len = s.rt.clip ? s.rt.clip->duration() : 0.0;
        o.startSec = std::clamp(fromSourceSec - s.rt.clipSourceStart, 0.0, len);
        if (len > 0 && o.startSec >= len) o.startSec = 0;
    }
    app_.playback.play(s, TriggerSource::Tile, o);
}

void ClipEditorPanel::togglePlay(Sound& s)
{
    if (s.playing() && !s.isEffect()) app_.playback.stop(s.uid);
    else play(s);
}

void ClipEditorPanel::playSelected()
{
    if (Sound* s = app_.selectedSound()) togglePlay(*s);
}

// One-line media summary: "drum-loop.wav · 0:04.000 · 44.1 kHz · Mono"
void ClipEditorPanel::mediaInfo(const MediaRef& ref, double duration, int rate, int channels)
{
    const auto& th = theme::colors();
    std::string name = ref.fileName().empty() ? "(no media)" : ref.fileName();
    ImGui::AlignTextToFramePadding();
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.05f);
    ImGui::TextUnformatted(ellipsize(name, 220 * theme::scale()).c_str());
    ImGui::PopFont();
    if (!ref.path.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) ImGui::SetTooltip("%s", ref.path.c_str());
    std::string chs = channels == 1 ? "Mono" : channels == 2 ? "Stereo" : str::format("%d ch", channels);
    ImGui::SameLine();
    TextDim("%s  ·  %.1f kHz  ·  %s%s", formatTime(duration).c_str(), rate / 1000.0, chs.c_str(), ref.hasVideo ? "  ·  video" : "");
    (void)th;
}

// Transport controls (same line as the media info): play/stop (the tile's) · loop · zoom · peak
void ClipEditorPanel::transport(Sound& s, double duration, const organic::AudioBuffer* src)
{
    const auto& th = theme::colors();
    double selStart = s.trimStart(), selEnd = s.trimEnd();
    bool playing = s.playing();
    bool effect = s.isEffect();
    float bh = ImGui::GetFrameHeight();
    ImGui::SameLine(0, 16);
    // music: Play <-> Stop like the tile; effect: Play stacks, a separate Stop ends every voice
    bool stopGlyph = playing && !effect;
    if (AccentButton(stopGlyph ? ICON_PH_STOP " Stop" : ICON_PH_PLAY " Play", ImVec2(0, 0))) togglePlay(s);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
        ImGui::SetTooltip(effect ? "Play the selection (stacks another voice) - Ctrl+Space" : "Play / stop the selection, as the tile does (Ctrl+Space)");
    if (effect && playing)
    {
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, th.playing);
        std::string lbl = s.rt.activeVoices > 1 ? str::format(ICON_PH_STOP " Stop x%d", s.rt.activeVoices) : std::string(ICON_PH_STOP " Stop");
        if (GhostButton(lbl.c_str(), ImVec2(0, 0))) app_.playback.stop(s.uid);
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) ImGui::SetTooltip("Stop every play of this effect");
    }
    ImGui::SameLine();
    if (ToggleButton(ICON_PH_REPEAT, loop_, ImVec2(bh, 0), "Loop the selection when playing from here")) loop_ = !loop_;
    ImGui::SameLine(0, 12);
    float waveW = ImGui::GetContentRegionMax().x - ImGui::GetWindowContentRegionMin().x;
    if (IconButton(ICON_PH_MAGNIFYING_GLASS_MINUS, "Zoom out (Ctrl+wheel)", ImVec2(bh, 0)))
        WaveformEditor::zoomAt(view_, 0.7, (selStart + selEnd) * 0.5, duration, waveW);
    ImGui::SameLine(0, 2);
    if (IconButton(ICON_PH_MAGNIFYING_GLASS_PLUS, "Zoom in (Ctrl+wheel)", ImVec2(bh, 0)))
        WaveformEditor::zoomAt(view_, 1.4, (selStart + selEnd) * 0.5, duration, waveW);
    ImGui::SameLine(0, 2);
    if (IconButton(ICON_PH_ARROWS_OUT_LINE_HORIZONTAL, "Fit the whole file (double-click the waveform)", ImVec2(bh, 0)))
        view_.pixelsPerSecond = 0;
    ImGui::SameLine(0, 2);
    if (IconButton(ICON_PH_SELECTION, "Zoom to the selection", ImVec2(bh, 0)) && selEnd > selStart)
    {
        double len = (selEnd - selStart) * 1.1;
        view_.pixelsPerSecond = waveW / len;
        view_.t0 = std::max(0.0, selStart - (selEnd - selStart) * 0.05);
    }
    if (src)
    {
        ImGui::SameLine(0, 12);
        float peak = ClipRenderer::peakAbs(*src, (size_t)std::max(0.0, selStart * src->sampleRate), (size_t)std::max(0.0, selEnd * src->sampleRate)) * ClipRenderer::dbToLinear(s.gainDb());
        float peakDb = peak > 1e-6f ? 20.f * std::log10(peak) : -120.f;
        ImGui::AlignTextToFramePadding();
        TextDim("peak %.1f dBFS%s", peakDb, s.normalize() ? " (normalized)" : "");
    }
}

// ---------------------------------------------------------------- sticker frames (video sources)
// A strip of thumbnails extracted from the movie; clicking one makes it the tile's picture.
// Returns the height it used (0 when nothing is shown).
float ClipEditorPanel::stickerFrameStrip(Sound& s)
{
    const std::vector<Application::FrameCandidate>* frames = app_.stickerFrames(s.uid);
    bool pending = app_.stickerFramesPending(s.uid);
    if (!frames && !pending) return 0.f; // (the sticker picker offers "Frames from the video")
    float thumbH = ImGui::GetFrameHeight() * 2.2f;
    float stripH = thumbH + ImGui::GetTextLineHeightWithSpacing() + 8;
    ImGui::BeginGroup();
    ImGui::AlignTextToFramePadding();
    if (pending) TextDim(ICON_PH_CIRCLE_NOTCH " Extracting frames of the video for the sticker...");
    else TextDim(ICON_PH_FILM_STRIP " Pick a frame of the video as the tile picture:");
    ImGui::SameLine();
    if (GhostButton(ICON_PH_ARROWS_CLOCKWISE " More frames", ImVec2(0, 0)) && !pending) app_.requestStickerFrames(s, 32);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) ImGui::SetTooltip("Extract 32 frames instead of 16");
    ImGui::SameLine();
    if (GhostButton(ICON_PH_X " Hide", ImVec2(0, 0))) app_.clearStickerFrames(s.uid);
    if (frames)
    {
        ImGui::BeginChild("##frames", ImVec2(0, thumbH + 6), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
        int i = 0;
        for (const Application::FrameCandidate& f : *frames)
        {
            if (i++) ImGui::SameLine(0, 6);
            ImGui::PushID(i);
            float aspect = f.image && f.image->height > 0 ? (float)f.image->width / (float)f.image->height : 1.f;
            ImVec2 sz(std::clamp(thumbH * aspect, thumbH * 0.5f, thumbH * 2.f), thumbH);
            bool clicked = false;
            if (f.texture.valid())
                clicked = ImGui::ImageButton("##frame", ImTextureRef((ImTextureID)(intptr_t)f.texture.glId), sz);
            else
                clicked = ImGui::Button(ICON_PH_IMAGE, sz);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) ImGui::SetTooltip("%s — click to use as the sticker", formatTime(f.time).c_str());
            if (clicked && f.image)
            {
                std::string err;
                if (!app_.setStickerFromImage(s, *f.image, &err)) OLOGW("Media", "Cannot use frame as sticker: " << err);
            }
            ImGui::PopID();
        }
        ImGui::EndChild();
    }
    else ImGui::Dummy(ImVec2(0, thumbH + 6));
    ImGui::EndGroup();
    return stripH;
}

// ---------------------------------------------------------------- existing sound
void ClipEditorPanel::drawSound(Sound& s)
{
    const auto& th = theme::colors();
    if (target_ != (const void*)&s) { target_ = &s; view_ = WaveformView(); app_.openSourceForEditing(s); }
    std::shared_ptr<const MediaAsset> asset = s.rt.sourceAsset;
    double duration = asset ? asset->duration() : std::max(s.source.durationSec, s.trimEnd());
    const organic::AudioBuffer* src = asset && asset->buffer ? asset->buffer.get() : nullptr;

    mediaInfo(s.source, duration, asset ? asset->sampleRate() : s.source.sampleRate, asset ? asset->channels() : s.source.channels);
    if (asset) transport(s, duration, src);
    float dupW = ImGui::CalcTextSize(ICON_PH_COPY " Duplicate").x + ImGui::GetStyle().FramePadding.x * 2 + 8;
    ImGui::SameLine(ImGui::GetContentRegionMax().x - dupW);
    if (ImGui::Button(ICON_PH_COPY " Duplicate", ImVec2(dupW, 0))) app_.importer.duplicateSound(s);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) ImGui::SetTooltip("Edits update this tile; duplicate first to keep the original");

    if (!asset)
    {
        if (s.rt.sourceLoading) TextDim(ICON_PH_CIRCLE_NOTCH " Loading source media...");
        else if (s.source.resolve(app_.project.bundleDir).empty())
        {
            ImGui::TextColored(th.warning, ICON_PH_WARNING " Source media not found: %s", s.source.fileName().c_str());
            ImGui::SameLine();
            if (ImGui::Button(ICON_PH_FOLDER_OPEN " Relink media...")) app_.relinkRequestUid = s.uid;
            ImGui::SameLine();
            TextDim("the tile still plays its rendered clip");
        }
        else if (ImGui::Button(ICON_PH_ARROW_COUNTER_CLOCKWISE " Load source")) app_.openSourceForEditing(s);
        // still show the rendered clip's waveform when available
        if (s.rt.clip)
        {
            static organic::Peaks clipPeaks;
            static const organic::AudioBuffer* peaksFor = nullptr;
            if (peaksFor != s.rt.clip.get()) { clipPeaks.build(*s.rt.clip, 256); peaksFor = s.rt.clip.get(); }
            WaveformSelection sel{ 0, s.rt.clip->duration() };
            WaveformView v; v.fit(s.rt.clip->duration(), ImGui::GetContentRegionAvail().x);
            editor_.draw("##clipwave", &clipPeaks, s.rt.clip->duration(), sel, v, 0, false, ImVec2(0, std::max(60.f, ImGui::GetContentRegionAvail().y - 4)));
        }
        return;
    }

    // video sources: thumbnails to pick the tile picture from (takes its row above the waveform)
    stickerFrameStrip(s);

    float detailsH = ImGui::GetFrameHeightWithSpacing() + 6;
    float waveH = std::max(60.f, ImGui::GetContentRegionAvail().y - detailsH);
    WaveformSelection sel{ s.trimStart(), s.trimEnd() };
    // running positions of the tile's voices in source seconds (PlaybackController measures each
    // voice against the clip it plays, so the playhead follows the audio and not the handles being
    // dragged): music has one, a stacked effect one per voice in its own colour
    std::vector<WaveformPlayhead> heads;
    heads.reserve(s.rt.voiceProgress.size());
    for (size_t i = 0; i < s.rt.voiceProgress.size(); i++)
    {
        WaveformPlayhead ph;
        ph.time = s.rt.voiceProgress[i].sourceSec;
        ph.color = theme::u32(s.isEffect() ? voiceColor((int)i) : th.playhead);
        heads.push_back(ph);
    }
    WaveformResult r = editor_.draw("##wave", asset->peaks.get(), duration, sel, view_, 0, false, ImVec2(0, waveH), nullptr, &heads);
    if (r.selectionDragBegan) { dragStart_ = s.trimStart(); dragEnd_ = s.trimEnd(); }
    if (r.selectionChanged) s.setTrim(sel.start, sel.end);   // live (the clip re-renders after the debounce)
    if (r.selectionDragEnded)
    {
        float os = (float)dragStart_, oe = (float)dragEnd_, ns = (float)s.trimStart(), ne = (float)s.trimEnd();
        if (os != ns || oe != ne)
        {
            Sound* sp = &s;
            organic::UndoManager::get().pushDone("Trim clip",
                [sp, ns, ne] { sp->setTrim(ns, ne); },
                [sp, os, oe] { sp->setTrim(os, oe); }, { sp });
        }
    }
    if (r.seekRequested) play(s, r.seekTime); // click = play from there (music restarts, an effect stacks)

    double s0 = s.trimStart(), s1 = s.trimEnd();
    if (TrimFields("trim", s0, s1, duration).changed) s.setTrimUndoable(s0, s1);
    ImGui::SameLine(0, 18);
    bool norm = s.normalize();
    if (ImGui::Checkbox("Normalize", &norm)) s.normalizeP->setUndoable(norm);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(100 * theme::scale());
    float g = s.gainDb();
    static Sound* s_gainSound = nullptr; static float s_gainOld = 0;
    if (ImGui::SliderFloat("##gain", &g, -24.f, 24.f, "%.1f dB"))
    {
        if (s_gainSound != &s) { s_gainSound = &s; s_gainOld = s.gainDb(); }
        s.gainDbP->setValue(g);
    }
    if (ImGui::IsItemActivated()) { s_gainSound = &s; s_gainOld = s.gainDb(); }
    if (ImGui::IsItemDeactivatedAfterEdit() && s_gainSound == &s) { s.gainDbP->recordEdit(s_gainOld, s.gainDbP->value); s_gainSound = nullptr; }
    ImGui::SameLine(0, 12);
    ImGui::AlignTextToFramePadding();
    if (s.rt.renderPending) TextDim(ICON_PH_CIRCLE_NOTCH " rendering...");
    else if (s.rt.clipDirty) TextDim("re-render pending");
    else TextDim(ICON_PH_CHECK " clip up to date");
}

void ClipEditorPanel::draw(bool* open)
{
    ImGui::SetNextWindowSize(ImVec2(900, 260), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Clip Editor", open))
    {
        if (Sound* s = app_.selectedSound()) drawSound(*s);
        else
        {
            target_ = nullptr; // deselecting never stops the sound: it is the tile's playback
            if (app_.importer.pendingImports() > 0)
                EmptyState("Decoding media...", str::format("%d file(s) in progress", app_.importer.pendingImports()).c_str(), ICON_PH_CIRCLE_NOTCH);
            else
                EmptyState("Select a sound to edit its clip", "or drop media into the soundboard: it becomes a tile right away", ICON_PH_WAVEFORM);
        }
        confirm_.draw();
    }
    ImGui::End();
}

} // namespace ui
} // namespace evobox
