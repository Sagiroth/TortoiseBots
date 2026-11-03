#include "../ai/playerbot/FankrissTankPolicy.h"
#include "../ai/playerbot/policy/raid/FankrissRaidPolicy.h"
#include "../ai/playerbot/EbonrocSwapPolicy.h"
#include <cassert>
#include <cmath>
#include <iostream>

static bool Near(float a, float b) { return std::fabs(a - b) < 0.01f; }

int main()
{
    using namespace ai;

    // Mortal Wound swap: only a wound-free volunteer, at the threshold.
    assert(!FankrissNeedsSwap(4, 0, 5, true));
    assert(FankrissNeedsSwap(5, 0, 5, true));
    assert(!FankrissNeedsSwap(7, 1, 5, true));
    // Fankriss on a non-tank: any wound-free assigned tank takes him at once.
    assert(FankrissNeedsSwap(0, 0, 5, false));
    assert(!FankrissNeedsSwap(0, 2, 5, false));

    // One volunteer per hand-off, retried after 2 s.
    FankrissSwapWindow window;
    assert(window.Ready(1000));
    assert(!window.Claim(1, 2, 1000));   // caller is not the chosen tank
    assert(!window.Claim(1, 0, 1000));   // nobody chosen
    assert(window.Claim(2, 2, 1000));
    assert(!window.Claim(3, 3, 2500));   // a second volunteer inside the window
    assert(window.Claim(3, 3, 3000));

    // Heal search widens after a web, never shrinks.
    assert(Near(FankrissHealingSearchRange(80.0f, false), 80.0f));
    assert(Near(FankrissHealingSearchRange(80.0f, true), 200.0f));
    assert(Near(FankrissHealingSearchRange(250.0f, true), 250.0f));

    // Rear and raid slots lie behind the boss's facing.
    FankrissRearPoint rear = FankrissRear(0.0f, 0.0f, 0.0f, 10.0f);
    assert(Near(rear.x, -7.0f) && Near(rear.y, 0.0f));
    FankrissRearPoint melee = FankrissRaidSlot(0.0f, 0.0f, 0.0f, 20.0f, false);
    assert(Near(melee.x, -7.0f));   // capped at 7 yd
    FankrissRearPoint hunter = FankrissRaidSlot(0.0f, 0.0f, 0.0f, 5.0f, true);
    assert(Near(hunter.x, -17.0f)); // outside the dead zone
    FankrissRearPoint centre = FankrissCenterTank(5.0f);
    assert(Near(centre.x, -8074.88f + 4.5f) && Near(centre.y, 1193.64f));
    assert(Near(FankrissCenterTank(0.5f).x, -8074.88f + 1.0f));

    assert(FankrissSpawnInRadius(29.0f, 0.0f, 0.0f, 0.0f));
    assert(!FankrissSpawnInRadius(31.0f, 0.0f, 0.0f, 0.0f));

    // Melee step toward a Spawn by at most 3 yd, never around to the front.
    FankrissRearPoint behind{-7.0f, 0.0f};
    FankrissRearPoint step = FankrissStackEdge(behind, -7.0f, 10.0f, 0.0f, 0.0f, 0.0f, 2.0f);
    assert(Near(step.x, -7.0f) && Near(step.y, 3.0f));
    FankrissRearPoint front = FankrissStackEdge(behind, 10.0f, 0.0f, 0.0f, 0.0f, 0.0f, 2.0f);
    assert(Near(front.x, -4.0f) && Near(front.y, 0.0f));
    FankrissRearPoint across = FankrissStackEdge(FankrissRearPoint{-1.5f, 0.0f}, 10.0f, 0.0f, 0.0f, 0.0f, 0.0f, 2.0f);
    assert(Near(across.x, -1.5f) && Near(across.y, 0.0f));

    assert(FankrissAtRear(-7.0f, 1.0f, behind, true, true));
    assert(!FankrissAtRear(-7.0f, 1.0f, behind, false, true));
    assert(!FankrissAtRear(-7.0f, 3.0f, behind, true, true));

    // The stack role blocks ordinary movement and AoE; the healing role lets a
    // healer walk only when it cannot heal from where it stands.
    assert(FankrissMovementBlocked("follow", true, false, false));
    assert(FankrissMovementBlocked("dps aoe", true, false, false));
    assert(!FankrissMovementBlocked("shoot", true, false, false));
    assert(!FankrissMovementBlocked("follow", false, false, false));
    assert(FankrissMovementBlocked("reach party member to heal", false, true, true));
    assert(!FankrissMovementBlocked("reach party member to heal", false, true, false));
    assert(FankrissMovementBlocked("reach melee", false, true, false));

    assert(EbonrocTauntAction("taunt") && EbonrocTauntAction("growl") && EbonrocTauntAction("challenging shout"));
    assert(!EbonrocTauntAction("sunder armor"));

    std::cout << "fankriss policy PASS\n";
}
