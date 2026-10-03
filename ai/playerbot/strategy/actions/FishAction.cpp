
#include "playerbot/playerbot.h"
#include "Timer.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/GuidPosition.h"
#include <ctime>
#include <cmath>
#include <mutex>
#include <unordered_map>
#include "FishAction.h"
#include "playerbot/TravelMgr.h"
#include "playerbot/FishingSpotPolicy.h"
#include "TellLosAction.h"
#include "EquipAction.h"

using namespace ai;

bool MoveToFishAction::isUseful()
{
    if (qualifier == "travel")
    {
        // The travel-table spot is primary; the open-water path below only
        // runs when the travel target is idle (levelling first - never while
        // an errand is being walked or worked).
        if (AI_VALUE(bool, "travel target active"))
            return false;
    }
    else if (AI_VALUE2(bool, "manual bool", "no fish water"))
    {
        // A fresh window has passed - let the search try again.
        if (WorldTimer::getMSTime() - uint32(AI_VALUE2(int, "manual int", "no fish water at")) < ai::FISH_NO_WATER_RETRY_MS)
            return false;
        RESET_AI_VALUE2(bool, "manual bool", "no fish water");
    }

    return true;
}

namespace
{
    // Levelling first: fishing never starts while the bot has real work -
    // an active travel errand, a friend in need... in code: a travel target
    // being walked or worked, a rewardable finished quest, vendor stock worth
    // a trip, a trainer visit pending, no money for ammo, or repair need.
    bool FishLevellingStandsDown(PlayerbotAI* ai)
    {
        AiObjectContext* context = ai->GetAiObjectContext();
        if (AI_VALUE(bool, "travel target active") || AI_VALUE(bool, "travel target working"))
            return true;
        if (AI_VALUE(bool, "has rewardable finished quest"))
            return true;
        if (AI_VALUE(bool, "should sell") || AI_VALUE(bool, "can sell"))
            return true;
        if (AI_VALUE2(bool, "should travel named", "trainer class"))
            return true;
        if (AI_VALUE(bool, "should get money"))
            return true;
        if (AI_VALUE(bool, "should repair"))
            return true;
        if (AI_VALUE(uint8, "bag space") > 90)
            return true;
        return false;
    }
}

void ai::EndFishingSession(PlayerbotAI* ai, char const* why)
{
    if (!ai)
        return;
    AiObjectContext* context = ai->GetAiObjectContext();
    uint32 const now = WorldTimer::getMSTime();
    int const casts = AI_VALUE2(int, "manual int", "fish session casts");
    if (AI_VALUE2(int, "manual int", "fish session start") == 0 && casts == 0)
        return;
    SET_AI_VALUE2(int, "manual int", "fish session end", int(now));
    RESET_AI_VALUE2(int, "manual int", "fish session start");
    RESET_AI_VALUE2(int, "manual int", "fish session casts");
    RESET_AI_VALUE2(WorldPosition, "custom position", "fish spot");
    if (why && ai->GetBot())
        ai->TellDebug(ai->GetMaster(), std::string("Fishing session ended (") + why + ")", "debug move");
}

namespace
{
    void FishStartSession(PlayerbotAI* ai)
    {
        AiObjectContext* context = ai->GetAiObjectContext();
        SET_AI_VALUE2(int, "manual int", "fish session start", int(WorldTimer::getMSTime()));
    }
}

bool ai::IsFishingSpotGuarded(Player* bot, WorldPosition const& spot, float radius)
{
    if (!bot)
        return false;
    uint32 const botLevel = bot->GetLevel();
    for (CreatureDataPair const* pair : spot.getCreaturesNear(radius))
    {
        GuidPosition guard(pair);
        CreatureInfo const* info = guard.GetCreatureTemplate();
        if (!info || info->level_max + 2 < botLevel)
            continue; // grey to the bot: no danger
        if (!guard.IsHostileTo(bot))
            continue;
        return true;
    }
    return false;
}

