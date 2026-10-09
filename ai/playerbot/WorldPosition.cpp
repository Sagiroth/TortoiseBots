#include "WorldPosition.h"
#include "GuidPosition.h"
#include "playerbot/PlayerbotAI.h"
#include "playerbot/TravelMgr.h"
#include "TravelNode.h"

#include "Maps/Map.h"

#include "World.h"
#include "Maps/CellImpl.h"
#include "ObjectAccessor.h"
#include "Transports/Transport.h"

#include "Maps/MoveMap.h"

#include "vmap/VMapFactory.h"

#include <numeric>
#include <iomanip>
#include <cstring>

using namespace ai;
using namespace MaNGOS;

WorldPosition::WorldPosition(const uint32 mapId, const GuidPosition& guidP, uint32 instanceId)
{
    if (guidP.mapId !=0 || guidP.x != 0 || guidP.y != 0 || guidP.z !=0) {
        set(WorldPosition(guidP.mapId, guidP.x, guidP.y, guidP.z, guidP.o));
        return;
    }

    set(ObjectGuid(guidP), guidP.mapId, instanceId);
 }

void WorldPosition::set(const ObjectGuid& guid, const uint32 mapId, const uint32 instanceId)
{
    switch (guid.GetHigh())
    {
    case HIGHGUID_PLAYER:
    {
        Player* player = sObjectAccessor.FindPlayer(guid);
        if (player)
        {
            set(player);
            return;
        }
        break;
    }
    case HIGHGUID_GAMEOBJECT:
    {
        GameObjectDataPair const* gpair = sObjectMgr.GetGODataPair(guid.GetCounter());
        if (gpair)
        {
            set(gpair);
            return;
        }
        break;
    }
    case HIGHGUID_UNIT:
    {
        setMapId(mapId);
        setX(1); //Pretend to know so map can be loaded.
        if (Map* map = getMap(instanceId))
        {
            Creature* creature = map->GetAnyTypeCreature(guid);
            if (creature)
            {
                set(creature);
                return;
            }
        }

        CreatureDataPair const* cpair = sObjectMgr.GetCreatureDataPair(guid.GetCounter());
        if (cpair)
        {
            set(cpair);
            return;
        }
        break;
    }
    case HIGHGUID_TRANSPORT:
    case HIGHGUID_MO_TRANSPORT:
    case HIGHGUID_ITEM:
    case HIGHGUID_PET:
    case HIGHGUID_DYNAMICOBJECT:
    case HIGHGUID_CORPSE:
        set(WorldPosition());
        return;
    }

    set(WorldPosition());
}

WorldPosition::WorldPosition(const std::vector<WorldPosition*>& list, const WorldPositionConst conType)
{
    uint32 size = list.size();
    if (size == 0)
        return;
    else if (size == 1)
        set(*list.front());
    else if (conType == WP_RANDOM)
        set(*list[urand(0, size - 1)]);
    else if (conType == WP_CENTROID)
        set(std::accumulate(list.begin(), list.end(), WorldLocation(list[0]->GetMapId(), 0, 0, 0, 0), [size](WorldLocation i, WorldPosition* j) {i.x += j->getX() / size; i.y += j->getY() / size; i.z += j->getZ() / size; i.o += j->getO() / size; return i; }));
    else if (conType == WP_MEAN_CENTROID)
    {
        WorldPosition pos = WorldPosition(list, WP_CENTROID);
        set(*pos.closestSq(list));
    }
}

WorldPosition::WorldPosition(const std::vector<WorldPosition>& list, const WorldPositionConst conType)
{
    uint32 size = list.size();
    if (size == 0)
        return;
    else if (size == 1)
        set(list.front());
    else if (conType == WP_RANDOM)
        set(list[urand(0, size - 1)]);
    else if (conType == WP_CENTROID)
        set(std::accumulate(list.begin(), list.end(), WorldLocation(list[0].GetMapId(), 0, 0, 0, 0), [size](WorldLocation i, WorldPosition j) {i.x += j.getX() / size; i.y += j.getY() / size; i.z += j.getZ() / size; i.o += j.getO() / size; return i; }));
    else if (conType == WP_MEAN_CENTROID)
    {
        WorldPosition pos = WorldPosition(list, WP_CENTROID);
        set(pos.closestSq(list));
    }
}

float WorldPosition::distance(const WorldPosition& to) const
{
    if(mapId == to.GetMapId())
        return relPoint(to).size();

    //this -> mapTransfer | mapTransfer -> center
    return sTravelMgr.MapTransDistance(*this, to);
};

float WorldPosition::fDist(const WorldPosition& to) const
{
    if (mapId == to.GetMapId())
        return sqrt(sqDistance2d(to));

    //this -> mapTransfer | mapTransfer -> center
    return sTravelMgr.MapTransDistance(*this, to);
};

//When moving from this along list return last point that falls within range.
//Distance is move distance along path.
WorldPosition WorldPosition::lastInRange(const std::vector<WorldPosition>& list, const float minDist, const float maxDist) const
{
    WorldPosition rPoint;

    float startDist = 0.0f;

    //Enter the path at the closest point.
    for (auto& p : list)
    {
        float curDist = distance(p);
        if (startDist < curDist || p == list.front())
            startDist = curDist + 0.1f;
    }

    float totalDist = 0.0f;

    //Follow the path from the last nearest point
    //Return last point in range.
    for (auto& p : list)
    {
        float curDist = distance(p);

        if (totalDist > 0) //We have started the path. Keep counting.
            totalDist += p.distance(*std::prev(&p, 1));

        if (curDist == startDist) //Start the path here.
            totalDist = startDist;

        if (minDist > 0 && totalDist < minDist)
            continue;

        if (maxDist > 0 && totalDist > maxDist)
            continue; //We do not break here because the path may loop back and have a second startDist point.

        rPoint = p;
    }

    return rPoint;
};

//Todo: remove or adjust to above standard.
WorldPosition WorldPosition::firstOutRange(const std::vector<WorldPosition>& list, const float minDist, const float maxDist) const
{
    WorldPosition rPoint;

    for (auto& p : list)
    {
        if (minDist > 0 && distance(p) < minDist)
            return p;

        if (maxDist > 0 && distance(p) > maxDist)
            return p;

        rPoint = p;
    }

    return rPoint;
}

//Returns true if (on the x-y plane) the position is inside the three points.
bool WorldPosition::isInside(const WorldPosition* p1, const WorldPosition* p2, const WorldPosition* p3) const
{
    if (getMapId() != p1->GetMapId() != p2->GetMapId() != p3->GetMapId())
        return false;

    float d1, d2, d3;
    bool has_neg, has_pos;

    d1 = mSign(p1, p2);
    d2 = mSign(p2, p3);
    d3 = mSign(p3, p1);

    has_neg = (d1 < 0) || (d2 < 0) || (d3 < 0);
    has_pos = (d1 > 0) || (d2 > 0) || (d3 > 0);

    return !(has_neg && has_pos);
}

