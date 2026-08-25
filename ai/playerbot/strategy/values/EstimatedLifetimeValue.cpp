// Forward-ported from mod-playerbots EstimatedLifetimeValue.cpp - modern donor
// Source: mod-playerbots@5397110, Shyalya@1f9497e Tortoise 1.18.1 baseline
/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "EstimatedLifetimeValue.h"
#include "AiFactory.h"
#include "PlayerbotAI.h"
#include "PlayerbotAIConfig.h"
#include "PlayerbotFactory.h"
#include "Playerbots.h"
#include "SharedDefines.h"

float EstimatedLifetimeValue::Calculate()
{
    Unit* target = AI_VALUE(Unit*, qualifier);
    if (!target || !target->IsAlive())
    {
        return 0.0f;
    }
    float dps = AI_VALUE(float, "estimated group dps");
    bool aoePenalty = AI_VALUE(uint8, "attacker count") >= 3;
    if (aoePenalty)
        dps *= 0.75;
    float res = target->GetHealth() / dps;
    // bot->Say(target->GetName() + " lifetime: " + std::to_string(res), LANG_UNIVERSAL);
    return res;
}

float EstimatedGroupDpsValue::Calculate()
{
    float totalDps = 0;

    std::vector<Player*> groupPlayer = {bot};
    if (Group* group = bot->GetGroup())
    {
        for (GroupReference* gref = group->GetFirstMember(); gref; gref = gref->next())
        {
            Player* member = gref->GetSource();
            if (member == bot)  // calculated
                continue;

            // Ignore real players and selfbots as they may not help with damage.
            if (!GET_PLAYERBOT_AI(member))
                continue;

            if (!member || !member->IsInWorld() || !member->IsAlive())
                continue;

            if (member->GetMapId() != bot->GetMapId())
                continue;

            if (member->GetDistance(bot) > sPlayerbotAIConfig.sightDistance)
                continue;

            groupPlayer.push_back(member);
        }
    }
    for (Player* player : groupPlayer)
    {
        float roleMultiplier;
        if (botAI->IsTank(player))
            roleMultiplier = 0.3f;
        else if (botAI->IsHeal(player))
            roleMultiplier = 0.1f;
        else
            roleMultiplier = 1.0f;
        float basicDps = GetBasicDps(player->GetLevel());
        float basicGs = GetBasicGs(player->GetLevel());
        uint32 mixedGearScore = GET_PLAYERBOT_AI(player)->GetEquipGearScore(player, false, false);
        float gs_modifier = (float)mixedGearScore / basicGs;
        // Scale the estimate for unusually strong gear.
        if (mixedGearScore >= 300)
        {
            gs_modifier *= 1 + (mixedGearScore - 300) * 0.01;
        }
        if (gs_modifier < 0.75)
            gs_modifier = 0.75;
        if (gs_modifier > 4)
            gs_modifier = 4;
        totalDps += basicDps * roleMultiplier * gs_modifier;
    }
    // Group buff bonus
    if (groupPlayer.size() >= 25)
        totalDps *= 1.2;
    else if (groupPlayer.size() >= 10)
        totalDps *= 1.1;
    else if (groupPlayer.size() >= 5)
        totalDps *= 1.05;
    return totalDps;
}

float EstimatedGroupDpsValue::GetBasicDps(uint32 level)
{
    float basic_dps;

    if (level <= 15)
    {
        basic_dps = 5 + level * 1;
    }
    else if (level <= 25)
    {
        basic_dps = 20 + (level - 15) * 2;
    }
    else if (level <= 45)
    {
        basic_dps = 40 + (level - 25) * 3;
    }
    else if (level <= 55)
    {
        basic_dps = 100 + (level - 45) * 20;
    }
    else if (level <= 60)
    {
        basic_dps = 300 + (level - 55) * 50;
    }
    else
    {
        // Tortoise's player level cap is 60; keep impossible higher levels at
        // the level-60 estimate instead of carrying expansion-era formulas.
        basic_dps = 550;
    }
    return basic_dps;
}

float EstimatedGroupDpsValue::GetBasicGs(uint32 level)
{
    float basic_gs;

    if (level <= 8)
    {
        basic_gs = std::max(1u, (level + 5) * 10u);
    }
    else if (level <= 15)
    {
        basic_gs = std::max(1u, (level + 5) * 12u);
    }
    else if (level <= 60)
    {
        basic_gs = std::max(1u, (level + 5) * 14u);
    }
    else
    {
        // Same level-60 cap as the DPS estimate above.
        basic_gs = 65u * 14u;
    }
    return basic_gs;
}
