# Soundboard + OSC — Complete UI/UX Specification

## Purpose

This document describes the complete **UI and UX specification** for a desktop application used to create and operate a customizable soundboard with OSC triggering.

The application should allow the user to:

- drag and drop media files,
- display the audio waveform from those files,
- select and trim only the portion they want,
- save that trimmed section as a reusable sound tile,
- assign a name and sticker/icon to each tile,
- organize tiles into categories,
- trigger playback instantly from the main soundboard,
- assign OSC commands to each sound,
- send those OSC commands to localhost or to saved remote OSC targets,
- define OSC actions at playback start and after playback ends.

This specification intentionally focuses on **interface design, behavior, user flow, and interaction logic** rather than implementation details.

The goal is for an AI copilot or designer/developer to understand exactly what the application should feel like and how the user should interact with it.

---

# 1. Product Concept

The application is a hybrid of:

- a soundboard,
- a sample launcher,
- a lightweight clip editor,
- a cue launcher,
- and a simple OSC trigger editor.

It should not feel like:

- a DAW,
- a network administration tool,
- or a complex show-control programming application.

The design should prioritize:

- speed,
- clarity,
- immediate feedback,
- minimal friction,
- easy live operation.

The ideal user flow is:

1. Drag in media.
2. Select the desired audio segment.
3. Preview it.
4. Save it as a named tile with an icon/sticker.
5. Assign the tile to a category.
6. Optionally configure OSC actions.
7. Trigger the sound instantly from the main soundboard.

---

# 2. Overall Application Layout

The application should use a **single desktop window** divided into four main areas:

1. **Left sidebar** — categories and navigation.
2. **Center workspace** — sound tile grid.
3. **Right sidebar** — OSC target and command configuration for the currently selected sound.
4. **Bottom panel** — waveform editor and clip trimming interface.

Suggested layout:

```text
┌─────────────┬────────────────────────────────────┬─────────────────┐
│             │                                    │                 │
│ Categories  │          Soundboard Grid           │  OSC Commands   │
│             │                                    │                 │
│             │                                    │                 │
├─────────────┴────────────────────────────────────┴─────────────────┤
│                                                                   │
│                     Waveform / Clip Editor                        │
│                                                                   │
└───────────────────────────────────────────────────────────────────┘
```

Suggested width proportions:

- Left sidebar: ~15%
- Center workspace: ~60%
- Right sidebar: ~25%

Suggested waveform panel height:

- around 25–35% of the total application height

These proportions are flexible but the UI should remain visually balanced.

---

# 3. Visual Style

The application should have a polished, professional, modern dark theme.

Recommended characteristics:

- dark charcoal/navy backgrounds,
- rounded cards and panels,
- subtle borders,
- strong blue or neon-blue accent for selected and active elements,
- tasteful secondary colors for different sound tiles,
- minimal gradients,
- high contrast text,
- clear visual hierarchy,
- professional media/show-control aesthetic.

The interface should feel closer to professional lighting/media tools than to a casual consumer music app.

Avoid excessive decoration.

The priority is legibility and speed of use.

---

# 4. Left Sidebar — Categories

The left sidebar is used to organize and navigate sounds.

## Default Categories

The UI may initially include categories such as:

- All Sounds
- Music
- Effects
- Voices
- Ambient
- Interface
- Custom

At the bottom of the list:

```text
+ Add Category
```

## Category Interaction

Clicking a category:

- selects it,
- filters the center sound tile grid,
- visually highlights the selected category.

Each category may have:

- an icon,
- a name,
- an optional accent color.

## Category Creation

The user should be able to create custom categories easily.

The interaction should be lightweight and quick.

Possible flow:

```text
+ Add Category
     ↓
Small inline field or modal
     ↓
Category name
Optional icon
Optional color
```

## Category Organization

It should be possible in the future to:

- rename categories,
- reorder them,
- delete them,
- drag sound tiles into categories.

