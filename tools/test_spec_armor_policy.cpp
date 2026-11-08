#include "../ai/playerbot/policy/gear/SpecArmorPolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::IsArmorExemptFromMaterialGate;
using ai::SpecAllowsHoldable;
using ai::SpecAllowsShield;
using ai::SpecArmorSubClassAllowed;

int main()
{
    std::cout << "Starting TortoiseBots spec armor policy tests...\n";

    // 1. Non-body armor material bypass: rings, necks, trinkets, cloaks, tabards, shirts.
    CHECK(IsArmorExemptFromMaterialGate(SPEC_ARMOR_INVTYPE_FINGER) == true);
    CHECK(IsArmorExemptFromMaterialGate(SPEC_ARMOR_INVTYPE_NECK) == true);
    CHECK(IsArmorExemptFromMaterialGate(SPEC_ARMOR_INVTYPE_TRINKET) == true);
    CHECK(IsArmorExemptFromMaterialGate(SPEC_ARMOR_INVTYPE_CLOAK) == true);
    CHECK(IsArmorExemptFromMaterialGate(SPEC_ARMOR_INVTYPE_TABARD) == true);
    CHECK(IsArmorExemptFromMaterialGate(SPEC_ARMOR_INVTYPE_BODY) == true);

    // Body armor slots, holdables, shields, relics are NOT exempt.
    CHECK(IsArmorExemptFromMaterialGate(SPEC_ARMOR_INVTYPE_HEAD) == false);
    CHECK(IsArmorExemptFromMaterialGate(SPEC_ARMOR_INVTYPE_SHOULDERS) == false);
    CHECK(IsArmorExemptFromMaterialGate(SPEC_ARMOR_INVTYPE_CHEST) == false);
    CHECK(IsArmorExemptFromMaterialGate(SPEC_ARMOR_INVTYPE_ROBE) == false);
    CHECK(IsArmorExemptFromMaterialGate(SPEC_ARMOR_INVTYPE_WAIST) == false);
    CHECK(IsArmorExemptFromMaterialGate(SPEC_ARMOR_INVTYPE_LEGS) == false);
    CHECK(IsArmorExemptFromMaterialGate(SPEC_ARMOR_INVTYPE_FEET) == false);
    CHECK(IsArmorExemptFromMaterialGate(SPEC_ARMOR_INVTYPE_WRISTS) == false);
    CHECK(IsArmorExemptFromMaterialGate(SPEC_ARMOR_INVTYPE_HANDS) == false);
    CHECK(IsArmorExemptFromMaterialGate(SPEC_ARMOR_INVTYPE_HOLDABLE) == false);
    CHECK(IsArmorExemptFromMaterialGate(SPEC_ARMOR_INVTYPE_SHIELD) == false);
    CHECK(IsArmorExemptFromMaterialGate(SPEC_ARMOR_INVTYPE_RELIC) == false);
    std::cout << "  [PASS] non-body armor material exemption\n";

    // 2. Held-in-hand off-hands (INVTYPE_HOLDABLE):
    // Allowed for casters/healers.
    CHECK(SpecAllowsHoldable(SPEC_ARMOR_CLASS_PRIEST, "holy") == true);
    CHECK(SpecAllowsHoldable(SPEC_ARMOR_CLASS_PRIEST, "disc") == true);
    CHECK(SpecAllowsHoldable(SPEC_ARMOR_CLASS_PRIEST, "shadow") == true);
    CHECK(SpecAllowsHoldable(SPEC_ARMOR_CLASS_MAGE, "fire") == true);
    CHECK(SpecAllowsHoldable(SPEC_ARMOR_CLASS_MAGE, "frost") == true);
    CHECK(SpecAllowsHoldable(SPEC_ARMOR_CLASS_MAGE, "arcane") == true);
    CHECK(SpecAllowsHoldable(SPEC_ARMOR_CLASS_WARLOCK, "affliction") == true);
    CHECK(SpecAllowsHoldable(SPEC_ARMOR_CLASS_WARLOCK, "destro") == true);
    CHECK(SpecAllowsHoldable(SPEC_ARMOR_CLASS_DRUID, "resto") == true);
    CHECK(SpecAllowsHoldable(SPEC_ARMOR_CLASS_DRUID, "balance") == true);
    CHECK(SpecAllowsHoldable(SPEC_ARMOR_CLASS_SHAMAN, "resto") == true);
    CHECK(SpecAllowsHoldable(SPEC_ARMOR_CLASS_SHAMAN, "elem") == true);
    CHECK(SpecAllowsHoldable(SPEC_ARMOR_CLASS_PALADIN, "holy") == true);

    // Refused for melee classes and specs.
    CHECK(SpecAllowsHoldable(SPEC_ARMOR_CLASS_WARRIOR, "prot") == false);
    CHECK(SpecAllowsHoldable(SPEC_ARMOR_CLASS_WARRIOR, "arms") == false);
    CHECK(SpecAllowsHoldable(SPEC_ARMOR_CLASS_WARRIOR, "fury") == false);
    CHECK(SpecAllowsHoldable(SPEC_ARMOR_CLASS_HUNTER, "beastmaster") == false);
    CHECK(SpecAllowsHoldable(SPEC_ARMOR_CLASS_HUNTER, "marksman") == false);
    CHECK(SpecAllowsHoldable(SPEC_ARMOR_CLASS_ROGUE, "combat") == false);
    CHECK(SpecAllowsHoldable(SPEC_ARMOR_CLASS_ROGUE, "assas") == false);
    CHECK(SpecAllowsHoldable(SPEC_ARMOR_CLASS_DRUID, "feraltank") == false);
    CHECK(SpecAllowsHoldable(SPEC_ARMOR_CLASS_DRUID, "feraldps") == false);
    CHECK(SpecAllowsHoldable(SPEC_ARMOR_CLASS_SHAMAN, "enhance") == false);
    CHECK(SpecAllowsHoldable(SPEC_ARMOR_CLASS_SHAMAN, "shamantank") == false);
    CHECK(SpecAllowsHoldable(SPEC_ARMOR_CLASS_PALADIN, "prot") == false);
    CHECK(SpecAllowsHoldable(SPEC_ARMOR_CLASS_PALADIN, "retrib") == false);
    std::cout << "  [PASS] held-in-hand off-hand permissions and refusals\n";

    // 3. Shields:
    // Allowed for specs whose weapon policy uses a shield off hand.
    CHECK(SpecAllowsShield(SPEC_ARMOR_CLASS_WARRIOR, "prot") == true);
    CHECK(SpecAllowsShield(SPEC_ARMOR_CLASS_PALADIN, "prot") == true);
    CHECK(SpecAllowsShield(SPEC_ARMOR_CLASS_PALADIN, "holy") == true);
    CHECK(SpecAllowsShield(SPEC_ARMOR_CLASS_SHAMAN, "shamantank") == true);
    CHECK(SpecAllowsShield(SPEC_ARMOR_CLASS_SHAMAN, "resto") == true);
    CHECK(SpecAllowsShield(SPEC_ARMOR_CLASS_SHAMAN, "elem") == true);

    // Refused for non-shield specs.
    CHECK(SpecAllowsShield(SPEC_ARMOR_CLASS_WARRIOR, "arms") == false);
    CHECK(SpecAllowsShield(SPEC_ARMOR_CLASS_WARRIOR, "fury") == false);
    CHECK(SpecAllowsShield(SPEC_ARMOR_CLASS_PALADIN, "retrib") == false);
    CHECK(SpecAllowsShield(SPEC_ARMOR_CLASS_SHAMAN, "enhance") == true);
    CHECK(SpecAllowsShield(SPEC_ARMOR_CLASS_HUNTER, "marksman") == false);
    CHECK(SpecAllowsShield(SPEC_ARMOR_CLASS_ROGUE, "combat") == false);
    CHECK(SpecAllowsShield(SPEC_ARMOR_CLASS_DRUID, "resto") == false);
    CHECK(SpecAllowsShield(SPEC_ARMOR_CLASS_PRIEST, "holy") == false);
    CHECK(SpecAllowsShield(SPEC_ARMOR_CLASS_MAGE, "frost") == false);
    CHECK(SpecAllowsShield(SPEC_ARMOR_CLASS_WARLOCK, "affliction") == false);
    std::cout << "  [PASS] shield permissions and refusals\n";

    // 4. SpecArmorSubClassAllowed end-to-end integration:
    // Rings, necks, cloaks bypass material checks regardless of class/spec.
    CHECK(SpecArmorSubClassAllowed(SPEC_ARMOR_CLASS_WARRIOR, "prot", SPEC_ARMOR_INVTYPE_CLOAK, SPEC_ARMOR_AS_CLOTH) == true);
    CHECK(SpecArmorSubClassAllowed(SPEC_ARMOR_CLASS_WARRIOR, "prot", SPEC_ARMOR_INVTYPE_FINGER, SPEC_ARMOR_AS_MISC) == true);
    CHECK(SpecArmorSubClassAllowed(SPEC_ARMOR_CLASS_WARRIOR, "prot", SPEC_ARMOR_INVTYPE_NECK, SPEC_ARMOR_AS_MISC) == true);
    CHECK(SpecArmorSubClassAllowed(SPEC_ARMOR_CLASS_WARRIOR, "prot", SPEC_ARMOR_INVTYPE_TRINKET, SPEC_ARMOR_AS_MISC) == true);
    CHECK(SpecArmorSubClassAllowed(SPEC_ARMOR_CLASS_PALADIN, "retrib", SPEC_ARMOR_INVTYPE_CLOAK, SPEC_ARMOR_AS_CLOTH) == true);
    CHECK(SpecArmorSubClassAllowed(SPEC_ARMOR_CLASS_SHAMAN, "enhance", SPEC_ARMOR_INVTYPE_CLOAK, SPEC_ARMOR_AS_CLOTH) == true);
    CHECK(SpecArmorSubClassAllowed(SPEC_ARMOR_CLASS_DRUID, "feraltank", SPEC_ARMOR_INVTYPE_CLOAK, SPEC_ARMOR_AS_CLOTH) == true);

    // Shields:
    CHECK(SpecArmorSubClassAllowed(SPEC_ARMOR_CLASS_WARRIOR, "prot", SPEC_ARMOR_INVTYPE_SHIELD, SPEC_ARMOR_AS_SHIELD) == true);
    CHECK(SpecArmorSubClassAllowed(SPEC_ARMOR_CLASS_WARRIOR, "arms", SPEC_ARMOR_INVTYPE_SHIELD, SPEC_ARMOR_AS_SHIELD) == false);
    CHECK(SpecArmorSubClassAllowed(SPEC_ARMOR_CLASS_PALADIN, "prot", SPEC_ARMOR_INVTYPE_SHIELD, SPEC_ARMOR_AS_SHIELD) == true);
    CHECK(SpecArmorSubClassAllowed(SPEC_ARMOR_CLASS_PALADIN, "holy", SPEC_ARMOR_INVTYPE_SHIELD, SPEC_ARMOR_AS_SHIELD) == true);
    CHECK(SpecArmorSubClassAllowed(SPEC_ARMOR_CLASS_PALADIN, "retrib", SPEC_ARMOR_INVTYPE_SHIELD, SPEC_ARMOR_AS_SHIELD) == false);
    CHECK(SpecArmorSubClassAllowed(SPEC_ARMOR_CLASS_SHAMAN, "shamantank", SPEC_ARMOR_INVTYPE_SHIELD, SPEC_ARMOR_AS_SHIELD) == true);
    CHECK(SpecArmorSubClassAllowed(SPEC_ARMOR_CLASS_SHAMAN, "resto", SPEC_ARMOR_INVTYPE_SHIELD, SPEC_ARMOR_AS_SHIELD) == true);
    CHECK(SpecArmorSubClassAllowed(SPEC_ARMOR_CLASS_SHAMAN, "elem", SPEC_ARMOR_INVTYPE_SHIELD, SPEC_ARMOR_AS_SHIELD) == true);
    CHECK(SpecArmorSubClassAllowed(SPEC_ARMOR_CLASS_SHAMAN, "enhance", SPEC_ARMOR_INVTYPE_SHIELD, SPEC_ARMOR_AS_SHIELD) == true);

    // Holdables:
    CHECK(SpecArmorSubClassAllowed(SPEC_ARMOR_CLASS_PRIEST, "holy", SPEC_ARMOR_INVTYPE_HOLDABLE, SPEC_ARMOR_AS_MISC) == true);
    CHECK(SpecArmorSubClassAllowed(SPEC_ARMOR_CLASS_MAGE, "frost", SPEC_ARMOR_INVTYPE_HOLDABLE, SPEC_ARMOR_AS_MISC) == true);
    CHECK(SpecArmorSubClassAllowed(SPEC_ARMOR_CLASS_WARLOCK, "affliction", SPEC_ARMOR_INVTYPE_HOLDABLE, SPEC_ARMOR_AS_MISC) == true);
    CHECK(SpecArmorSubClassAllowed(SPEC_ARMOR_CLASS_DRUID, "resto", SPEC_ARMOR_INVTYPE_HOLDABLE, SPEC_ARMOR_AS_MISC) == true);
    CHECK(SpecArmorSubClassAllowed(SPEC_ARMOR_CLASS_DRUID, "balance", SPEC_ARMOR_INVTYPE_HOLDABLE, SPEC_ARMOR_AS_MISC) == true);
    CHECK(SpecArmorSubClassAllowed(SPEC_ARMOR_CLASS_SHAMAN, "resto", SPEC_ARMOR_INVTYPE_HOLDABLE, SPEC_ARMOR_AS_MISC) == true);
    CHECK(SpecArmorSubClassAllowed(SPEC_ARMOR_CLASS_SHAMAN, "elem", SPEC_ARMOR_INVTYPE_HOLDABLE, SPEC_ARMOR_AS_MISC) == true);
    CHECK(SpecArmorSubClassAllowed(SPEC_ARMOR_CLASS_PALADIN, "holy", SPEC_ARMOR_INVTYPE_HOLDABLE, SPEC_ARMOR_AS_MISC) == true);
    CHECK(SpecArmorSubClassAllowed(SPEC_ARMOR_CLASS_WARRIOR, "prot", SPEC_ARMOR_INVTYPE_HOLDABLE, SPEC_ARMOR_AS_MISC) == false);
    CHECK(SpecArmorSubClassAllowed(SPEC_ARMOR_CLASS_PALADIN, "prot", SPEC_ARMOR_INVTYPE_HOLDABLE, SPEC_ARMOR_AS_MISC) == false);
    CHECK(SpecArmorSubClassAllowed(SPEC_ARMOR_CLASS_PALADIN, "retrib", SPEC_ARMOR_INVTYPE_HOLDABLE, SPEC_ARMOR_AS_MISC) == false);
    CHECK(SpecArmorSubClassAllowed(SPEC_ARMOR_CLASS_HUNTER, "marksman", SPEC_ARMOR_INVTYPE_HOLDABLE, SPEC_ARMOR_AS_MISC) == false);
    CHECK(SpecArmorSubClassAllowed(SPEC_ARMOR_CLASS_ROGUE, "combat", SPEC_ARMOR_INVTYPE_HOLDABLE, SPEC_ARMOR_AS_MISC) == false);
    CHECK(SpecArmorSubClassAllowed(SPEC_ARMOR_CLASS_DRUID, "feraltank", SPEC_ARMOR_INVTYPE_HOLDABLE, SPEC_ARMOR_AS_MISC) == false);
    CHECK(SpecArmorSubClassAllowed(SPEC_ARMOR_CLASS_DRUID, "feraldps", SPEC_ARMOR_INVTYPE_HOLDABLE, SPEC_ARMOR_AS_MISC) == false);
    CHECK(SpecArmorSubClassAllowed(SPEC_ARMOR_CLASS_SHAMAN, "enhance", SPEC_ARMOR_INVTYPE_HOLDABLE, SPEC_ARMOR_AS_MISC) == false);
    CHECK(SpecArmorSubClassAllowed(SPEC_ARMOR_CLASS_SHAMAN, "shamantank", SPEC_ARMOR_INVTYPE_HOLDABLE, SPEC_ARMOR_AS_MISC) == false);

    // Relics:
    CHECK(SpecArmorSubClassAllowed(SPEC_ARMOR_CLASS_PALADIN, "holy", SPEC_ARMOR_INVTYPE_RELIC, SPEC_ARMOR_AS_LIBRAM) == true);
    CHECK(SpecArmorSubClassAllowed(SPEC_ARMOR_CLASS_PALADIN, "prot", SPEC_ARMOR_INVTYPE_RELIC, SPEC_ARMOR_AS_LIBRAM) == true);
    CHECK(SpecArmorSubClassAllowed(SPEC_ARMOR_CLASS_PALADIN, "retrib", SPEC_ARMOR_INVTYPE_RELIC, SPEC_ARMOR_AS_LIBRAM) == true);
    CHECK(SpecArmorSubClassAllowed(SPEC_ARMOR_CLASS_PALADIN, "holy", SPEC_ARMOR_INVTYPE_RELIC, SPEC_ARMOR_AS_IDOL) == false);
    CHECK(SpecArmorSubClassAllowed(SPEC_ARMOR_CLASS_SHAMAN, "resto", SPEC_ARMOR_INVTYPE_RELIC, SPEC_ARMOR_AS_TOTEM) == true);
    CHECK(SpecArmorSubClassAllowed(SPEC_ARMOR_CLASS_SHAMAN, "enhance", SPEC_ARMOR_INVTYPE_RELIC, SPEC_ARMOR_AS_TOTEM) == true);
    CHECK(SpecArmorSubClassAllowed(SPEC_ARMOR_CLASS_SHAMAN, "resto", SPEC_ARMOR_INVTYPE_RELIC, SPEC_ARMOR_AS_LIBRAM) == false);
    CHECK(SpecArmorSubClassAllowed(SPEC_ARMOR_CLASS_DRUID, "resto", SPEC_ARMOR_INVTYPE_RELIC, SPEC_ARMOR_AS_IDOL) == true);
    CHECK(SpecArmorSubClassAllowed(SPEC_ARMOR_CLASS_DRUID, "feraltank", SPEC_ARMOR_INVTYPE_RELIC, SPEC_ARMOR_AS_IDOL) == true);
    CHECK(SpecArmorSubClassAllowed(SPEC_ARMOR_CLASS_DRUID, "resto", SPEC_ARMOR_INVTYPE_RELIC, SPEC_ARMOR_AS_TOTEM) == false);
    CHECK(SpecArmorSubClassAllowed(SPEC_ARMOR_CLASS_WARRIOR, "prot", SPEC_ARMOR_INVTYPE_RELIC, SPEC_ARMOR_AS_LIBRAM) == false);

    // Body armor:
    CHECK(SpecArmorSubClassAllowed(SPEC_ARMOR_CLASS_WARRIOR, "prot", SPEC_ARMOR_INVTYPE_CHEST, SPEC_ARMOR_AS_PLATE) == true);
    CHECK(SpecArmorSubClassAllowed(SPEC_ARMOR_CLASS_WARRIOR, "prot", SPEC_ARMOR_INVTYPE_CHEST, SPEC_ARMOR_AS_MAIL) == true);
    CHECK(SpecArmorSubClassAllowed(SPEC_ARMOR_CLASS_WARRIOR, "prot", SPEC_ARMOR_INVTYPE_CHEST, SPEC_ARMOR_AS_LEATHER) == false);
    CHECK(SpecArmorSubClassAllowed(SPEC_ARMOR_CLASS_WARRIOR, "prot", SPEC_ARMOR_INVTYPE_CHEST, SPEC_ARMOR_AS_CLOTH) == false);
    CHECK(SpecArmorSubClassAllowed(SPEC_ARMOR_CLASS_WARRIOR, "arms", SPEC_ARMOR_INVTYPE_CHEST, SPEC_ARMOR_AS_LEATHER) == true);

    CHECK(SpecArmorSubClassAllowed(SPEC_ARMOR_CLASS_PALADIN, "holy", SPEC_ARMOR_INVTYPE_CHEST, SPEC_ARMOR_AS_CLOTH) == true);
    CHECK(SpecArmorSubClassAllowed(SPEC_ARMOR_CLASS_PALADIN, "retrib", SPEC_ARMOR_INVTYPE_CHEST, SPEC_ARMOR_AS_CLOTH) == false);

    CHECK(SpecArmorSubClassAllowed(SPEC_ARMOR_CLASS_HUNTER, "marksman", SPEC_ARMOR_INVTYPE_CHEST, SPEC_ARMOR_AS_MAIL) == true);
    CHECK(SpecArmorSubClassAllowed(SPEC_ARMOR_CLASS_HUNTER, "marksman", SPEC_ARMOR_INVTYPE_CHEST, SPEC_ARMOR_AS_PLATE) == false);

    CHECK(SpecArmorSubClassAllowed(SPEC_ARMOR_CLASS_ROGUE, "combat", SPEC_ARMOR_INVTYPE_CHEST, SPEC_ARMOR_AS_LEATHER) == true);
    CHECK(SpecArmorSubClassAllowed(SPEC_ARMOR_CLASS_ROGUE, "combat", SPEC_ARMOR_INVTYPE_CHEST, SPEC_ARMOR_AS_MAIL) == false);

    CHECK(SpecArmorSubClassAllowed(SPEC_ARMOR_CLASS_DRUID, "feraltank", SPEC_ARMOR_INVTYPE_CHEST, SPEC_ARMOR_AS_CLOTH) == false);
    CHECK(SpecArmorSubClassAllowed(SPEC_ARMOR_CLASS_DRUID, "resto", SPEC_ARMOR_INVTYPE_CHEST, SPEC_ARMOR_AS_CLOTH) == true);

    std::cout << "  [PASS] full spec armor subclass matrix\n";

    std::cout << "All spec armor policy checks PASSED!\n";
    return 0;
}
