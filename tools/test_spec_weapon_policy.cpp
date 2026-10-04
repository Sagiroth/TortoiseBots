#include "../ai/playerbot/strategy/values/SpecWeaponPolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

// Subclass literals mirrored from core ItemPrototype.h (see the policy header).
static uint32_t const WAXE = 0, WAXE2 = 1, WBOW = 2, WGUN = 3, WMACE = 4, WMACE2 = 5,
    WPOLE = 6, WSWORD = 7, WSWORD2 = 8, WFIST = 13, WDAG = 15, WXBOW = 18;
static uint32_t const WEAPON = 2, ARMOR = 4, MISC = 0, SHIELD = 6, LIBRAM = 7;
static uint32_t const WAR = 1, PAL = 2, ROGUE = 4;
static uint32_t const MH = 15, OH = 16, RH = 17;

using ai::SpecUsesShieldOffHand;
using ai::SpecWeaponAllowed;
using ai::VendorShieldRank;

int main()
{
    std::cout << "Starting TortoiseBots spec weapon policy tests...\n";

    // Owner weapon matrix: protection (warrior + paladin) is 1H main hand,
    // shield off hand. The off-hand line is the ping-pong guard: no weapon
    // subclass may ever read as off-hand-legal for a shield spec.
    CHECK(SpecWeaponAllowed(WAR, "prot", WEAPON, WSWORD, MH, true) == true);
    CHECK(SpecWeaponAllowed(WAR, "prot", WEAPON, WDAG, MH, true) == true);
    CHECK(SpecWeaponAllowed(WAR, "prot", ARMOR, SHIELD, OH, true) == true);
    for (uint32_t w : {WAXE, WMACE, WSWORD, WDAG, WFIST, WSWORD2, WAXE2, WMACE2, WPOLE})
        CHECK(SpecWeaponAllowed(WAR, "prot", WEAPON, w, OH, true) == false);
    CHECK(SpecWeaponAllowed(WAR, "prot", ARMOR, MISC, OH, true) == false);
    CHECK(SpecWeaponAllowed(PAL, "prot", WEAPON, WSWORD, MH, true) == true);
    CHECK(SpecWeaponAllowed(PAL, "prot", ARMOR, SHIELD, OH, true) == true);
    CHECK(SpecWeaponAllowed(PAL, "prot", WEAPON, WSWORD, OH, true) == false);
    CHECK(SpecWeaponAllowed(PAL, "prot", WEAPON, WSWORD2, MH, true) == false);
    CHECK(SpecWeaponAllowed(PAL, "holy", WEAPON, WMACE, MH, true) == true);
    CHECK(SpecWeaponAllowed(PAL, "holy", ARMOR, SHIELD, OH, true) == true);
    CHECK(SpecWeaponAllowed(PAL, "holy", ARMOR, MISC, OH, true) == true);
    CHECK(SpecWeaponAllowed(PAL, "holy", WEAPON, WSWORD, OH, true) == false);
    std::cout << "  [PASS] protection/holy off hand admits shields only\n";

    // The ping-pong cycle end to end: the bag 1H weapon a prot warrior
    // carries reads as main-hand-legal but off-hand-forbidden, so the audit
    // compares it against the main hand (never the shield) and the shield
    // stays once equipped.
    CHECK(SpecWeaponAllowed(WAR, "prot", WEAPON, WSWORD, MH, true) == true);
    CHECK(SpecWeaponAllowed(WAR, "prot", WEAPON, WSWORD, OH, true) == false);
    CHECK(SpecWeaponAllowed(PAL, "prot", WEAPON, WAXE, MH, true) == true);
    CHECK(SpecWeaponAllowed(PAL, "prot", WEAPON, WAXE, OH, true) == false);
    std::cout << "  [PASS] ping-pong reverse guard: bag 1H never targets the shield slot\n";

    // Damage specs keep their 2H preference (never a shield).
    CHECK(SpecWeaponAllowed(WAR, "arms", WEAPON, WSWORD2, MH, true) == true);
    CHECK(SpecWeaponAllowed(WAR, "arms", WEAPON, WSWORD, MH, true) == false);
    CHECK(SpecWeaponAllowed(PAL, "retrib", WEAPON, WSWORD2, MH, true) == true);
    CHECK(SpecWeaponAllowed(PAL, "retrib", WEAPON, WSWORD, MH, true) == false);
    CHECK(SpecWeaponAllowed(PAL, "retrib", ARMOR, SHIELD, OH, true) == false);
    std::cout << "  [PASS] arms/retribution stay 2H-only\n";

    // Fury: 1H pair once Dual Wield is learned, 2H stand-in before that.
    CHECK(SpecWeaponAllowed(WAR, "fury", WEAPON, WSWORD, MH, true) == true);
    CHECK(SpecWeaponAllowed(WAR, "fury", WEAPON, WSWORD2, MH, true) == false);
    CHECK(SpecWeaponAllowed(WAR, "fury", WEAPON, WSWORD, OH, true) == true);
    CHECK(SpecWeaponAllowed(WAR, "fury", WEAPON, WSWORD2, OH, true) == false);
    CHECK(SpecWeaponAllowed(WAR, "fury", WEAPON, WSWORD2, MH, false) == true);
    CHECK(SpecWeaponAllowed(WAR, "fury", WEAPON, WPOLE, MH, false) == true);
    std::cout << "  [PASS] fury dual-wields from Dual Wield, 2H stand-in before\n";

    // Shield-off-hand detection feeds the audit and the vendor pass.
    CHECK(SpecUsesShieldOffHand(WAR, "prot") == true);
    CHECK(SpecUsesShieldOffHand(PAL, "prot") == true);
    CHECK(SpecUsesShieldOffHand(PAL, "holy") == true);
    CHECK(SpecUsesShieldOffHand(WAR, "arms") == false);
    CHECK(SpecUsesShieldOffHand(WAR, "fury") == false);
    CHECK(SpecUsesShieldOffHand(PAL, "retrib") == false);
    CHECK(SpecUsesShieldOffHand(ROGUE, "combat") == false);
    std::cout << "  [PASS] shield-spec detection\n";

    // Vendor shield-first: a shieldless shield-bot ranks the shield before
    // any weapon upgrade; without the need the order is untouched.
    CHECK(VendorShieldRank(true, ARMOR, SHIELD) == 0);
    CHECK(VendorShieldRank(true, WEAPON, WSWORD) == 1);
    CHECK(VendorShieldRank(true, WEAPON, WSWORD2) == 1);
    CHECK(VendorShieldRank(false, ARMOR, SHIELD) == 1);
    CHECK(VendorShieldRank(false, WEAPON, WSWORD) == 1);
    CHECK(VendorShieldRank(true, ARMOR, MISC) == 1);
    std::cout << "  [PASS] shieldless shield-bot ranks shields first\n";

    std::cout << "All spec weapon policy checks PASSED!\n";
    return 0;
}
