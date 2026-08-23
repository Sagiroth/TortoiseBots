#pragma once
#define DT_POLYREF64 1

#include "ObjectMgr.h"
#include "Spells/SpellMgr.h"
#include "World.h"
#include "Maps/PathFinder.h"

class ByteBuffer;

namespace G3D
{
    class Vector2;
    class Vector3;
    class Vector4;
}

namespace ai
{
    //Constructor types for WorldPosition
    enum WorldPositionConst
    {
        WP_RANDOM = 0,
        WP_CENTROID = 1,
        WP_MEAN_CENTROID = 2,
        WP_CLOSEST = 3
    };

    template <class D, class W, class URBG>
    inline void WeightedShuffle
    (D first, D last
        , W first_weight, W last_weight
        , URBG&& g)
    {
        while (first != last && first_weight != last_weight)
        {
            std::discrete_distribution<int> dd(first_weight, last_weight);
            auto i = dd(g);

            if (i)
            {
                std::swap(*first, *std::next(first, i));
                std::swap(*first_weight, *std::next(first_weight, i));
            }
            ++first;
            ++first_weight;
        }
    }

    class GuidPosition;

    typedef std::pair<int, int> mGridPair;

    //Extension of WorldLocation with distance functions.
    class WorldPosition : public WorldLocation
    {
    public:
        //Constructors
        WorldPosition() : WorldLocation(0,0,0,0,0) {}
        WorldPosition(const WorldLocation& loc) : WorldLocation(loc) {}
        WorldPosition(const WorldPosition& pos) : WorldLocation(pos) {}
        WorldPosition(const std::string str) {char p; std::stringstream  out(str); out >> mapId >> p >> x >> p >> y >> p >> z >> p >> o; }
        WorldPosition(const uint32 mapId, const float x, const float y, const float z = 0, float o = 0) : WorldLocation(mapId, x, y, z, o) {}
        WorldPosition(const uint32 mapId, const Position& pos) : WorldLocation(mapId, pos.x, pos.y, pos.z, pos.o) {}
        WorldPosition(const WorldObject* wo) { if (wo) { set(WorldLocation(wo->GetMapId(), wo->GetPositionX(), wo->GetPositionY(), wo->GetPositionZ(), wo->GetOrientation())); } }
        // Penqle EMBEDS WorldLocation as `position` member; cmangos has flat fields.
        WorldPosition(const CreatureDataPair* cdPair) { if (cdPair) { set(cdPair->second.position); } }
        WorldPosition(const GameObjectDataPair* cdPair) { if (cdPair) { set(cdPair->second.position); } }
        WorldPosition(const uint32 mapId, const GuidPosition& guidP, uint32 instanceId);
        WorldPosition(const std::vector<WorldPosition*>& list, const WorldPositionConst conType);
        WorldPosition(const std::vector<WorldPosition>& list, const WorldPositionConst conType);
        WorldPosition(const uint32 mapId, const GridPair grid) : WorldLocation(mapId, (int32(grid.x_coord) - CENTER_GRID_ID - 0.5)* SIZE_OF_GRIDS + CENTER_GRID_OFFSET, (int32(grid.y_coord) - CENTER_GRID_ID - 0.5)* SIZE_OF_GRIDS + CENTER_GRID_OFFSET, 0, 0) {}
        WorldPosition(const uint32 mapId, const CellPair cell) : WorldLocation(mapId, (int32(cell.x_coord) - CENTER_GRID_CELL_ID - 0.5)* SIZE_OF_GRID_CELL + CENTER_GRID_CELL_OFFSET, (int32(cell.y_coord) - CENTER_GRID_CELL_ID - 0.5)* SIZE_OF_GRID_CELL + CENTER_GRID_CELL_OFFSET, 0, 0) {}
        WorldPosition(const uint32 mapId, const mGridPair grid) : WorldLocation(mapId, (32 - grid.first)* SIZE_OF_GRIDS, (32 - grid.second)* SIZE_OF_GRIDS, 0, 0) {}
        // Penqle's SpellTargetPosition is a typedef for WorldLocation (cmangos has its own struct with target_X/Y/Z/mapId fields).
        WorldPosition(const SpellTargetPosition* pos) : WorldLocation(pos->mapId, pos->x, pos->y, pos->z) {}
        WorldPosition(const TaxiNodesEntry* pos) : WorldLocation(pos->map_id, pos->x, pos->y, pos->z) {}
        // Penqle's WorldSafeLocsEntry has no o field; pass 0.
        WorldPosition(const WorldSafeLocsEntry* pos) : WorldLocation(pos->map_id, pos->x, pos->y, pos->z, 0.0f) {}
        WorldPosition(const PlayerInfo* pos) : WorldLocation(pos->mapId,pos->positionX, pos->positionY, pos->positionZ, pos->orientation) {}
        WorldPosition(const Vector3& pos, const uint32 mapId = 0, float o = 0) : WorldLocation(mapId, pos.x, pos.y, pos.z, o) {}

