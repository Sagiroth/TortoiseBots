#pragma once
#include <string>

namespace ai
{
    inline bool GarrIncidentalDamageAction(std::string const& name, bool aoeThreat, bool healing, bool crowdControl)
    {
        if (healing || crowdControl)
            return false;
        if (aoeThreat)
            return true;
        // These actions are marked SINGLE/NONE by the existing class graph,
        // despite splash, chain, cleave, or autonomous damage targeting.
        for (char const* action : { "multi-shot", "chain lightning", "cleave", "whirlwind",
            "thunder clap", "sweeping strikes", "blade flurry", "carve", "hurricane",
            "consecration", "holy nova", "rain of fire", "hellfire", "fire nova",
            "fire nova totem", "magma totem", "searing totem", "screech" })
            if (name == action)
                return true;
        return name.compare(0, 14, "explosive trap") == 0;
    }
}
