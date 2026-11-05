#include "EnchantCapital.h"

#include "../ai/playerbot/GroupMembers.h"
#include "../ai/playerbot/PlayerbotAI.h"
#include "PlayerbotAIStorage.h"
#include "../ai/playerbot/GuidPosition.h"
#include "../ai/playerbot/TravelMgr.h"
#include "../ai/playerbot/WorldPosition.h"
#include "../ai/playerbot/strategy/values/TravelValues.h"
#include "../host/ModuleLog.h"

// pi-lens-ignore: clang:pp_file_not_found
#include "ObjectMgr.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "Player.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "Maps/Map.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "WorldSession.h"

namespace TortoiseBots
{
namespace EnchantCapital
{
namespace
{

bool InGroupWithRealPlayer(Player* bot)
{
    Group* group = bot ? bot->GetGroup() : nullptr;
    if (!group)
        return false;
    for (Player* member : LiveGroupMembers(group))
        if (member && member != bot && member->GetSession() && !member->GetSession()->IsHeadless())
            return true;
    return false;
}

bool InInstance(Player* bot)
{
    if (!bot)
        return true;
    if (bot->InBattleGround() || bot->InBattleGroundQueue())
        return true;
    if (Map* map = bot->GetMap())
        if (map->IsDungeon() || map->IsRaid() || map->IsBattleGround())
            return true;
    return false;
}

// The capital's zone id for one of its map coordinates. The spawn table
// carries positions, not zone ids, so each innkeeper candidate is checked
// with the same terrain lookup the Player getters use.
uint32 ZoneOf(uint32 mapId, float x, float y, float z)
{
    return sTerrainMgr.getZoneId(mapId, x, y, z);
}

} // namespace

CapitalInn FindCapitalInn(bool alliance)
{
    CapitalInn out;
    uint32 const wantZone = CapitalZoneId(alliance);
    uint32 const wantMap = CapitalMapId(alliance);

    auto all = ai::WorldPosition().GetCreaturesNear();
    for (auto cpair : all)
    {
        if (!cpair)
            continue;
        uint32 entry = cpair->second.creature_id[0];
        if (!entry)
            continue;
        CreatureInfo const* cInfo = sObjectMgr.GetCreatureTemplate(entry);
        if (!cInfo || (cInfo->npc_flags & UNIT_NPC_FLAG_INNKEEPER) == 0)
            continue;
        ai::GuidPosition gpos(cpair);
        if (gpos.IsEventUnspawned())
            continue;
        ai::WorldPosition pos(cpair);
        if (!pos.isValid() || pos.getMapId() != wantMap)
            continue;
        if (ZoneOf(pos.getMapId(), pos.getX(), pos.getY(), pos.getZ()) != wantZone)
            continue;
        out.mapId = pos.getMapId();
        out.x = pos.getX();
        out.y = pos.getY();
        out.z = pos.getZ();
        out.o = pos.getO();
        out.found = true;
        return out;
    }
    return out;
}

CapitalInn FallbackInn(bool alliance)
{
    //Beside the innkeeper spawn in the world DB: the DB row is a spawn
    // point, not a floor the bot can stand on, so the fallback keeps to
    // the inn room with the same facing instead of the exact spawn.
    CapitalInn out;
    if (alliance)
    {
        out.mapId = 0;
        out.x = -8867.2f;
        out.y = 675.9f;
        out.z = 97.9f;
        out.o = 5.20f;
    }
    else
    {
        out.mapId = 1;
        out.x = -1672.6f;
        out.y = 2026.2f;
        out.z = 54.1f;
        out.o = 2.78f;
    }
    out.found = true;
    return out;
}

bool BotInCapital(Player* bot)
{
    if (!bot)
        return false;
    bool const alliance = bot->GetTeam() == ALLIANCE;
    return IsInCapital(bot->GetZoneId(), alliance);
}

bool BotBusyForMeet(Player* bot)
{
    if (!bot || !bot->IsInWorld())
        return true;
    if (InInstance(bot))
        return true;
    return InGroupWithRealPlayer(bot);
}

TeleportWait WaitReason(Player* bot)
{
    if (!bot || !bot->GetSession() || !bot->GetSession()->IsHeadless() || !bot->IsInWorld())
        return TeleportWait::Dead;
    return CanTeleportNow(bot->IsInCombat(), InInstance(bot),
        InGroupWithRealPlayer(bot), bot->IsAlive(), bot->IsTaxiFlying());
}

bool TeleportToCapitalInn(Player* bot)
{
    if (!bot)
        return false;
    if (WaitReason(bot) != TeleportWait::Ok)
        return false;

    bool const alliance = bot->GetTeam() == ALLIANCE;
    // The spawn table scan runs once per capital, not per teleport.
    static CapitalInn const allianceInn = FindCapitalInn(true);
    static CapitalInn const hordeInn = FindCapitalInn(false);
    CapitalInn inn = alliance ? allianceInn : hordeInn;
    if (!inn.found)
        inn = FallbackInn(alliance);

    // Reuse the rescue path: dismount, stop movement, clear the travel
    // target so the travel layer does not drag the bot back mid-trip, then
    // a plain TeleportTo that keeps the hearthstone cooldown and homebind.
    if (::PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(bot))
    {
        ai->Unmount();
        ai->StopMoving();
        if (ai->GetAiObjectContext())
        {
            ai::TravelTarget* target =
                ai->GetAiObjectContext()->GetValue<ai::TravelTarget*>("travel target")->Get();
            if (target)
                MaNGOS::Singleton<ai::TravelMgr>::Instance().SetNullTravelTarget(target);
        }
    }
    else
    {
        bot->StopMoving();
    }
    if (bot->GetTransport())
    {
        bot->GetTransport()->RemovePassenger(bot);
        bot->m_movementInfo.RemoveMovementFlag(MOVEFLAG_ONTRANSPORT);
    }
    bool const moved = bot->TeleportTo(inn.mapId, inn.x, inn.y, inn.z, inn.o);
    if (!moved)
        return false;
    bot->SaveToDB();
    TB_LOG_BASIC("TortoiseBots: capital teleport bot %s to %s inn (map %u %.1f %.1f %.1f)",
        bot->GetName(), alliance ? "Stormwind" : "Orgrimmar",
        inn.mapId, inn.x, inn.y, inn.z);
    return true;
}

} // namespace EnchantCapital
} // namespace TortoiseBots
