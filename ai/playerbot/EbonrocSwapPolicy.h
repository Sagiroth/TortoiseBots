#pragma once
#include <string>

namespace ai
{
    // Every taunt action name, single-target and area. Encounter roles that
    // own the boss's threat hand-off zero these on the boss.
    inline bool EbonrocTauntAction(std::string const& name)
    {
        return name == "taunt" || name.compare(0, 6, "taunt ") == 0 || name == "growl" ||
            name == "hand of reckoning" || name == "righteous defense" ||
            name == "challenging shout" || name == "challenging roar" || name == "mocking blow";
    }
}