        //Setters
        void set(const WorldLocation& pos) { mapId = pos.mapId; x = pos.x; y = pos.y; z = pos.z; o = pos.o; }
        void set(const WorldPosition& pos) { mapId = pos.mapId; x = pos.x; y = pos.y; z = pos.z; o = pos.o; }
        void set(const WorldObject* wo) { set(WorldLocation(wo->GetMapId(), wo->GetPositionX(), wo->GetPositionY(), wo->GetPositionZ(), wo->GetOrientation())); }
        void set(const ObjectGuid& guid, const uint32 mapId, const uint32 instanceId);
        void setMapId(const uint32 id) { this->mapId = id; }
        void setX(const float x) { this->x = x; }
        void setY(const float y) { this->y = y; }
        void setZ(const float z) { this->z = z; }
        void setO(const float o) {this->o = o;}

        //Operators
        operator bool() const { return  x != 0 || y != 0 || z != 0; }
        bool operator==(const WorldPosition& p1) const { return mapId == p1.mapId && x == p1.x && y == p1.y && z == p1.z && o == p1.o; }
        bool operator!=(const WorldPosition& p1) const { return mapId != p1.mapId || x != p1.x || y != p1.y || z != p1.z || o != p1.o; }
        
        WorldPosition& operator+=(const WorldPosition& p1) { x += p1.x; y += p1.y; z += p1.z; return *this; }
        WorldPosition& operator-=(const WorldPosition& p1) { x -= p1.x; y -= p1.y; z -= p1.z; return *this; }

        WorldPosition& operator*=(const float s) { x *= s; y *= s; z *= s; return *this; }
        WorldPosition& operator/=(const float s) { x /= s; y /= s; z /= s; return *this; }

        WorldPosition operator+(const WorldPosition& p1) const { WorldPosition p(*this); p += p1; return p; }
        WorldPosition operator-(const WorldPosition& p1) const { WorldPosition p(*this); p -= p1; return p; }

        WorldPosition operator*(const float s) const { WorldPosition p(*this); p *= s; return p; }
        WorldPosition operator/(const float s) const { WorldPosition p(*this); p /= s; return p; }

        float operator*(const WorldPosition& p1) const { return (x * x) + (y * y) + (z * z); }

        float projectOnSegment(const WorldPosition& p1, const WorldPosition& p2) const;
        