WorldPosition* ai::GetSafeFishSpot(Player* bot, bool onlyNearestGrid)
{
    WorldPosition* spot = nullptr;
    for (int attempt = 0; attempt < 8; ++attempt)
    {
        spot = sTravelMgr.GetFishSpot(WorldPosition(bot), onlyNearestGrid);
        if (!spot || !IsFishingSpotGuarded(bot, *spot))
            break;
    }
    return spot;
}

// Open-water search (issue #402). Ported from mod-playerbots
// `src/Ai/Base/Actions/FishingAction.cpp` (`FindWaterRadial`,
// `FindFishingHole`, `HasFishableWaterOrLand`), adapted to the Tortoise core:
// liquid comes from `TerrainInfo::GetWaterLevel` / `getLiquidStatus` and line
// of sight from `Map::isInLineOfSight`. Donor SHA b6696bd.
namespace
{
    struct FishWaterCacheEntry
    {
        WorldPosition water;
        uint32 cachedAtMs = 0;
        bool hasWater = false;
    };

    // Shared per-map-cell water verdicts (30 min): the radial search is ~88
    // probes with terrain/LOS queries each, so a cell a bot already searched
    // never pays it again - whichever bot comes next reads the verdict.
    std::unordered_map<std::uint64_t, FishWaterCacheEntry>& FishWaterCache()
    {
        static std::unordered_map<std::uint64_t, FishWaterCacheEntry> cache;
        return cache;
    }
    std::mutex& FishWaterCacheMutex()
    {
        static std::mutex mutex;
        return mutex;
    }

    std::uint64_t FishCellOf(WorldPosition const& pos)
    {
        CellPair const cell = pos.getCellPair();
        return ai::FishingCellKey(pos.GetMapId(), (std::int32_t)cell.x_coord, (std::int32_t)cell.y_coord);
    }

    // Fishable water (not magma/slime) deep enough to cast into at (x, y),
    // else INVALID_HEIGHT. `z` is the query height (bot level); the water
    // surface is only trusted when it sits above the lakebed under it.
    float FishableWaterLevelAt(Player* bot, float x, float y, float z)
    {
        if (!bot || !bot->GetMap())
            return INVALID_HEIGHT;
        TerrainInfo const* terrain = bot->GetMap()->GetTerrain();
        if (!terrain)
            return INVALID_HEIGHT;
        float ground = 0.0f;
        float const waterLevel = terrain->GetWaterLevel(x, y, z, &ground);
        if (waterLevel <= INVALID_HEIGHT)
            return INVALID_HEIGHT;
        if (ground <= INVALID_HEIGHT)
            return INVALID_HEIGHT;
        if (!ai::FishingWaterDeepEnough(waterLevel, ground))
            return INVALID_HEIGHT;
        GridMapLiquidData liquid{};
        if (terrain->getLiquidStatus(x, y, ground, MAP_ALL_LIQUIDS, &liquid) == LIQUID_MAP_NO_WATER)
            return INVALID_HEIGHT;
        // Tortoise liquid flags are 1 << LiquidType.Type (GridMap.cpp): water
        // and ocean are 0x08/0x02, magma 0x01 and slime 0x04 poison no cast.
        if (liquid.type_flags == MAP_LIQUID_TYPE_MAGMA || liquid.type_flags == MAP_LIQUID_TYPE_SLIME)
            return INVALID_HEIGHT;
        // Surface queries can echo the bot's own water tile when asked from
        // above it; the cast point must be real water under open sky.
        if (waterLevel > z + 0.5f)
            return INVALID_HEIGHT;
        return waterLevel;
    }
}

