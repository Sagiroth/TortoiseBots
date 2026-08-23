// Forward-ported from mod-playerbots AggressiveTargetValue.cpp - modern donor
// Source: mod-playerbots@5397110, Shyalya@1f9497e Tortoise 1.18.1 baseline
/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "AggressiveTargetValue.h"
#include "Playerbots.h"
#include "ServerFacade.h"
#include "SharedDefines.h"

Unit* AggressiveTargetValue::Calculate()
{
    Player* master = GetMaster();

    if (master && (master == bot || master->GetMapId() != bot->GetMapId() || master->IsBeingTeleported() ||
                   !GET_PLAYERBOT_AI(master)))
        master = nullptr;

    std::list<ObjectGuid> targets = AI_VALUE(std::list<ObjectGuid>, "possible targets");
    if (targets.empty())
        return nullptr;

    float aggroRange = sPlayerbotAIConfig.aggroDistance;
    float distance = 0;
    Unit* result = nullptr;

    for (ObjectGuid const guid : targets)
    {
        Unit* unit = botAI->GetUnit(guid);
        if (!unit || !unit->IsAlive())
            continue;

        if (!unit->IsInWorld())
            continue;

        CreatureInfo const* creatureInfo = unit->ToCreature() ? unit->ToCreature()->GetCreatureInfo() : nullptr;
        if (creatureInfo && !creatureInfo->loot_id &&
            bot->GetReactionTo(unit) >= REP_NEUTRAL)
            continue;

        if (!bot->IsHostileTo(unit) && creatureInfo && creatureInfo->npc_flags != UNIT_NPC_FLAG_NONE)
            continue;

        if (abs(bot->getPositionZ() - unit->getPositionZ()) > INTERACTION_DISTANCE)
            continue;

        if (!bot->InBattleGround() && master && botAI->HasStrategy("follow", BotState::BOT_STATE_NON_COMBAT) &&
            ServerFacade::instance().getDistance2d(master, unit) > aggroRange)
            continue;

        if (!bot->IsWithinLOSInMap(unit))
            continue;

        if (bot->getDistance(unit) > aggroRange)
            continue;

        float newdistance = bot->getDistance(unit);
        if (!result || (newdistance < distance))
        {
            distance = newdistance;
            result = unit;
        }
    }

    return result;
}
