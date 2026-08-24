// Forward-ported from mod-playerbots PartyMemberSnaredTargetValue.cpp - modern donor
// Source: mod-playerbots@5397110, Shyalya@1f9497e Tortoise 1.18.1 baseline
/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "PartyMemberSnaredTargetValue.h"
#include "PlayerbotAIAware.h"
#include "Playerbots.h"
#include <limits>

class PartyMemberSnaredTargetPredicate : public FindPlayerPredicate, public PlayerbotAIAware
{
public:
    PartyMemberSnaredTargetPredicate(PlayerbotAI* botAI)
        : PlayerbotAIAware(botAI)
    {
    }

    bool Check(Unit* unit) override
    {
        if (!unit || !unit->IsAlive() || !unit->IsInWorld() || unit == botAI->GetBot())
            return false;

        if (unit->GetMapId() != botAI->GetBot()->GetMapId())
            return false;

        if (!botAI->GetBot()->IsWithinLOSInMap(unit))
            return false;

        bool movementImpaired = unit->HasAuraType(SPELL_AURA_MOD_ROOT) ||
                                unit->HasAuraType(SPELL_AURA_MOD_STUN) ||
                                unit->HasAuraType(SPELL_AURA_MOD_DECREASE_SPEED);
        return movementImpaired && !botAI->HasAnyAuraOf(unit, "stealth", "prowl", nullptr);
    }
};

Unit* PartyMemberSnaredTargetValue::Calculate()
{
    Group* group = bot->GetGroup();
    if (!group)
        return nullptr;

    PartyMemberSnaredTargetPredicate predicate(ai);
    Player* bestTarget = nullptr;
    float closestDistance = std::numeric_limits<float>::max();

    for (GroupReference* gref = group->GetFirstMember(); gref; gref = gref->next())
    {
        Player* member = gref->GetSource();
        if (!member)
            continue;

        if (!predicate.Check(member))
            continue;

        float const distance = bot->GetDistance2d(member);
        if (distance < closestDistance)
        {
            closestDistance = distance;
            bestTarget = member;
        }
    }

    return bestTarget;
}