A vertical list is preferable to a dropdown because categories are important navigation and should remain visible at all times.

---

# 5. Center Workspace — Sound Tile Library

This is the main operational area of the application.

It contains all sound tiles belonging to the currently selected category.

## Header Area

The header should contain:

- current category name,
- number of clips,
- search field,
- optional view toggle,
- Add Sound button.

Example:

```text
All Sounds
12 clips

[ Search sounds... ]     [ Grid/List ]     [ + Add Sound ]
```

## Search

The search field should filter the displayed sounds instantly.

Placeholder:

```text
Search sounds...
```

Search should primarily operate within the currently selected category.

---

# 6. Adding Media

The user should be able to add media in several intuitive ways:

- click **+ Add Sound**,
- drag and drop media anywhere into the center workspace,
- drag a file onto a dedicated Add Sound tile.

Example Add Sound tile:

```text
       +

   Add Sound
or drag & drop
```

When a media file is added:

- the waveform editor opens or becomes active,
- the media waveform is displayed,
- the user can immediately select the desired audio portion.

The user may add:

- audio files,
- video files containing audio,
- other supported media containing a soundtrack.

The key UX goal is that the user should not care about the original media type once imported.

They should simply see and edit the audio waveform.

---

# 7. Sound Tile Design

Each saved sound segment becomes a tile in the center grid.

Each tile should contain:

- sticker/icon/emoji-like visual,
- clip name,
- duration,
- selected state,
- playback state.

Example:

```text
┌───────────────┐
│      🥁       │
│               │
│   Drum Hit    │
│     0:02      │
└───────────────┘
```

Another:

```text
┌───────────────┐
│      🚀       │
│               │
│     Intro     │
│     0:05      │
└───────────────┘
```

The tiles should be visually large enough to work as live performance buttons.

---

# 8. Tile States

Each sound tile should have several clear visual states.

## Normal

Default appearance.

## Hover

Slight border or background emphasis.

## Selected

Strong accent border or glow.

Selection means:

- the right OSC panel shows this tile's settings,
- the bottom waveform panel shows this tile's media segment.

## Playing

The tile should visibly indicate active playback.

Implemented: a pulsing border in the "playing" colour plus a progress indicator that always covers
the **selected region** of the sound (the rendered clip *is* the trim range: 0 = trim start,
1 = trim end — never the whole source file):

- **music** tiles: a very thin line right under the box, full tile width, in the gap below the tile;
  the tile's content does not move,
- **effect** tiles: a short bar inside the tile above the name, following the most recently started
  voice of the stack (the stop circle and the `xN` count sit next to it).

Other possible indicators (progress ring, small play animation) were not needed.

## Disabled / Missing Media

Optional state for unavailable source media.

---

# 9. Tile Interaction

The selected tile and the played tile can conceptually be the same interaction, but the behavior should remain intuitive.

Recommended behavior:

- Single click selects the tile.
- A clearly visible play button inside the tile triggers playback. On a playing *music* tile it
  becomes the stop button; on a playing *effect* tile it keeps stacking plays and a separate stop
  circle (bottom-left) stops all of that effect's plays — also in performance mode.

Alternative behavior:

- Clicking the main body triggers playback.
- A secondary click/selection mode is used for editing.

For live operation, there must be **one very obvious playback action**.

Avoid requiring a double click during performance use.

---

# 10. Tile Context Actions

Secondary actions can be placed in a context menu or small hover menu.

Suggested actions:

- Play
- Edit Clip
- Rename
- Change Sticker
- Move to Category
- Duplicate
- Delete

These controls should not clutter the default tile appearance.

---

# 11. Empty Category State

If a category contains no sounds, show a clean empty state.

Example:

```text
No sounds yet

Drop media here or click Add Sound
```

The empty state should make drag-and-drop behavior obvious.

---

# 12. Right Sidebar — OSC Commands

The right sidebar contains the OSC configuration for the currently selected sound tile.

Title:

