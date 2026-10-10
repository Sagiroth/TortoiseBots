#pragma once
// Shared Thaddius lookup: single home for the add/boss finder used by
// the phase triggers, the pet attack, and the even-HP multiplier.
// Cached lists only (attackers, possible attack targets, current
// target) — no grid sweeps per tick. During the fight all three adds
// are on threat group-wide, so nothing is missed.

#include "playerbot/playerbot.h"

namespace ai
{
    inline constexpr uint32 kStalaggEntry = 15929;
    inline constexpr uint32 kFeugenEntry = 15930;
    inline constexpr uint32 kThaddiusEntry = 15928;

    inline void FindThaddiusAdds(PlayerbotAI* ai, Player* bot, Unit*& stalagg, Unit*& feugen, Unit*& thaddius)
    {
        stalagg = nullptr;
        feugen = nullptr;
        thaddius = nullptr;
        Unit* current = ai->GetAiObjectContext()->GetValue<Unit*>("current target")->Get();
        if (current && current->IsAlive())
        {
            if (current->GetEntry() == kStalaggEntry)
                stalagg = current;
            else if (current->GetEntry() == kFeugenEntry)
                feugen = current;
            else if (current->GetEntry() == kThaddiusEntry)
                thaddius = current;
        }
        if (stalagg && feugen && thaddius)
            return;
        const std::list<ObjectGuid> attackers =
            ai->GetAiObjectContext()->GetValue<std::list<ObjectGuid>>("attackers")->Get();
        for (const ObjectGuid& guid : attackers)
        {
            Unit* unit = ai->GetUnit(guid);
            if (!unit || !unit->IsAlive())
                continue;
            if (!stalagg && unit->GetEntry() == kStalaggEntry)
                stalagg = unit;
            else if (!feugen && unit->GetEntry() == kFeugenEntry)
                feugen = unit;
            else if (!thaddius && unit->GetEntry() == kThaddiusEntry)
                thaddius = unit;
            if (stalagg && feugen && thaddius)
                return;
        }
        const std::list<ObjectGuid> targets =
            ai->GetAiObjectContext()->GetValue<std::list<ObjectGuid>>("possible attack targets")->Get();
        for (const ObjectGuid& guid : targets)
        {
            Unit* unit = ai->GetUnit(guid);
            if (!unit || !unit->IsAlive())
                continue;
            if (!stalagg && unit->GetEntry() == kStalaggEntry)
                stalagg = unit;
            else if (!feugen && unit->GetEntry() == kFeugenEntry)
                feugen = unit;
            else if (!thaddius && unit->GetEntry() == kThaddiusEntry)
                thaddius = unit;
            if (stalagg && feugen && thaddius)
                return;
        }
        (void)bot;
    }
}