        //Getters
        uint32 getMapId() const { return mapId; }
        uint32 GetMapId() const { return getMapId(); }
        float getX() const { return x; }
        float getY() const { return y; }
        float getZ() const { return z; }
        float getO() const { return o; }
        float GetX() const { return getX(); }
        float GetY() const { return getY(); }
        float GetZ() const { return getZ(); }
        float GetO() const { return getO(); }
        // Penqle compat - capitalized aliases for ChatHelper etc
        uint32 GetFirstInstanceId() const { return getFirstInstanceId(); }
        Map* GetMap(uint32 instanceId) const { return getMap(instanceId); }
        std::string GetAreaName(bool fullName=true, bool zoneName=false) const { return getAreaName(fullName, zoneName); }
        float GetAngleTo(const WorldPosition& endPos) const { return getAngleTo(endPos); }
        G3D::Vector3 getVector3() const;
        G3D::Vector3 GetVector3() const { return getVector3(); }
        std::string print(uint8 precision = 2, bool onlyXyz = false) const;
        virtual std::string to_string() const { char p = '|'; std::stringstream out; out << mapId << p << x << p << y << p << z << p << o; return out.str(); };

        static void printWKT(const std::vector<WorldPosition>& points, std::ostringstream& out, const uint32 dim = 0, const bool loop = false);
        void printWKT(std::ostringstream& out) const { printWKT({ *this }, out); }

        bool isOverworld() const { return mapId == 0 || mapId == 1 || mapId == 530 || mapId == 571 || mapId == 609; }
        bool IsOverworld() const { return isOverworld(); }
        bool isBg() const { return mapId == 30 || mapId == 489 || mapId == 529 || mapId == 566 || mapId == 607 || mapId == 628; }
        bool isArena() const { return mapId == 559 || mapId == 572 || mapId == 562 || mapId == 617 || mapId == 618; }
        bool isInstance() const { return !isOverworld() || mapId == 609;}
        bool isInWater() const { return getTerrain() ? getTerrain()->IsInWater(x, y, z) : false; };
        bool isUnderWater() const { return getTerrain() ? getTerrain()->IsUnderWater(x, y, z) : false; };
        bool IsUnderWater() const { return isUnderWater(); }
        bool setAtWaterSurface();
        bool isUnderground() const;
        float getWaterLevel() const { return getTerrain() ? getTerrain()->GetWaterLevel(x, y, z) : -200000.0f; };
        float getGroundLevel() const { float ground = 0.0f; getTerrain()->GetWaterLevel(x, y, z, &ground); return ground; };

        WorldPosition relPoint(const WorldPosition& center) const { return WorldPosition(mapId, x - center.x, y - center.y, z - center.z, o); }
        WorldPosition offset(const WorldPosition& center) const { return WorldPosition(mapId, x + center.x, y + center.y, z + center.z, o); }
        float size() const { return sqrt(pow(x, 2.0) + pow(y, 2.0) + pow(z, 2.0)); }

        //Slow distance function using possible map transfers.
        float distance(const WorldPosition& to) const;

        float fDist(const WorldPosition& to) const;

        //Returns the closest point from the list.
        WorldPosition* closest(const std::vector<WorldPosition*>& list) const { return *std::min_element(list.begin(), list.end(), [this](WorldPosition* i, WorldPosition* j) {return this->distance(*i) < this->distance(*j); }); }
        WorldPosition closest(const std::vector<WorldPosition>& list) const { return *std::min_element(list.begin(), list.end(), [this](WorldPosition i, WorldPosition j) {return this->distance(i) < this->distance(j); }); }

        WorldPosition* furtest(const std::vector<WorldPosition*>& list) const { return *std::max_element(list.begin(), list.end(), [this](WorldPosition* i, WorldPosition* j) {return this->distance(*i) < this->distance(*j); }); }
        WorldPosition furtest(const std::vector<WorldPosition>& list) const { return *std::max_element(list.begin(), list.end(), [this](WorldPosition i, WorldPosition j) {return this->distance(i) < this->distance(j); }); }

        template<class T>
        std::pair<T, WorldPosition>  closest(const std::list<std::pair<T, WorldPosition>>& list) const { return *std::min_element(list.begin(), list.end(), [this](std::pair<T, WorldPosition> i, std::pair<T, WorldPosition> j) {return this->distance(i.second) < this->distance(j.second); }); }
        template<class T>
        std::pair<T, WorldPosition> closest(const std::list<T>& list) const { return closest(GetPosList(list)); }

