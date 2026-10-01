// ModelCommon.h — shared includes and helpers for the domain model.
// The model layer depends only on imgui_organic (Container / Parameter / BaseItem /
// BaseManager / UndoManager / Selection); it never includes UI headers.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include "OrganicCore.h"
#include "OrganicManager.h"

namespace evobox
{

using json = nlohmann::json;
using Uid = uint64_t; // organic BaseItem uid (stable, persisted); 0 = none / default

// Objects interested in structural changes below them (items added/removed/moved,
// plain-field edits) implement this; notifyStructureChanged() walks the parent chain.
class StructureListener
{
public:
    virtual ~StructureListener() = default;
    virtual void onStructureChanged(organic::Container* source) = 0;
};

// Walks from `source` up through Container::parent and calls every StructureListener.
void notifyStructureChanged(organic::Container* source);

// Undoable edit of a plain (non-Parameter) field such as a uid cross reference.
template <typename T>
void setFieldUndoable(organic::Container* owner, T& field, const T& newValue, const std::string& actionName)
{
    if (field == newValue) return;
    T oldValue = field;
    T* ptr = &field;
    organic::UndoManager::get().perform(
        actionName,
        [ptr, newValue, owner] { *ptr = newValue; notifyStructureChanged(owner); },
        [ptr, oldValue, owner] { *ptr = oldValue; notifyStructureChanged(owner); },
        { owner });
}

// Hides organic's automatic Color / Enabled parameters from generic editors.
inline void hideParam(organic::Parameter* p) { if (p) p->hideInEditor = true; }

// JSON helpers tolerant to missing keys / wrong types.
template <typename T>
T jget(const json& j, const char* key, const T& def)
{
    if (!j.is_object() || !j.contains(key)) return def;
    try { return j.at(key).get<T>(); }
    catch (...) { return def; }
}

} // namespace evobox
