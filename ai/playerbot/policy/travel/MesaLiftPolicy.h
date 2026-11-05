#pragma once

#include <cmath>
#include <cstdint>

// Pure policy for the mesa lifts (Freewind Post, Thunder Bluff). The core
// keeps lifts as static objects with no animation path, so a bot cannot ride
// one and the mesa top has no walkable link to the ground: a bot sent to the
// Freewind trade vendor walked to the foot of the mesa and gave up (#1478).
// The bot walks to the lift station, then a teleport stands in for the ride
// to the other station; the world-facing guards (pool bot, nobody watching)
// live in MoveToTravelTargetAction, like the Undercity lift.

namespace ai
{
    inline constexpr uint32_t kMesaLiftMapId = 1;

    struct MesaLift
    {
        char const* name;
        // Station points: the resting spot of the lower and the upper lift
        // car (gameobject spawns); the bot is snapped onto the mesh next to them.
        float footX, footY, footZ;
        float topX, topY, topZ;
    };

    struct Mesa
    {
        // The mesa top: within radius of the centre and above minZ.
        float centreX, centreY, radius, minZ;
        MesaLift const* lifts;
        int liftCount;
    };

    inline constexpr MesaLift kFreewindLifts[] = {
        { "Freewind Post", -5382.5f, -2489.4f, -40.5f, -5399.2f, -2504.6f, 89.0f },
    };
    inline constexpr MesaLift kThunderBluffLifts[] = {
        { "Thunder Bluff west", -1308.4f, 185.3f, 68.6f, -1286.2f, 189.7f, 130.1f },
        { "Thunder Bluff east", -1028.0f, -28.4f, 69.0f, -1037.3f, -49.2f, 140.5f },
    };

    // Freewind NPCs stand at z 89-90, the needles floor below at z -60..-45.
    // Thunder Bluff NPCs stand at z 120-170, the Mulgore ground around it and
    // the lower lift stations at z 60 and below.
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

    // The lift a same-map trip needs, the one with the shortest straight
    // line bot -> station -> other station -> destination.
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
                MesaLift const& lift = mesa.lifts[i];
                float const enterX = goalOn ? lift.footX : lift.topX;
                float const enterY = goalOn ? lift.footY : lift.topY;
                float const leaveX = goalOn ? lift.topX : lift.footX;
                float const leaveY = goalOn ? lift.topY : lift.footY;
                float const cost = std::hypot(bx - enterX, by - enterY) + std::hypot(gx - leaveX, gy - leaveY);
                if (!pick.lift || cost < best)
                {
                    pick.lift = &lift;
                    best = cost;
                }
            }
            return pick;
        }
        return pick;
    }
}
