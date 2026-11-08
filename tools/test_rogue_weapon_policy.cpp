#include "../ai/playerbot/policy/gear/RogueWeaponPolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::RogueSpecWantsDaggers;
using ai::RogueWeaponAllowed;

static constexpr uint32_t MACE = ROGUE_POLICY_WS_MACE;
static constexpr uint32_t SWORD = ROGUE_POLICY_WS_SWORD;
static constexpr uint32_t FIST = ROGUE_POLICY_WS_FIST;
static constexpr uint32_t DAGGER = ROGUE_POLICY_WS_DAGGER;
static constexpr uint32_t AXE = 0;
static constexpr uint32_t SWORD2 = 8;
static constexpr uint32_t BOW = 2;

int main()
{
    std::cout << "Starting TortoiseBots rogue weapon policy tests...\n";

    // 1. RogueSpecWantsDaggers helper:
    // Assassination and Subtlety need a dagger main hand (core refuses
    // Backstab/Ambush without one), so both specs allow daggers only.
    CHECK(RogueSpecWantsDaggers("assas") == true);
    CHECK(RogueSpecWantsDaggers("subtle") == true);
    CHECK(RogueSpecWantsDaggers("combat") == false);
    CHECK(RogueSpecWantsDaggers("") == false);
    CHECK(RogueSpecWantsDaggers("fury") == false);
    CHECK(RogueSpecWantsDaggers("prot") == false);
    std::cout << "  [PASS] assas/subtle want daggers\n";

    // 2. Table row 1: assassination / subtlety (daggers only)
    for (auto spec : {"assas", "subtle"})
    {
        // Allowed regardless of talent flags
        CHECK(RogueWeaponAllowed(spec, false, false, false, false, DAGGER) == true);
        CHECK(RogueWeaponAllowed(spec, true, false, false, false, DAGGER) == true);

        // All other weapon types refused
        CHECK(RogueWeaponAllowed(spec, false, false, false, false, SWORD) == false);
        CHECK(RogueWeaponAllowed(spec, false, false, false, false, MACE) == false);
        CHECK(RogueWeaponAllowed(spec, false, false, false, false, FIST) == false);
        CHECK(RogueWeaponAllowed(spec, false, false, false, false, AXE) == false);
        CHECK(RogueWeaponAllowed(spec, false, false, false, false, SWORD2) == false);
        CHECK(RogueWeaponAllowed(spec, false, false, false, false, BOW) == false);
    }
    std::cout << "  [PASS] assas/subtle allow daggers only, refuse swords/maces/fists/others\n";

    // 3. Table row 2: combat, no points in any weapon-specialization talent
    // (dagger, sword, mace, fist allowed - stat weight decides)
    CHECK(RogueWeaponAllowed("combat", false, false, false, false, DAGGER) == true);
    CHECK(RogueWeaponAllowed("combat", false, false, false, false, SWORD) == true);
    CHECK(RogueWeaponAllowed("combat", false, false, false, false, MACE) == true);
    CHECK(RogueWeaponAllowed("combat", false, false, false, false, FIST) == true);
    CHECK(RogueWeaponAllowed("combat", false, false, false, false, AXE) == false);
    CHECK(RogueWeaponAllowed("combat", false, false, false, false, SWORD2) == false);
    CHECK(RogueWeaponAllowed("combat", false, false, false, false, BOW) == false);
    std::cout << "  [PASS] combat without spec talents allows dagger, sword, mace, fist\n";

    // 4. Table row 3: combat with points in Sword Specialization
    CHECK(RogueWeaponAllowed("combat", true, false, false, false, SWORD) == true);
    CHECK(RogueWeaponAllowed("combat", true, false, false, false, DAGGER) == false);
    CHECK(RogueWeaponAllowed("combat", true, false, false, false, MACE) == false);
    CHECK(RogueWeaponAllowed("combat", true, false, false, false, FIST) == false);
    CHECK(RogueWeaponAllowed("combat", true, false, false, false, AXE) == false);
    std::cout << "  [PASS] combat with sword spec allows sword, refuses dagger/mace/fist\n";

    // 5. Table row 3: combat with points in Mace Specialization
    CHECK(RogueWeaponAllowed("combat", false, true, false, false, MACE) == true);
    CHECK(RogueWeaponAllowed("combat", false, true, false, false, SWORD) == false);
    CHECK(RogueWeaponAllowed("combat", false, true, false, false, DAGGER) == false);
    CHECK(RogueWeaponAllowed("combat", false, true, false, false, FIST) == false);
    std::cout << "  [PASS] combat with mace spec allows mace, refuses sword/dagger/fist\n";

    // 6. Table row 3: combat with points in Fist Weapon Specialization
    CHECK(RogueWeaponAllowed("combat", false, false, true, false, FIST) == true);
    CHECK(RogueWeaponAllowed("combat", false, false, true, false, SWORD) == false);
    CHECK(RogueWeaponAllowed("combat", false, false, true, false, MACE) == false);
    CHECK(RogueWeaponAllowed("combat", false, false, true, false, DAGGER) == false);
    std::cout << "  [PASS] combat with fist spec allows fist, refuses sword/mace/dagger\n";

    // 7. Table row 4: combat with points in Dagger Specialization
    CHECK(RogueWeaponAllowed("combat", false, false, false, true, DAGGER) == true);
    CHECK(RogueWeaponAllowed("combat", false, false, false, true, SWORD) == false);
    CHECK(RogueWeaponAllowed("combat", false, false, false, true, MACE) == false);
    CHECK(RogueWeaponAllowed("combat", false, false, false, true, FIST) == false);
    std::cout << "  [PASS] combat with dagger spec allows dagger, refuses sword/mace/fist\n";

    // 8. Union of multiple specializations
    // Sword + Dagger
    CHECK(RogueWeaponAllowed("combat", true, false, false, true, SWORD) == true);
    CHECK(RogueWeaponAllowed("combat", true, false, false, true, DAGGER) == true);
    CHECK(RogueWeaponAllowed("combat", true, false, false, true, MACE) == false);
    CHECK(RogueWeaponAllowed("combat", true, false, false, true, FIST) == false);

    // Sword + Mace
    CHECK(RogueWeaponAllowed("combat", true, true, false, false, SWORD) == true);
    CHECK(RogueWeaponAllowed("combat", true, true, false, false, MACE) == true);
    CHECK(RogueWeaponAllowed("combat", true, true, false, false, DAGGER) == false);
    CHECK(RogueWeaponAllowed("combat", true, true, false, false, FIST) == false);

    // Mace + Fist + Dagger
    CHECK(RogueWeaponAllowed("combat", false, true, true, true, MACE) == true);
    CHECK(RogueWeaponAllowed("combat", false, true, true, true, FIST) == true);
    CHECK(RogueWeaponAllowed("combat", false, true, true, true, DAGGER) == true);
    CHECK(RogueWeaponAllowed("combat", false, true, true, true, SWORD) == false);

    // All four
    CHECK(RogueWeaponAllowed("combat", true, true, true, true, SWORD) == true);
    CHECK(RogueWeaponAllowed("combat", true, true, true, true, MACE) == true);
    CHECK(RogueWeaponAllowed("combat", true, true, true, true, FIST) == true);
    CHECK(RogueWeaponAllowed("combat", true, true, true, true, DAGGER) == true);
    CHECK(RogueWeaponAllowed("combat", true, true, true, true, AXE) == false);
    std::cout << "  [PASS] combat with multiple specs allows union\n";

    // 9. Unknown/fallback spec
    CHECK(RogueWeaponAllowed("", false, false, false, false, DAGGER) == true);
    CHECK(RogueWeaponAllowed("", false, false, false, false, SWORD) == true);
    CHECK(RogueWeaponAllowed("", false, false, false, false, MACE) == true);
    CHECK(RogueWeaponAllowed("", false, false, false, false, FIST) == true);
    CHECK(RogueWeaponAllowed("", false, false, false, false, AXE) == false);
    std::cout << "  [PASS] fallback spec allows standard one-handers\n";

    std::cout << "All rogue weapon policy checks PASSED!\n";
    return 0;
}