void WorldPosition::distancePartition(const std::vector<float>& distanceLimits, WorldPosition* to, std::vector<std::vector<WorldPosition*>>& partitions) const
{
    float dist = distance(*to);

    for (uint8 l = 0; l < distanceLimits.size(); l++)
        if (dist <= distanceLimits[l])
            partitions[l].push_back(to);
}

std::vector<std::vector<WorldPosition*>> WorldPosition::distancePartition(const std::vector<float>& distanceLimits, std::vector<WorldPosition*> points) const
{
    std::vector<std::vector<WorldPosition*>> partitions;

    for (auto lim : distanceLimits)
        partitions.push_back({});

    for (auto& point : points)
    {
        distancePartition(distanceLimits, point, partitions);
    }

    return partitions;
}

std::vector<WorldPosition*> WorldPosition::GetNextPoint(std::vector<WorldPosition*> points, uint32 amount) const {
    std::vector<WorldPosition*> retVec;

    if (points.size() < 2)
    {
        retVec.push_back(points[0]);
        return retVec;
    }

    retVec = points;

    std::vector<uint32> weights;

    std::transform(retVec.begin(), retVec.end(), std::back_inserter(weights), [this](WorldPosition* point) { return 200000 / (1 + this->distance(*point)); });

    //If any weight is 0 add 1 to all weights.
    for (auto& w : weights)
    {
        if (w > 0)
            continue;

        std::for_each(weights.begin(), weights.end(), [](uint32& d) { d += 1; });
        break;

    }

    std::mt19937 gen(time(0));

    WeightedShuffle(retVec.begin(), retVec.end(), weights.begin(), weights.end(), gen);

    return retVec;
}

std::vector<WorldPosition> WorldPosition::GetNextPoint(std::vector<WorldPosition> points, uint32 amount) const {
    std::vector<WorldPosition> retVec;

    if (points.size() < 2)
    {
        if (points.size() == 1)
            retVec.push_back(points[0]);
        return retVec;
    }

    retVec = points;


    std::vector<uint32> weights;

    //List of weights based on distance (Gausian curve that starts at 100 and lower to 1 at 1000 distance)
    //std::transform(retVec.begin(), retVec.end(), std::back_inserter(weights), [center](WorldPosition point) { return 1 + 1000 * exp(-1 * pow(point.distance(center) / 400.0, 2)); });

    //List of weights based on distance (Twice the distance = half the weight). Caps out at 200.0000 range.
    std::transform(retVec.begin(), retVec.end(), std::back_inserter(weights), [this](WorldPosition point) { return 200000 / (1 + this->distance(point)); });

    //If any weight is 0 add 1 to all weights.
    for (auto& w : weights)
    {
        if (w > 0)
            continue;

        std::for_each(weights.begin(), weights.end(), [](uint32& d) { d += 1; });
        break;

    }

    std::mt19937 gen(time(0));

    WeightedShuffle(retVec.begin(), retVec.end(), weights.begin(), weights.end(), gen);

    return retVec;
}

bool WorldPosition::IsInStaticLineOfSight(WorldPosition pos, float heightMod) const
{
    if (mapId != pos.mapId)
    {
        return false;
    }

    float srcX = x;
    float srcY = y;
    float srcZ = z + heightMod;
    float dstX = pos.x;
    float dstY = pos.y;
    float dstZ = pos.z + heightMod;

    return VMAP::VMapFactory::createOrGetVMapManager()->isInLineOfSight(mapId, srcX, srcY, srcZ, dstX, dstY, dstZ);
}

float WorldPosition::projectOnSegment(const WorldPosition& p1, const WorldPosition& p2) const
{
    if (p1.GetMapId() != p2.GetMapId() || p1.GetMapId() != getMapId())
        return 0.0f;

    float dx = p2.x - p1.x;
    float dy = p2.y - p1.y;
    float dz = p2.z - p1.z;

    float lenSq = dx * dx + dy * dy + dz * dz;
    if (lenSq == 0.0f)
        return 0.0f; // p1 and p2 are the same point

    return ((x - p1.x) * dx + (y - p1.y) * dy + (z - p1.z) * dz) / lenSq;
}

G3D::Vector3 WorldPosition::getVector3() const
{
    return G3D::Vector3(x, y, z);
}

std::string WorldPosition::print(uint8 precision, bool onlyXyz) const
{
    std::ostringstream out;

    if (!onlyXyz)
        out << mapId << ";";

    out << std::fixed << std::setprecision(precision);
    out << x;
    out << ';' << y;
    out << ';' << z;

    if (!onlyXyz)
        out << ';' << o;

    return out.str();
}

void WorldPosition::printWKT(const std::vector<WorldPosition>& points, std::ostringstream& out, const uint32 dim, const bool loop)
{
    switch (dim) {
    case 0:
        if(points.size() == 1)
            out << "\"POINT(";
        else
            out << "\"MULTIPOINT(";
        break;
    case 1:
        out << "\"LINESTRING(";
        break;
    case 2:
        out << "\"POLYGON((";
    }

    for (auto& p : points)
        out << p.getDisplayX() << " " << p.getDisplayY() << (!loop && &p == &points.back() ? "" : ",");

    if (loop)
        out << points.front().getDisplayX() << " " << points.front().getDisplayY();

    out << (dim == 2 ? "))\"," : ")\",");
}

WorldPosition WorldPosition::getDisplayLocation() const
{
    WorldPosition mapOffset = sTravelNodeMap.getMapOffset(getMapId());
    return offset(mapOffset);
};

AreaTableEntry const* WorldPosition::GetArea() const
{
    // getAreaFlag returns an area *flag*; GetById expects an area *id*. Handing
    // one to the other returns whatever area happens to carry that number as its
    // id - a position in the Barrens reported "Silverpine Forest". The warning
    // was already written into isEnemyHomeZoneFor below, which works around it,
    // but this function was left as it was and everything else went on using it.
    //
    // What it cost: TravelMgr::IsLocationLevelValid measures a travel point
    // against the level of the area it sits in, so quest turn-ins were being
    // judged by an unrelated zone's level and discarded.
    return AreaEntry::GetByAreaFlagAndMap(getAreaFlag(), getMapId());
}

bool WorldPosition::isEnemyHomeZoneFor(Team team) const
{
    // Deliberately not GetArea(): that passes an area *flag* to
    // AreaEntry::GetById(), which expects an area *id*, and returns unrelated
    // areas - a position in the Barrens reported "Silverpine Forest".
    AreaEntry const* area = AreaEntry::GetByAreaFlagAndMap(getAreaFlag(), getMapId());
    if (!area)
        return false;

    uint32 areaTeam = area->Team;
    if (areaTeam == AREATEAM_NONE && area->ZoneId)
        if (AreaEntry const* zone = AreaEntry::GetById(area->ZoneId))
            areaTeam = zone->Team;

    return (areaTeam == AREATEAM_ALLY  && team == HORDE)
        || (areaTeam == AREATEAM_HORDE && team == ALLIANCE);
}