        template<class T>
        std::pair<T, WorldPosition>  closest(const std::vector<std::pair<T, WorldPosition>>& list) const { return *std::min_element(list.begin(), list.end(), [this](std::pair<T, WorldPosition> i, std::pair<T, WorldPosition> j) {return this->distance(i.second) < this->distance(j.second); }); }
        template<class T>
        std::pair<T, WorldPosition> closest(const std::vector<T>& list) const { return closest(GetPosVector(list)); }

        bool IsWithinDist(const WorldPosition& other, float dist2compare) const { return sqDistance(other) < dist2compare * dist2compare; }

        //Quick square distance in 2d plane.
        float sqDistance2d(const WorldPosition& to) const { return (x - to.x) * (x - to.x) + (y - to.y) * (y - to.y); };

        //Quick square distance calculation without map check. Used for getting the minimum distant points.
        float sqDistance(const WorldPosition& to) const { return (x - to.x) * (x - to.x) + (y - to.y) * (y - to.y) + (z - to.z) * (z - to.z); };

        //Returns the closest point of the list. Fast but only works for the same map.
        WorldPosition* closestSq(const std::vector<WorldPosition*>& list) const { return *std::min_element(list.begin(), list.end(), [this](WorldPosition* i, WorldPosition* j) {return sqDistance(*i) < sqDistance(*j); }); }
        WorldPosition closestSq(const std::vector<WorldPosition>& list) const { return *std::min_element(list.begin(), list.end(), [this](WorldPosition i, WorldPosition j) {return sqDistance(i) < sqDistance(j); }); }

        float getAngleTo(const WorldPosition& endPos) const { float ang = atan2(endPos.y - y, endPos.x - x); return (ang >= 0) ? ang : 2 * M_PI_F + ang; };
        float getAngleBetween(const WorldPosition& dir1, const WorldPosition& dir2) const { return abs(getAngleTo(dir1) - getAngleTo(dir2)); };

        void rotateXY(const float angle) { float nx = cos(angle) * x - sin(angle) * y, ny = sin(angle) * x + cos(angle) * y; x = nx; y = ny; }

        WorldPosition limit(const WorldPosition& center, const float maxDistance) { WorldPosition pos(*this); pos -= center; float size = pos.size(); if (size > maxDistance) { pos /= pos.size(); pos *= maxDistance; pos += center; } return pos; }

        WorldPosition lastInRange(const std::vector<WorldPosition>& list, const float minDist = -1, const float maxDist = -1) const;
        WorldPosition firstOutRange(const std::vector<WorldPosition>& list, const float minDist = -1, const float maxDist = -1) const;

        float mSign(const WorldPosition* p1, const WorldPosition* p2) const { return(x - p2->x) * (p1->y - p2->y) - (p1->x - p2->x) * (y - p2->y); }
        bool isInside(const WorldPosition* p1, const WorldPosition* p2, const WorldPosition* p3) const;

        void distancePartition(const std::vector<float>& distanceLimits, WorldPosition* to, std::vector<std::vector<WorldPosition*>>& partition) const;
        std::vector<std::vector<WorldPosition*>> distancePartition(const std::vector<float>& distanceLimits, std::vector<WorldPosition*> points) const;

        std::vector <WorldPosition*> GetNextPoint(std::vector<WorldPosition*> points, uint32 amount = 1) const;
        std::vector <WorldPosition> GetNextPoint(std::vector<WorldPosition> points, uint32 amount = 1) const;
        
        template<class T>
        void GetNextPoint(std::vector <std::pair<T, WorldPosition*>>& data) const
        {
            std::vector<uint32> weights;

            std::transform(data.begin(), data.end(), std::back_inserter(weights), [this](std::pair<T, WorldPosition*> point) { return 200000 / (1 + this->distance(*point.second)); });

            //If any weight is 0 add 1 to all weights.
            for (auto& w : weights)
            {
                if (w > 0)
                    continue;

                std::for_each(weights.begin(), weights.end(), [](uint32& d) { d += 1; });
                break;
            }

            std::mt19937 gen(time(0));

            WeightedShuffle(data.begin(), data.end(), weights.begin(), weights.end(), gen);
        }

