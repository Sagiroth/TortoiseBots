#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

// Pure policy for the city lifts (Undercity, Thunder Bluff, Freewind Post).
// The core keeps lifts as static objects and never carries a passenger: a
// player rides because the client animates the car from the server clock and
// reports its own position. A bot has no client, so it walked to the foot of
// a mesa and gave up (#1478), or stood in the Undercity throne room. The
// module rides for it: the car's height follows the client animation
// (the client transport animation table, below) at phase server-ms-time % period, the bot
// boards a car parked at its level and steps off when the car parks at the
// other one. The world-facing part lives in MoveToTravelTargetAction.

namespace ai
{
    struct LiftKey
    {
        uint32_t ms;
        float dz;
    };

    // Client transport animation table: height above the spawn over one cycle; equal
    // neighbouring keys are a stop at a level.
    inline constexpr LiftKey kTbWestDown[] = { {0, 0.0f}, {5000, 0.0f}, {6100, -1.75f}, {6933, -5.18f}, {12867, -56.81f},
        {13833, -59.85f}, {15000, -61.24f}, {20000, -61.24f}, {21333, -59.83f}, {22233, -56.74f}, {28133, -5.25f},
        {28800, -1.59f}, {30033, 0.0f} };
    inline constexpr LiftKey kTbWestUp[] = { {0, 0.0f}, {5000, 0.0f}, {5900, 1.2f}, {6667, 4.69f}, {10567, 32.55f},
        {13100, 53.47f}, {14067, 59.58f}, {15000, 61.5f}, {20000, 61.5f}, {21000, 59.55f}, {21800, 54.27f},
        {25267, 29.11f}, {27933, 8.59f}, {28967, 1.97f}, {30000, 0.0f} };
    inline constexpr LiftKey kTbEastUp[] = { {0, 0.0f}, {5000, 0.0f}, {5900, 1.2f}, {6667, 4.69f}, {10567, 37.67f},
        {13100, 63.45f}, {14067, 69.56f}, {15000, 71.48f}, {20000, 71.48f}, {21000, 69.53f}, {21800, 64.25f},
        {25267, 34.23f}, {27933, 8.59f}, {28967, 1.97f}, {30000, 0.0f} };
    inline constexpr LiftKey kTbEastDown[] = { {0, 0.0f}, {5000, 0.0f}, {6100, -1.75f}, {6933, -5.18f}, {12867, -66.79f},
        {13833, -69.82f}, {15000, -71.22f}, {20000, -71.22f}, {21333, -69.81f}, {22233, -66.71f}, {28133, -5.25f},
        {28800, -1.59f}, {30033, 0.0f} };
    inline constexpr LiftKey kFreewindDown[] = { {0, 0.0f}, {5000, 0.0f}, {6100, -1.75f}, {6867, -5.18f}, {12867, -124.87f},
        {13833, -127.9f}, {15000, -129.3f}, {20000, -129.3f}, {21333, -127.89f}, {22233, -124.79f}, {28133, -5.25f},
        {28800, -1.59f}, {29900, -0.11f}, {30000, 0.0f} };
    inline constexpr LiftKey kFreewindUp[] = { {0, 0.0f}, {5000, 0.0f}, {5900, 1.2f}, {6667, 4.69f}, {13100, 121.52f},
        {14067, 127.64f}, {15000, 129.55f}, {20000, 129.55f}, {21000, 127.6f}, {21800, 122.33f}, {27933, 8.59f},
        {28967, 1.97f}, {30000, 0.0f} };
    inline constexpr LiftKey kUndercityNorth[] = { {0, 0.0f}, {1833, -55.47f}, {6833, -55.47f}, {10333, 41.04f},
        {15167, 41.04f}, {16667, 0.0f} };
    inline constexpr LiftKey kUndercityEast[] = { {0, 0.0f}, {167, 0.0f}, {3667, 96.5f}, {8500, 96.5f}, {11833, 0.0f},
        {16667, 0.0f} };
    inline constexpr LiftKey kUndercityWest[] = { {0, 0.0f}, {167, 0.0f}, {3500, 0.0f}, {7000, 96.5f}, {11833, 96.5f},
        {15167, 0.0f}, {16667, 0.0f} };

    struct LiftCar
    {
        uint32_t entry;           // gameobject entry
        float x, y, z;            // spawn = position at phase 0
        LiftKey const* keys;
        int keyCount;
    };