```text
OSC Commands
```

The panel should contain three logical parts:

1. OSC Targets
2. At Play (Start)
3. After Play

There should be **no Before Play section**.

There should be **no dedicated Copy/Paste section**.

Normal text field copy/paste behavior is sufficient.

---

# 13. OSC Target Concept

OSC commands may need to be sent to several different computers or devices.

Typical examples:

- local machine,
- lighting PC,
- media server,
- laser computer,
- other show-control system.

The user should not have to repeatedly type IP addresses.

The application should provide reusable saved OSC targets.

---

# 14. OSC Targets Section

At the top of the OSC sidebar include:

```text
OSC Targets                   + Add Target
```

Example:

```text
● Localhost (default)      127.0.0.1
○ Lighting PC              192.168.0.50
○ Media Server             192.168.0.60
○ Laser                    192.168.0.70
```

Each target entry shows:

- target name,
- IP address,
- indication if it is the default,
- optional edit/menu control.

The user should normally interact with the friendly name rather than the raw IP address.

---

# 15. Localhost Default Behavior

Localhost must always be available by default.

Default OSC target:

```text
Localhost
127.0.0.1
```

If a command has no explicitly selected target, it should visually default to:

```text
Localhost
```

The user should only need to change the destination when sending OSC to another machine.

This dramatically reduces repetitive configuration.

---

# 16. Adding or Editing an OSC Target

The user should be able to:

- add a target,
- rename a target,
- edit the IP address,
- optionally define a port,
- set a target as default,
- delete a target.

Example lightweight editor:

```text
Name
Lighting PC

IP Address
192.168.0.50

Port
8000
```

This configuration should feel like a simple address book, not a network administration screen.

Port configuration may be hidden under an advanced option if desired.

---

# 17. At Play (Start) Section

This section defines OSC commands triggered when audio playback starts.

Header example:

```text
At play (start)          0 ms      + Add OSC command
```

The delay field controls when the group is triggered relative to playback start.

Default:

```text
0 ms
```

Examples:

```text
0 ms     → exactly when playback starts
200 ms   → 200 ms after playback starts
```

If negative timing is supported later:

```text
-100 ms  → 100 ms before playback starts
```

Negative timing is optional and not necessary for the first version.

---

# 18. OSC Command Row

Every OSC command should appear as a compact inline row.

Example:

```text
/sound/trigger 1       [ Lighting PC ▼ ]      ×
```

Another:

```text
/lighting/flash 1      [ Laser ▼ ]            ×
```

Each command row contains:

1. OSC command/address field
2. Target selector
3. Remove/delete action

The target selector uses saved OSC targets.

Default selection:

```text
Localhost
```

No extra dialog should be required to change the target.

---

# 19. Multiple OSC Commands Per Event

One sound can trigger several commands at once.

Example:

```text
At play (start)                           0 ms

/sound/trigger 1       Media Server
/lighting/flash 1      Lighting PC
/laser/start 1         Laser

+ Add OSC command
```

Each OSC command may use a different target.

This is important.

The application must **not** assume that all commands for one sound are sent to the same IP.

---

# 20. After Play Section

This section contains OSC commands sent after the sound has finished playing.

Header example:

```text
After play              500 ms      + Add OSC command
```

Examples:

```text
/lighting/flash 0       Lighting PC
/laser/start 0          Laser
```

The delay is relative to the end of playback.

Examples:

```text
0 ms     → immediately when the sound ends
500 ms   → half a second after the sound ends
```

Again, negative timing could be considered later if useful.

---

# 21. OSC Configuration UX Goal

The OSC interface should visually read like:

```text
WHEN            COMMAND               WHERE

Start           /light/flash 1        Lighting PC
Start           /laser/start 1        Laser
End             /light/flash 0        Lighting PC
```

The user should not need to think about networking during routine configuration.

The saved target system exists specifically to hide unnecessary IP repetition.

---

# 22. OSC Feedback

