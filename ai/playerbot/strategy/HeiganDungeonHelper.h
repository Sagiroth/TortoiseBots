#pragma once
// Shared Heigan lookup: single home for the boss finder used by the
// dance/platform triggers, the dance action, and the suppression
// multiplier. Attackers-list first (cheap guid walk), grid fallback
// only when the boss is off threat, map early-out before any scan.

#include "playerbot/playerbot.h"
#include "Maps/GridNotifiers.h"
#include "Maps/GridNotifiersImpl.h"
#include "Maps/CellImpl.h"

namespace ai
{
    inline constexpr uint32 kHeiganEntry = 15936;
    inline constexpr uint32 kHeiganMapId = 533;
    inline constexpr uint32 kHeiganPlagueCloud = 29350;

    inline Unit* FindHeiganBoss(PlayerbotAI* ai, Player* bot)
    {
        if (!bot || bot->GetMapId() != kHeiganMapId)
            return nullptr;
        const std::list<ObjectGuid> attackers =
            ai->GetAiObjectContext()->GetValue<std::list<ObjectGuid>>("attackers")->Get();
        for (const ObjectGuid& guid : attackers)
        {
            Unit* unit = ai->GetUnit(guid);
            if (unit && unit->IsAlive() && unit->GetEntry() == kHeiganEntry)
                return unit;
        }
        std::list<Unit*> nearby;
        MaNGOS::AllCreaturesOfEntryInRange check(bot, kHeiganEntry, 100.0f);
        MaNGOS::UnitListSearcher<MaNGOS::AllCreaturesOfEntryInRange> searcher(nearby, check);
        Cell::VisitAllObjects(bot, searcher, 100.0f);
        for (Unit* unit : nearby)
        {
            if (unit && unit->IsAlive())
                return unit;
        }
        return nullptr;
    }

    // Dance detection. Primary: Plague Cloud aura on Heigan (self-cast
    // at dance start, TARGET_UNIT_CASTER, 45s). Fallback: Heigan is
    // engaged but victimless — during the dance he is REACT_PASSIVE with
    // AttackStop, while pre-pull he is not in combat and mid-fight he
    // holds a victim. The fallback carries no clock; callers use it to
    // gate, not to index.
    inline bool IsHeiganDancing(PlayerbotAI* ai, Unit* boss)
    {
        if (!boss)
            return false;
        if (ai->HasAura(kHeiganPlagueCloud, boss))
            return true;
        return boss->IsInCombat() && boss->GetVictim() == nullptr;
    }
}
