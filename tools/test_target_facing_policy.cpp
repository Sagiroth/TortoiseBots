// Standalone check for ai/playerbot/policy/combat/TargetFacingPolicy.h:
//   g++ -std=c++17 -Wall -Wextra tools/test_target_facing_policy.cpp -o /tmp/test_target_facing_policy
//   /tmp/test_target_facing_policy
#include "../ai/playerbot/policy/combat/TargetFacingPolicy.h"

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
    std::cout << "Starting TortoiseBots target facing policy tests...\n";

    // gouge_from_behind_refused: Gouge (Attributes 0x150010) needs the target to face the caster. A rogue on the
    // `behind` strategy stands outside the mob's front arc, the core answers SPELL_FAILED_NOT_INFRONT on every probe
    // and broadcasts the failure (Tortoise Trials DPS-ROG-KICK: 0 Gouges landed in 4 reps). From the front it goes.
    CHECK(kTargetMustFaceCasterAttributes == 0x150010u);
    CHECK(TargetFacingRefuses(0x150010, false, false));
    CHECK(!TargetFacingRefuses(0x150010, false, true));

    // backstab_from_front_refused: a from-behind-only spell (Backstab) is refused while the target has the caster in
    // its front arc (SPELL_FAILED_NOT_BEHIND), and goes from behind.
    CHECK(TargetFacingRefuses(0, true, true));
    CHECK(!TargetFacingRefuses(0, true, false));
    // With attributes other than Gouge's only the behind rule decides.
    CHECK(TargetFacingRefuses(0x50010, true, true));
    CHECK(!TargetFacingRefuses(0x50010, true, false));

    // attributes_compared_whole: the core compares the whole field with ==. One extra bit (0x150011) is not refused
    // from behind, and a spell that merely shares Gouge's bits is not either.
    CHECK(!TargetFacingRefuses(0x150011, false, false));
    CHECK(!TargetFacingRefuses(0x150010 | 0x200, false, false));
    CHECK(!TargetFacingRefuses(0x10, false, false));

    // Any other spell is never refused for facing, front or back.
    CHECK(!TargetFacingRefuses(0, false, false));
    CHECK(!TargetFacingRefuses(0, false, true));

    std::cout << "target facing policy: all tests passed\n";
    return 0;
}
