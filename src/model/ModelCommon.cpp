#include "model/ModelCommon.h"

namespace evobox
{

void notifyStructureChanged(organic::Container* source)
{
    organic::Container* c = source;
    while (c)
    {
        if (auto* l = dynamic_cast<StructureListener*>(c)) l->onStructureChanged(source);
        c = c->parent;
    }
}

} // namespace evobox
