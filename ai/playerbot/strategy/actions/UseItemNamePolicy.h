#pragma once

#include <string>

// Name match for "u <object>" (UseAction::Execute): a nearby game object is a
// candidate when its name contains the text the player typed. Kept free of
// core headers so the rule is unit-testable (tools/test_use_item_name_policy.cpp).
//
// std::string::find returns npos on a miss, and npos is non-zero, so a bare
// find() is truthy on a miss and falsy on a hit at position 0. Compare with npos.

namespace ai
{
    inline bool UseNameMatchesObject(const std::string& objectName, const std::string& useName)
    {
        return objectName.find(useName) != std::string::npos;
    }
}  // namespace ai
