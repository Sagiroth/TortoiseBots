#include "../ai/playerbot/GeddonBombPolicy.h"
#include <cassert>
#include <iostream>
using namespace ai;
int main()
{
    GeddonBombState s;
    s.Observe(true, 10, {0,0,0}, 0);
    assert(!s.Active()); // Opt-in only.
    s.Arm({0,30,0}, 9, 17);
    assert(s.InScope(409,9,17));
    assert(!s.InScope(249,9,17) && !s.InScope(409,10,17) && !s.InScope(409,9,18));
    // Melee carrier, including parked bots: the order is explicit, with no
    // stay/follow/class gate. The home location is captured before movement.
    s.Observe(true, 10, {4,0,0}, 100);
    assert(s.phase == GeddonBombPhase::Outbound && s.home.x == 4);
    assert(!GeddonBombAllowsAction("melee") && !GeddonBombAllowsAction("stay"));
    assert(!GeddonBombAllowsAction("reach melee") && !GeddonBombAllowsAction("fireball"));
    assert(GeddonBombAllowsAction("geddon bomb move"));
    s.waitingPoint = s.spot; s.hasWaitingPoint = true;
    s.Observe(true, 10, {0,29,0}, 7100);
    assert(s.phase == GeddonBombPhase::Waiting && s.home.x == 4);
    s.Observe(true, 10, {0,30,0}, 8100);
    assert(s.phase == GeddonBombPhase::Waiting); // Never return on a timer while aura remains.
    s.Observe(false, 0, {0,30,0}, 8200);
    assert(s.phase == GeddonBombPhase::Returning);
    s.Observe(true, 11, {0,20,0}, 8300);
    assert(s.phase == GeddonBombPhase::Outbound && s.home.x == 4);
    s.Observe(false, 0, {0,20,0}, 8400);
    s.Observe(false, 0, {4,0,0}, 9000);
    assert(!s.Active() && s.configured);
    s.Observe(true, 12, {5,0,0}, 10000);
    assert(s.home.x == 5); // Next bomb captures the new raid position.
    s.Observe(false, 0, {0,30,0}, 18000);
    s.Observe(false, 0, {0,30,0}, 48000);
    assert(!s.Active()); // A blocked return cannot suspend AI forever.
    s.Clear();
    assert(!s.configured && !s.Active());
    GeddonBombState nearby;
    nearby.Arm({0,30,0},9,17);
    nearby.Observe(true,20,{4,0,0},0);
    nearby.waitingPoint = {8,30,0}; nearby.hasWaitingPoint = true;
    nearby.Observe(true,20,{0,30,0},1000);
    assert(nearby.phase == GeddonBombPhase::Outbound); // Burning marker is not arrival.
    nearby.Observe(true,20,{8,30,0},2000);
    assert(nearby.phase == GeddonBombPhase::Waiting && nearby.spot.x == 0);
    nearby.Observe(true,21,{8,30,0},3000);
    assert(!nearby.hasWaitingPoint && nearby.phase == GeddonBombPhase::Outbound);
    std::cout << "Geddon bomb policy: PASS\n";
}