When an OSC command is sent, provide subtle visual feedback.

Possible indicators:

- brief row highlight,
- small activity pulse,
- tiny send icon animation.

Avoid notifications or popups for every OSC message.

The feedback should help during testing while remaining unobtrusive during live operation.

---

# 23. Bottom Panel — Waveform / Clip Editor

The bottom panel is used to select and trim the desired portion of imported media.

This is intentionally a **lightweight clip editor**, not a full audio workstation.

The user should immediately understand:

- where the sound starts,
- where it ends,
- which part is selected,
- how long the selected segment is.

---

# 24. Media Information

At the top-left of the waveform panel display simple media information.

Example:

```text
drum-hit.wav
0:08.432   |   44.1 kHz   |   Stereo
```

Useful information:

- file name,
- total duration,
- sample rate,
- mono/stereo.

Avoid excessive metadata.

---

# 25. Transport Controls

The waveform editor should contain lightweight transport controls.

Recommended controls:

- Play
- Stop
- Zoom Out
- Zoom In
- Fit waveform
- Normalize (optional)

Example:

```text
▶   ■   −Zoom   +Zoom   Fit   Normalize
```

The controls should remain visually compact.

---

# 26. Waveform Display

The waveform occupies most of the bottom panel.

It should display:

- audio amplitude,
- timeline labels,
- current playback position,
- active selection range.

Example timeline:

```text
0:00     0:01     0:02     0:03     0:04     0:05
```

The selected range should be highlighted with the main accent color.

Unselected waveform regions should remain visible but visually subdued.

---

# 27. Selection Handles

The selected sound segment should have two obvious draggable handles:

- start handle,
- end handle.

The user drags these horizontally to define the clip.

The handles should remain easy to grab even when zoomed out.

The waveform selection must provide strong visual confirmation of what will be saved.

---

# 28. Start, End and Duration Fields

Display precise numeric values for:

- Start
- End
- Duration

Example:

```text
Start       0:01.230
End         0:03.540
Duration    0:02.310
```

These values should update live when the selection handles move.

Ideally, Start and End may also be directly editable for precise trimming.

Duration can be calculated automatically.

---

# 29. Play Selection (linked to the tile)

Include a prominent button:

```text
▶ Play   /   ■ Stop
```

This plays only the currently selected region — and it is **the same playback as the tile's
button**, not a separate preview copy: pressing Play here or on the tile starts one voice of the
sound (its rendered clip, OSC triggers included), both buttons show the same state, and the
waveform shows a running playhead in both cases. Music toggles Play/Stop; an effect's Play stacks
another voice and a `■ Stop xN` button next to it ends them all. The waveform shows **one playhead
per playing voice** — music one, a stacked effect as many as voices are running, each in its own
colour (the first in the standard playhead yellow, then orange, sky, pink, green, violet).
Clicking in the waveform plays from that point (music restarts there, an effect stacks a voice).
`Ctrl+Space` is the same button. The sound is never heard twice from two players.

The user should be able to repeatedly play while adjusting the trim points: the clip re-renders
after a short debounce, so the next press plays the new region.

---

# 30. Loop Selection

Provide an optional toggle:

```text
Loop selection
```

This is useful when adjusting short sounds or testing boundaries. It applies to voices started
from the Clip Editor (button, Ctrl+Space, click in the waveform); the tile's own play never loops.

It should remain secondary to the main play button.

---

# 31. Saving a Clip (revised: there is no separate save step)

Imported or dropped media becomes a tile **immediately** (the whole file is the clip, one undo
step). The Clip Editor always edits the selected sound in place: moving the handles, gain and
normalize re-render the clip after a short debounce and the tile updates. There is no
`Cancel` / `Save Clip` row any more — deleting the tile is the way to discard an import.

The right end of the header row keeps a secondary `Duplicate` action (a copy of the sound to
trim differently).

---

# 31b. Picture from the video