        //Map functions. Player independent.
        // cmangos uses sMapStore (DBCStorage<MapEntry>);
        // Penqle uses sMapStorage (SQLStorage) with templated LookupEntry.
        const MapEntry* getMapEntry() const { return sMapStorage.LookupEntry<MapEntry>(mapId); }
        const MapEntry* GetMapEntry() const { return getMapEntry(); }
        uint32 getFirstInstanceId() const { for (auto& map : sMapMgr.Maps()) { if (map.second->GetId() == getMapId()) return map.second->GetInstanceId(); }; return 0; }

        // Penqle has no sObjectMgr.GetInstanceTemplate; stub returns nullptr.
        // Real implementation if any caller needs it.
        InstanceTemplate const* getInstanceTemplate() { return nullptr; }
        Map* getMap(uint32 instanceId) const { if (!*this) return nullptr; loadMapAndVMap(instanceId); return sMapMgr.FindMap(mapId, instanceId ? instanceId : (getMapEntry()->Instanceable() ? getFirstInstanceId() : 0)); }
        const TerrainInfo* getTerrain() const { return getMap(getFirstInstanceId()) ? getMap(getFirstInstanceId())->GetTerrain() : sTerrainMgr.LoadTerrain(getMapId()); }
        const TerrainInfo* GetTerrain() const { return getTerrain(); }
        bool isDungeon() { return getMapEntry()->IsDungeon(); }
        bool IsDungeon() { return isDungeon(); }
        // Penqle's AreaEntry uses Flags (capital F); cmangos uses flags.
        bool isCity() { return GetArea() && GetArea()->Flags & (AREA_FLAG_CITY | AREA_FLAG_SLAVE_CAPITAL); }
        float getVisibilityDistance() { return getMap(0) ? getMap(0)->GetVisibilityDistance() : (isOverworld() ? World::GetMaxVisibleDistanceOnContinents() : World::GetMaxVisibleDistanceInInstances()); }

        bool IsInStaticLineOfSight(WorldPosition pos, float heightMod = 0.5f) const;
#if defined(MANGOSBOT_TWO)
        bool IsInLineOfSight(WorldPosition pos, float heightMod = 0.5f) const { return mapId == pos.mapId && getMap(getFirstInstanceId()) && getMap(getFirstInstanceId())->isInLineOfSight(x, y, z + heightMod, pos.x, pos.y, pos.z + heightMod, 0, true); }
        bool GetHitPosition(WorldPosition& pos) const { return getMap(getFirstInstanceId())->GetHitPosition(x, y, z, pos.x, pos.y, pos.z,0, 0.0f);};
#else
        // Penqle uses lowercase isInLineOfSight (cmangos uppercase IsInLineOfSight).
        bool IsInLineOfSight(WorldPosition pos, float heightMod = 0.5f) const { return mapId == pos.mapId && getMap(getFirstInstanceId()) && getMap(getFirstInstanceId())->isInLineOfSight(x, y, z + heightMod, pos.x, pos.y, pos.z + heightMod, true); }
        // Penqle's equivalent of cmangos's GetHitPosition is GetLosHitPosition (signature: srcX,Y,Z, destX,Y,Z, modifyDist).
        bool GetHitPosition(WorldPosition& pos) { return getMap(getFirstInstanceId())->GetLosHitPosition(x, y, z, pos.x, pos.y, pos.z, 0.0f);};
#endif


