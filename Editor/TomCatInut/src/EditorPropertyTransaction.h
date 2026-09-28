#pragma once

#include "TomCat/Scene/ComponentRegistry.h"
#include "TomCat/Scene/Entity.h"
#include <vector>

namespace TomCat::EditorProperties {

// A multi-selection edit is one operation. Preserve all selected values when a
// later target rejects the value, including a setter that mutates before failure.
inline bool SetAll(const PropertyDescriptor& property, const std::vector<Entity>& targets,
    const PropertyValue& value, std::string& error)
{
    std::vector<PropertyValue> before;
    before.reserve(targets.size());
    for (Entity target : targets) before.push_back(property.Get(target));
    size_t attempted = 0;
    for (Entity target : targets)
    {
        ++attempted;
        if (property.Set(target, value, error)) continue;
        for (size_t i = attempted; i > 0; --i)
        {
            std::string rollback;
            if (!property.Set(targets[i - 1], before[i - 1], rollback))
                error += "; rollback failed: " + rollback;
        }
        return false;
    }
    error.clear();
    return true;
}

}
