// Standalone policy test for the mesa lift teleport: a bot below Freewind
// Post or Thunder Bluff with a destination on top goes up the nearest lift,
// a bot on top with a destination off it goes down, everything else walks.
// Mirrors ai/playerbot/policy/travel/MesaLiftPolicy.h; no server headers needed.
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra tools/test_mesa_lift_policy.cpp -o /tmp/test_mesa_lift_policy && /tmp/test_mesa_lift_policy

#include "../ai/playerbot/policy/travel/MesaLiftPolicy.h"

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
    ai::MesaLiftPick p = MesaLiftFor(1, -5356.7f, -2487.1f, -45.9f, 1, -5441.9f, -2432.0f, 89.6f);
    CHECK(p.dir == MesaLiftDir::Up && Named(p, "Freewind Post"));
    // Far away in the needles -> Freewind flight master: still up the lift.
    CHECK(MesaLiftFor(1, -5900.0f, -3900.0f, -58.0f, 1, -5407.1f, -2419.6f, 89.7f).dir == MesaLiftDir::Up);
    // Freewind top -> needles floor: down.
    p = MesaLiftFor(1, -5441.9f, -2432.0f, 89.6f, 1, -5409.9f, -2532.2f, -48.1f);
    CHECK(p.dir == MesaLiftDir::Down && Named(p, "Freewind Post"));
    // On top to on top, below to below: walk.
    CHECK(MesaLiftFor(1, -5441.9f, -2432.0f, 89.6f, 1, -5407.1f, -2419.6f, 89.7f).dir == MesaLiftDir::None);
    CHECK(MesaLiftFor(1, -5409.9f, -2532.2f, -48.1f, 1, -5900.0f, -3900.0f, -58.0f).dir == MesaLiftDir::None);

    // Mulgore west of Thunder Bluff -> city centre: up the west lift.
    p = MesaLiftFor(1, -1345.9f, 259.2f, 2.8f, 1, -1277.9f, 138.5f, 131.9f);
    CHECK(p.dir == MesaLiftDir::Up && Named(p, "Thunder Bluff west"));
    // From the south-east (Bloodhoof road side) -> Elder Rise-side NPC: east lift.
    p = MesaLiftFor(1, -983.0f, -75.0f, 20.4f, 1, -1090.2f, -15.4f, 140.7f);
    CHECK(p.dir == MesaLiftDir::Up && Named(p, "Thunder Bluff east"));
    // City -> Bloodhoof Village: down.
    CHECK(MesaLiftFor(1, -1277.9f, 138.5f, 131.9f, 1, -2350.0f, -350.0f, -9.0f).dir == MesaLiftDir::Down);
    // The lower lift station itself counts as below.
    CHECK(MesaLiftFor(1, -1330.2f, 193.4f, 60.2f, 1, -1277.9f, 138.5f, 131.9f).dir == MesaLiftDir::Up);

    // Every station: foot below its mesa top, top on it.
    for (ai::Mesa const& mesa : ai::kMesas)
        for (int i = 0; i < mesa.liftCount; ++i)
        {
            CHECK(!ai::OnMesa(mesa, 1, mesa.lifts[i].footX, mesa.lifts[i].footY, mesa.lifts[i].footZ));
            CHECK(ai::OnMesa(mesa, 1, mesa.lifts[i].topX, mesa.lifts[i].topY, mesa.lifts[i].topZ));
        }

    // Other maps, cross-map trips: never.
    CHECK(MesaLiftFor(0, -5356.7f, -2487.1f, -45.9f, 0, -5441.9f, -2432.0f, 89.6f).dir == MesaLiftDir::None);
    CHECK(MesaLiftFor(1, -5356.7f, -2487.1f, -45.9f, 0, -5441.9f, -2432.0f, 89.6f).dir == MesaLiftDir::None);

    if (failures)
    {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "test_mesa_lift_policy: all checks passed\n";
    return 0;
}
