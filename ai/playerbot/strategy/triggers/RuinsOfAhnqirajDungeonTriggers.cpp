#include "playerbot/playerbot.h"
#include "RuinsOfAhnqirajDungeonTriggers.h"
#include "playerbot/OssirianCrystalPolicy.h"

using namespace ai;

namespace
{
    // Minimum weakness-debuff duration left on Ossirian across the five
    // weakness spells (core SpellWeakness: 25177/78/80/81/83). Large when
    // no debuff is up (buff phase or pre-fight).
    int32 OssirianDebuffMsRemaining(Unit* boss, PlayerbotAI* ai)
    {
        static const uint32 weakness[] = { 25177, 25178, 25180, 25181, 25183 };
        int32 remaining = 0xffffff;
        for (uint32 spellId : weakness)
        {
            if (Aura* aura = ai->GetAura(spellId, boss))
            {
                int32 duration = aura->GetAuraDuration();
                if (duration < remaining)
                    remaining = duration;
            }
        }
        return remaining;
    }

    Unit* FindOssirian(PlayerbotAI* ai)
    {
        // Cheap first: attackers list (boss usually has the bot on threat).
        const std::list<ObjectGuid> attackers = AI_VALUE(std::list<ObjectGuid>, "attackers");
        for (const ObjectGuid& guid : attackers)
        {
            Unit* unit = ai->GetUnit(guid);
            if (unit && unit->GetEntry() == 15339)
                return unit;
        }
        // Fallback: 100yd grid sweep by entry (phase helpers need the boss
        // before it has hit this bot).
        Player* bot = ai->GetBot();
        std::list<Unit*> nearby;
        MaNGOS::AllCreaturesOfEntryInRange check(bot, 15339, 100.0f);
        MaNGOS::UnitListSearcher<MaNGOS::AllCreaturesOfEntryInRange> searcher(nearby, check);
        Cell::VisitAllObjects(bot, searcher, 100.0f);
        for (Unit* unit : nearby)
        {
            if (unit && unit->IsAlive())
                return unit;
        }
        return nullptr;
    }
}

bool OssirianCrystalRunTrigger::IsActive()
{
    Unit* boss = FindOssirian(ai);
    if (!boss || !boss->IsInCombat())
        return false;

    const bool buffUp = ai->HasAura(25176, boss);
    const int32 debuffMs = OssirianDebuffMsRemaining(boss, ai);

    // Run time to the nearest crystal: grid sweep for GO 180619 in 200yd
    // (donor range), estimate ~7yd/s run speed.
    GameObject* crystal = bot->FindNearestGameObject(180619, 200.0f);
    int32 runMs = 0;
    if (crystal)
        runMs = int32(bot->GetDistance(crystal) / 7.0f * 1000.0f);

    return ShouldRunToOssirianCrystal(true, buffUp, debuffMs, crystal ? runMs : 0);
}