std::once_flag WorldPosition::s_hostileTownOnceFlag;
std::unordered_map<WorldPosition::HostileTownCellKey, std::vector<WorldPosition::HostileTownGuard>, WorldPosition::HostileTownCellKeyHash> WorldPosition::s_hostileTownCells[2];
size_t WorldPosition::s_hostileTownGuards[2] = { 0, 0 };
std::atomic<bool> WorldPosition::s_hostileTownIndexBuilt{ false };
std::once_flag WorldPosition::s_pointDangerOnceFlag;
std::unordered_map<WorldPosition::PointDangerCellKey, std::vector<WorldPosition::PointDangerSpawn>, WorldPosition::PointDangerCellKeyHash> WorldPosition::s_pointDangerCells;

// One-pass build over the static spawn table (~88k rows): template + faction
// lookups only, no world objects, no DB, no map loads. Runs once via call_once
// on the first hostile-town query (creature data is loaded by then); bot AI
// runs on parallel map threads, so no double-checked locking — after the build
// the maps are immutable and queries never take a lock.
void WorldPosition::EnsureHostileTownIndex()
{
    std::call_once(s_hostileTownOnceFlag, []()
    {
        // Build into locals, then move in: readers only ever run after
        // call_once returns, so they see the full index or (before the first
        // build finishes) block inside call_once — never a half-built map.
        std::unordered_map<HostileTownCellKey, std::vector<HostileTownGuard>, HostileTownCellKeyHash> cells[2];
        size_t guards[2] = { 0, 0 };
        struct HostileTownBuildWorker
        {
            std::unordered_map<HostileTownCellKey, std::vector<HostileTownGuard>, HostileTownCellKeyHash>* cells;
            size_t* guards;
            bool operator()(CreatureDataPair const& dataPair)
            {
                uint32 entry = dataPair.second.creature_id[0];
                if (!entry)
                    return false;
                uint32 mapId = dataPair.second.position.mapId;
                // Open world only: battleground/dungeon sentries (e.g. AV map 30
                // bunkers) are objectives, not towns — indexing them would make
                // bots refuse to assault towers as "hostile towns".
                if (mapId != 0 && mapId != 1)
                    return false;
                CreatureInfo const* info = sObjectMgr.GetCreatureTemplate(entry);
                if (!info || !info->faction)
                    return false;
                // Civilians never guard: excludes vendors/quest NPCs whose name
                // happens to contain a guard word (e.g. Fanny Forgeguard 62463,
                // an Ironforge vendor). Note some "Guard"-labeled quest NPCs ARE
                // civilian-flagged (Guard Parker 464) — they don't aggro, out.
                if (info->civilian)
                    return false;
                // Guard identity: CREATURE_FLAG_EXTRA_GUARD bit (0x400,
                // Creature.h:62) OR a guard-title name match. The DB is
                // inconsistent: Splintertree 12903, Nijel's 8151, Guard Clarke
                // 934, all Braves/Grunts/Watchers/Elites/Cavalrymen carry 0x80000
                // (PVP) WITHOUT 0x400, yet kill bots. Titles cover EN + FR/DE/ES
                // client locales. Numeric literal: the cmangos shim only aliases
                // the INVISIBLE bit. strstr cost is build-time only.
                uint32 constexpr GUARD_EXTRA_FLAG = 0x00000400;
                bool isGuard = (info->flags_extra & GUARD_EXTRA_FLAG) != 0;
                if (!isGuard && info->name.c_str() != nullptr)
                {
                    const char* n = info->name.c_str();
                    isGuard = strstr(n, "Guard") != nullptr ||
                        strstr(n, "Sentinel") != nullptr || strstr(n, "Sentry") != nullptr ||
                        strstr(n, "Deathguard") != nullptr || strstr(n, "Brave") != nullptr ||
                        strstr(n, "Grunt") != nullptr || strstr(n, "Watcher") != nullptr ||
                        strstr(n, "Elite") != nullptr || strstr(n, "Cavalryman") != nullptr ||
                        strstr(n, "Mountaineer") != nullptr || strstr(n, "Gardien") != nullptr ||
                        strstr(n, "Schildwache") != nullptr || strstr(n, "Guardia") != nullptr;
                }
                if (!isGuard)
                    return false;
                // Faction routing (data-driven, core IsHostileTo: enemy/friend
                // lists first, then hostile mask). Player templates: 1 = Alliance,
                // 2 = Horde.
                FactionTemplateEntry const* ally = sObjectMgr.GetFactionTemplateEntry(1);
                FactionTemplateEntry const* horde = sObjectMgr.GetFactionTemplateEntry(2);
                FactionTemplateEntry const* guardFaction = info->faction ? sObjectMgr.GetFactionTemplateEntry(info->faction) : nullptr;
                if (!ally || !horde || !guardFaction)
                    return false;
                auto hostileTo = [](FactionTemplateEntry const* guard, FactionTemplateEntry const* player)
                {
                    return guard->IsHostileTo(*player);
                };
                // Neutral-town bruisers: guard-bit faction neutral to BOTH baseline
                // templates (Booty Bay 121, Gadgetzan 475, Ratchet 637, Everlook
                // 854 — all our 1 / hostile 8). They kill whoever fights in town
                // regardless of standing, so both teams avoid them unconditionally
                // (query skips the live check for these).
                bool neutralBruiser = !hostileTo(guardFaction, ally) && !hostileTo(guardFaction, horde);
                // Opposing-faction guard: hostile to T, NOT hostile to T's enemy
                // (keeps out monster-team mobs that hate everyone — no town).
                // Faction id stored for the query-time live check (bot hostile to
                // the guard faction via template, at-war or forced-rank).
                bool threatensAlly = hostileTo(guardFaction, ally) && !hostileTo(guardFaction, horde);
                bool threatensHorde = hostileTo(guardFaction, horde) && !hostileTo(guardFaction, ally);
                if (!neutralBruiser && !threatensAlly && !threatensHorde)
                    return false;
                // Query radius is 2D (sqDistance2d at IsValid). Store the exact
                // position so the query does the exact 2D distance check.
                HostileTownCellKey key{ mapId,
                    HostileTownCellCoord(dataPair.second.position.x),
                    HostileTownCellCoord(dataPair.second.position.y) };
                auto push = [&](uint32 teamIdx)
                {
                    cells[teamIdx][key].push_back(HostileTownGuard{ dataPair.second.position.x, dataPair.second.position.y, info->faction, neutralBruiser });
                    ++guards[teamIdx];
                };
                if (neutralBruiser)
                {
                    push(0);
                    push(1);
                }
                else
                {
                    if (threatensAlly)
                        push(0);
                    if (threatensHorde)
                        push(1);
                }
                return false;
            }
        };
        HostileTownBuildWorker worker{ cells, guards };
        sObjectMgr.DoCreatureData(worker);
        s_hostileTownCells[0] = std::move(cells[0]);
        s_hostileTownCells[1] = std::move(cells[1]);
        s_hostileTownGuards[0] = guards[0];
        s_hostileTownGuards[1] = guards[1];
        s_hostileTownIndexBuilt.store(true, std::memory_order_release);
    });
}

