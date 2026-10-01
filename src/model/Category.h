// Category.h — sound categories (left navigator). Defaults from UI.md §4 are builtin:
// renamable / reorderable, deletable only when empty. "All Sounds" is a virtual entry.
#pragma once

#include "model/ModelCommon.h"

namespace evobox
{

class Category : public organic::BaseItem
{
public:
    static constexpr const char* kType = "Category";

    Category();

    organic::Parameter* iconP = nullptr; // "ph:folder" (Phosphor glyph name) / "emoji:..." / "img:<file>"
    bool builtin = false;

    std::string icon() const { return iconP->stringValue(); }
    ImVec4 color() const { return colorP->color(); }

    std::string inspectableTypeName() const override { return "Category"; }
    json save() const override;
    void load(const json& j) override;
};

class CategoryManager : public organic::BaseManager
{
public:
    explicit CategoryManager(organic::Container* parent = nullptr);

    Category* category(size_t i) const { return static_cast<Category*>(items[i].get()); }
    Category* find(Uid uid) const { return static_cast<Category*>(findItem(uid)); }
    Category* findByName(const std::string& name) const;
    Category* customCategory() const; // the fallback for orphaned sounds ("Custom", else first)
    std::vector<Category*> categories() const;

    void seedDefaults();          // Music, Effects, Voices, Ambient, Interface, Custom
    Category* addCategoryUndoable(const std::string& name, const std::string& icon, ImVec4 color);

    void onItemsChanged() override;
    void load(const json& j) override;
};

} // namespace evobox