When the source is a video, a strip of 16 evenly spaced frames appears above the waveform:

```text
🎞 Pick a frame of the video as the tile picture:    ⟳ More frames    ✕ Hide
[▣][▣][▣][▣][▣][▣][▣][▣][▣][▣][▣][▣][▣][▣][▣][▣]
```

Clicking a frame stores it as the tile picture (a PNG inside the show file, see §37) and the
tile shows the picture instead of a glyph. The same strip can be requested later from the
sticker picker ("Frames from the video").

---

# 32. Clip Details

For every sound the user can define, in the Inspector header card and in the Clip Editor:

- clip name,
- sticker/icon — a Phosphor glyph, an emoji, or a **picture** ("Picture from file..." in the
  sticker picker, or a video frame, §31b),
- category,
- **Effect** checkbox: an effect plays on top of everything and stacks when pressed again (press
  three times = three overlapping plays). Unticked, the sound is *music*: starting it stops the
  music that was playing (the previous music, or its own earlier play = restart). Effects carry a
  ✦ marker on the tile and a `x3` voice count while stacked; a playing effect's play button keeps
  its ▶ glyph (press again to stack) and a **stop circle appears in the tile's bottom-left corner**
  while any of its plays is running: it stops every play of that effect at once (three stacked
  plays → all three end) and disappears until the effect is played again. The list view shows the
  same pair (▶ + ■ while playing); the context menu, the Inspector's Stop and Esc still work.

Example:

```text
Clip Name
Laser Hit

Sticker
⭐  🚀  💥  🔊  🎤  🎵

Category
Effects ▼
```

Sticker selection should be visual and fast.

The user should not need to navigate through a complex asset browser.

Simple icons, emojis, or a curated sticker library are sufficient.

This step can be shown as:

- lightweight modal,
- inline mini-form,
- small side panel.

Avoid a large multi-step wizard.

---

# 33. Editing Existing Clips

When the user selects an existing sound tile:

- its waveform appears in the bottom editor,
- its trim handles show the saved clip region,
- the OSC panel shows that tile's OSC actions.

The user should be able to edit:

- trim start,
- trim end,
- clip name,
- sticker/icon,
- category,
- OSC commands.

Changes should update the existing tile unless the user explicitly chooses Duplicate.

---

# 34. Selection Model

The application should always maintain one clear **currently selected sound**.

The selected sound controls:

- the right OSC sidebar,
- the bottom waveform editor.

Mental model:

```text
Selected Sound Tile
        ↓
OSC settings on the right
        ↓
Waveform settings on the bottom
```

This relationship should always be visually obvious.

---

# 35. Dragging Tiles Between Categories

A useful interaction is drag-and-drop category assignment.

Example:

1. User drags `Explosion` tile.
2. User drops it onto `Effects` in the left sidebar.
3. Tile is reassigned to `Effects`.

This keeps organization fast and intuitive.

---

# 36. Reordering Tiles

The user should be able to manually reorder tiles within a category.

This is particularly useful for live shows.

A user may want to arrange tiles according to:

- show order,
- scene order,
- song order,
- importance,
- personal preference.

Drag-and-drop reordering is the preferred interaction.

---

# 37. Grid and List Views

Grid view should be the default.

It is the natural view for a soundboard and emphasizes fast triggering.

Optional list view may help manage larger libraries.

Grid emphasizes:

- sticker,
- name,
- duration,
- playback.

List view could show:

- sound name,
- category,
- duration,
- number of OSC actions,
- assigned targets.

Grid remains the primary operational view.

---

# 38. Tile Size Options

A future enhancement could support:

- Compact
- Standard
- Large

Large mode is useful for live triggering.

Compact mode is useful when many sounds need to fit on screen.

This is optional for the first version.

---

# 39. Keyboard and Live Operation UX

Keyboard shortcuts can improve speed.

Possible shortcuts:

