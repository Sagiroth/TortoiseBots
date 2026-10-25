#include "../ai/playerbot/RaidTankAssignmentPolicy.h"
#include <cassert>
#include <iostream>

int main()
{
    using namespace ai;
    auto plan = PlanGolemaggTanks({{10, true, false}, {20, false, true}}, 100, {201, 200});
    assert(plan.bossTank == 10);
    assert(plan.addOwners.size() == 2);
    assert(plan.addOwners[0] == std::make_pair(std::uint64_t(200), std::uint64_t(20)));
    assert(plan.addOwners[1].second == 20);
    // Independent input order produces identical, non-converging add ownership.
    auto first = PlanGolemaggTanks({{30, false, true}, {10, true, false}, {20, false, true}}, 100, {201, 200});
    auto second = PlanGolemaggTanks({{20, false, true}, {30, false, true}, {10, true, false}}, 100, {200, 201});
    assert(first.bossTank == second.bossTank && first.addOwners == second.addOwners);
    assert(first.addOwners[0].second == 20 && first.addOwners[1].second == 30);
    // Missing/dead boss tank: promote one add tank, leave the other on both adds.
    plan = PlanGolemaggTanks({{30, false, true}, {20, false, true}}, 100, {200, 201});
    assert(plan.bossTank == 20 && plan.addOwners[0].second == 30 && plan.addOwners[1].second == 30);
    plan = PlanGolemaggTanks({{20, false, true}}, 100, {200, 201});
    assert(plan.bossTank == 20 && plan.addOwners[0].second == 0 && plan.addOwners[1].second == 0);
    // A manual boss holder keeps the boss; the automatic boss candidate stands by.
    plan = PlanGolemaggTanks({{10, true, false}, {20, false, true}, {50, true, false, 100}}, 100, {200});
    assert(plan.bossTank == 50 && plan.addOwners[0].second == 20);
    // Explicit add orders reserve that add, without duplicating automatic pickups.
    plan = PlanGolemaggTanks({{10, true, false}, {20, false, true, 201}, {30, false, true}}, 100, {201, 200, 200});
    assert(plan.addOwners.size() == 2 && plan.addOwners[0].second == 30 && plan.addOwners[1].second == 20);
    plan = PlanGolemaggTanks({{10, true, false, 201}, {20, false, true}, {30, false, true}}, 100, {200, 201});
    assert(plan.bossTank == 20 && plan.addOwners[0].second == 30 && plan.addOwners[1].second == 10);
    plan = PlanGolemaggTanks({}, 100, {200});
    assert(plan.bossTank == 0 && plan.addOwners[0].second == 0);
    std::cout << "Raid tank assignment policy: passed\n";
}
