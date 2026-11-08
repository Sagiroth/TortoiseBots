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

static constexpr uint32_t AXE = ROGUE_POLICY_WS_AXE;
static constexpr uint32_t MACE = ROGUE_POLICY_WS_MACE;
static constexpr uint32_t SWORD = ROGUE_POLICY_WS_SWORD;
static constexpr uint32_t FIST = ROGUE_POLICY_WS_FIST;
static constexpr uint32_t DAGGER = ROGUE_POLICY_WS_DAGGER;
static constexpr uint32_t SWORD2 = 8;
static constexpr uint32_t BOW = 2;

int main()
{
    std::cout << "Starting TortoiseBots rogue weapon policy tests (Turtle 1.18.1)...\n";

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

    // 2. Primary Turtle 1.18.1 API:
    // RogueWeaponAllowed(specName, hasHackAndSlash, hasCloseQuartersCombat, hasConcussiveBlows, subclass)

    // 2a. assas / subtle: daggers only in both hands
    for (auto spec : {"assas", "subtle"})
    {
        CHECK(RogueWeaponAllowed(spec, false, false, false, DAGGER) == true);
        CHECK(RogueWeaponAllowed(spec, true, true, true, DAGGER) == true);

        CHECK(RogueWeaponAllowed(spec, false, false, false, SWORD) == false);
        CHECK(RogueWeaponAllowed(spec, false, false, false, MACE) == false);
        CHECK(RogueWeaponAllowed(spec, false, false, false, FIST) == false);
        CHECK(RogueWeaponAllowed(spec, false, false, false, AXE) == false);
        CHECK(RogueWeaponAllowed(spec, false, false, false, SWORD2) == false);
        CHECK(RogueWeaponAllowed(spec, false, false, false, BOW) == false);
    }
    std::cout << "  [PASS] [Turtle 1.18.1] assas/subtle allow daggers only\n";

    // 2b. combat: no weapon preference talents
    // (dagger, sword, mace, fist, axe allowed - stat weight decides)
    CHECK(RogueWeaponAllowed("combat", false, false, false, DAGGER) == true);
    CHECK(RogueWeaponAllowed("combat", false, false, false, SWORD) == true);
    CHECK(RogueWeaponAllowed("combat", false, false, false, MACE) == true);
    CHECK(RogueWeaponAllowed("combat", false, false, false, FIST) == true);
    CHECK(RogueWeaponAllowed("combat", false, false, false, AXE) == true);
    CHECK(RogueWeaponAllowed("combat", false, false, false, SWORD2) == false);
    CHECK(RogueWeaponAllowed("combat", false, false, false, BOW) == false);
    std::cout << "  [PASS] [Turtle 1.18.1] combat without preference talents allows dagger, sword, mace, fist, axe\n";

    // 2c. combat: Hack and Slash (Talent 123 -> Axe + Sword)
    CHECK(RogueWeaponAllowed("combat", true, false, false, AXE) == true);
    CHECK(RogueWeaponAllowed("combat", true, false, false, SWORD) == true);
    CHECK(RogueWeaponAllowed("combat", true, false, false, DAGGER) == false);
    CHECK(RogueWeaponAllowed("combat", true, false, false, MACE) == false);
    CHECK(RogueWeaponAllowed("combat", true, false, false, FIST) == false);
    std::cout << "  [PASS] [Turtle 1.18.1] combat with Hack and Slash allows axe + sword, refuses others\n";

    // 2d. combat: Close Quarters Combat (Talent 121 -> Mace + Dagger + Fist)
    CHECK(RogueWeaponAllowed("combat", false, true, false, MACE) == true);
    CHECK(RogueWeaponAllowed("combat", false, true, false, DAGGER) == true);
    CHECK(RogueWeaponAllowed("combat", false, true, false, FIST) == true);
    CHECK(RogueWeaponAllowed("combat", false, true, false, SWORD) == false);
    CHECK(RogueWeaponAllowed("combat", false, true, false, AXE) == false);
    std::cout << "  [PASS] [Turtle 1.18.1] combat with Close Quarters Combat allows mace + dagger + fist, refuses sword/axe\n";

    // 2e. combat: Concussive Blows (Talent 119 -> Mace)
    CHECK(RogueWeaponAllowed("combat", false, false, true, MACE) == true);
    CHECK(RogueWeaponAllowed("combat", false, false, true, DAGGER) == false);
    CHECK(RogueWeaponAllowed("combat", false, false, true, SWORD) == false);
    CHECK(RogueWeaponAllowed("combat", false, false, true, FIST) == false);
    CHECK(RogueWeaponAllowed("combat", false, false, true, AXE) == false);
    std::cout << "  [PASS] [Turtle 1.18.1] combat with Concussive Blows allows mace, refuses others\n";

    // 2f. Unions of preference talents:
    // Hack and Slash + Close Quarters Combat -> Axe, Sword, Mace, Dagger, Fist (all 5)
    CHECK(RogueWeaponAllowed("combat", true, true, false, AXE) == true);
    CHECK(RogueWeaponAllowed("combat", true, true, false, SWORD) == true);
    CHECK(RogueWeaponAllowed("combat", true, true, false, MACE) == true);
    CHECK(RogueWeaponAllowed("combat", true, true, false, DAGGER) == true);
    CHECK(RogueWeaponAllowed("combat", true, true, false, FIST) == true);

    // Hack and Slash + Concussive Blows -> Axe, Sword, Mace
    CHECK(RogueWeaponAllowed("combat", true, false, true, AXE) == true);
    CHECK(RogueWeaponAllowed("combat", true, false, true, SWORD) == true);
    CHECK(RogueWeaponAllowed("combat", true, false, true, MACE) == true);
    CHECK(RogueWeaponAllowed("combat", true, false, true, DAGGER) == false);
    CHECK(RogueWeaponAllowed("combat", true, false, true, FIST) == false);

    // Close Quarters Combat + Concussive Blows -> Mace, Dagger, Fist
    CHECK(RogueWeaponAllowed("combat", false, true, true, MACE) == true);
    CHECK(RogueWeaponAllowed("combat", false, true, true, DAGGER) == true);
    CHECK(RogueWeaponAllowed("combat", false, true, true, FIST) == true);
    CHECK(RogueWeaponAllowed("combat", false, true, true, SWORD) == false);
    CHECK(RogueWeaponAllowed("combat", false, true, true, AXE) == false);

    // All three -> all 5
    CHECK(RogueWeaponAllowed("combat", true, true, true, AXE) == true);
    CHECK(RogueWeaponAllowed("combat", true, true, true, SWORD) == true);
    CHECK(RogueWeaponAllowed("combat", true, true, true, MACE) == true);
    CHECK(RogueWeaponAllowed("combat", true, true, true, DAGGER) == true);
    CHECK(RogueWeaponAllowed("combat", true, true, true, FIST) == true);
    std::cout << "  [PASS] [Turtle 1.18.1] combat with talent unions allows correct sets\n";

    // 2g. Unknown / fallback spec
    CHECK(RogueWeaponAllowed("", false, false, false, DAGGER) == true);
    CHECK(RogueWeaponAllowed("", false, false, false, SWORD) == true);
    CHECK(RogueWeaponAllowed("", false, false, false, MACE) == true);
    CHECK(RogueWeaponAllowed("", false, false, false, FIST) == true);
    CHECK(RogueWeaponAllowed("", false, false, false, AXE) == true);
    CHECK(RogueWeaponAllowed("", false, false, false, SWORD2) == false);
    CHECK(RogueWeaponAllowed("", false, false, false, BOW) == false);
    std::cout << "  [PASS] [Turtle 1.18.1] fallback spec allows all rogue one-handers\n";

    // 3. 4-boolean compatibility overload:
    // RogueWeaponAllowed(specName, hasSwordSpec, hasMaceSpec, hasFistSpec, hasDaggerSpec, subclass)
    for (auto spec : {"assas", "subtle"})
    {
        CHECK(RogueWeaponAllowed(spec, false, false, false, false, DAGGER) == true);
        CHECK(RogueWeaponAllowed(spec, true, true, true, true, DAGGER) == true);
        CHECK(RogueWeaponAllowed(spec, false, false, false, false, SWORD) == false);
    }

    CHECK(RogueWeaponAllowed("combat", false, false, false, false, DAGGER) == true);
    CHECK(RogueWeaponAllowed("combat", false, false, false, false, SWORD) == true);
    CHECK(RogueWeaponAllowed("combat", false, false, false, false, MACE) == true);
    CHECK(RogueWeaponAllowed("combat", false, false, false, false, FIST) == true);
    CHECK(RogueWeaponAllowed("combat", false, false, false, false, AXE) == true);

    CHECK(RogueWeaponAllowed("combat", true, false, false, false, SWORD) == true);
    CHECK(RogueWeaponAllowed("combat", true, false, false, false, AXE) == true);
    CHECK(RogueWeaponAllowed("combat", true, false, false, false, DAGGER) == false);

    CHECK(RogueWeaponAllowed("combat", false, true, false, false, MACE) == true);
    CHECK(RogueWeaponAllowed("combat", false, true, false, false, SWORD) == false);

    CHECK(RogueWeaponAllowed("combat", false, false, true, false, FIST) == true);
    CHECK(RogueWeaponAllowed("combat", false, false, true, false, SWORD) == false);

    CHECK(RogueWeaponAllowed("combat", false, false, false, true, DAGGER) == true);
    CHECK(RogueWeaponAllowed("combat", false, false, false, true, SWORD) == false);

    CHECK(RogueWeaponAllowed("combat", true, false, false, true, SWORD) == true);
    CHECK(RogueWeaponAllowed("combat", true, false, false, true, AXE) == true);
    CHECK(RogueWeaponAllowed("combat", true, false, false, true, DAGGER) == true);
    CHECK(RogueWeaponAllowed("combat", true, false, false, true, MACE) == false);
    std::cout << "  [PASS] 4-boolean compatibility overload behaves consistently\n";

    std::cout << "All rogue weapon policy checks PASSED!\n";
    return 0;
}
