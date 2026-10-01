// Tiles.h — the soundboard / gallery tiles (UI.md §7-§10) drawn with ImDrawList.
#pragma once

#include <string>
#include "app/Prefs.h"
#include "imgui.h"
#include "live/GiftCatalog.h"
#include "model/GiftAction.h"
#include "model/RoomEventAction.h"
#include "model/Sound.h"

namespace evobox
{
namespace ui
{

enum class TileAction { None, Select, Play, Stop, ContextMenu, DoubleClick };

ImVec2 tileSizeFor(TileSize s);
float  tileStickerScale(TileSize s);

struct SoundTileState
{
    bool selected = false;
    bool playing = false;
    float progress01 = 0.f;       // latest voice: 0 = trim start .. 1 = trim end (the clip is the selected region)
    bool missingMedia = false;
    bool decoding = false;
    double now = 0;
    double lastOscPulse = -1;
    double lastTrigger = -1;
    int oscCommandCount = 0;
    bool performanceMode = false; // body click = play
    bool effect = false;          // stackable: the play button never turns into Stop; while playing a
                                  // separate stop circle (bottom-left) stops every voice of the sound
    int voices = 0;               // active voices (badge "x3" on stacked effects)
};

// Draws one sound tile at the cursor. Sets up an ImGui drag source ("EVOBOX_SOUND", uid) and a
// drop target for reordering (outDropUid receives the dragged uid, outDropAfter the side).
TileAction SoundTile(Sound& s, ImVec2 size, const SoundTileState& st, Uid* outDropUid = nullptr, bool* outDropAfter = nullptr);
bool AddSoundTile(ImVec2 size, bool highlight);

struct GiftTileState
{
    bool selected = false;
    int configuredCommands = 0;   // badge
    bool hasSound = false;
    bool active = false;          // Stop timer pending
    double now = 0;
    double lastPulse = -1;
    int receivedCount = 0;
};
TileAction GiftTile(GiftInfo& g, ImVec2 size, const GiftTileState& st);

// Room event row/tile for the navigator (icon · name · badge)
const char* roomEventIcon(RoomEventKind k);

} // namespace ui
} // namespace evobox
