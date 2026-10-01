// ProjectMerge.h — File > Merge show...: adds another show's content to the current project.
//   always : its sounds (rendered clips copied into this project's clips/ under new names, picture
//            stickers copied into icons/, fresh uids) and their categories (matched by name here,
//            created when missing)
//   option : the OSC setup — targets (matched by host:port or name, created when missing), the
//            sounds' OSC commands (targets remapped), gift actions (skipped when the gift is already
//            configured here) and room-event actions (commands appended to this project's fixed
//            events; their sound adopted when this project has none)
// Settings are never merged. The source is read as plain JSON + files, never through a Project
// object (constructing one clears the undo history). Files are copied first, then the model changes
// in ONE undo step ("Merge show"); an undone merge leaves the copied files for save()'s garbage
// collection. New sounds get their clips through the app's lazy clip loader.
#pragma once

#include <string>
#include "model/Project.h"

namespace evobox
{

struct MergeOptions
{
    bool osc = true; // targets + the sounds' commands + gift and room-event actions
};

// What another show contains — shown in the merge dialog before deciding.
struct MergeSource
{
    std::string path;         // as given: .liv, .evobox folder or its project.json
    std::string title;        // show name (file / folder stem)
    bool archive = false;     // .liv: files are read from the zip; else from `dir`
    std::string dir;          // folder bundle directory
    nlohmann::json project;   // its project.json
    int sounds = 0, categories = 0, targets = 0, commands = 0, giftActions = 0, roomEventCommands = 0;
    bool valid() const { return project.is_object(); }
};

struct MergeReport
{
    int sounds = 0, categories = 0, targets = 0, commands = 0, giftActions = 0, giftsSkipped = 0, roomEvents = 0;
    int clipsCopied = 0, clipsMissing = 0, pictures = 0;
    std::string summary() const; // "3 sounds, 1 category, 5 OSC commands, 1 target"
};

class ProjectMerge
{
public:
    // Reads the other show's project.json and counts its content. Fails with `err` when `path` is
    // not an EvoMusicBox show.
    static bool inspect(const std::string& path, MergeSource& out, std::string* err = nullptr);
    // Merges `src` into `p` (one undo step). Returns false with `err` and an unchanged model when
    // a clip or picture file cannot be copied.
    static bool merge(Project& p, const MergeSource& src, const MergeOptions& opts, MergeReport* report = nullptr,
                      std::string* err = nullptr);
    static bool merge(Project& p, const std::string& path, const MergeOptions& opts, MergeReport* report = nullptr,
                      std::string* err = nullptr);
};

} // namespace evobox