        bool isOutside() const { WorldPosition high(*this); high.setZ(z + 500.0f); return IsInLineOfSight(high); }
        bool canFly() const;

#if defined(MANGOSBOT_TWO)
        const float getHeight(bool swim = false) const { if(getMap(getFirstInstanceId())) return getMap(getFirstInstanceId())->GetHeight(0, x, y, z, swim); return 0.0;}
        float GetHeightInRange(float maxSearchDist = 4.0f) const { float z = z;  return getMap(getFirstInstanceId()) ? (getMap(getFirstInstanceId())->GetHeightInRange(0, x, y, z, maxSearchDist) ? z : z) : z; }
#else
        // Penqle's Map::GetHeight signature is (x, y, z, vmap=true, maxSearchDist=...).
        // Bot's `swim` parameter doesn't map directly; pass `true` for vmap (most common bot use case is on-map height).
        float getHeight(bool swim = false) const { return getMap(getFirstInstanceId()) ? getMap(getFirstInstanceId())->GetHeight(x, y, z, true) : z; }
        float GetHeight(bool swim = false) const { return getHeight(swim); }
        // Penqle has no GetHeightInRange method. Approximate with GetHeight (loses range-search behavior).
        float GetHeightInRange(float maxSearchDist = 4.0f) const { return getMap(getFirstInstanceId()) ? getMap(getFirstInstanceId())->GetHeight(x, y, z, true, maxSearchDist) : z; }
#endif

        float currentHeight() const { return z - getHeight(); }

        std::set<GenericTransport*> getTransports(uint32 entry = 0);
        std::set<GenericTransport*> GetTransports(uint32 entry = 0) { return getTransports(entry); }
        void CalculatePassengerPosition(GenericTransport* transport);
        void CalculatePassengerOffset(GenericTransport* transport);

        static float GetTransporFloorOffset(uint32 entry);
        void SetTranpotHeightToFloor(uint32 entry) { z += GetTransporFloorOffset(entry); }
        bool isOnTransport(GenericTransport* transport);
        bool SetOnTransport(GenericTransport* transport, int32 startHeight = 10, int32 endHeight = -1);
        WorldPosition RandomPointOnTrans(GenericTransport* transport, uint32 radius, Player* botForPath, std::vector<WorldPosition>& path);
        WorldPosition RandomPointOnTrans(GenericTransport* transport, uint32 radius = 10);

        GridPair getGridPair() const { return MaNGOS::ComputeGridPair(x, y); };
        std::vector<GridPair> getGridPairs(const WorldPosition& secondPos) const;
        static std::vector<WorldPosition> fromGridPair(const GridPair& gridPair, uint32 mapId);

        CellPair getCellPair() const { return MaNGOS::ComputeCellPair(x, y); }
        std::vector<WorldPosition> fromCellPair(const CellPair& cellPair) const;
        std::vector<WorldPosition> gridFromCellPair(const CellPair& cellPair) const;

        mGridPair getmGridPair() const {
            return std::make_pair((int)(32 - x / SIZE_OF_GRIDS), (int)(32 - y / SIZE_OF_GRIDS)); }

        std::vector<mGridPair> getmGridPairs(const WorldPosition& secondPos) const;
        static std::vector<WorldPosition> frommGridPair(const mGridPair& gridPair, uint32 mapId);

        static bool isVmapLoaded(uint32 mapId, int x, int y);

        bool isVmapLoaded() const { return isVmapLoaded(getMapId(), getmGridPair().first, getmGridPair().second); }

        static bool isMmapLoaded(uint32 mapId, uint32 instanceId, int x, int y);

        bool isMmapLoaded(uint32 instanceId) const { return isMmapLoaded(getMapId(), instanceId, getmGridPair().first, getmGridPair().second); }

        static bool loadMapAndVMap(uint32 mapId, uint32 instanceId, int x, int y);
        bool loadMapAndVMap(uint32 instanceId) const {return loadMapAndVMap(getMapId(), instanceId, getmGridPair().first, getmGridPair().second); }
        void loadMapAndVMaps(const WorldPosition& secondPos, uint32 instanceId) const;
        static void unloadMapAndVMaps(uint32 mapId);

        static bool loadVMap(uint32 mapId, int x, int y);
        bool loadVMap() const { return loadVMap(getMapId(), getmGridPair().first, getmGridPair().second); }

