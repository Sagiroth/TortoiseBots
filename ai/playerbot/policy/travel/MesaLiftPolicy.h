#pragma once

#include <cmath>
#include <cstdint>

// Pure policy for the mesa lifts (Freewind Post, Thunder Bluff). The core
// keeps lifts as static objects and never carries a passenger: a player
// rides because the client animates the car from the server clock and
// reports its own position. A bot has no client, so it walked to the foot of
// the mesa and gave up (#1478). The module rides for it: the car's height
// follows the client animation (TransportAnimation.dbc, below) at phase
// server-ms-time % period, the bot boards a car parked at its level and steps
// off when the car parks at the other one. The world-facing part lives in
// MoveToTravelTargetAction.

namespace ai
{
    inline constexpr uint32_t kMesaLiftMapId = 1;

    struct LiftKey
    {
        uint32_t ms;
        float dz;
    };

    // TransportAnimation.dbc: every car parks 5 s at its spawn level, rides
    // 10 s, parks 5 s at the other level and rides back (30 s cycle).
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

    // Parked at the spawn level from 0 ms, at the other level from 15000 ms.
    inline constexpr uint32_t kLiftParkMs = 5000;
    inline constexpr uint32_t kLiftOtherParkStartMs = 15000;

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

    inline bool LiftSpawnOnTop(LiftCar const& car) { return LiftCarOffset(car, kLiftOtherParkStartMs) < 0.0f; }

    inline float LiftLevelZ(LiftCar const& car, bool top)
    {
        return top == LiftSpawnOnTop(car) ? car.z : car.z + LiftCarOffset(car, kLiftOtherParkStartMs);
    }

    // Milliseconds the car still stands at the level (0: not parked there).
    inline uint32_t LiftParkedLeft(LiftCar const& car, uint32_t serverMs, bool top)
    {
        uint32_t const t = serverMs % LiftPeriod(car);
        uint32_t const start = top == LiftSpawnOnTop(car) ? 0 : kLiftOtherParkStartMs;
        return t >= start && t < start + kLiftParkMs ? start + kLiftParkMs - t : 0;
    }

    // Milliseconds until the car next parks at the level (0: parked now).
    inline uint32_t LiftUntilParked(LiftCar const& car, uint32_t serverMs, bool top)
    {
        if (LiftParkedLeft(car, serverMs, top))
            return 0;
        uint32_t const period = LiftPeriod(car);
        uint32_t const t = serverMs % period;
        uint32_t const start = top == LiftSpawnOnTop(car) ? 0 : kLiftOtherParkStartMs;
        return t < start ? start - t : period - t + start;
    }

    struct MesaLift
    {
        char const* name;
        LiftCar cars[2];
    };

    struct Mesa
    {
        // The mesa top: within radius of the centre and above minZ.
        float centreX, centreY, radius, minZ;
        MesaLift const* lifts;
        int liftCount;
    };

    template <int N> constexpr int LiftKeyCount(LiftKey const (&)[N]) { return N; }

    inline constexpr MesaLift kFreewindLifts[] = {
        { "Freewind Post", { { 11898, -5399.19f, -2504.62f, 89.02f, kFreewindDown, LiftKeyCount(kFreewindDown) },
                             { 11899, -5382.5f, -2489.42f, -40.53f, kFreewindUp, LiftKeyCount(kFreewindUp) } } },
    };
    inline constexpr MesaLift kThunderBluffLifts[] = {
        { "Thunder Bluff west", { { 4170, -1286.24f, 189.72f, 130.08f, kTbWestDown, LiftKeyCount(kTbWestDown) },
                                  { 4171, -1308.38f, 185.29f, 68.59f, kTbWestUp, LiftKeyCount(kTbWestUp) } } },
        { "Thunder Bluff east", { { 47297, -1037.27f, -49.24f, 140.5f, kTbEastDown, LiftKeyCount(kTbEastDown) },
                                  { 47296, -1028.04f, -28.36f, 69.02f, kTbEastUp, LiftKeyCount(kTbEastUp) } } },
    };

    // Freewind NPCs stand at z 89-90, the needles floor below at z -60..-45.
    // Thunder Bluff NPCs stand at z 120-170, the Mulgore ground around it and
    // the lower lift stations at z 69 and below.
    inline constexpr Mesa kMesas[] = {
        { -5430.0f, -2450.0f, 150.0f, 60.0f, kFreewindLifts, 1 },
        { -1200.0f, 50.0f, 350.0f, 85.0f, kThunderBluffLifts, 2 },
    };

    inline bool OnMesa(Mesa const& mesa, uint32_t mapId, float x, float y, float z)
    {
        return mapId == kMesaLiftMapId && z > mesa.minZ &&
            std::hypot(x - mesa.centreX, y - mesa.centreY) < mesa.radius;
    }

    enum class MesaLiftDir
    {
        None,
        Up,   // bot below, destination on the mesa
        Down, // bot on the mesa, destination off it
    };

    struct MesaLiftPick
    {
        MesaLiftDir dir = MesaLiftDir::None;
        MesaLift const* lift = nullptr;
    };

    // The lift a same-map trip needs: the one with the shortest straight
    // line bot -> lift -> destination.
    inline MesaLiftPick MesaLiftFor(uint32_t botMap, float bx, float by, float bz,
        uint32_t goalMap, float gx, float gy, float gz)
    {
        MesaLiftPick pick;
        if (botMap != kMesaLiftMapId || goalMap != botMap)
            return pick;
        for (Mesa const& mesa : kMesas)
        {
            bool const botOn = OnMesa(mesa, botMap, bx, by, bz);
            bool const goalOn = OnMesa(mesa, goalMap, gx, gy, gz);
            if (botOn == goalOn)
                continue;
            pick.dir = goalOn ? MesaLiftDir::Up : MesaLiftDir::Down;
            float best = 0.0f;
            for (int i = 0; i < mesa.liftCount; ++i)
            {
                LiftCar const& car = mesa.lifts[i].cars[0];
                float const cost = std::hypot(bx - car.x, by - car.y) + std::hypot(gx - car.x, gy - car.y);
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

    // The car to wait for at a level: the one that parks there soonest.
    inline int NextLiftCar(MesaLift const& lift, uint32_t serverMs, bool top)
    {
        return LiftUntilParked(lift.cars[0], serverMs, top) <= LiftUntilParked(lift.cars[1], serverMs, top) ? 0 : 1;
    }
}