static WorldPosition FindNearbyWater(Player* bot, WorldPosition const& from, float searchRadius)
{
    if (!bot || !bot->GetMap() || !from)
        return WorldPosition();
    // Shared verdict first: another bot already searched this map cell.
    {
        std::lock_guard<std::mutex> lock(FishWaterCacheMutex());
        auto it = FishWaterCache().find(FishCellOf(from));
        if (it != FishWaterCache().end() && ai::FishingWaterCacheFresh(it->second.cachedAtMs, WorldTimer::getMSTime()))
            return it->second.hasWater ? it->second.water : WorldPosition();
    }
    // Fish where the bot stands: water in casting range with line of sight.
    // Rings grow outward and the first ring with water wins, so the nearest
    // shore is picked; the middle candidate fishes square to the bank.
    std::vector<WorldPosition> found;
    for (float dist = ai::FISH_MIN_CAST_DISTANCE; dist <= searchRadius + ai::FISH_MAX_CAST_DISTANCE; dist += ai::FISH_SEARCH_STEP)
    {
        found.clear();
        for (int i = 0; i < ai::FISH_SEARCH_DIRECTIONS; ++i)
        {
            float const angle = 2.0f * M_PI_F * float(i) / float(ai::FISH_SEARCH_DIRECTIONS);
            float const checkX = from.getX() + cos(angle) * dist;
            float const checkY = from.getY() + sin(angle) * dist;
            float const waterZ = FishableWaterLevelAt(bot, checkX, checkY, from.getZ());
            if (waterZ <= INVALID_HEIGHT)
                continue;
            float const botZ = bot->GetPositionZ() + bot->GetCollisionHeight();
            if (!bot->GetMap()->isInLineOfSight(bot->GetPositionX(), bot->GetPositionY(), botZ, checkX, checkY, waterZ))
                continue;
            found.emplace_back(WorldPosition(bot->GetMapId(), checkX, checkY, waterZ));
        }
        if (!found.empty())
            break;
    }
    if (found.empty())
    {
        std::lock_guard<std::mutex> lock(FishWaterCacheMutex());
        FishWaterCache()[FishCellOf(from)] = { WorldPosition(), WorldTimer::getMSTime(), false };
        return WorldPosition();
    }
    WorldPosition const pick = found.size() == 1 ? found[0] : found[found.size() / 2];
    {
        std::lock_guard<std::mutex> lock(FishWaterCacheMutex());
        FishWaterCache()[FishCellOf(from)] = { pick, WorldTimer::getMSTime(), true };
    }
    return pick;
}

WorldPosition ai::FindNearbyFishingHole(PlayerbotAI* ai, Player* bot, float searchRadius)
{
    if (!ai || !bot)
        return WorldPosition();
    GameObject* nearestHole = nullptr;
    float nearestDist = searchRadius;
    AiObjectContext* context = ai->GetAiObjectContext();
    for (ObjectGuid const& guid : AI_VALUE(std::list<ObjectGuid>, "nearest game objects no los"))
    {
        GameObject* go = ai->GetGameObject(guid);
        if (!go || go->GetGoType() != GAMEOBJECT_TYPE_FISHINGHOLE)
            continue;
        float const dist = bot->GetDistance2d(go->GetPositionX(), go->GetPositionY());
        if (dist > nearestDist)
            continue;
        nearestDist = dist;
        nearestHole = go;
    }
    if (!nearestHole)
        return WorldPosition();
    return WorldPosition(nearestHole->GetMapId(), nearestHole->GetPositionX(), nearestHole->GetPositionY(), nearestHole->GetPositionZ());
}

WorldPosition ai::GetShoreCastSpot(Player* bot, WorldPosition const& from, WorldPosition const& water)
{
    if (!bot || !bot->GetMap() || !from || !water)
        return WorldPosition();
    float const dx = water.getX() - from.getX();
    float const dy = water.getY() - from.getY();
    float const dist2d = sqrt(dx * dx + dy * dy);
    // The cast lands out from the bank, so the water must sit inside the
    // 10-20 yd window from where the bot stands.
    if (!ai::FishingCastInRange(dist2d))
        return WorldPosition();
    // Dry land underfoot: the bot fishes from the bank, never from the swim.
    TerrainInfo const* terrain = bot->GetMap()->GetTerrain();
    if (terrain && terrain->IsInWater(from.getX(), from.getY(), from.getZ()))
        return WorldPosition();
    float const ground = bot->GetMap()->GetHeight(from.getX(), from.getY(), from.getZ(), true);
    if (ground <= INVALID_HEIGHT)
        return WorldPosition();
    if ((water.getZ() - 0.5f) > from.getZ() && (water.getZ() - 0.5f) > ground)
        return WorldPosition();
    float const botZ = bot->GetPositionZ() + bot->GetCollisionHeight();
    if (!bot->GetMap()->isInLineOfSight(bot->GetPositionX(), bot->GetPositionY(), botZ, water.getX(), water.getY(), water.getZ()))
        return WorldPosition();
    WorldPosition spot(from);
    spot.setO(atan2(dy, dx));
    return spot;
}