// One-pass build over the static spawn table (~88k rows): template + faction
// lookups only, no world objects, no DB, no map loads. Runs once via call_once
// on the first point-danger query (creature data is loaded by then); bot AI
// runs on parallel map threads, so no double-checked locking - after the build
// the map is immutable and queries never take a lock.
void WorldPosition::EnsurePointDangerIndex()
{
    std::call_once(s_pointDangerOnceFlag, []()
    {
        std::unordered_map<PointDangerCellKey, std::vector<PointDangerSpawn>, PointDangerCellKeyHash> cells;
        struct PointDangerBuildWorker
        {
            std::unordered_map<PointDangerCellKey, std::vector<PointDangerSpawn>, PointDangerCellKeyHash>* cells;
            bool operator()(CreatureDataPair const& dataPair)
            {
                uint32 entry = dataPair.second.creature_id[0];
                if (!entry)
                    return false;
                uint32 mapId = dataPair.second.position.mapId;
                if (mapId != 0 && mapId != 1)
                    return false;
                CreatureInfo const* info = sObjectMgr.GetCreatureTemplate(entry);
                if (!info || info->civilian)
                    return false;
                // Invisible triggers and no-target spawns never aggro.
                if (info->flags_extra & 0x00000080u /* CREATURE_FLAG_EXTRA_INVISIBLE */)
                    return false;
                if (info->flags_extra & 0x00020000u /* CREATURE_FLAG_EXTRA_NO_TARGET */)
                    return false;
                if (info->type == CREATURE_TYPE_CRITTER)
                    return false;
                FactionTemplateEntry const* spawnFaction = info->faction ? sObjectMgr.GetFactionTemplateEntry(info->faction) : nullptr;
                if (!spawnFaction)
                    return false;
                FactionTemplateEntry const* ally = sObjectMgr.GetFactionTemplateEntry(1);
                FactionTemplateEntry const* horde = sObjectMgr.GetFactionTemplateEntry(2);
                if (!ally || !horde)
                    return false;
                // Static hostility per side (same reaction the target selection
                // applies): a neutral-to-everyone spawn (wildlife on a neutral
                // template) counts for neither side and never bars a point.
                bool const toAlly = ally->IsHostileTo(*spawnFaction);
                bool const toHorde = horde->IsHostileTo(*spawnFaction);
                if (!toAlly && !toHorde)
                    return false;
                PointDangerCellKey key{ mapId,
                    PointDangerCellCoord(dataPair.second.position.x),
                    PointDangerCellCoord(dataPair.second.position.y) };
                (*cells)[key].push_back(PointDangerSpawn{ dataPair.second.position.x,
                    dataPair.second.position.y, toAlly, toHorde, info->level_max });
                return false;
            }
        };
        PointDangerBuildWorker worker{ &cells };
        sObjectMgr.DoCreatureData(worker);
        s_pointDangerCells = std::move(cells);
    });
}

uint32 WorldPosition::getHighestHostileLevelNear(float radius, Team botTeam) const
{
    // O(1)-ish lock-free cell lookup after the one-time build: the query cell
    // + neighbours within radius, exact 2D distance per spawn. Static template
    // reaction only (no reputation traffic, no map loads, no per-query
    // allocation): sub-10 pool bots have no meaningful standings, and the
    // caller only runs this gate for pool bots below level 10.
    if (botTeam != ALLIANCE && botTeam != HORDE)
        return 0;
    EnsurePointDangerIndex();
    bool const isHorde = botTeam == HORDE;
    int32 minCX = PointDangerCellCoord(x - radius);
    int32 maxCX = PointDangerCellCoord(x + radius);
    int32 minCY = PointDangerCellCoord(y - radius);
    int32 maxCY = PointDangerCellCoord(y + radius);
    float const radiusSq = radius * radius;
    uint32 highest = 0;
    for (int32 cx = minCX; cx <= maxCX; ++cx)
        for (int32 cy = minCY; cy <= maxCY; ++cy)
        {
            auto it = s_pointDangerCells.find(PointDangerCellKey{ mapId, cx, cy });
            if (it == s_pointDangerCells.end())
                continue;
            for (auto const& spawn : it->second)
            {
                if (!(isHorde ? spawn.hostileToHorde : spawn.hostileToAlliance))
                    continue;
                float dx = spawn.x - x;
                float dy = spawn.y - y;
                if (dx * dx + dy * dy > radiusSq)
                    continue;
                if (spawn.levelMax > highest)
                    highest = spawn.levelMax;
            }
        }
    return highest;
}

// Diagnostics only: 0 until the one-time build completes (callers must check
// IsHostileTownIndexBuilt() first). After the build the maps are immutable, so
// size reads need no lock.
size_t WorldPosition::GetHostileTownIndexCells()
{
    if (!s_hostileTownIndexBuilt.load(std::memory_order_acquire))
        return 0;
    return s_hostileTownCells[0].size() + s_hostileTownCells[1].size();
}

size_t WorldPosition::GetHostileTownIndexGuards()
{
    if (!s_hostileTownIndexBuilt.load(std::memory_order_acquire))
        return 0;
    return s_hostileTownGuards[0] + s_hostileTownGuards[1];
}

