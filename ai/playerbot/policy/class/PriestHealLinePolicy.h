#pragma once

#include <cstdint>
#include <functional>

class Player;

namespace ai
{
    // Heal spell-line ranks (core spell_template, name 'Heal'): 2054 is
    // rank 1 (level 16), then 2055, 6063, 6064. Lesser Heal (2050/2052/2053)
    // is the level 1-18 line it replaces.
    inline bool PriestHealLineKnown(std::function<bool(std::uint32_t)> const& has)
    {
        return has(2054) || has(2055) || has(6063) || has(6064);
    }

    inline bool PriestKnowsHealLine(Player* bot)
    {
        return PriestHealLineKnown([bot](std::uint32_t id) { return bot->HasSpell(id); });
    }
}
