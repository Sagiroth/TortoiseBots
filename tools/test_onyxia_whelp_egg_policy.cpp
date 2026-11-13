#include "../ai/playerbot/policy/raid/OnyxiaWhelpEggPolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using namespace ai;

int main()
{
    // Turtle 1.18.1 ids, not 3.3.5a.
    CHECK(kOnyxiaMapId == 249);
    CHECK(kOnyxianWhelpEntry == 11262);
    CHECK(kOnyxiaEggEntry == 176511);
    CHECK(kOnyxiaEggDangerRadius > kOnyxiaEggTriggerRadius);

    // Egg zone box: covers every egg spawn row, rejects the arena centre.
    CHECK(IsInOnyxiaEggZone(-32.0f, -151.4f));
    CHECK(IsInOnyxiaEggZone(-115.2f, -236.7f));
    CHECK(IsInOnyxiaEggZone(-33.0f, -266.3f));
    CHECK(IsInOnyxiaEggZone(-60.0f, -279.3f));
    CHECK(!IsInOnyxiaEggZone(0.0f, -210.0f));
    CHECK(!IsInOnyxiaEggZone(-75.0f, -210.0f - 100.0f));
    CHECK(!IsInOnyxiaEggZone(-200.0f, -210.0f));
    CHECK(!IsInOnyxiaEggZone(-60.0f, -100.0f));

    // Danger circle: inside radius + margin only.
    CHECK(IsInsideOnyxiaEggDanger(0.0f));
    CHECK(IsInsideOnyxiaEggDanger(11.9f));
    CHECK(!IsInsideOnyxiaEggDanger(12.0f));
    CHECK(!IsInsideOnyxiaEggDanger(30.0f));
    CHECK(OnyxiaDist2d(0.0f, 0.0f, 3.0f, 4.0f) > 4.99f && OnyxiaDist2d(0.0f, 0.0f, 3.0f, 4.0f) < 5.01f);

    // Avoid decision needs all four conditions.
    CHECK(ShouldAvoidOnyxiaEggs(true, true, true, true));
    CHECK(!ShouldAvoidOnyxiaEggs(false, true, true, true)); // toggle off
    CHECK(!ShouldAvoidOnyxiaEggs(true, false, true, true)); // other map
    CHECK(!ShouldAvoidOnyxiaEggs(true, true, false, true)); // outside the egg fields
    CHECK(!ShouldAvoidOnyxiaEggs(true, true, true, false)); // no egg close

    // Nearest exit: north cave mouth vs south cave mouth.
    CHECK(NearestOnyxiaEggExit(-45.0f, -165.0f) == 0);
    CHECK(NearestOnyxiaEggExit(-45.0f, -265.0f) == 1);
    CHECK(NearestOnyxiaEggExit(-100.0f, -150.0f) == 0);
    CHECK(NearestOnyxiaEggExit(-100.0f, -280.0f) == 1);

    // Whelp duty: AoE-capable ranged DPS only.
    CHECK(IsOnyxiaWhelpHunter(kWhelpClassMage, false, false, true));
    CHECK(IsOnyxiaWhelpHunter(kWhelpClassWarlock, false, false, true));
    CHECK(IsOnyxiaWhelpHunter(kWhelpClassHunter, false, false, true));
    CHECK(IsOnyxiaWhelpHunter(kWhelpClassShaman, false, false, true));
    CHECK(IsOnyxiaWhelpHunter(kWhelpClassDruid, false, false, true));
    CHECK(!IsOnyxiaWhelpHunter(kWhelpClassMage, true, false, true));   // tank flag
    CHECK(!IsOnyxiaWhelpHunter(kWhelpClassShaman, false, true, true)); // healer
    CHECK(!IsOnyxiaWhelpHunter(kWhelpClassShaman, false, false, false)); // enhancement
    CHECK(!IsOnyxiaWhelpHunter(5, false, false, true));  // priest: no AoE
    CHECK(!IsOnyxiaWhelpHunter(1, false, false, false)); // warrior
    CHECK(!IsOnyxiaWhelpHunter(4, false, false, false)); // rogue
    CHECK(!IsOnyxiaWhelpHunter(2, false, false, false)); // paladin

    // Pickable whelps: alive, within range, not within an egg's trap circle.
    CHECK(IsOnyxiaWhelpPickable(true, 10.0f, false));
    CHECK(IsOnyxiaWhelpPickable(true, kOnyxiaWhelpEngageRange, false));
    CHECK(!IsOnyxiaWhelpPickable(false, 10.0f, false));
    CHECK(!IsOnyxiaWhelpPickable(true, kOnyxiaWhelpEngageRange + 1.0f, false));
    CHECK(!IsOnyxiaWhelpPickable(true, 10.0f, true));

    // Whelp vs the egg list (real 176511 spawn rows, north cave mouth and
    // south cave mouth eggs plus one deep in the west tunnel).
    const OnyxiaPoint eggs[] = {
        { -32.5829f, -162.944f }, { -37.5972f, -167.346f }, { -43.2843f, -164.474f },
        { -38.458f, -262.433f },  { -33.0413f, -266.323f }, { -100.0f, -215.0f },
    };
    const std::size_t n = sizeof(eggs) / sizeof(eggs[0]);
    // Core whelp spawn points (boss_onyxia.cpp afSpawnLocations): both lie
    // inside the padded egg-field box but 11.5 and 11.9 yd from the nearest
    // real egg, outside the 10 yd trap circle.
    CHECK(IsInOnyxiaEggZone(-30.127f, -254.463f) && IsInOnyxiaEggZone(-30.817f, -177.106f));
    CHECK(!IsNearAnyOnyxiaEgg(-30.127f, -254.463f, eggs, n));
    CHECK(!IsNearAnyOnyxiaEgg(-30.817f, -177.106f, eggs, n));
    CHECK(IsOnyxiaWhelpPickable(true, 20.0f, IsNearAnyOnyxiaEgg(-30.127f, -254.463f, eggs, n)));
    CHECK(IsOnyxiaWhelpPickable(true, 20.0f, IsNearAnyOnyxiaEgg(-30.817f, -177.106f, eggs, n)));
    // Phase 2 departure point.
    CHECK(!IsNearAnyOnyxiaEgg(-57.8f, -215.6f, eggs, n));
    CHECK(IsOnyxiaWhelpPickable(true, 20.0f, IsNearAnyOnyxiaEgg(-60.0f, -215.0f, eggs, n)));
    // A whelp inside an egg's circle is skipped; the edge is exclusive.
    CHECK(IsNearAnyOnyxiaEgg(-36.0f, -165.0f, eggs, n));
    CHECK(!IsOnyxiaWhelpPickable(true, 20.0f, IsNearAnyOnyxiaEgg(-36.0f, -165.0f, eggs, n)));
    CHECK(IsNearAnyOnyxiaEgg(-100.0f, -205.5f, eggs, n));
    CHECK(!IsNearAnyOnyxiaEgg(-100.0f, -205.0f, eggs, n));
    // No eggs found: nothing is skipped.
    CHECK(!IsNearAnyOnyxiaEgg(-36.0f, -165.0f, eggs, 0));

    // Hunt decision.
    CHECK(ShouldHuntOnyxiaWhelps(true, true, true, true, false, false, true));
    CHECK(!ShouldHuntOnyxiaWhelps(false, true, true, true, false, false, true)); // toggle off
    CHECK(!ShouldHuntOnyxiaWhelps(true, false, true, true, false, false, true)); // other map
    CHECK(!ShouldHuntOnyxiaWhelps(true, true, false, true, false, false, true)); // grounded phase
    CHECK(!ShouldHuntOnyxiaWhelps(true, true, true, false, false, false, true)); // not on duty
    CHECK(!ShouldHuntOnyxiaWhelps(true, true, true, true, true, false, true));   // explicit order
    CHECK(!ShouldHuntOnyxiaWhelps(true, true, true, true, false, true, true));   // already on a whelp
    CHECK(!ShouldHuntOnyxiaWhelps(true, true, true, true, false, false, false)); // no whelp near

    // Hold decision.
    CHECK(ShouldHoldOnyxiaWhelpTarget(true, true, true, true, false, true));
    CHECK(!ShouldHoldOnyxiaWhelpTarget(false, true, true, true, false, true));
    CHECK(!ShouldHoldOnyxiaWhelpTarget(true, true, false, true, false, true)); // phase 3 grounded
    CHECK(!ShouldHoldOnyxiaWhelpTarget(true, true, true, false, false, true));
    CHECK(!ShouldHoldOnyxiaWhelpTarget(true, true, true, true, true, true));
    CHECK(!ShouldHoldOnyxiaWhelpTarget(true, true, true, true, false, false));

    // OnyxiaWhelpDutyFor_States: the add crew switches the whelp duty only once the row's switch unit is dead: state 1
    // takes it, state 2 leaves it, anything else (the key off, no row, the switch unit alive) keeps today's answer.
    CHECK(OnyxiaWhelpDutyFor(true, 1));
    CHECK(OnyxiaWhelpDutyFor(false, 1)); // a tank or a melee bot joins the crew: today's false becomes true
    CHECK(!OnyxiaWhelpDutyFor(true, 2));
    CHECK(!OnyxiaWhelpDutyFor(false, 2));
    CHECK(OnyxiaWhelpDutyFor(true, 0));
    CHECK(!OnyxiaWhelpDutyFor(false, 0));
    CHECK(OnyxiaWhelpDutyFor(true, 3)); // an unknown state is no crew state: today's duty
    CHECK(!OnyxiaWhelpDutyFor(false, 3));
    CHECK(OnyxiaWhelpDutyFor(IsOnyxiaWhelpHunter(kWhelpClassMage, false, false, true), 1));
    CHECK(!OnyxiaWhelpDutyFor(IsOnyxiaWhelpHunter(kWhelpClassMage, false, false, true), 2));

    // ShouldReturnToOnyxia_Gates: a ranged damage dealer off the crew leaves a whelp for the hovering boss; every gate
    // flips it off.
    CHECK(ShouldReturnToOnyxia(true, 2, true, true, true, false, true));
    CHECK(!ShouldReturnToOnyxia(false, 2, true, true, true, false, true));  // key off
    CHECK(!ShouldReturnToOnyxia(true, 0, true, true, true, false, true));   // the switch unit still lives
    CHECK(!ShouldReturnToOnyxia(true, 1, true, true, true, false, true));   // on the crew: the whelp is its
    CHECK(!ShouldReturnToOnyxia(true, 2, false, true, true, false, true));  // not a ranged damage dealer
    CHECK(!ShouldReturnToOnyxia(true, 2, true, false, true, false, true));  // she is grounded
    CHECK(!ShouldReturnToOnyxia(true, 2, true, true, false, false, true));  // not on a whelp
    CHECK(!ShouldReturnToOnyxia(true, 2, true, true, true, true, true));    // an explicit order wins
    CHECK(!ShouldReturnToOnyxia(true, 2, true, true, true, false, false));  // no live, valid boss

    std::cout << "onyxia whelp/egg policy: OK\n";
    return 0;
}
