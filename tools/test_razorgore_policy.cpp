#include "../ai/playerbot/RazorgorePolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::IsRazorgoreUncontrolled;
using ai::kBlackDragonEggEntry;
using ai::kPossessSpellId;
using ai::kRazorgoreConeRadius;
using ai::kRazorgoreEntry;
using ai::kRazorgoreMeleeDistance;
using ai::kRazorgoreRangedDistance;
using ai::ShouldBackOffRazorgore;
using ai::ShouldEscapeRazorgoreCone;
using ai::ShouldHoldRazorgore;

int main()
{
    std::cout << "Starting TortoiseBots razorgore policy tests...\n";

    // Pinned ids and geometry: Razorgore 12435, Possess 19832, egg GO
    // 177807, 15y cone, 15y ranged hold, 3y melee.
    CHECK(kRazorgoreEntry == 12435);
    CHECK(kPossessSpellId == 19832);
    CHECK(kBlackDragonEggEntry == 177807);
    CHECK(kRazorgoreConeRadius == 15.0f);
    CHECK(kRazorgoreRangedDistance == 15.0f);
    CHECK(kRazorgoreMeleeDistance == 3.0f);
    std::cout << "  [PASS] ids and geometry pinned\n";

    // Uncontrolled phase: no Possess aura means the raid works the cone
    // escape and the off-tank holds.
    CHECK(IsRazorgoreUncontrolled(false));
    CHECK(!IsRazorgoreUncontrolled(true));
    std::cout << "  [PASS] uncontrolled phase rule\n";

    // Cone escape: non-victims inside step behind; the victim holds so the
    // boss does not rotate; anyone outside holds.
    CHECK(ShouldEscapeRazorgoreCone(false, true));
    CHECK(!ShouldEscapeRazorgoreCone(true, true));
    CHECK(!ShouldEscapeRazorgoreCone(false, false));
    std::cout << "  [PASS] cone escape rule\n";

    // Ranged back-off: ranged non-victims outside the cone but inside 15y
    // step out (War Stomp); melee, victims, and safe ranged hold.
    CHECK(ShouldBackOffRazorgore(true, false, false, true));
    CHECK(!ShouldBackOffRazorgore(false, false, false, true));
    CHECK(!ShouldBackOffRazorgore(true, true, false, true));
    CHECK(!ShouldBackOffRazorgore(true, false, true, true));
    CHECK(!ShouldBackOffRazorgore(true, false, false, false));
    std::cout << "  [PASS] ranged back-off rule\n";

    // Off-tank hold: only while eggs live and only the designated tank.
    CHECK(ShouldHoldRazorgore(true, true));
    CHECK(!ShouldHoldRazorgore(false, true));
    CHECK(!ShouldHoldRazorgore(true, false));
    std::cout << "  [PASS] off-tank hold rule\n";

    std::cout << "All razorgore policy tests passed.\n";
    return 0;
}