WorldPosition ai::StepTowardFishableWater(Player* bot, WorldPosition const& from, WorldPosition const& water, float searchRadius)
{
    if (!bot || !bot->GetMap() || !from || !water)
        return WorldPosition();
    float const dx = water.getX() - from.getX();
    float const dy = water.getY() - from.getY();
    float const dist2d = sqrt(dx * dx + dy * dy);
    if (dist2d < 1.0f || dist2d > searchRadius + ai::FISH_MAX_CAST_DISTANCE)
        return WorldPosition();
    // Walk the bank: sample the bot-to-water ray from the closest point that
    // can still cast (10 yd off the water) back toward the bot, and take the
    // nearest dry stand that reaches. Bounded (fixed samples), no scan.
    float const stepFrom = std::max(0.0f, dist2d - ai::FISH_MAX_CAST_DISTANCE);
    for (float back = stepFrom; back <= dist2d - 1.0f; back += ai::FISH_SEARCH_STEP)
    {
        float const t = back / dist2d;
        float const sx = from.getX() + dx * t;
        float const sy = from.getY() + dy * t;
        float const standZ = bot->GetMap()->GetHeight(sx, sy, from.getZ(), true);
        if (standZ <= INVALID_HEIGHT)
            continue;
        WorldPosition const stand(bot->GetMapId(), sx, sy, standZ);
        WorldPosition cast = GetShoreCastSpot(bot, stand, water);
        if (cast)
            return cast;
    }
    return WorldPosition();
}


bool MoveToFishAction::Execute(Event& event)
{
    WorldPosition fishSpot;

    fishSpot = AI_VALUE2(WorldPosition, "custom position", "fish spot");

    if (!fishSpot && qualifier == "travel") //Get travel fish spot if available.
    {
        TravelTarget* target = AI_VALUE(TravelTarget*, "leader travel target");
        fishSpot = *target->getPosition();

        if (AI_VALUE(TravelTarget*, "travel target") != target) //Do not fish ontop of master.
            fishSpot = *GetSafeFishSpot(bot, true);
    }

    if (!fishSpot) //Get any fish spot.
    {
        fishSpot = *GetSafeFishSpot(bot);

        if (fishSpot)
        {
            TravelPath movePath = sTravelNodeMap.GetFullPath(bot, fishSpot, bot);

            if (movePath.empty())
                return false;

            AI_VALUE(LastMovement&, "last movement").setPath(movePath);
        }
    }

    // No travel fish spot (the FISH_LOCATION table is empty and generation is
    // off): a masterless pool bot fishing the travel errand falls back to
    // nearby open water. Owned/hired bots keep player control and never take
    // this path. Levelling first: the fallback needs an idle bot (no travel
    // errand at all), an unspent hourly session (max 5 casts / 5 min), and a
    // fresh throttle window - then at most a short step to the shore, never a
    // destination pick that could bypass the level gates.
    uint32 const nowFish = WorldTimer::getMSTime();
    bool noFishWater = AI_VALUE2(bool, "manual bool", "no fish water");
    if (noFishWater && nowFish - uint32(AI_VALUE2(int, "manual int", "no fish water at")) >= ai::FISH_NO_WATER_RETRY_MS)
    {
        RESET_AI_VALUE2(bool, "manual bool", "no fish water");
        noFishWater = false;
    }
    int fishCasts = AI_VALUE2(int, "manual int", "fish session casts");
    int fishStart = AI_VALUE2(int, "manual int", "fish session start");
    int fishEnd = AI_VALUE2(int, "manual int", "fish session end");
    if (!fishSpot && ai::FishingSpotApplies(sRandomBotFacade.IsRandomBot(bot), ai->HasRealPlayerMaster()) && !noFishWater &&
        ai::FishingSearchAllowed(uint32(AI_VALUE2(int, "manual int", "fish search at")), nowFish) &&
        ai::FishingSessionAllowsSearch(uint32(fishStart), uint32(fishEnd), fishCasts, nowFish) && !FishLevellingStandsDown(ai))
    {
        SET_AI_VALUE2(int, "manual int", "fish search at", int(nowFish));
        WorldPosition const botPos(bot);
        float const radius = ai::FishingSearchRadius(ai->GetMaster() != nullptr);
        WorldPosition water = FindNearbyFishingHole(ai, bot, radius);
        bool const aimedAtHole = (bool)water;
        if (!water)
            water = FindNearbyWater(bot, botPos, radius);
        if (water)
        {
            fishSpot = GetShoreCastSpot(bot, botPos, water);
            // Water is near but not castable from here: walk the bank - the
            // nearest dry stand along the bot-to-water ray that can cast at
            // the hole/water (still inside the search window, no destination).
            if (!fishSpot)
                fishSpot = StepTowardFishableWater(bot, botPos, water, radius);
            // A hole out of casting range is still a place to stand next to:
            // fishing there casts at open water. Only the blind walk is
            // refused, never the hole itself.
            if (!fishSpot && aimedAtHole)
                fishSpot = water;
            if (fishSpot && IsFishingSpotGuarded(bot, fishSpot))
            {
                fishSpot = WorldPosition();
                water = WorldPosition();
            }
        }
        if (!fishSpot)
        {
            SET_AI_VALUE2(bool, "manual bool", "no fish water", true);
            SET_AI_VALUE2(int, "manual int", "no fish water at", int(nowFish));
        }
        else if (uint32(fishStart) == 0)
            FishStartSession(ai);
    }

    SET_AI_VALUE2(WorldPosition, "custom position", "fish spot", fishSpot);

    if (fishSpot.distance(bot) < 1.0f)
        return false;

    return MoveTo(fishSpot);
}