bool WorldPosition::isGuardedHostileTownFor(Player const* bot, float radius, bool neutralTowns) const
{
    // O(1)-ish lock-free cell lookup after the one-time build: the query cell
    // + neighbours within radius, exact 2D distance per guard, then the live
    // hostility check below. No level gate: same-level enemy towns (Lakeshire
    // 55s, Splintertree 40s) kill too. No spawn-table walk, no DB, no map
    // loads, no per-query allocation.
    if (!bot)
        return false;
    Team team = bot->GetTeam();
    if (team != ALLIANCE && team != HORDE)
        return false;
    EnsureHostileTownIndex();
    uint32 teamIdx = HostileTownTeamIndex(team);
    int32 minCX = HostileTownCellCoord(x - radius);
    int32 maxCX = HostileTownCellCoord(x + radius);
    int32 minCY = HostileTownCellCoord(y - radius);
    int32 maxCY = HostileTownCellCoord(y + radius);
    float const radiusSq = radius * radius;
    FactionTemplateEntry const* botFaction = bot->GetFactionTemplateEntry();
    auto const& cells = s_hostileTownCells[teamIdx];
    for (int32 cx = minCX; cx <= maxCX; ++cx)
        for (int32 cy = minCY; cy <= maxCY; ++cy)
        {
            auto it = cells.find(HostileTownCellKey{ mapId, cx, cy });
            if (it == cells.end())
                continue;
            for (auto const& guard : it->second)
            {
                float dx = guard.x - x;
                float dy = guard.y - y;
                if (dx * dx + dy * dy > radiusSq)
                    continue;
                // Neutral-town bruisers (Booty Bay/Gadgetzan/Ratchet/Everlook)
                // kill whoever fights in town regardless of standing: always
                // guarded inside the radius.
                if (guard.neutralBruiser)
                {
                    if (neutralTowns)
                        return true;
                    continue;
                }
                // Live check per the rule: hostile to the bot only if the bot is
                // hostile to the guard's faction. Static template reaction first
                // (covers the indexed opposing-faction case with zero reputation
                // traffic), then the bot's live standing: forced-rank override,
                // else at-war state — the same two calls the core's GetReactionTo
                // consults before the mask math.
                FactionTemplateEntry const* guardFaction = sObjectMgr.GetFactionTemplateEntry(guard.factionTemplate);
                if (!guardFaction || !botFaction)
                    continue;
                if (!botFaction->IsHostileTo(*guardFaction))
                    continue;
                if (bot->GetReputationMgr().GetForcedRankIfAny(guardFaction) != nullptr)
                    return true;
                if (FactionEntry const* factionEntry = sObjectMgr.GetFactionEntry(guardFaction->faction))
                    if (bot->GetReputationMgr().IsAtWar(factionEntry))
                        return true;
                return true;
            }
        }
    return false;
}

std::string WorldPosition::getAreaName(const bool fullName, const bool zoneName) const
{
    if (!isOverworld())
    {
        MapEntry const* map = sMapStore.LookupEntry(getMapId());
        if (map)
            return std::string(map->name ? map->name : "");
    }

    AreaTableEntry const* area = GetArea();

    if (!area)
        return "";

    std::string areaName = area->Name ? area->Name : "";

    if (fullName)
    {
        uint16 zoneId = area->ZoneId;

        while (zoneId > 0)
        {
            AreaTableEntry const* parentArea = GetAreaEntryByAreaID(zoneId);

            if (!parentArea)
                break;

            std::string subAreaName = parentArea->Name ? parentArea->Name : "";

            if (zoneName)
                areaName = subAreaName;
            else
                areaName = subAreaName + " " + areaName;

            zoneId = parentArea->ZoneId;
        }
    }

    return areaName;
}

int32 WorldPosition::getAreaLevel() const
{
    if (mapId == 609)
        return 1;

    if(GetArea())
        return sTravelMgr.GetAreaLevel(GetArea()->Id);

    return 0;
}

bool WorldPosition::HasAreaFlag(const AreaFlags flag) const
{
    AreaTableEntry const* areaEntry = GetArea();
    if (areaEntry)
    {
        if (areaEntry->ZoneId)
            areaEntry = GetAreaEntryByAreaID(areaEntry->ZoneId);

        if (areaEntry && areaEntry->Flags & flag)
            return true;
    }

    return false;
}

bool WorldPosition::HasFaction(const Team team) const
{
    AreaTableEntry const* areaEntry = GetArea();
    if (areaEntry)
    {
        if (areaEntry->Team == 2 && team == ALLIANCE)
            return true;
        if (areaEntry->Team == 4 && team == HORDE)
            return true;
        if (areaEntry->Team == 6)
            return true;
    }
    return false;
}

std::set<GenericTransport*> WorldPosition::getTransports(uint32 entry)
{
    std::set<GenericTransport*> transports;
    HashMapHolder<Transport>::ReadGuard guard(HashMapHolder<Transport>::GetLock());
    for (auto const& transportEntry : HashMapHolder<Transport>::GetContainer()) // Boats & zeppelins.
    {
        GenericTransport* transport = transportEntry.second;
        if (transport && transport->GetMapId() == getMapId() && (!entry || transport->GetEntry() == entry))
            transports.insert(transport);
    }

    if (transports.empty() || !entry) //Elevators&rams
    {
        // gopair->first is a bare spawn id, not an ObjectGuid. Handing it over
        // straight compiled - ObjectGuid has a converting constructor from uint64 -
        // but produced a guid whose high bits are 0 instead of HIGHGUID_GAMEOBJECT,
        // so GetGameObject could never match it and this branch always came back
        // empty. Built properly it needs the entry too, which is gopair->second.id.
        for (auto gopair : getGameObjectsNear(0.0f, entry))
            if (GameObject* go = getMap(getFirstInstanceId())->GetGameObject(
                    ObjectGuid(HIGHGUID_GAMEOBJECT, gopair->second.id, gopair->first)))
                if (GenericTransport* transport = dynamic_cast<GenericTransport*>(go))
                    transports.insert(transport);
    }

    return transports;
}

void WorldPosition::CalculatePassengerPosition(GenericTransport* transport)
{
    transport->CalculatePassengerPosition(x, y, z, &o);
}

void WorldPosition::CalculatePassengerOffset(GenericTransport* transport)
{
    transport->CalculatePassengerOffset(x, y, z, &o);
}

bool WorldPosition::isOnTransport(GenericTransport* transport)
{
    if (!transport)
        return false;

    WorldPosition trans(transport);

    if (distance(trans) > 40.0f)
        return false;

    WorldPosition below(*this);

    below.setZ(below.getZ() - 5.0f);

    bool result0 = VMAP::VMapFactory::createOrGetVMapManager()->getObjectHitPos(mapId, x, y, z + 0.5f, below.getX(), below.getY(), below.getZ(), below.x, below.y, below.z, 0.0f);

    if (result0)
        return false;

    return GetHitPosition(below);
}

float WorldPosition::GetTransporFloorOffset(uint32 entry)
{
    auto data = sGOStorage.LookupEntry<GameObjectInfo>(entry);
    switch (data->displayId)
    {
        case 3831: //Subway
            return -10.0f;
        case 807: //Vator
            return -1.25f;
        case 455: //Undervator
            return -0.46f;
        case 3015: //Boat
            return 6.0f;
        case 3031: //Zepelin
            return -17.0f;
        case 7087: //Moonspray
            return 4.88f;
        default:
            return 0.0f;
    }

    return 0.0f;
}

bool WorldPosition::SetOnTransport(GenericTransport* transport, int32 startHeight, int32 endHeight)
{
    if (!transport)
        return false;

    WorldPosition transPos(transport);

    transPos.SetTranpotHeightToFloor(transport->GetEntry());

    if (sqDistance2d(transPos) > 1600)
        return false;

    WorldPosition start(*this), below(*this);

    start.setZ(transPos.getZ() + startHeight);
    below.setZ(transPos.getZ() + endHeight);

    bool result = VMAP::VMapFactory::createOrGetVMapManager()->getObjectHitPos(mapId, x, y, z, below.getX(), below.getY(), below.getZ(), below.x, below.y, below.z, 0.0f);

    if (result)
        return false;

    bool gotHit = start.GetHitPosition(below);

    if (gotHit)
        set(below);

    return gotHit;
}