- Space → Play/Stop selected clip
- Enter → Play selected clip
- Delete → Delete selected tile
- Ctrl/Cmd + F → Search
- Ctrl/Cmd + D → Duplicate
- Arrow keys → Move selection
- Ctrl/Cmd + N / O / Shift+O / S / Shift+S / I → New / Open / Merge show / Save / Save As / Import media

Later, individual clips could also have custom hotkeys.

Keyboard shortcuts should remain secondary and should not clutter the UI.

---

# 40. Multiple Sound Playback

Implemented behaviour (project setting `Playback policy`):

- **Music stops previous music** (default): starting a non-effect sound stops every playing
  non-effect voice (including an earlier play of the same sound, i.e. pressing a music tile again
  restarts it). Effects (§32 checkbox) are never interrupted by this rule and stack freely.
- **Overlap everything**: every trigger starts a new voice.
- **Stop others**: any trigger stops every other sound (effects included).

`Space` on a selected music tile toggles it; on an effect it fires it again (stop a running
effect with the tile's bottom-left stop circle, which ends all of its stacked plays). `Esc` stops all.

Possible future playback modes: exclusive groups, fade out previous sound, per-tile toggle
behaviour.

---

# 40b. Show file

`Save` / `Save As` write one **`<name>.liv`** file (a zip). It contains `project.json`
(categories, sounds and their OSC commands, gift actions, room events, OSC targets, settings),
`clips/*.mp3` (the rendered sounds, MP3 — a 3-minute song is ~4 MB) and `icons/*.png` (picture
stickers) — never the source media or videos. Opening a `.liv` (menu, recent list, drop on the
window, command line) restores the full context. Shows saved by earlier versions contain float32
`clips/*.wav`: they open as before, their clips are converted to MP3 in the background while the
show loads (status line: "Converted N clip(s) to MP3 - save the show to shrink the file"; the
show is not marked as modified) and the next Save writes the smaller file. The legacy
`<name>.evobox/` folder layout remains readable.

**File → Merge show…** (`Ctrl+Shift+O`) adds another show to the current one instead of replacing
it. After picking the `.liv` (or an `.evobox` folder's `project.json`) a small dialog summarises
what it contains and asks what to take:

- **Sounds only** — the sounds with their rendered clips, pictures and categories.
- **Sounds and OSC** — plus their OSC commands, the targets they use, gift actions and room-event
  actions.

Categories are matched by name (both shows start with the same builtin set), OSC targets by
host:port then by name; matches are reused, everything else is created. A gift that already has an
action here keeps it (the other show's is skipped); room events are fixed, so the other show's
commands are appended and its sound adopted only when the event has none. Settings are never
merged. The merge is one undo step ("Merge show"); the status line reports what was added.

---

# 41. Avoid Unnecessary Dialogs

Routine actions should happen directly in the main interface.

Avoid dialogs for:

- switching OSC target,
- adding OSC commands,
- choosing categories,
- trimming audio.

Small dialogs are acceptable for:

- Add/Edit OSC Target,
- choosing sticker/icon,
- destructive action confirmation.

The application should feel continuous and fast.

---

# 42. Fullscreen / Performance Mode

A future performance mode could hide configuration panels and maximize the sound tile grid.

Example:

```text
┌───────────────────────────────────────────────┐
│                    EFFECTS                    │
│                                               │
│  [Explosion] [Laser] [Impact] [Whoosh]       │
│                                               │
│  [Applause ] [Intro] [Success] [Error]       │
│                                               │
└───────────────────────────────────────────────┘
```

This would be useful when all editing is complete and the application is used live.

Editing mode and performance mode should feel like two different working states of the same application.

---

# 43. Example Complete Sound Configuration

Selected tile:

```text
💥
Explosion
0:03
```

OSC panel:

```text
OSC Targets

Localhost      127.0.0.1
Lighting PC    192.168.0.50
Media Server   192.168.0.60
Laser          192.168.0.70


At play (start)                       0 ms

/explosion/start 1       Media Server
/light/flash 1           Lighting PC

+ Add OSC command


After play                            250 ms

/light/flash 0           Lighting PC

+ Add OSC command
```

Everything should be editable without leaving the main screen.

---

# 44. Example Media Import Flow

The user is currently viewing:

```text
Effects
```

They drag in:

```text
explosion-long.mp4
```

The tile `explosion-long` appears at once in the Effects category (whole file, 0:12) and is
selected; the waveform editor shows it and, because the source is a video, a strip of 16 frames.

The user selects:

```text
Start:    0:03.200
End:      0:05.850
Duration: 0:02.650
```

The clip re-renders in the background; the tile now reads `0:02`. They click:

```text
Preview Selection
```

The result sounds correct.

They enter in the Inspector header card:

```text
Name: Explosion
Picture: (clicks the 7th frame of the strip)
Category: Effects
☑ Effect
```

Pressing the tile three times quickly plays three overlapping explosions; pressing a music tile
afterwards stops the music that was playing but leaves the explosions alone. `Ctrl+S` writes
`MyShow.liv`.

---

# 45. Example Live Soundboard

The main workspace may look like:

```text
┌─────────────┐ ┌─────────────┐ ┌─────────────┐
│ 🥁          │ │ 👏          │ │ ⭐          │
│ Drum Hit    │ │ Applause    │ │ Laser       │
│ 0:02        │ │ 0:04        │ │ 0:01        │
└─────────────┘ └─────────────┘ └─────────────┘

┌─────────────┐ ┌─────────────┐ ┌─────────────┐
│ 🚀          │ │ 💥          │ │ 🔔          │
│ Intro       │ │ Impact      │ │ Notification│
│ 0:05        │ │ 0:02        │ │ 0:03        │
└─────────────┘ └─────────────┘ └─────────────┘
```

Tiles should be large enough to trigger confidently during a live show.

The interface should minimize the risk of clicking the wrong sound.

---

# 46. Nice-to-Have Future Enhancements

Optional future UX features:

- favorite/starred sounds,
- recent sounds,
- custom tile colors,
- custom hotkeys per tile,
- duplicate tile,
- multiple soundboard pages,
- adjustable tile size,
- import/export soundboard configuration,
- fullscreen performance mode,
- lock editing during live mode,
- OSC test button,
- fade-in/fade-out handles in the waveform editor,
- clip volume adjustment,
- pan control,
- tile playback progress indicator.

These should remain secondary to the main workflow.

---

# 47. Final Mental Model

The UI should communicate four simple ideas:

## Left

**Where are my sounds?**

Categories and organization.

## Center

**Which sound do I want to trigger?**

Soundboard tiles.

## Right

**What OSC behavior belongs to this sound?**

OSC targets and commands.

## Bottom

**Which exact part of the source media is this sound?**

Waveform selection and trimming.

---

# 48. Core UX Principles

The application should always prioritize:

### Speed

The user should go from media file to usable sound tile in seconds.

### Clarity

At any moment the user should know:

- which category is selected,
- which sound tile is selected,
- which part of the media is being used,
- what OSC commands are associated with it,
- where those OSC commands will be sent.

### Minimal Repetition

Saved OSC targets should eliminate repeated IP entry.

Localhost should work by default.

### Immediate Feedback

Playback, selection, waveform changes, and OSC sends should all produce clear but subtle visual feedback.

### Live Usability

The tile grid must remain easy to operate during a show.

### Low Complexity

The application should expose only the controls that are needed for this workflow.

It should remain much simpler than a DAW or professional automation editor.

---

# 49. Final Design Goal

The finished application should feel like a professional **soundboard + lightweight sampler + OSC trigger launcher**.

The user should be able to understand the interface almost immediately:

- drop media,
- trim it,
- name it,
- assign a sticker,
- save it,
- optionally add OSC commands,
- press the tile to trigger the experience.

The design should remain **fast, clean, visual, and performance-oriented**.