bool FishAction::isUseful()
{
    // Side-activity budget: combat or an expired session never casts. The
    // travel-target check below stays: the travel errand's own spot still
    // needs a working target; the open-water spot only exists while idle.
    if (sServerFacade.IsInCombat(bot))
    {
        EndFishingSession(ai, "combat");
        return false;
    }
    uint32 const nowUseful = WorldTimer::getMSTime();
    int const casts = AI_VALUE2(int, "manual int", "fish session casts");
    int const start = AI_VALUE2(int, "manual int", "fish session start");
    int const ended = AI_VALUE2(int, "manual int", "fish session end");
    bool const poolPath = ai::FishingSpotApplies(sRandomBotFacade.IsRandomBot(bot), ai->HasRealPlayerMaster());
    if (poolPath && uint32(start) != 0 && ai::FishingSessionExpired(uint32(start), casts, nowUseful))
    {
        EndFishingSession(ai, "budget spent");
        return false;
    }
    // An abandoned session (expired, never ended) closes here so its hourly
    // cooldown starts - otherwise the stale `start` would bypass the budget.
    if (poolPath && uint32(start) == 0 && uint32(ended) == 0 && casts > 0)
    {
        RESET_AI_VALUE2(int, "manual int", "fish session casts");
        return false;
    }
    if (qualifier == "travel")
    {
        if (!AI_VALUE(bool, "travel target working"))
            return false;

        TravelTarget* target = AI_VALUE(TravelTarget*, "leader travel target");

        if (target->GetDestination()->GetPurpose() != TravelDestinationPurpose::GatherFishing)
            return false;

        if (!bot->GetGroup() || ai->IsGroupLeader() || target->GetTimeLeft() < 0)
            target->CheckStatus();
    }

    WorldPosition fishSpot = AI_VALUE2(WorldPosition, "custom position", "fish spot");

    if (!fishSpot)
        return false;

    if (!AI_VALUE(bool, "can fish"))
        return false;

    if (fishSpot.distance(bot) > 1.0f)
        return false;

    return true;
}