WorldPosition WorldPosition::RandomPointOnTrans(GenericTransport* transport, uint32 radius)
{
    std::vector<WorldPosition> path;
    return RandomPointOnTrans(transport, radius, nullptr, path);
}

WorldPosition WorldPosition::RandomPointOnTrans(GenericTransport* transport, uint32 radius, Player* botForPath, std::vector<WorldPosition>& path)
{
    GenericTransport* oldTrans = botForPath ? botForPath->GetTransport() : nullptr;

    if (!transport)
        return WorldPosition();

    WorldPosition transPos(transport);
    transPos.SetTranpotHeightToFloor(transport->GetEntry());
    WorldPosition bestPos;
    std::vector<WorldPosition> bestPath;

    bool wantThisPoint = false;

    uint32 tries = 0;

    for (uint32 i = 0; i < 100; i++)
    {
        WorldPosition pos = transPos + WorldPosition(0, irand(-radius, radius), irand(-radius, radius));

        pos.SetOnTransport(transport, 1, -1);

        tries++;

        if (pos.getZ() < transPos.getZ() - 1.0f)
            continue;

        if (pos.getZ() > transPos.getZ() + 1.0f)
            continue;

        pos += WorldPosition(0, 0, 0, 0.1f);

        if (!pos.isOnTransport(transport))
            continue;

        bestPos = pos;

        if (botForPath)
        {
            botForPath->SetTransport(transport);

            std::vector<WorldPosition> posPath = pos.getPathStepFrom(WorldPosition(botForPath), botForPath, false);

            if (posPath.empty())
                continue;

            WorldPosition wantedEnd = pos;
            wantedEnd.CalculatePassengerOffset(transport);

            if (wantedEnd.sqDistance(posPath.back()) > 5.0f)
                continue;

            bestPath = posPath;
        }

        if (bestPath.size() > 2)
            break;
    }

    if (botForPath)
        botForPath->SetTransport(oldTrans);

    path = bestPath;

    return bestPos;
}

std::vector<GridPair> WorldPosition::getGridPairs(const WorldPosition& secondPos) const
{
    std::vector<GridPair> retVec;

    int lx = std::min(getGridPair().x_coord, secondPos.getGridPair().x_coord);
    int ly = std::min(getGridPair().y_coord, secondPos.getGridPair().y_coord);
    int ux = std::max(getGridPair().x_coord, secondPos.getGridPair().x_coord);
    int uy = std::max(getGridPair().y_coord, secondPos.getGridPair().y_coord);
    int border = 1;

    lx = std::min(std::max(border, lx), MAX_NUMBER_OF_GRIDS - border);
    ly = std::min(std::max(border, ly), MAX_NUMBER_OF_GRIDS - border);
    ux = std::min(std::max(border, ux), MAX_NUMBER_OF_GRIDS - border);
    uy = std::min(std::max(border, uy), MAX_NUMBER_OF_GRIDS - border);

    for (int x = lx - border; x <= ux + border; x++)
    {
        for (int y = ly - border; y <= uy + border; y++)
        {
            retVec.push_back(GridPair(x, y));
        }
    }

    return retVec;
}

std::vector<WorldPosition> WorldPosition::fromGridPair(const GridPair& gridPair, uint32 mapId)
{
    std::vector<WorldPosition> retVec;
    GridPair g;

    for (uint32 d = 0; d < 4; d++)
    {
        g = gridPair;

        if (d == 1 || d == 2)
            g >> 1;
        if (d == 2 || d == 3)
            g += 1;

        retVec.push_back(WorldPosition(mapId, g));
    }

    return retVec;
}

std::vector<WorldPosition> WorldPosition::fromCellPair(const CellPair& cellPair) const
{
    std::vector<WorldPosition> retVec;
    CellPair p;

    for (uint32 d = 0; d < 4; d++)
    {
        p = cellPair;

        if (d == 1 || d == 2)
            p >> 1;
        if (d == 2 || d == 3)
            p += 1;

        retVec.push_back(WorldPosition(getMapId(), p));
    }
    return retVec;
}

std::vector<WorldPosition> WorldPosition::gridFromCellPair(const CellPair& cellPair) const
{
    Cell c(cellPair);

    return fromGridPair(GridPair(c.GridX(), c.GridY()), getMapId());
}

std::vector<std::pair<int,int>> WorldPosition::getmGridPairs(const WorldPosition& secondPos) const
{
    std::vector<mGridPair> retVec;

    int lx = std::min(getmGridPair().first, secondPos.getmGridPair().first);
    int ly = std::min(getmGridPair().second, secondPos.getmGridPair().second);
    int ux = std::max(getmGridPair().first, secondPos.getmGridPair().first);
    int uy = std::max(getmGridPair().second, secondPos.getmGridPair().second);
    int border = 1;

    lx = std::min(std::max(border, lx), MAX_NUMBER_OF_GRIDS - border);
    ly = std::min(std::max(border, ly), MAX_NUMBER_OF_GRIDS - border);
    ux = std::min(std::max(border, ux), MAX_NUMBER_OF_GRIDS - border);
    uy = std::min(std::max(border, uy), MAX_NUMBER_OF_GRIDS - border);

    for (int x = lx - border; x <= ux + border; x++)
    {
        for (int y = ly - border; y <= uy + border; y++)
        {
            retVec.push_back(std::make_pair(x, y));
        }
    }

    return retVec;
}

std::vector<WorldPosition> WorldPosition::frommGridPair(const mGridPair& gridPair, uint32 mapId)
{
    std::vector<WorldPosition> retVec;
    mGridPair g;

    for (uint32 d = 0; d < 4; d++)
    {
        g = gridPair;

        if (d == 1 || d == 2)
            g.second++;
        if (d == 2 || d == 3)
            g.first++;

        retVec.push_back(WorldPosition(mapId, g));
    }

    return retVec;
}

bool WorldPosition::isVmapLoaded(uint32 /*mapId*/, int /*x*/, int /*y*/)
{
    // Penqle has no IsTileLoaded equivalent; assume true.
    return true;
}

bool WorldPosition::isMmapLoaded(uint32 mapId, uint32 instanceId, int x, int y)
{
    return MMAP::MMapFactory::createOrGetMMapManager()->GetNavMesh(mapId) != nullptr;
}