    inline uint32_t LiftPeriod(LiftCar const& car) { return car.keys[car.keyCount - 1].ms; }

    // Car height above its spawn at a server time, linear between key frames.
    inline float LiftCarOffset(LiftCar const& car, uint32_t serverMs)
    {
        uint32_t const t = serverMs % LiftPeriod(car);
        for (int i = 1; i < car.keyCount; ++i)
        {
            LiftKey const& a = car.keys[i - 1];
            LiftKey const& b = car.keys[i];
            if (t < b.ms)
                return a.dz + (b.dz - a.dz) * float(t - a.ms) / float(b.ms - a.ms);
        }
        return 0.0f;
    }

    inline float LiftLevelDz(LiftCar const& car, bool top)
    {
        float dz = car.keys[0].dz;
        for (int i = 1; i < car.keyCount; ++i)
            dz = top ? std::max(dz, car.keys[i].dz) : std::min(dz, car.keys[i].dz);
        return dz;
    }

    inline float LiftLevelZ(LiftCar const& car, bool top) { return car.z + LiftLevelDz(car, top); }

    // Key segment i runs from keys[i] to keys[i + 1]; it is a stop at the
    // level when both ends stand there.
    inline bool LiftSegmentStops(LiftCar const& car, int i, float levelDz)
    {
        return std::fabs(car.keys[i].dz - levelDz) < 0.05f && std::fabs(car.keys[i + 1].dz - levelDz) < 0.05f;
    }

    inline int LiftSegmentAt(LiftCar const& car, uint32_t t)
    {
        for (int i = 0; i + 1 < car.keyCount; ++i)
            if (t < car.keys[i + 1].ms)
                return i;
        return car.keyCount - 2;
    }

    // Milliseconds the car still stands at the level (0: not parked there).
    // A stop may run over the end of the cycle into its start.
    inline uint32_t LiftParkedLeft(LiftCar const& car, uint32_t serverMs, bool top)
    {
        float const level = LiftLevelDz(car, top);
        int const segments = car.keyCount - 1;
        int i = LiftSegmentAt(car, serverMs % LiftPeriod(car));
        uint32_t t = serverMs % LiftPeriod(car);
        uint32_t left = 0;
        for (int n = 0; n < segments && LiftSegmentStops(car, i, level); ++n)
        {
            left += car.keys[i + 1].ms - t;
            i = (i + 1) % segments;
            t = car.keys[i].ms;
        }
        return left;
    }

    // Milliseconds until the car next parks at the level (0: parked now).
    inline uint32_t LiftUntilParked(LiftCar const& car, uint32_t serverMs, bool top)
    {
        if (LiftParkedLeft(car, serverMs, top))
            return 0;
        float const level = LiftLevelDz(car, top);
        uint32_t const period = LiftPeriod(car);
        uint32_t const t = serverMs % period;
        int const segments = car.keyCount - 1;
        int i = LiftSegmentAt(car, t);
        for (int n = 1; n <= segments; ++n)
        {
            int const next = (i + n) % segments;
            if (LiftSegmentStops(car, next, level))
            {
                uint32_t const start = car.keys[next].ms;
                return start > t ? start - t : period - t + start;
            }
        }
        return period;
    }

    struct MesaLift
    {
        char const* name;
        LiftCar const* cars;
        int carCount;
    };

    // A place a lift leads into: within radius of the centre, above splitZ
    // for a mesa top, below it for an underground city.
    struct Mesa
    {
        uint32_t mapId;
        float centreX, centreY, radius, splitZ;
        bool underground;
        MesaLift const* lifts;
        int liftCount;
    };

    inline constexpr LiftCar kFreewindCars[] = {
        { 11898, -5399.19f, -2504.62f, 89.02f, kFreewindDown, 14 },
        { 11899, -5382.5f, -2489.42f, -40.53f, kFreewindUp, 13 },
    };
    inline constexpr LiftCar kTbWestCars[] = {
        { 4170, -1286.24f, 189.72f, 130.08f, kTbWestDown, 13 },
        { 4171, -1308.38f, 185.29f, 68.59f, kTbWestUp, 15 },
    };
    inline constexpr LiftCar kTbEastCars[] = {
        { 47297, -1037.27f, -49.24f, 140.5f, kTbEastDown, 13 },
        { 47296, -1028.04f, -28.36f, 69.02f, kTbEastUp, 15 },
    };
    inline constexpr LiftCar kUndercityNorthCar[] = { { 20649, 1596.15f, 291.8f, 14.68f, kUndercityNorth, 6 } };
    inline constexpr LiftCar kUndercityEastCar[] = { { 20652, 1595.26f, 188.64f, -40.78f, kUndercityEast, 6 } };
    inline constexpr LiftCar kUndercityWestCar[] = { { 20655, 1544.24f, 240.77f, -40.78f, kUndercityWest, 7 } };