bool FishAction::Execute(Event& event)
{
    // Combat stops the session at once; the weapon comes back via the
    // "attacked while fishing" wake-up plus the immediate re-equip below.
    if (sServerFacade.IsInCombat(bot))
    {
        EndFishingSession(ai, "combat");
        return false;
    }
    if (qualifier == "travel")
    {
        if (!AI_VALUE(bool, "travel target working"))
            return false;
    }

    if (bot->IsMoving())
    {
        ai->StopMoving();
        SetDuration(100);
        return true;
    }

    WorldPosition fishSpot = AI_VALUE2(WorldPosition, "custom position", "fish spot");

    // The open-water cast ("7731 <bot>") always lands in front of the bot, so
    // aim at the water: prefer a visible fishing hole in range, else the
    // nearest water in casting range. When neither is there the spot was a
    // bank step that cannot reach - drop it and let the search walk on. The
    // cast search reuses the warm cell cache, so this aim costs no new scan.
    uint32 const nowCast = WorldTimer::getMSTime();
    WorldPosition water = FindNearbyFishingHole(ai, bot, ai::FISH_MAX_CAST_DISTANCE);
    if (!water)
        water = FindNearbyWater(bot, WorldPosition(bot), ai::FISH_MAX_CAST_DISTANCE);
    if (water)
        fishSpot.setO(atan2(water.getY() - bot->getPositionY(), water.getX() - bot->getPositionX()));
    else
    {
        RESET_AI_VALUE2(WorldPosition, "custom position", "fish spot");
        SET_AI_VALUE2(bool, "manual bool", "no fish water", true);
        SET_AI_VALUE2(int, "manual int", "no fish water at", int(nowCast));
        return false;
    }

    if (abs(fishSpot.getO() - bot->getOrientation()) > 0.5)
    {
        bot->SetFacingTo(fishSpot.getO());
        SetDuration(100);
        return true;
    }

    ai->StopMoving();

    // The zone must be fishable for this bot (same rule as the travel fish
    // errand): no base skill means no fish here, otherwise skill covers the
    // requirement with the errand's -5 head start. Checked at cast time so a
    // bot that walks to water in a zone it cannot fish never casts.
    if (water)
    {
        WorldPosition const waterPos(water);
        uint32 zoneId = 0, areaId = 0;
        sTerrainMgr.GetZoneAndAreaIdByAreaFlag(zoneId, areaId, waterPos.GetAreaFlag(), waterPos.GetMapId());
        int32 baseSkill = sObjectMgr.GetFishingBaseSkillLevel(areaId ? areaId : zoneId);
        if (!baseSkill && areaId && areaId != zoneId)
            baseSkill = sObjectMgr.GetFishingBaseSkillLevel(zoneId);
        if (!ai::FishingZoneSkillOk(baseSkill, bot->GetSkillValue(SKILL_FISHING)))
        {
            RESET_AI_VALUE2(WorldPosition, "custom position", "fish spot");
            SET_AI_VALUE2(bool, "manual bool", "no fish water", true);
            SET_AI_VALUE2(int, "manual int", "no fish water at", int(nowCast));
            return false;
        }
    }

    // A hostile creature near the bot's level within 30 yd right now - a patrol, a respawn -
    // ends the fishing here: the spot is dropped and a fishing travel target expires, so the
    // next pick is somewhere else instead of the bot standing still until it is dead.
    for (ObjectGuid const& guid : AI_VALUE(std::list<ObjectGuid>, "possible targets no los"))
    {
        Unit* unit = ai->GetUnit(guid);
        if (!unit || !unit->IsCreature() || unit->GetLevel() + 2 < bot->GetLevel() || !sServerFacade.IsHostileTo(bot, unit))
            continue;
        if (bot->GetDistance(unit) > 30.0f)
            continue;
        RESET_AI_VALUE2(WorldPosition, "custom position", "fish spot");
        if (qualifier == "travel")
            if (TravelTarget* travelTarget = AI_VALUE(TravelTarget*, "travel target"))
                travelTarget->SetStatus(TravelStatus::TRAVEL_STATUS_EXPIRED);
        ai->TellDebug(GetMaster(), "No fishing here - " + std::string(unit->GetName()) + " is too close", "debug move");
        if (sPlayerbotAIConfig.hasLog("unreachable_targets.csv"))
        {
            time_t const nowFish = time(nullptr);
            char stampFish[32];
            strftime(stampFish, sizeof(stampFish), "%Y-%m-%d %H:%M:%S", localtime(&nowFish));
            std::ostringstream outFish;
            outFish << stampFish << "," << bot->GetName() << "," << bot->GetLevel() << ",FISH " << unit->GetName() << "," << unit->GetLevel() << ",guarded";
            sPlayerbotAIConfig.log("unreachable_targets.csv", outFish.str().c_str());
        }
        return false;
    }

    std::list<Item*> poles = AI_VALUE2(std::list<Item*>, "inventory items", "fishing pole");

    if (poles.empty())
        return false;

    // A pole already in hand stays in hand. The list holds the equipped pole and every spare
    // one, and its first entry was a spare more often than not: equipping it swapped the two
    // poles, the next tick swapped them back - 140,000 swaps in half an hour for four bots
    // that carried a second pole. Without a pole in hand the best one is taken.
    Item* mainHand = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_MAINHAND);
    bool const poleInHand = mainHand && mainHand->GetProto()->Class == ITEM_CLASS_WEAPON &&
        mainHand->GetProto()->SubClass == ITEM_SUBCLASS_WEAPON_FISHING_POLE;
    if (!poleInHand)
    {
        Item* pole = poles.front();
        for (Item* candidate : poles)
            if (candidate->GetProto()->ItemLevel > pole->GetProto()->ItemLevel)
                pole = candidate;
        EquipAction::EquipItem(ai, GetMaster(), pole);
    }

    Event fishCastEvent = Event("fish", "7731 " + chat->formatWorldobject(bot));
    bool didCast = CastCustomSpellAction::Execute(fishCastEvent);
    if (didCast)
    {
        SET_AI_VALUE2(int, "manual int", "last fish cast", int(nowCast));
        RESET_AI_VALUE2(bool, "manual bool", "no fish water");
        int const castsNow = AI_VALUE2(int, "manual int", "fish session casts") + 1;
        SET_AI_VALUE2(int, "manual int", "fish session casts", castsNow);
        // Budget spent (5 casts or 5 min): the session ends now and the pole
        // comes off at once - no waiting for the next tick's audit.
        uint32 const sessionStart = uint32(AI_VALUE2(int, "manual int", "fish session start"));
        if (ai::FishingSessionExpired(sessionStart, castsNow, WorldTimer::getMSTime()))
        {
            EndFishingSession(ai, "budget spent");
            if (ai->CanDoSpecificAction("equip upgrades"))
                ai->DoSpecificAction("equip upgrades", Event("fishing session end"), true);
        }
    }

    SetDuration(sPlayerbotAIConfig.globalCoolDown);

    return didCast;
}

