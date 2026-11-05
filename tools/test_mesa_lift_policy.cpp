// Standalone policy test for the city lifts: a bot below Freewind Post or
// Thunder Bluff with a destination on top takes the nearest lift up, a bot in
// Undercity with a destination outside goes up, the way back goes down,
// everything else walks; the car schedule follows the client animation.
// Mirrors ai/playerbot/policy/travel/MesaLiftPolicy.h; no server headers needed.
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra tools/test_mesa_lift_policy.cpp -o /tmp/test_mesa_lift_policy && /tmp/test_mesa_lift_policy

#include "../ai/playerbot/policy/travel/MesaLiftPolicy.h"

#include <cmath>
#include <cstring>
#include <iostream>

namespace
{
int failures = 0;
#define CHECK(cond) do { \
    if (!(cond)) { \
        std::cerr << "FAIL line " << __LINE__ << ": " #cond "\n"; \
        ++failures; \
    } \
} while (0)

bool Named(ai::MesaLiftPick const& pick, char const* name)
{
    return pick.lift && std::strcmp(pick.lift->name, name) == 0;
}
} // namespace

using ai::MesaLiftDir;
using ai::MesaLiftFor;

int main()
{
    // Foot of Freewind (the #1478 stuck spot) -> trade vendor on top: up.
    ai::MesaLiftPick p = MesaLiftFor(1, -5356.7f, -2487.1f, -45.9f, 0, 1, -5441.9f, -2432.0f, 89.6f, 0);
    CHECK(p.dir == MesaLiftDir::Up && Named(p, "Freewind Post"));
    // Anywhere in the needles -> Freewind flight master: up the lift.
    CHECK(MesaLiftFor(1, -5600.0f, -2700.0f, -50.0f, 0, 1, -5407.1f, -2419.6f, 89.7f, 0).dir == MesaLiftDir::Up);
    CHECK(MesaLiftFor(1, -5900.0f, -3900.0f, -58.0f, 0, 1, -5407.1f, -2419.6f, 89.7f, 0).dir == MesaLiftDir::Up);
    // Freewind top -> needles floor: down.
    p = MesaLiftFor(1, -5441.9f, -2432.0f, 89.6f, 0, 1, -5409.9f, -2532.2f, -48.1f, 0);
    CHECK(p.dir == MesaLiftDir::Down && Named(p, "Freewind Post"));
    // On top to on top, below to below: walk.
    CHECK(MesaLiftFor(1, -5441.9f, -2432.0f, 89.6f, 0, 1, -5407.1f, -2419.6f, 89.7f, 0).dir == MesaLiftDir::None);
    CHECK(MesaLiftFor(1, -5409.9f, -2532.2f, -48.1f, 0, 1, -5900.0f, -3900.0f, -58.0f, 0).dir == MesaLiftDir::None);

    // Mulgore west of Thunder Bluff -> city centre: up the west lift.
    p = MesaLiftFor(1, -1345.9f, 259.2f, 2.8f, 0, 1, -1277.9f, 138.5f, 131.9f, 0);
    CHECK(p.dir == MesaLiftDir::Up && Named(p, "Thunder Bluff west"));
    // From the south-east (Bloodhoof road side) -> Elder Rise-side NPC: east lift.
    p = MesaLiftFor(1, -983.0f, -75.0f, 20.4f, 0, 1, -1090.2f, -15.4f, 140.7f, 0);
    CHECK(p.dir == MesaLiftDir::Up && Named(p, "Thunder Bluff east"));
    // City -> Bloodhoof Village: down.
    CHECK(MesaLiftFor(1, -1277.9f, 138.5f, 131.9f, 0, 1, -2350.0f, -350.0f, -9.0f, 0).dir == MesaLiftDir::Down);
    // The lower lift station itself counts as below.
    CHECK(MesaLiftFor(1, -1330.2f, 193.4f, 60.2f, 0, 1, -1277.9f, 138.5f, 131.9f, 0).dir == MesaLiftDir::Up);

    // Every car: the lower level is outside a mesa top / inside an
    // underground city, the upper one the other way round; the cars of a
    // lift serve the same two levels and stop at both.
    for (ai::Mesa const& mesa : ai::kMesas)
        for (int i = 0; i < mesa.liftCount; ++i)
        {
            ai::MesaLift const& lift = mesa.lifts[i];
            for (int c = 0; c < lift.carCount; ++c)
            {
                ai::LiftCar const& car = lift.cars[c];
                CHECK(ai::InMesa(mesa, mesa.mapId, car.x, car.y, ai::LiftLevelZ(car, false)) == mesa.underground);
                CHECK(ai::InMesa(mesa, mesa.mapId, car.x, car.y, ai::LiftLevelZ(car, true)) != mesa.underground);
                CHECK(car.keys[car.keyCount - 1].dz == 0.0f && car.keys[0].dz == 0.0f);
                for (bool top : { false, true })
                {
                    uint32_t moving = 0;
                    while (ai::LiftParkedLeft(car, moving, top))
                        moving += 100;
                    uint32_t const wait = ai::LiftUntilParked(car, moving, top);
                    CHECK(wait > 0 && wait < ai::LiftPeriod(car));
                    CHECK(ai::LiftParkedLeft(car, moving + wait + 10, top) >= 3000);
                }
                CHECK(std::fabs(ai::LiftLevelZ(car, true) - ai::LiftLevelZ(lift.cars[0], true)) < 1.0f);
                CHECK(std::fabs(ai::LiftLevelZ(car, false) - ai::LiftLevelZ(lift.cars[0], false)) < 1.0f);
            }
        }

    // Thunder Bluff west: the up car (4171) parks below at 0-5 s, rides up,
    // parks on top at 15-20 s; the down car mirrors it.
    ai::LiftCar const& upCar = ai::kThunderBluffLifts[0].cars[1];
    ai::LiftCar const& downCar = ai::kThunderBluffLifts[0].cars[0];
    CHECK(ai::LiftParkedLeft(upCar, 60000 + 1000, false) == 4000);
    CHECK(ai::LiftParkedLeft(upCar, 60000 + 1000, true) == 0);
    CHECK(ai::LiftParkedLeft(upCar, 60000 + 16000, true) == 4000);
    CHECK(ai::LiftParkedLeft(upCar, 60000 + 10000, true) == 0 && ai::LiftParkedLeft(upCar, 60000 + 10000, false) == 0);
    CHECK(ai::LiftUntilParked(upCar, 60000 + 6000, true) == 9000);
    CHECK(ai::LiftUntilParked(upCar, 60000 + 21000, false) == 9000);
    CHECK(ai::LiftUntilParked(upCar, 60000 + 2000, false) == 0);
    CHECK(std::fabs(ai::LiftCarOffset(upCar, 17000) - 61.5f) < 0.01f);
    CHECK(std::fabs(ai::LiftCarOffset(upCar, 2000)) < 0.01f);
    float const mid = ai::LiftCarOffset(upCar, 10567 + (13100 - 10567) / 2);
    CHECK(mid > 32.55f && mid < 53.47f);
    CHECK(ai::LiftParkedLeft(downCar, 16000, false) == 4000);
    // The nearest shaft: next to the 4171 shaft, next to the 4170 shaft.
    CHECK(ai::NearestLiftCar(ai::kThunderBluffLifts[0], -1310.0f, 183.0f) == 1);
    CHECK(ai::NearestLiftCar(ai::kThunderBluffLifts[0], -1284.0f, 192.0f) == 0);

    // Undercity west (20655): below from 15.2 s over the cycle end to 3.5 s,
    // on top 7-11.8 s; north (20649) starts mid-shaft.
    ai::LiftCar const& ucWest = ai::kUndercityLifts[2].cars[0];
    CHECK(ai::LiftParkedLeft(ucWest, 16000, false) == 667 + 3500);
    CHECK(ai::LiftParkedLeft(ucWest, 1000, false) == 2500);
    CHECK(ai::LiftParkedLeft(ucWest, 8000, true) == 3833);
    CHECK(ai::LiftUntilParked(ucWest, 4000, true) == 3000);
    CHECK(ai::LiftUntilParked(ucWest, 12000, false) == 3167);
    ai::LiftCar const& ucNorth = ai::kUndercityLifts[0].cars[0];
    CHECK(ai::LiftParkedLeft(ucNorth, 0, false) == 0 && ai::LiftParkedLeft(ucNorth, 0, true) == 0);
    CHECK(ai::LiftUntilParked(ucNorth, 0, false) == 1833);
    CHECK(std::fabs(ai::LiftLevelZ(ucNorth, false) + 40.79f) < 0.05f && std::fabs(ai::LiftLevelZ(ucNorth, true) - 55.72f) < 0.05f);

    // Undercity: throne room -> auctioneer below goes down, the auction
    // house -> Brill or another map goes up, inside to inside walks.
    CHECK(MesaLiftFor(0, 1567.2f, 240.6f, 57.6f, 0, 0, 1542.4f, 255.2f, -56.8f, 0).dir == MesaLiftDir::Down);
    CHECK(MesaLiftFor(0, 1609.7f, 187.2f, -56.8f, 0, 0, 2260.0f, 290.0f, 34.0f, 0).dir == MesaLiftDir::Up);
    CHECK(MesaLiftFor(0, 1609.7f, 187.2f, -56.8f, 0, 1, 1600.0f, -4400.0f, 10.0f, 0).dir == MesaLiftDir::Up);
    CHECK(MesaLiftFor(0, 1609.7f, 187.2f, -56.8f, 0, 0, 1542.4f, 255.2f, -56.8f, 0).dir == MesaLiftDir::None);
    CHECK(MesaLiftFor(0, 1567.2f, 240.6f, 57.6f, 0, 0, 2260.0f, 290.0f, 34.0f, 0).dir == MesaLiftDir::None);
    // Brill to the city: to the lift as well.
    CHECK(MesaLiftFor(0, 2260.0f, 290.0f, 34.0f, 0, 0, 1542.4f, 255.2f, -56.8f, 0).dir == MesaLiftDir::Down);
    // The ruins courtyard outside the throne room: down.
    CHECK(MesaLiftFor(0, 1700.0f, 240.0f, 62.0f, 0, 0, 1542.4f, 255.2f, -56.8f, 0).dir == MesaLiftDir::Down);

    // The Great Lift: Barrens -> needles floor or Freewind goes down it,
    // needles floor -> Barrens goes up it, Freewind top -> Barrens takes the
    // Freewind lift first, Barrens -> Barrens walks.
    p = MesaLiftFor(1, -4400.0f, -1900.0f, 92.0f, ai::kBarrensZone, 1, -5409.9f, -2532.2f, -48.1f, ai::kThousandNeedlesZone);
    CHECK(p.dir == MesaLiftDir::Down && Named(p, "Great Lift"));
    p = MesaLiftFor(1, -4400.0f, -1900.0f, 92.0f, ai::kBarrensZone, 1, -5441.9f, -2432.0f, 89.6f, ai::kThousandNeedlesZone);
    CHECK(p.dir == MesaLiftDir::Down && Named(p, "Great Lift"));
    p = MesaLiftFor(1, -4800.0f, -1950.0f, -40.0f, ai::kThousandNeedlesZone, 1, -2000.0f, -2600.0f, 92.0f, ai::kBarrensZone);
    CHECK(p.dir == MesaLiftDir::Up && Named(p, "Great Lift"));
    p = MesaLiftFor(1, -5441.9f, -2432.0f, 89.6f, ai::kThousandNeedlesZone, 1, -2000.0f, -2600.0f, 92.0f, ai::kBarrensZone);
    CHECK(p.dir == MesaLiftDir::Down && Named(p, "Freewind Post"));
    CHECK(MesaLiftFor(1, -4400.0f, -1900.0f, 92.0f, ai::kBarrensZone, 1, -2000.0f, -2600.0f, 92.0f, ai::kBarrensZone).dir == MesaLiftDir::None);
    // On the top station, which may count as the needles: still down.
    p = MesaLiftFor(1, -4660.0f, -1820.0f, 85.4f, ai::kThousandNeedlesZone, 1, -5409.9f, -2532.2f, -48.1f, ai::kThousandNeedlesZone);
    CHECK(p.dir == MesaLiftDir::Down && Named(p, "Great Lift"));
    // Freewind top -> needles floor: the Freewind lift, not the Great Lift.
    p = MesaLiftFor(1, -5441.9f, -2432.0f, 89.6f, ai::kThousandNeedlesZone, 1, -5409.9f, -2532.2f, -48.1f, ai::kThousandNeedlesZone);
    CHECK(p.dir == MesaLiftDir::Down && Named(p, "Freewind Post"));
    for (ai::LiftCar const& car : ai::kGreatLiftCars)
    {
        CHECK(std::fabs(ai::LiftLevelZ(car, true) - 85.41f) < 0.3f);
        CHECK(std::fabs(ai::LiftLevelZ(car, false) + 44.0f) < 0.3f);
    }

    // Other map with the same coordinates: never.
    CHECK(MesaLiftFor(0, -5356.7f, -2487.1f, -45.9f, 0, 0, -5441.9f, -2432.0f, 89.6f, 0).dir == MesaLiftDir::None);
    // Cross-map goal from the Freewind top: down (leave the mesa first).
    CHECK(MesaLiftFor(1, -5441.9f, -2432.0f, 89.6f, 0, 0, 1600.0f, 200.0f, -50.0f, 0).dir == MesaLiftDir::Down);

    if (failures)
    {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "test_mesa_lift_policy: all checks passed\n";
    return 0;
}
