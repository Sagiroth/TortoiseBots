// pi-lens-ignore-file: all
#include "BotCommandHandlers.h"
#include "CommandShared.h"

// pi-lens-ignore: clang:pp_file_not_found
#include "../ai/playerbot/CampMgr.h"
#include "../ai/playerbot/policy/rpg/CampCommandPolicy.h"

// pi-lens-ignore: clang:pp_file_not_found
#include "Chat.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "Creature.h"
#include "Database/DatabaseEnv.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "Maps/Map.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "Maps/PathFinder.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "Player.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "WorldSession.h"

#include <cctype>
#include <cmath>
#include <memory>
#include <string>

namespace TortoiseBots {
namespace BotCommands {

namespace
{

bool RequireCampGm(ChatHandler* handler, Player*& gm)
{
    gm = nullptr;
    WorldSession* session = handler ? handler->GetSession() : nullptr;
    if (!session || !session->GetPlayer())
    {
        if (handler)
            handler->PSendSysMessage("You must be in-game as a GameMaster to use .bot camp.");
        return false;
    }
    if (session->GetSecurity() < SEC_GAMEMASTER)
    {
        handler->PSendSysMessage("You do not have permission to use .bot camp.");
        return false;
    }
    gm = session->GetPlayer();
    if (!gm->IsInWorld() || !gm->GetMap())
    {
        handler->PSendSysMessage("You must be in the world to use .bot camp.");
        return false;
    }
    return true;
}

float SnapCampGroundZ(Map* map, float x, float y, float z)
{
    if (!map)
        return INVALID_HEIGHT;
    return map->GetHeight(x, y, z + 5.0f);
}

bool IsCampPointReachable(Player* gm, float fromX, float fromY, float fromZ, float toX, float toY, float toZ)
{
    if (toZ <= INVALID_HEIGHT || fromZ <= INVALID_HEIGHT)
        return false;
    PathFinder path(gm);
    path.calculate(Vector3(fromX, fromY, fromZ), Vector3(toX, toY, toZ), false);
    uint32 const pt = static_cast<uint32>(path.getPathType());
    return (pt & PATHFIND_NORMAL) != 0 &&
           (pt & (PATHFIND_NOPATH | PATHFIND_INCOMPLETE | PATHFIND_NOT_USING_PATH)) == 0;
}

CampAddPointCheck BuildPointCheck(Map* map, TerrainInfo const* terrain, float x, float y, float z, bool reachable)
{
    CampAddPointCheck check;
    if (z <= INVALID_HEIGHT)
        return check;
    check.z = z;
    check.outdoors = terrain && terrain->IsOutdoors(x, y, z);
    check.inLiquid = !terrain || terrain->IsInWater(x, y, z) || terrain->IsUnderWater(x, y, z);
    check.onNavmeshAndReachable = reachable;
    (void)map;
    return check;
}

void SendCampUsage(ChatHandler* handler)
{
    handler->PSendSysMessage("Usage: .bot camp <add [name]|show|remove>");
    handler->PSendSysMessage("  .bot camp add [name]  - validate and save a camp spot at your position");
    handler->PSendSysMessage("  .bot camp show        - spawn 3-minute visuals for the nearest spot within 60 yd");
    handler->PSendSysMessage("  .bot camp remove      - remove the nearest spot within 10 yd");
}

bool HandleCampAdd(ChatHandler* handler, Player* gm, std::string const& rawName)
{
    Map* map = gm->GetMap();
    TerrainInfo const* terrain = map->GetTerrain();
    uint32 const mapId = gm->GetMapId();
    uint32 const zoneId = gm->GetZoneId();
    float const x = gm->GetPositionX();
    float const y = gm->GetPositionY();
    float const z = gm->GetPositionZ();
    float const o = gm->GetOrientation();

    std::string spotName = Trim(rawName);
    if (spotName.empty())
    {
        std::string zoneName;
        if (AreaEntry const* area = AreaEntry::GetById(zoneId))
        {
            if (area->Name && *area->Name)
                zoneName = area->Name;
        }
        spotName = FormatDefaultCampSpotName(zoneName);
    }
    else if (spotName.size() > CAMP_SPOT_NAME_MAX_LEN)
    {
        spotName.resize(CAMP_SPOT_NAME_MAX_LEN);
    }

    CampAddValidationInput input;
    std::optional<std::pair<CampSpot, float>> const nearby =
        sCampMgr.FindNearestSpotWithDist(mapId, x, y, z, CAMP_TENT_EXCLUSION_RANGE_YD);
    if (nearby)
    {
        input.nearbySpotExists = true;
        input.nearestSpotDistYd = nearby->second;
    }

    CampPoint const castPt = ComputeTentCastPoint(x, y, z, o);
    CampPoint const firePt = ComputeFireCastPoint(x, y, z, o);
    CampPoint const seatPt = ComputeOwnerSeatPoint(x, y, z, o);

    float const tentZ = SnapCampGroundZ(map, x, y, z);
    float const castZ = SnapCampGroundZ(map, castPt.x, castPt.y, castPt.z);
    float const fireZ = SnapCampGroundZ(map, firePt.x, firePt.y, firePt.z);
    float const seatZ = SnapCampGroundZ(map, seatPt.x, seatPt.y, seatPt.z);

    bool const castOk = IsCampPointReachable(gm, x, y, tentZ, castPt.x, castPt.y, castZ);
    bool const tentOk = IsCampPointReachable(gm, castPt.x, castPt.y, castZ, x, y, tentZ);
    bool const fireOk = IsCampPointReachable(gm, x, y, tentZ, firePt.x, firePt.y, fireZ);
    bool const seatOk = IsCampPointReachable(gm, x, y, tentZ, seatPt.x, seatPt.y, seatZ);

    input.tent = BuildPointCheck(map, terrain, x, y, tentZ, tentOk);
    input.cast = BuildPointCheck(map, terrain, castPt.x, castPt.y, castZ, castOk);
    input.fire = BuildPointCheck(map, terrain, firePt.x, firePt.y, fireZ, fireOk);
    input.seat = BuildPointCheck(map, terrain, seatPt.x, seatPt.y, seatZ, seatOk);

    CampAddValidationResult const val = EvaluateCampAddSpot(input);
    switch (val.failure)
    {
        case CampAddCheckFailure::None:
            break;
        case CampAddCheckFailure::TooCloseToExistingSpot:
            handler->PSendSysMessage(
                "Cannot add camp spot: too close to existing spot #%u '%s' (%.1f yd; minimum %.0f yd).",
                nearby->first.id, nearby->first.name.c_str(), nearby->second, CAMP_TENT_EXCLUSION_RANGE_YD);
            return true;
        case CampAddCheckFailure::NotOutdoors:
            handler->PSendSysMessage("Cannot add camp spot: %s point is not outdoors.", val.failedPoint);
            return true;
        case CampAddCheckFailure::InLiquid:
            handler->PSendSysMessage("Cannot add camp spot: %s point is in water/liquid.", val.failedPoint);
            return true;
        case CampAddCheckFailure::NotOnNavmeshOrUnreachable:
            handler->PSendSysMessage("Cannot add camp spot: %s point is not on the navmesh or is unreachable.", val.failedPoint);
            return true;
        case CampAddCheckFailure::TerrainTooSteep:
            handler->PSendSysMessage(
                "Cannot add camp spot: terrain is too steep (4-point Z spread %.2f yd; max %.0f yd).",
                val.spreadYd, CAMP_MAX_SLOPE_Z_DIFF_YD);
            return true;
    }

    std::string safeName = spotName;
    WorldDatabase.escape_string(safeName);
    if (!WorldDatabase.DirectPExecute(
            "INSERT INTO `ai_playerbot_camp_spot` "
            "(`map`, `x`, `y`, `z`, `orientation`, `zone_id`, `name`, `note`, `source`) "
            "VALUES (%u, %.6f, %.6f, %.6f, %.6f, %u, '%s', 'Added by GM', %u)",
            mapId, x, y, tentZ, o, zoneId, safeName.c_str(), static_cast<unsigned>(CAMP_SPOT_SOURCE_GM)))
    {
        handler->PSendSysMessage("Failed to save camp spot to the world database.");
        return true;
    }

    uint32 newId = 0;
    std::unique_ptr<QueryResult> res(WorldDatabase.PQuery(
        "SELECT `id` FROM `ai_playerbot_camp_spot` "
        "WHERE `map` = %u AND `zone_id` = %u AND `source` = %u AND `name` = '%s' "
        "ORDER BY `id` DESC LIMIT 1",
        mapId, zoneId, static_cast<unsigned>(CAMP_SPOT_SOURCE_GM), safeName.c_str()));
    if (res)
        newId = res->Fetch()[0].GetUInt32();

    CampSpot spot;
    spot.id = newId;
    spot.map = mapId;
    spot.x = x;
    spot.y = y;
    spot.z = tentZ;
    spot.orientation = o;
    spot.zoneId = zoneId;
    spot.name = spotName;
    spot.note = "Added by GM";
    spot.source = CAMP_SPOT_SOURCE_GM;
    sCampMgr.AddSpot(spot);

    handler->PSendSysMessage("Added camp spot #%u '%s' (4-point Z spread %.2f yd).",
        spot.id, spot.name.c_str(), val.spreadYd);
    return true;
}

bool HandleCampShow(ChatHandler* handler, Player* gm)
{
    std::optional<std::pair<CampSpot, float>> const found = sCampMgr.FindNearestSpotWithDist(
        gm->GetMapId(), gm->GetPositionX(), gm->GetPositionY(), gm->GetPositionZ(), CAMP_SHOW_RANGE_YD);
    if (!found)
    {
        handler->PSendSysMessage("No camp spot found within %.0f yd.", CAMP_SHOW_RANGE_YD);
        return true;
    }

    CampSpot const& spot = found->first;
    float const distYd = found->second;
    Map* map = gm->GetMap();

    CampPoint const castPt = ComputeTentCastPoint(spot.x, spot.y, spot.z, spot.orientation);
    CampPoint const firePt = ComputeFireCastPoint(spot.x, spot.y, spot.z, spot.orientation);
    CampPoint const seatPt = ComputeOwnerSeatPoint(spot.x, spot.y, spot.z, spot.orientation);

    float tentZ = SnapCampGroundZ(map, spot.x, spot.y, spot.z);
    if (tentZ <= INVALID_HEIGHT) tentZ = spot.z;
    float castZ = SnapCampGroundZ(map, castPt.x, castPt.y, castPt.z);
    if (castZ <= INVALID_HEIGHT) castZ = castPt.z;
    float fireZ = SnapCampGroundZ(map, firePt.x, firePt.y, firePt.z);
    if (fireZ <= INVALID_HEIGHT) fireZ = firePt.z;
    float seatZ = SnapCampGroundZ(map, seatPt.x, seatPt.y, seatPt.z);
    if (seatZ <= INVALID_HEIGHT) seatZ = seatPt.z;

    float const spread = ComputeCamp4PointSpread(tentZ, castZ, fireZ, seatZ);

    uint32 const tentEntry = (gm->GetTeam() == HORDE) ? CAMP_TENT_GO_HORDE : CAMP_TENT_GO_ALLIANCE;
    gm->SummonGameObject(
        tentEntry,
        spot.x, spot.y, tentZ, spot.orientation,
        0.0f, 0.0f, std::sin(spot.orientation / 2.0f), std::cos(spot.orientation / 2.0f),
        CAMP_SHOW_DURATION_SEC, false);
    gm->SummonGameObject(
        CAMP_FIRE_GO_ENTRY,
        firePt.x, firePt.y, fireZ, firePt.o,
        0.0f, 0.0f, std::sin(firePt.o / 2.0f), std::cos(firePt.o / 2.0f),
        CAMP_SHOW_DURATION_SEC, false);
    gm->SummonCreature(
        CAMP_SEAT_MARKER_CREATURE_ENTRY,
        seatPt.x, seatPt.y, seatZ, seatPt.o,
        TEMPSPAWN_TIMED_DESPAWN, CAMP_SHOW_DURATION_SEC * 1000);

    handler->PSendSysMessage(
        "Showing camp spot #%u '%s' (%.1f yd away, 4-point Z spread %.2f yd; visuals despawn in 3 min).",
        spot.id, spot.name.c_str(), distYd, spread);
    return true;
}

bool HandleCampRemove(ChatHandler* handler, Player* gm)
{
    std::optional<std::pair<CampSpot, float>> const found = sCampMgr.FindNearestSpotWithDist(
        gm->GetMapId(), gm->GetPositionX(), gm->GetPositionY(), gm->GetPositionZ(), CAMP_REMOVE_RANGE_YD);
    if (!found)
    {
        handler->PSendSysMessage("No camp spot found within %.0f yd.", CAMP_REMOVE_RANGE_YD);
        return true;
    }

    CampSpot const spot = found->first;
    float const distYd = found->second;

    if (!WorldDatabase.DirectPExecute("DELETE FROM `ai_playerbot_camp_spot` WHERE `id` = %u", spot.id))
    {
        handler->PSendSysMessage("Failed to remove camp spot #%u '%s' from the world database.",
            spot.id, spot.name.c_str());
        return true;
    }

    sCampMgr.RemoveSpotById(spot.id);

    if (spot.source == CAMP_SPOT_SOURCE_SHIPPED)
    {
        handler->PSendSysMessage(
            "Removed camp spot #%u '%s' (%.1f yd away, source 0). "
            "Note: a removed source-0 spot comes back on the next boot unless also deleted from data/sql/world/20261115180000_world.sql.",
            spot.id, spot.name.c_str(), distYd);
    }
    else
    {
        handler->PSendSysMessage(
            "Removed camp spot #%u '%s' (%.1f yd away, source %u).",
            spot.id, spot.name.c_str(), distYd, static_cast<unsigned>(spot.source));
    }
    return true;
}

}  // namespace

bool HandleCamp(ChatHandler* handler, char const* args)
{
    Player* gm = nullptr;
    if (!RequireCampGm(handler, gm))
        return true;

    std::string const trimmed = Trim(args ? args : "");
    if (trimmed.empty())
    {
        SendCampUsage(handler);
        return true;
    }

    size_t const space = trimmed.find_first_of(" \t");
    std::string sub = (space == std::string::npos) ? trimmed : trimmed.substr(0, space);
    for (char& ch : sub)
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    std::string const rest = (space == std::string::npos) ? std::string() : trimmed.substr(space + 1);

    if (sub == "add")
        return HandleCampAdd(handler, gm, rest);
    if (sub == "show")
        return HandleCampShow(handler, gm);
    if (sub == "remove")
        return HandleCampRemove(handler, gm);

    SendCampUsage(handler);
    return true;
}

}  // namespace BotCommands
}  // namespace TortoiseBots