bool UseFishingBobberAction::Execute(Event& event)
{
    std::list<GameObject*> objects = TellLosAction::GoGuidListToObjList(ai, AI_VALUE(std::list<ObjectGuid>, "nearest game objects no los"));

    for (auto& obj : objects)
    {
        if (obj->GetEntry() != 35591)
            continue;

        if (obj->GetOwnerGuid() != bot->getObjectGuid())
            continue;

        if (obj->getLootState() != GO_READY)
        {
            time_t bobberActiveTime = obj->GetRespawnTime() - FISHING_BOBBER_READY_TIME;
            if (bobberActiveTime > time(0))
                SetDuration((bobberActiveTime - time(0)) * IN_MILLISECONDS + 500);
            else
                SetDuration(1000);
            return true;
        }

        std::unique_ptr<WorldPacket> packet(new WorldPacket(CMSG_GAMEOBJ_USE));
        *packet << obj->getObjectGuid();
        bot->GetSession()->QueuePacket(packet.release());

        std::ostringstream out; out << "Opening " << chat->formatGameobject(obj);
        ai->TellPlayerNoFacing(ai->GetMaster(), out.str(), PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);

        SetDuration(3000);

        if (!urand(0,10))
        {
            RESET_AI_VALUE2(WorldPosition, "custom position", "fish spot");
        }

        return true;
    }

    return false;
}