bool WorldPosition::loadMapAndVMap(uint32 mapId, uint32 instanceId, int x, int y)
{
    std::string logName = "load_map_grid.csv";

    bool hasMmap = false;
    if (mapId == 0 || mapId == 1)
        hasMmap = isMmapLoaded(mapId, 0, x, y);
    else
        hasMmap = isMmapLoaded(mapId, instanceId, x, y);

    if (hasMmap)
        return true;

    if (sTravelMgr.IsBadMmap(mapId, x, y))
        return false;

    bool isLoaded = false;

    if (!hasMmap)
    {
        // Penqle owns one navmesh per map; instances do not need a separate load.
        isLoaded = MMAP::MMapFactory::createOrGetMMapManager()->loadMap(mapId, x, y);

        //if (!isLoaded)
        //    sTravelMgr.AddBadMmap(mapId, x, y);
    }

    //if (!hasVmap)
    //{
    //    loadVMap(mapId, x, y);
    //}

    if (sPlayerbotAIConfig.hasLog(logName))
    {
        std::ostringstream out;
        out << sPlayerbotAIConfig.GetTimestampStr();
        out << "+00,\"mmap\", " << x << "," << y << "," << (sTravelMgr.IsBadMmap(mapId, x, y) ? "0" : "1") << ",";
        printWKT(frommGridPair(mGridPair(x, y), mapId), out, 1, true);
        sPlayerbotAIConfig.log(logName, out.str().c_str());
    }

    return isLoaded;
}

void WorldPosition::loadMapAndVMaps(const WorldPosition& secondPos, uint32 instanceId) const
{
    for (auto& grid : getmGridPairs(secondPos))
    {
        loadMapAndVMap(getMapId(), instanceId, grid.first, grid.second);
    }
}

void WorldPosition::unloadMapAndVMaps(uint32 mapId)
{
    //TerrainInfoAccess* terrain = reinterpret_cast<TerrainInfoAccess*>(const_cast<TerrainInfo*>(sTerrainMgr.LoadTerrain(mapId)));
    //terrain->UnLoadUnused();
}

bool WorldPosition::loadVMap(uint32 mapId, int x, int y)
{
    if (isVmapLoaded(mapId, x, y))
        return true;

    return VMAP::VMapFactory::createOrGetVMapManager()->loadMap(sWorld.GetDataPath().c_str(), mapId, x, y);
}

std::vector<WorldPosition> WorldPosition::fromPointsArray(const std::vector<G3D::Vector3>& path) const
{
    std::vector<WorldPosition> retVec;
    for (auto p : path)
        retVec.push_back(WorldPosition(getMapId(), p.x, p.y, p.z, getO()));

    return retVec;
}

std::vector<G3D::Vector3> WorldPosition::toPointsArray(const std::vector<WorldPosition>& path) const
{
    std::vector<G3D::Vector3> retVec;
    for (auto p : path)
        retVec.push_back(p.getVector3());

    return retVec;
}

//A single pathfinding attempt from one position to another. Returns pathfinding status and path.
std::vector<WorldPosition> WorldPosition::getPathStepFrom(const WorldPosition& startPos, std::unique_ptr<PathFinder>& pathfinder, const Unit* bot, bool forceNormalPath) const
{
    std::hash<std::thread::id> hasher;
    uint32 instanceId;
    if (sTravelNodeMap.gethasToGen())
        instanceId = 0;
    else if (!bot || bot->GetMapId() != startPos.GetMapId())
        instanceId = hasher(std::this_thread::get_id());
    else
        instanceId = bot->GetInstanceId();
    //Load mmaps and vmaps between the two points.

    loadMapAndVMaps(startPos, instanceId);

    PointsArray points;
    PathType type;

    WorldPosition start = startPos, end = *this;

    if (bot && bot->GetTransport())
    {
        start.CalculatePassengerOffset(bot->GetTransport());
        end.CalculatePassengerOffset(bot->GetTransport());
    }

    pathfinder->calculate(start.getVector3(), end.getVector3(), false);

    points = pathfinder->getPath();

    if (bot && bot->GetTransport())
    {
        for (auto& p : points)
            bot->GetTransport()->CalculatePassengerPosition(p.x, p.y, p.z);
    }

    type = pathfinder->getPathType();

    std::vector<WorldPosition> retvec = fromPointsArray(points);

    if (type == PATHFIND_INCOMPLETE)
    {
        WorldPosition lastPoint = retvec.back();

        float dist = lastPoint.distance(end);

        if (lastPoint.distance(end) < 50.0f && lastPoint.isUnderWater() && end.isUnderWater() && lastPoint.IsInLineOfSight(end))
        {
            if (dist < 5.0f)
                retvec.push_back(end);
            else
            {
                WorldPosition stepPoint = lastPoint + ((end - lastPoint) / dist * 5.0f);
                retvec.push_back(stepPoint);
            }

            return retvec;
        }
    }

    if ((!forceNormalPath && type == PATHFIND_INCOMPLETE) || type == PATHFIND_NORMAL)
        return retvec;

    return {};
}

std::vector<WorldPosition> WorldPosition::getPathStepFrom(const WorldPosition& startPos, const Unit* bot, bool forceNormalPath) const
{
    std::unique_ptr<PathFinder> pathfinder = std::make_unique<PathFinder>(bot);
    return getPathStepFrom(startPos, pathfinder, bot, forceNormalPath);
}

bool WorldPosition::isPathTo(const std::vector<WorldPosition>& path, float const maxDistance, float const maxZDistance) const
{
    float realMaxDistance = maxDistance ? maxDistance : sPlayerbotAIConfig.targetPosRecalcDistance;
    return !path.empty() && path.back().GetMapId() == getMapId() && sqDistance2d(path.back()) < realMaxDistance * realMaxDistance && abs(path.back().getZ() - getZ()) < maxZDistance;
};

bool WorldPosition::setAtWaterSurface()
{
    if (!isInWater() && !isUnderWater())
        return false;

    float waterLevel = getWaterLevel();
    if (waterLevel > -100000.0f)
    {
        z = waterLevel + 0.5f;
        return true;
    }
    return false;
}


bool WorldPosition::cropPathTo(std::vector<WorldPosition>& path, const float maxDistance) const
{
    float realMaxDistance = maxDistance ? maxDistance : sPlayerbotAIConfig.targetPosRecalcDistance;

    if (path.empty())
        return false;

   auto bestPos = std::min_element(path.begin(), path.end(), [this](WorldPosition i, WorldPosition j) {return this->sqDistance(i) < this->sqDistance(j); });

   if (bestPos == path.end())
       return false;

   bool insRange = this->sqDistance(*bestPos) <= realMaxDistance * realMaxDistance;

   path.erase(std::next(bestPos), path.end());

   return insRange;
}

