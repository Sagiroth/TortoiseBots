#include "playerbot/playerbot.h"
#include "RuinsOfAhnqirajDungeonActions.h"
#include "playerbot/OssirianCrystalPolicy.h"
#include "Maps/GridNotifiers.h"
#include "Maps/GridNotifiersImpl.h"
#include "Maps/CellImpl.h"

using namespace ai;

namespace
{
    Unit* FindOssirianForCrystal(PlayerbotAI* ai, Player* bot)
    {
        const std::list<ObjectGuid> attackers = ai->GetAiObjectContext()->GetValue<std::list<ObjectGuid>>("attackers")->Get();
        for (const ObjectGuid& guid : attackers)
        {
            Unit* unit = ai->GetUnit(guid);
            if (unit && unit->GetEntry() == 15339)
                return unit;
        }
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

    int32 OssirianWeaknessMsLeft(PlayerbotAI* ai, Unit* boss)
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
}

bool UseOssirianCrystalAction::Execute(Event& event)
{
    Unit* boss = FindOssirianForCrystal(ai, bot);
    if (!boss)
        return false;

    GameObject* crystal = bot->FindNearestGameObject(180619, 200.0f);
    if (!crystal)
        return false;

    if (bot->GetDistance(crystal) > INTERACTION_DISTANCE)
        return MoveTo(bot->GetMapId(), crystal->GetPositionX(), crystal->GetPositionY(), crystal->GetPositionZ());

    // In range: wait here until the buff is up or the weakness is nearly
    // out, then use iff Ossirian is close enough and the crystal is idle.
    if (!ShouldUseOssirianCrystal(boss->GetDistance(crystal),
        crystal->HasFlag(GAMEOBJECT_FLAGS, GO_FLAG_IN_USE),
        ai->HasAura(25176, boss), OssirianWeaknessMsLeft(ai, boss)))
        return false;

    if (!bot->GetGameObjectIfCanInteractWith(crystal->getObjectGuid()))
        return false;

    std::unique_ptr<WorldPacket> packet(new WorldPacket(CMSG_GAMEOBJ_USE));
    *packet << crystal->getObjectGuid();
    bot->GetSession()->QueuePacket(packet.release());
    return true;
}
