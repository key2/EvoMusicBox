#include "model/Sound.h"
#include "model/InspectorHooks.h"
#include "util/TimeFormat.h"
#include <algorithm>

namespace evobox
{

std::function<void(Sound&)>           InspectorHooks::sound;
std::function<void(GiftAction&)>      InspectorHooks::gift;
std::function<void(RoomEventAction&)> InspectorHooks::roomEvent;

// ================================================================ Sound
Sound::Sound() : organic::BaseItem(kType, "New sound"), oscActions(this)
{
    stickerP    = addString("Sticker", "ph:speaker-high", "Tile sticker (ph:<name>, emoji:<char>)");
    trimStartP  = addFloatUnbounded("Trim Start", 0.f, "Clip start in the source media (s)");
    trimEndP    = addFloatUnbounded("Trim End", 0.f, "Clip end in the source media (s)");
    gainDbP     = addFloat("Gain", 0.f, -24.f, 24.f, "Clip gain (dB)");
    normalizeP  = addBool("Normalize", false, "Normalize the rendered clip to -1 dBFS peak");
    fadeInMsP   = addFloat("Fade In", 0.f, 0.f, 5000.f, "Fade in (ms)");
    fadeOutMsP  = addFloat("Fade Out", 0.f, 0.f, 5000.f, "Fade out (ms)");
    hotkeyP     = addString("Hotkey", "", "Reserved: per-tile keyboard shortcut");
    isEffectP   = addBool("Effect", false, "Effect: plays on top of everything and stacks when triggered again.\n"
                                           "Music (off): starting it stops the music that was playing");
    trimStartP->unit = "s"; trimEndP->unit = "s";
    gainDbP->unit = "dB"; fadeInMsP->unit = "ms"; fadeOutMsP->unit = "ms";
    trimStartP->dragSpeed = trimEndP->dragSpeed = 0.01f;

    colorP->setValue(ImVec4(0.36f, 0.56f, 0.95f, 1.f), false);
    colorP->defaultValue = colorP->value;
    hideParam(enabledP);

    oscActions.addPhase("At play (start)", Anchor::Start, 0);
    oscActions.addPhase("After play",      Anchor::End,   0);
}

void Sound::setCategoryUidUndoable(Uid c)
{
    setFieldUndoable<Uid>(this, categoryUid, c, "Move to category");
}

void Sound::setTrim(double start, double end, bool notify)
{
    if (end < start) std::swap(start, end);
    trimStartP->setValue((float)start, notify);
    trimEndP->setValue((float)end, notify);
}

void Sound::setTrimUndoable(double start, double end)
{
    if (end < start) std::swap(start, end);
    float os = trimStartP->floatValue(), oe = trimEndP->floatValue();
    float ns = (float)start, ne = (float)end;
    if (os == ns && oe == ne) return;
    Sound* self = this;
    organic::UndoManager::get().perform("Trim clip",
        [self, ns, ne] { self->setTrim(ns, ne); },
        [self, os, oe] { self->setTrim(os, oe); },
        { this });
}

void Sound::inspectorGui()
{
    if (InspectorHooks::sound) InspectorHooks::sound(*this);
    else organic::Container::inspectorGui();
}

void Sound::onParamChanged(organic::Parameter* p)
{
    if (p == trimStartP || p == trimEndP || p == gainDbP || p == normalizeP || p == fadeInMsP || p == fadeOutMsP)
    {
        rt.clipDirty = true;
        rt.dirtyTime = nowSeconds();
    }
}

json Sound::save() const
{
    json j = BaseItem::save();
    j["categoryUid"] = categoryUid;
    j["source"] = source.toJson();
    j["clipFile"] = clipFile;
    j["actions"] = oscActions.save();
    return j;
}

void Sound::load(const json& j)
{
    BaseItem::load(j);
    categoryUid = jget<Uid>(j, "categoryUid", 0);
    if (j.contains("source")) source = MediaRef::fromJson(j["source"]);
    clipFile = jget<std::string>(j, "clipFile", "");
    if (j.contains("actions")) oscActions.load(j["actions"]);
    rt.clipDirty = false; // trims came from disk, not from an edit
}

// ================================================================ SoundManager
SoundManager::SoundManager(organic::Container* parent)
    : organic::BaseManager("Sounds", parent)
{
    addDef("Sound", Sound::kType, [] { return std::make_unique<Sound>(); });
}

std::vector<Sound*> SoundManager::sounds() const
{
    std::vector<Sound*> out;
    out.reserve(items.size());
    for (auto& i : items) out.push_back(static_cast<Sound*>(i.get()));
    return out;
}

std::vector<Sound*> SoundManager::soundsInCategory(Uid categoryUid) const
{
    std::vector<Sound*> out;
    for (auto& i : items)
    {
        auto* s = static_cast<Sound*>(i.get());
        if (categoryUid == 0 || s->categoryUid == categoryUid) out.push_back(s);
    }
    return out;
}

size_t SoundManager::countInCategory(Uid categoryUid) const
{
    if (categoryUid == 0) return items.size();
    size_t n = 0;
    for (auto& i : items) if (static_cast<Sound*>(i.get())->categoryUid == categoryUid) n++;
    return n;
}

Sound* SoundManager::addSoundUndoable(std::unique_ptr<Sound> s, int index)
{
    if (!s) return nullptr;
    Sound* raw = s.get();
    addItem(std::move(s), index);
    json data = raw->save();
    data["_index"] = indexOf(raw);
    Uid uid = raw->uid;
    SoundManager* self = this;
    organic::UndoManager::get().pushDone("Add sound",
        [self, data] { self->addItemFromJson(data); },
        [self, uid]  { self->removeItem(uid); },
        { self });
    return raw;
}

void SoundManager::moveToCategoryUndoable(const std::vector<Sound*>& sounds, Uid categoryUid)
{
    struct Rec { Uid uid; Uid oldCat; };
    std::vector<Rec> recs;
    for (Sound* s : sounds)
        if (s && s->manager == this && s->categoryUid != categoryUid) recs.push_back({ s->uid, s->categoryUid });
    if (recs.empty()) return;
    SoundManager* self = this;
    organic::UndoManager::get().perform("Move to category",
        [self, recs, categoryUid]
        {
            for (auto& r : recs) if (Sound* s = self->find(r.uid)) s->categoryUid = categoryUid;
            notifyStructureChanged(self);
        },
        [self, recs]
        {
            for (auto& r : recs) if (Sound* s = self->find(r.uid)) s->categoryUid = r.oldCat;
            notifyStructureChanged(self);
        },
        { self });
}

void SoundManager::reorderUndoable(Sound* dragged, Sound* dropTarget, bool after)
{
    if (!dragged || !dropTarget || dragged == dropTarget) return;
    int from = indexOf(dragged);
    int to = indexOf(dropTarget);
    if (from < 0 || to < 0) return;
    if (after && from > to) to += 1;
    if (!after && from < to) to -= 1;
    if (from == to) return;
    undoableMove(from, to);
}

void SoundManager::onItemsChanged() { notifyStructureChanged(this); }

} // namespace evobox
