#include "model/Category.h"

namespace evobox
{

Category::Category() : organic::BaseItem(kType, "Category")
{
    iconP = addString("Icon", "ph:folder", "Sticker shown next to the name (ph:<phosphor-name>)");
    colorP->setValue(ImVec4(0.45f, 0.55f, 0.70f, 1.f), false);
    colorP->defaultValue = colorP->value;
    hideParam(enabledP);
}

json Category::save() const
{
    json j = BaseItem::save();
    j["builtin"] = builtin;
    return j;
}

void Category::load(const json& j)
{
    BaseItem::load(j);
    builtin = jget<bool>(j, "builtin", false);
}

// ================================================================ CategoryManager
CategoryManager::CategoryManager(organic::Container* parent)
    : organic::BaseManager("Categories", parent)
{
    selectionScopeName = "categories";
    addDef("Category", Category::kType, [] { return std::make_unique<Category>(); });
}

Category* CategoryManager::findByName(const std::string& name) const
{
    for (auto& i : items) if (i->niceName == name) return static_cast<Category*>(i.get());
    return nullptr;
}

Category* CategoryManager::customCategory() const
{
    if (auto* c = findByName("Custom")) return c;
    for (auto& i : items) if (static_cast<Category*>(i.get())->builtin) return static_cast<Category*>(i.get());
    return items.empty() ? nullptr : static_cast<Category*>(items[0].get());
}

std::vector<Category*> CategoryManager::categories() const
{
    std::vector<Category*> out;
    for (auto& i : items) out.push_back(static_cast<Category*>(i.get()));
    return out;
}

void CategoryManager::seedDefaults()
{
    struct Def { const char* name; const char* icon; ImVec4 color; };
    static const Def defs[] = {
        { "Music",     "ph:music-notes",   ImVec4(0.36f, 0.56f, 0.95f, 1.f) },
        { "Effects",   "ph:sparkle",       ImVec4(0.95f, 0.62f, 0.25f, 1.f) },
        { "Voices",    "ph:microphone",    ImVec4(0.85f, 0.35f, 0.55f, 1.f) },
        { "Ambient",   "ph:cloud",         ImVec4(0.30f, 0.72f, 0.62f, 1.f) },
        { "Interface", "ph:cursor-click",  ImVec4(0.62f, 0.55f, 0.90f, 1.f) },
        { "Custom",    "ph:folder",        ImVec4(0.55f, 0.60f, 0.68f, 1.f) },
    };
    for (auto& d : defs)
    {
        if (findByName(d.name)) continue;
        auto c = std::make_unique<Category>();
        c->setNiceName(d.name);
        c->builtin = true;
        c->iconP->setValue(std::string(d.icon), false);
        c->iconP->defaultValue = c->iconP->value;
        c->colorP->setValue(d.color, false);
        c->colorP->defaultValue = c->colorP->value;
        addItem(std::move(c));
    }
}

Category* CategoryManager::addCategoryUndoable(const std::string& name, const std::string& icon, ImVec4 color)
{
    auto c = std::make_unique<Category>();
    c->setNiceName(name.empty() ? "New category" : name);
    if (!icon.empty()) c->iconP->setValue(icon, false);
    c->colorP->setValue(color, false);
    Category* raw = c.get();
    addItem(std::move(c));
    json data = raw->save();
    data["_index"] = indexOf(raw);
    Uid uid = raw->uid;
    CategoryManager* self = this;
    organic::UndoManager::get().pushDone("Add category",
        [self, data] { self->addItemFromJson(data); },
        [self, uid]  { self->removeItem(uid); },
        { self });
    return raw;
}

void CategoryManager::onItemsChanged() { notifyStructureChanged(this); }

void CategoryManager::load(const json& j)
{
    BaseManager::load(j);
    if (items.empty()) seedDefaults();
}

} // namespace evobox