        //Display functions
        WorldPosition getDisplayLocation() const;
        float getDisplayX() const { return getDisplayLocation().y * -1.0; }
        float getDisplayY() const { return getDisplayLocation().x; }
        float GetDisplayX() const { return getDisplayX(); }
        float GetDisplayY() const { return getDisplayY(); }

        bool isValid() const { return MaNGOS::IsValidMapCoord(x, y, z, o); };
        virtual uint16 getAreaFlag() const {
            loadVMap();
            return isValid() && isVmapLoaded() ? sTerrainMgr.GetAreaFlag(getMapId(), x, y, z) : 0; };
        AreaTableEntry const* GetArea() const;
        uint16 GetAreaFlag() const { return getAreaFlag(); }

        // Does this position sit in the home territory of the faction opposing
        // 'team'? Contested zones (team NONE) are not. Sub-areas inherit their
        // zone's team - "The Crossroads" itself carries none, The Barrens does.
        bool isEnemyHomeZoneFor(Team team) const;
        bool IsEnemyHomeZoneFor(Team team) const { return isEnemyHomeZoneFor(team); }
        std::string getAreaName(const bool fullName = true, const bool zoneName = false) const;
        // Penqle's TerrainInfo has no AreaNameInfo or GetAreaName method.
        // Stub to empty string (loses WMO area-override lookup; would need a Penqle-side equivalent).
        std::string getAreaOverride() const { return ""; }
        int32 getAreaLevel() const;
        int32 GetAreaLevel() const { return getAreaLevel(); }

        bool HasAreaFlag(const AreaFlags flag = AREA_FLAG_CAPITAL) const;
        bool HasFaction(const Team team) const;

        std::vector<WorldPosition> fromPointsArray(const std::vector<G3D::Vector3>& path) const;
        std::vector<G3D::Vector3> toPointsArray(const std::vector<WorldPosition>& path) const;

        //Pathfinding
        std::vector<WorldPosition> getPathStepFrom(const WorldPosition& startPos, std::unique_ptr<PathFinder>& pathfinder, const Unit* bot, bool forceNormalPath = false) const;
        std::vector<WorldPosition> getPathStepFrom(const WorldPosition& startPos, const Unit* bot, bool forceNormalPath = false) const;
        std::vector<WorldPosition> GetPathStepFrom(const WorldPosition& startPos, const Unit* bot, bool forceNormalPath = false) const { return getPathStepFrom(startPos, bot, forceNormalPath); }
        std::vector<WorldPosition> getPathFromPath(const std::vector<WorldPosition>& startPath, const Unit* bot, const uint8 maxAttempt = 40) const;
        std::vector<WorldPosition> GetPathFromPath(const std::vector<WorldPosition>& startPath, const Unit* bot, const uint8 maxAttempt = 40) const { return getPathFromPath(startPath, bot, maxAttempt); }
        std::vector<WorldPosition> getPathFrom(const WorldPosition& startPos, const Unit* bot) { return getPathFromPath({ startPos }, bot); };
        std::vector<WorldPosition> GetPathFrom(const WorldPosition& startPos, const Unit* bot) { return getPathFrom(startPos, bot); }
        std::vector<WorldPosition> getPathTo(WorldPosition endPos, const Unit* bot) const { return endPos.getPathFrom(*this, bot); }
        std::vector<WorldPosition> GetPathTo(WorldPosition endPos, const Unit* bot) const { return getPathTo(endPos, bot); }
        bool isPathTo(const std::vector<WorldPosition>& path, float const maxDistance = 0, float const maxZDistance = 2.0f) const;
        bool IsPathTo(const std::vector<WorldPosition>& path, float const maxDistance = 0, float const maxZDistance = 2.0f) const { return isPathTo(path, maxDistance, maxZDistance); }
        bool cropPathTo(std::vector<WorldPosition>& path, const float maxDistance = 0) const;
        bool canPathTo(const WorldPosition& endPos, const Unit* bot) const { return endPos.isPathTo(getPathTo(endPos, bot)); }