    inline constexpr MesaLift kFreewindLifts[] = { { "Freewind Post", kFreewindCars, 2 } };
    inline constexpr MesaLift kThunderBluffLifts[] = {
        { "Thunder Bluff west", kTbWestCars, 2 },
        { "Thunder Bluff east", kTbEastCars, 2 },
    };
    inline constexpr MesaLift kUndercityLifts[] = {
        { "Undercity north", kUndercityNorthCar, 1 },
        { "Undercity east", kUndercityEastCar, 1 },
        { "Undercity west", kUndercityWestCar, 1 },
    };

    // Freewind NPCs stand at z 89-90, the needles floor below at z -60..-45.
    // Thunder Bluff NPCs stand at z 120-170, the Mulgore ground around it and
    // the lower lift stations at z 69 and below. Undercity creatures stand at
    // z -85..-23 under the Ruins of Lordaeron (z 55-69).
    inline constexpr Mesa kMesas[] = {
        { 1, -5430.0f, -2450.0f, 150.0f, 60.0f, false, kFreewindLifts, 1 },
        { 1, -1200.0f, 50.0f, 350.0f, 85.0f, false, kThunderBluffLifts, 2 },
        { 0, 1530.0f, 250.0f, 320.0f, -15.0f, true, kUndercityLifts, 3 },
    };

    inline bool InMesa(Mesa const& mesa, uint32_t mapId, float x, float y, float z)
    {
        return mapId == mesa.mapId && (mesa.underground ? z < mesa.splitZ : z > mesa.splitZ) &&
            std::hypot(x - mesa.centreX, y - mesa.centreY) < mesa.radius;
    }

    enum class MesaLiftDir
    {
        None,
        Up,
        Down,
    };

    struct MesaLiftPick
    {
        MesaLiftDir dir = MesaLiftDir::None;
        MesaLift const* lift = nullptr;
    };

    // A bot further out than this past the place's edge walks its normal
    // route first.
    inline constexpr float kLiftApproachYd = 250.0f;

    // The lift a trip needs to get into or out of a place: the one with the
    // shortest straight line bot -> lift -> destination. A destination on
    // another map counts as outside.
    inline MesaLiftPick MesaLiftFor(uint32_t botMap, float bx, float by, float bz,
        uint32_t goalMap, float gx, float gy, float gz)
    {
        MesaLiftPick pick;
        for (Mesa const& mesa : kMesas)
        {
            if (botMap != mesa.mapId ||
                std::hypot(bx - mesa.centreX, by - mesa.centreY) > mesa.radius + kLiftApproachYd)
                continue;
            bool const botIn = InMesa(mesa, botMap, bx, by, bz);
            bool const goalIn = InMesa(mesa, goalMap, gx, gy, gz);
            if (botIn == goalIn)
                continue;
            pick.dir = goalIn != mesa.underground ? MesaLiftDir::Up : MesaLiftDir::Down;
            float best = 0.0f;
            for (int i = 0; i < mesa.liftCount; ++i)
            {
                LiftCar const& car = mesa.lifts[i].cars[0];
                float const cost = std::hypot(bx - car.x, by - car.y) +
                    (goalMap == botMap ? std::hypot(gx - car.x, gy - car.y) : 0.0f);
                if (!pick.lift || cost < best)
                {
                    pick.lift = &mesa.lifts[i];
                    best = cost;
                }
            }
            return pick;
        }
        return pick;
    }

    // The car to wait for: the one whose shaft is nearest the bot.
    inline int NearestLiftCar(MesaLift const& lift, float x, float y)
    {
        int best = 0;
        for (int i = 1; i < lift.carCount; ++i)
            if (std::hypot(x - lift.cars[i].x, y - lift.cars[i].y) < std::hypot(x - lift.cars[best].x, y - lift.cars[best].y))
                best = i;
        return best;
    }
}