//A sequential series of pathfinding attempts. Returns the complete path and if the patfinder eventually found a way to the destination.
std::vector<WorldPosition> WorldPosition::getPathFromPath(const std::vector<WorldPosition>& startPath, const Unit* bot, uint8 maxAttempt) const
{
    //We start at the end of the last path.
    WorldPosition currentPos = startPath.back();

    //No pathfinding across maps.
    if (getMapId() != currentPos.GetMapId())
        return { };

    std::vector<WorldPosition> subPath, fullPath = startPath;

    std::hash<std::thread::id> hasher;
    uint32 instanceId;
    if (sTravelNodeMap.gethasToGen())
        instanceId = 0;
    else if (!bot || bot->GetMapId() != currentPos.GetMapId())
        instanceId = hasher(std::this_thread::get_id());
    else
        instanceId = bot->GetInstanceId();
    //Load mmaps and vmaps between the two points.

    std::unique_ptr<PathFinder> pathfinder = nullptr;

    if (bot && instanceId == bot->GetInstanceId())
        pathfinder = std::make_unique<PathFinder>(bot);
    else
    {
        if (!bot)
            return {};
        pathfinder = std::make_unique<PathFinder>(bot);
    }

    //Limit the pathfinding attempts
    for (uint32 i = 0; i < maxAttempt; i++)
    {
        //Try to pathfind to this position.
        subPath = getPathStepFrom(currentPos, pathfinder, bot);

        //If we could not find a path return what we have now.
        if (subPath.empty() || currentPos.distance(subPath.back()) < sPlayerbotAIConfig.targetPosRecalcDistance)
            break;

        //Append the path excluding the start (this should be the same as the end of the startPath)
        fullPath.insert(fullPath.end(), std::next(subPath.begin(), 1), subPath.end());

        //Are we there yet?
        if (isPathTo(subPath))
            break;

        //Continue pathfinding.
        currentPos = subPath.back();
    }

    return fullPath;
}

bool WorldPosition::ClosestCorrectPoint(float maxRange, float maxHeight, uint32 instanceId)
{
    MANGOS_ASSERT(std::isfinite(x) && std::isfinite(y) && std::isfinite(z));

    MMAP::MMapManager* mmap = MMAP::MMapFactory::createOrGetMMapManager();

    MANGOS_ASSERT(mmap);

    dtNavMeshQuery const* query = mmap->GetNavMeshQuery(getMapId());

    MANGOS_ASSERT(query && query->getAttachedNavMesh());

    float curPoint[VERTEX_SIZE] = {y, z, x };
    float extend[VERTEX_SIZE] = { maxRange, maxHeight, maxRange };
    float newPoint[VERTEX_SIZE];

    dtQueryFilter filter;
    dtPolyRef polyRef = INVALID_POLYREF;

    uint16 includeFlags = 0;
    uint16 excludeFlags = 0;

    includeFlags |= (NAV_GROUND );
    excludeFlags |= (NAV_MAGMA_SLIME | NAV_GROUND_STEEP | NAV_WATER);


    filter.setIncludeFlags(includeFlags);
    filter.setExcludeFlags(excludeFlags);

    dtStatus dtResult = query->findNearestPoly(curPoint, extend, &filter, &polyRef, newPoint);

    y = newPoint[0];
    z = newPoint[1];
    x = newPoint[2];

    return dtStatusSucceed(dtResult) && polyRef != INVALID_POLYREF;
}

bool WorldPosition::GetReachableRandomPointOnGround(const Player* bot, const float radius, const bool randomRange)
{
    if (radius <= 0.0f)
        return false;

    Map* map = getMap(bot ? bot->GetInstanceId() : getFirstInstanceId());
    if (!map)
        return false;

    WorldPosition const start = *this;
    uint32 const attempts = 8;
    for (uint32 attempt = 0; attempt < attempts; ++attempt)
    {
        float const angle = rand_norm_f() * 2.0f * M_PI_F;
        float const distance = randomRange ? radius * rand_norm_f() : radius;

        WorldPosition candidate = start;
        candidate.x += cos(angle) * distance;
        candidate.y += sin(angle) * distance;
        candidate.z = map->GetHeight(candidate.x, candidate.y, start.z, true);

        // The core has no navmesh random-point query. Height plus static VMap
        // line of sight is the strongest generic terrain contract available to
        // the module; the caller still performs ordinary core pathfinding.
        if (!std::isfinite(candidate.z) || candidate.z <= INVALID_HEIGHT || !candidate.isValid())
            continue;
        if (!start.IsInStaticLineOfSight(candidate))
            continue;

        *this = candidate;
        return true;
    }

    return false;
}

bool WorldPosition::isUnderground() const
{
    float groundZ = getMap(getFirstInstanceId())->GetHeight(x, y, z+0.5f, true), maxZ;
    maxZ = getTerrain()->GetWaterOrGroundLevel(x, y, z + 0.5f, &groundZ, true);

    if (maxZ > INVALID_HEIGHT)
    {
        if (z + 0.5f > maxZ)
            return false;
        else if (z < groundZ)
            return true;
    }

    return true;
}

std::vector<WorldPosition> WorldPosition::ComputePathToRandomPoint(const Player* bot, const float radius, const bool randomRange)
{
    WorldPosition start = *this;

    float angle = rand_norm_f() * 2 * M_PI_F;
    float range = radius;

    if (randomRange)
        range *= rand_norm_f();

    x += range * cos(angle);
    y += range * sin(angle);

    std::unique_ptr<PathFinder> pathfinder = std::make_unique<PathFinder>(bot);

    std::vector<WorldPosition> path = getPathStepFrom(start, pathfinder, bot);

    if (path.size())
        set(path.back());
    else
        set(start);

    return path;
}

uint32 WorldPosition::getUnitsAggro(const std::list<ObjectGuid>& units, const Player* bot) const
{
    uint32 count = 0;
    for (auto guid : units)
    {
        Unit* unit = GuidPosition(guid,bot).GetUnit(bot->GetInstanceId());

        if (!unit) continue;

        float attackDistance = unit->GetCombatReach(bot, false, 0.0f);
        if (this->sqDistance(unit) > attackDistance * attackDistance)
            continue;

        count++;
    }

    return count;
};



bool FindPointCreatureData::operator()(CreatureDataPair const& dataPair)
{
    if (!entry || dataPair.second.creature_id[0] == entry)
        if ((!point || dataPair.second.position.mapId == point.GetMapId()) && (!radius || point.sqDistance(WorldPosition(dataPair.second.position.mapId, dataPair.second.position.x, dataPair.second.position.y, dataPair.second.position.z)) < radius * radius))
        {
            data.push_back(&dataPair);
        }

    return false;
}

bool FindPointGameObjectData::operator()(GameObjectDataPair const& dataPair)
{
    if (!entry || dataPair.second.id == entry)
        if ((!point || dataPair.second.position.mapId == point.GetMapId()) && (!radius || point.sqDistance(WorldPosition(dataPair.second.position.mapId, dataPair.second.position.x, dataPair.second.position.y, dataPair.second.position.z)) < radius * radius))
        {
            data.push_back(&dataPair);
        }

    return false;
}

std::vector<CreatureDataPair const*> WorldPosition::getCreaturesNear(const float radius, const uint32 entry) const
{
    FindPointCreatureData worker(*this, radius, entry);
    sObjectMgr.DoCreatureData(worker);
    return worker.GetResult();
}

std::vector<GameObjectDataPair const*> WorldPosition::getGameObjectsNear(const float radius, const uint32 entry) const
{
    FindPointGameObjectData worker(*this, radius, entry);
    sObjectMgr.DoGOData(worker);
    return worker.GetResult();
}
