#pragma once
// Shared Ossirian crystal helpers (boss lookup + weakness clock) used by
// both the run trigger and the use action. Single home for the logic the
// review flagged as duplicated across the two translation units.

#include "playerbot/playerbot.h"
#include "Maps/GridNotifiers.h"
#include "Maps/GridNotifiersImpl.h"
#include "Maps/CellImpl.h"

namespace ai
{
    // Ossirian entry (tw_world creature_template verified).
    inline constexpr uint32 kOssirianEntry = 15339;
    // Ossirian Crystal GO (tw_world gameobject_template verified).
    inline constexpr uint32 kOssirianCrystalEntry = 180619;

    // Attackers-list hit first (cheap, cached per tick), then a 100yd
    // entry sweep for a boss that has not hit this bot yet.
    inline Unit* FindOssirianBoss(PlayerbotAI* ai, Player* bot)
    {
        const std::list<ObjectGuid> attackers =
            ai->GetAiObjectContext()->GetValue<std::list<ObjectGuid>>("attackers")->Get();
        for (const ObjectGuid& guid : attackers)
        {
            Unit* unit = ai->GetUnit(guid);
            if (unit && unit->IsAlive() && unit->GetEntry() == kOssirianEntry)
                return unit;
        }
        std::list<Unit*> nearby;
        MaNGOS::AllCreaturesOfEntryInRange check(bot, kOssirianEntry, 100.0f);
        MaNGOS::UnitListSearcher<MaNGOS::AllCreaturesOfEntryInRange> searcher(nearby, check);
        Cell::VisitAllObjects(bot, searcher, 100.0f);
        for (Unit* unit : nearby)
        {
            if (unit && unit->IsAlive())
                return unit;
        }
        return nullptr;
    }

    // Minimum weakness-debuff duration left across the five weakness
    // spells (core SpellWeakness: 25177/78/80/81/83). Large when no
    // debuff is up (buff phase).
    inline int32 OssirianWeaknessMsLeft(PlayerbotAI* ai, Unit* boss)
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

    // Nearest crystal to the BOSS (not the bot): the raid kites Ossirian
    // toward one of the two active crystals, and a bot near the other
    // crystal must not pick its own — it would strand itself out of the
    // 25yd use range (donor Aq20Utils.cpp:42).
    inline GameObject* NearestOssirianCrystalToBoss(Unit* boss)
    {
        return boss ? boss->FindNearestGameObject(kOssirianCrystalEntry, 200.0f) : nullptr;
    }

    // Crystal runners are DPS only: tanks hold the +300% boss, healers
    // heal. (Donor assigns runners; without assignment infra, every
    // non-tank non-healer runs.)
    inline bool IsOssirianCrystalRunner(PlayerbotAI* ai, Player* bot)
    {
        return !PlayerbotAI::IsTank(bot) && !PlayerbotAI::IsHeal(bot);
    }
}