        float getPathLength(const std::vector<WorldPosition>& points) const { float dist = 0.0f; for (auto& p : points) if (&p == &points.front()) dist = 0; else dist += std::prev(&p, 1)->distance(p); return dist; }
        float GetPathLength(const std::vector<WorldPosition>& points) const { return getPathLength(points); }

        bool ClosestCorrectPoint(float maxRange, float maxHeight = 5.0f, uint32 instanceId = 0);
        bool GetReachableRandomPointOnGround(const Player* bot, const float radius, const bool randomRange = true); //Generic terrain.
        std::vector<WorldPosition> ComputePathToRandomPoint(const Player* bot, const float radius, const bool randomRange = true); //For use with transports.

        uint32 getUnitsAggro(const std::list<ObjectGuid>& units, const Player* bot) const;
        uint32 GetUnitsAggro(const std::list<ObjectGuid>& units, const Player* bot) const { return getUnitsAggro(units, bot); }

        //Creatures
        std::vector<CreatureDataPair const*> getCreaturesNear(const float radius = 0, const uint32 entry = 0) const;
        std::vector<CreatureDataPair const*> GetCreaturesNear(const float radius = 0, const uint32 entry = 0) const { return getCreaturesNear(radius, entry); }
        //GameObjects
        std::vector<GameObjectDataPair const*> getGameObjectsNear(const float radius = 0, const uint32 entry = 0) const;
        std::vector<GameObjectDataPair const*> GetGameObjectsNear(const float radius = 0, const uint32 entry = 0) const { return getGameObjectsNear(radius, entry); }
    };

    inline ByteBuffer& operator<<(ByteBuffer& b, WorldPosition& guidP)
    {
        b << guidP.getMapId();
        b << guidP.x;
        b << guidP.y;
        b << guidP.z;
        b << guidP.o;
        return b;
    }

    inline ByteBuffer& operator>>(ByteBuffer& b, WorldPosition& g)
    {
        uint32 mapId;
        float x;
        float y;
        float z;
        float o;
        b >> mapId;
        b >> x;
        b >> y;
        b >> z;
        b >> o;

        return b;
    }

    //Generic creature finder
    class FindPointCreatureData
    {
    public:
        FindPointCreatureData(WorldPosition point1 = WorldPosition(), float radius1 = 0, uint32 entry1 = 0) { point = point1; radius = radius1; entry = entry1; }

        bool operator()(CreatureDataPair const& dataPair);
        std::vector<CreatureDataPair const*> GetResult() const { return data; };
    private:
        WorldPosition point;
        float radius;
        uint32 entry;

        std::vector<CreatureDataPair const*> data;
    };

    //Generic gameObject finder
    class FindPointGameObjectData
    {
    public:
        FindPointGameObjectData(WorldPosition point1 = WorldPosition(), float radius1 = 0, uint32 entry1 = 0) { point = point1; radius = radius1; entry = entry1; }

        bool operator()(GameObjectDataPair const& dataPair);
        std::vector<GameObjectDataPair const*> GetResult() const { return data; };
    private:
        WorldPosition point;
        float radius;
        uint32 entry;

        std::vector<GameObjectDataPair const*> data;
    };    
}

namespace std
{
    template <>
    struct hash<ai::WorldPosition>
    {
        size_t operator()(const ai::WorldPosition& p) const
        {
            size_t seed = 0;

            auto combine = [&seed](size_t h) {
                seed ^= h + 0x9e3779b9 + (seed << 6) + (seed >> 2);
            };

            combine(std::hash<uint32_t> {}(p.mapId));
            combine(std::hash<float> {}(p.x));
            combine(std::hash<float> {}(p.y));
            combine(std::hash<float> {}(p.z));
            combine(std::hash<float> {}(p.o));

            return seed;
        }
    };
}
