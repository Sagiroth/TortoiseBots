#include "../ai/playerbot/policy/loot/LootSpecPriorityPolicy.h"

#include <cstdlib>
#include <iostream>
#include <string>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using namespace ai;

int main()
{
    std::cout << "Starting TortoiseBots loot spec priority policy tests...\n";

    // 1. Item primary stat derivation
    {
        // Agility dagger/ring
        ItemStatSummary agiDagger{ 15, 0, 0, 0, 8 };
        CHECK(DeriveItemStatFamily(agiDagger) == ItemStatFamily::Agility);

        // Strength 2H / plate
        ItemStatSummary str2H{ 0, 25, 0, 0, 12 };
        CHECK(DeriveItemStatFamily(str2H) == ItemStatFamily::Strength);

        // Intellect cloak / staff
        ItemStatSummary intCloak{ 0, 0, 18, 0, 6 };
        CHECK(DeriveItemStatFamily(intCloak) == ItemStatFamily::Spell);

        // Tank shield / defense ring
        ItemStatSummary tankRing{ 0, 0, 0, 15, 20 };
        CHECK(DeriveItemStatFamily(tankRing) == ItemStatFamily::Tank);

        // Pure stamina item (no clear primary stat)
        ItemStatSummary pureStam{ 0, 0, 0, 0, 20 };
        CHECK(DeriveItemStatFamily(pureStam) == ItemStatFamily::None);

        // Tied str and agi (no dominant primary stat)
        ItemStatSummary tiedPhysical{ 10, 10, 0, 0, 10 };
        CHECK(DeriveItemStatFamily(tiedPhysical) == ItemStatFamily::None);

        std::cout << "  [PASS] Item primary stat family derivation\n";
    }

    // 2. Spec primary stat derivation from ai_playerbot_weightscale_data weights
    {
        // Prot warrior: str=7, agi=4, tank=30 (defrtng=8, dodgertng=7, parryrtng=6, blockrtng=6, block=3)
        SpecWeightSummary protWar{ 7, 4, 0, 30 };
        CHECK(DeriveSpecStatFamily(protWar) == ItemStatFamily::Tank);

        // Arms warrior: str=4, agi=2, tank=0
        SpecWeightSummary armsWar{ 4, 2, 0, 0 };
        CHECK(DeriveSpecStatFamily(armsWar) == ItemStatFamily::Strength);

        // Combat rogue: str=2, agi=4, tank=0
        SpecWeightSummary combatRogue{ 2, 4, 0, 0 };
        CHECK(DeriveSpecStatFamily(combatRogue) == ItemStatFamily::Agility);

        // Hunter (marksman): str=0, agi=4, tank=0
        SpecWeightSummary marksHunter{ 0, 4, 0, 0 };
        CHECK(DeriveSpecStatFamily(marksHunter) == ItemStatFamily::Agility);

        // Frost mage: str=0, agi=0, spell=210, tank=0
        SpecWeightSummary frostMage{ 0, 0, 210, 0 };
        CHECK(DeriveSpecStatFamily(frostMage) == ItemStatFamily::Spell);

        // Holy priest: str=0, agi=0, spell=196, tank=0
        SpecWeightSummary holyPriest{ 0, 0, 196, 0 };
        CHECK(DeriveSpecStatFamily(holyPriest) == ItemStatFamily::Spell);

        // Retribution paladin: str=4, agi=2, tank=0
        SpecWeightSummary retPal{ 4, 2, 0, 0 };
        CHECK(DeriveSpecStatFamily(retPal) == ItemStatFamily::Strength);

        // Prot paladin: str=1, agi=5, tank=35
        SpecWeightSummary protPal{ 1, 5, 4, 35 };
        CHECK(DeriveSpecStatFamily(protPal) == ItemStatFamily::Tank);

        // Enhancement shaman: str=4, agi=2, tank=0
        SpecWeightSummary enhSham{ 4, 2, 0, 0 };
        CHECK(DeriveSpecStatFamily(enhSham) == ItemStatFamily::Strength);

        // Resto shaman: str=0, agi=0, spell=155, tank=0
        SpecWeightSummary restoSham{ 0, 0, 155, 0 };
        CHECK(DeriveSpecStatFamily(restoSham) == ItemStatFamily::Spell);

        // Feral tank druid: str=0, agi=6, tank=114
        SpecWeightSummary feralTank{ 0, 6, 0, 114 };
        CHECK(DeriveSpecStatFamily(feralTank) == ItemStatFamily::Tank);

        // Feral DPS druid: str=11, agi=10, tank=0
        SpecWeightSummary feralDps{ 11, 10, 0, 0 };
        CHECK(DeriveSpecStatFamily(feralDps) == ItemStatFamily::Strength);

        std::cout << "  [PASS] Spec primary stat family from weight scales\n";
    }

    // 3. Player class families
    {
        CHECK(PlayerClassMayUseStatFamily(SPEC_PRIORITY_CLASS_ROGUE, ItemStatFamily::Agility));
        CHECK(!PlayerClassMayUseStatFamily(SPEC_PRIORITY_CLASS_ROGUE, ItemStatFamily::Strength));
        CHECK(!PlayerClassMayUseStatFamily(SPEC_PRIORITY_CLASS_ROGUE, ItemStatFamily::Spell));

        CHECK(PlayerClassMayUseStatFamily(SPEC_PRIORITY_CLASS_WARRIOR, ItemStatFamily::Strength));
        CHECK(PlayerClassMayUseStatFamily(SPEC_PRIORITY_CLASS_WARRIOR, ItemStatFamily::Tank));
        CHECK(!PlayerClassMayUseStatFamily(SPEC_PRIORITY_CLASS_WARRIOR, ItemStatFamily::Agility));
        CHECK(!PlayerClassMayUseStatFamily(SPEC_PRIORITY_CLASS_WARRIOR, ItemStatFamily::Spell));

        CHECK(PlayerClassMayUseStatFamily(SPEC_PRIORITY_CLASS_MAGE, ItemStatFamily::Spell));
        CHECK(!PlayerClassMayUseStatFamily(SPEC_PRIORITY_CLASS_MAGE, ItemStatFamily::Agility));
        CHECK(!PlayerClassMayUseStatFamily(SPEC_PRIORITY_CLASS_MAGE, ItemStatFamily::Strength));

        // Hybrid paladin covers Strength, Spell, Tank
        CHECK(PlayerClassMayUseStatFamily(SPEC_PRIORITY_CLASS_PALADIN, ItemStatFamily::Strength));
        CHECK(PlayerClassMayUseStatFamily(SPEC_PRIORITY_CLASS_PALADIN, ItemStatFamily::Spell));
        CHECK(PlayerClassMayUseStatFamily(SPEC_PRIORITY_CLASS_PALADIN, ItemStatFamily::Tank));
        CHECK(!PlayerClassMayUseStatFamily(SPEC_PRIORITY_CLASS_PALADIN, ItemStatFamily::Agility));

        std::cout << "  [PASS] Player class stat family coverage\n";
    }

    // 4. Primary weapon user vs stat-stick
    {
        // Melee weapons
        CHECK(IsPrimaryWeaponUser(SPEC_PRIORITY_CLASS_ROGUE, "combat", false) == true);
        CHECK(IsPrimaryWeaponUser(SPEC_PRIORITY_CLASS_WARRIOR, "arms", false) == true);
        CHECK(IsPrimaryWeaponUser(SPEC_PRIORITY_CLASS_PALADIN, "retrib", false) == true);
        CHECK(IsPrimaryWeaponUser(SPEC_PRIORITY_CLASS_PALADIN, "holy", false) == false);
        CHECK(IsPrimaryWeaponUser(SPEC_PRIORITY_CLASS_SHAMAN, "enhance", false) == true);
        CHECK(IsPrimaryWeaponUser(SPEC_PRIORITY_CLASS_SHAMAN, "resto", false) == false);
        CHECK(IsPrimaryWeaponUser(SPEC_PRIORITY_CLASS_HUNTER, "marks", false) == false); // Hunter melee slot is stat stick
        CHECK(IsPrimaryWeaponUser(SPEC_PRIORITY_CLASS_DRUID, "feraldps", false) == false); // Feral melee form is normalized
        CHECK(IsPrimaryWeaponUser(SPEC_PRIORITY_CLASS_MAGE, "frost", false) == false);

        // Ranged weapons
        CHECK(IsPrimaryWeaponUser(SPEC_PRIORITY_CLASS_HUNTER, "marks", true) == true);
        CHECK(IsPrimaryWeaponUser(SPEC_PRIORITY_CLASS_WARRIOR, "arms", true) == false);
        CHECK(IsPrimaryWeaponUser(SPEC_PRIORITY_CLASS_ROGUE, "combat", true) == false);

        std::cout << "  [PASS] Primary weapon user classification\n";
    }

    // 5. Scenario: agility dagger rogue vs prot warrior vs hunter
    {
        // Item: Agility melee dagger
        ItemStatFamily daggerStat = ItemStatFamily::Agility;
        bool isWeapon = true;
        bool isRanged = false;

        // Prot warrior rolling bot vs Rogue bot: Rogue fits better!
        CHECK(MemberFitsBetter(ItemStatFamily::Tank, SPEC_PRIORITY_CLASS_WARRIOR, "prot",
                               ItemStatFamily::Agility, SPEC_PRIORITY_CLASS_ROGUE, "combat",
                               daggerStat, isWeapon, isRanged, false) == true);

        // Prot warrior rolling bot vs Hunter bot: Hunter fits better than Prot warrior!
        CHECK(MemberFitsBetter(ItemStatFamily::Tank, SPEC_PRIORITY_CLASS_WARRIOR, "prot",
                               ItemStatFamily::Agility, SPEC_PRIORITY_CLASS_HUNTER, "marks",
                               daggerStat, isWeapon, isRanged, false) == true);

        // Hunter rolling bot vs Rogue bot: both share Agility, but Rogue is primary weapon user and Hunter is stat-stick -> Rogue fits better!
        CHECK(MemberFitsBetter(ItemStatFamily::Agility, SPEC_PRIORITY_CLASS_HUNTER, "marks",
                               ItemStatFamily::Agility, SPEC_PRIORITY_CLASS_ROGUE, "combat",
                               daggerStat, isWeapon, isRanged, false) == true);

        // Rogue rolling bot vs Prot warrior: Prot warrior does NOT fit better than Rogue
        CHECK(MemberFitsBetter(ItemStatFamily::Agility, SPEC_PRIORITY_CLASS_ROGUE, "combat",
                               ItemStatFamily::Tank, SPEC_PRIORITY_CLASS_WARRIOR, "prot",
                               daggerStat, isWeapon, isRanged, false) == false);

        // Rogue rolling bot vs Hunter: Hunter does NOT fit better than Rogue
        CHECK(MemberFitsBetter(ItemStatFamily::Agility, SPEC_PRIORITY_CLASS_ROGUE, "combat",
                               ItemStatFamily::Agility, SPEC_PRIORITY_CLASS_HUNTER, "marks",
                               daggerStat, isWeapon, isRanged, false) == false);

        // Prot warrior rolling bot vs real player Rogue: Rogue player fits better!
        CHECK(MemberFitsBetter(ItemStatFamily::Tank, SPEC_PRIORITY_CLASS_WARRIOR, "prot",
                               ItemStatFamily::None, SPEC_PRIORITY_CLASS_ROGUE, "",
                               daggerStat, isWeapon, isRanged, true) == true);

        // Hunter rolling bot vs real player Rogue: Rogue player fits better on melee dagger!
        CHECK(MemberFitsBetter(ItemStatFamily::Agility, SPEC_PRIORITY_CLASS_HUNTER, "marks",
                               ItemStatFamily::None, SPEC_PRIORITY_CLASS_ROGUE, "",
                               daggerStat, isWeapon, isRanged, true) == true);

        std::cout << "  [PASS] Agility dagger: rogue vs prot warrior vs hunter\n";
    }

    // 6. Scenario: strength 2H warrior vs rogue
    {
        // Item: Strength 2H sword
        ItemStatFamily str2HStat = ItemStatFamily::Strength;
        bool isWeapon = true;
        bool isRanged = false;

        // Rogue rolling bot vs Arms warrior: Warrior fits better!
        CHECK(MemberFitsBetter(ItemStatFamily::Agility, SPEC_PRIORITY_CLASS_ROGUE, "combat",
                               ItemStatFamily::Strength, SPEC_PRIORITY_CLASS_WARRIOR, "arms",
                               str2HStat, isWeapon, isRanged, false) == true);

        // Arms warrior rolling bot vs Rogue: Rogue does NOT fit better
        CHECK(MemberFitsBetter(ItemStatFamily::Strength, SPEC_PRIORITY_CLASS_WARRIOR, "arms",
                               ItemStatFamily::Agility, SPEC_PRIORITY_CLASS_ROGUE, "combat",
                               str2HStat, isWeapon, isRanged, false) == false);

        // Rogue rolling bot vs real player Warrior: Warrior fits better!
        CHECK(MemberFitsBetter(ItemStatFamily::Agility, SPEC_PRIORITY_CLASS_ROGUE, "combat",
                               ItemStatFamily::None, SPEC_PRIORITY_CLASS_WARRIOR, "",
                               str2HStat, isWeapon, isRanged, true) == true);

        std::cout << "  [PASS] Strength 2H: warrior vs rogue\n";
    }

    // 7. Scenario: intellect cloak caster vs warrior
    {
        // Item: Intellect cloak (armor, not weapon)
        ItemStatFamily intCloakStat = ItemStatFamily::Spell;
        bool isWeapon = false;
        bool isRanged = false;

        // Arms warrior rolling bot vs Holy priest bot: Priest fits better!
        CHECK(MemberFitsBetter(ItemStatFamily::Strength, SPEC_PRIORITY_CLASS_WARRIOR, "arms",
                               ItemStatFamily::Spell, SPEC_PRIORITY_CLASS_PRIEST, "holy",
                               intCloakStat, isWeapon, isRanged, false) == true);

        // Prot warrior rolling bot vs Frost mage bot: Mage fits better!
        CHECK(MemberFitsBetter(ItemStatFamily::Tank, SPEC_PRIORITY_CLASS_WARRIOR, "prot",
                               ItemStatFamily::Spell, SPEC_PRIORITY_CLASS_MAGE, "frost",
                               intCloakStat, isWeapon, isRanged, false) == true);

        // Priest rolling bot vs Warrior: Warrior does NOT fit better
        CHECK(MemberFitsBetter(ItemStatFamily::Spell, SPEC_PRIORITY_CLASS_PRIEST, "holy",
                               ItemStatFamily::Strength, SPEC_PRIORITY_CLASS_WARRIOR, "arms",
                               intCloakStat, isWeapon, isRanged, false) == false);

        // Warrior rolling bot vs real player Mage: Mage player fits better!
        CHECK(MemberFitsBetter(ItemStatFamily::Strength, SPEC_PRIORITY_CLASS_WARRIOR, "arms",
                               ItemStatFamily::None, SPEC_PRIORITY_CLASS_MAGE, "",
                               intCloakStat, isWeapon, isRanged, true) == true);

        std::cout << "  [PASS] Intellect cloak: caster vs warrior\n";
    }

    // 8. Scenario: agility ring rogue vs mage
    {
        // Item: Agility ring (armor, not weapon)
        ItemStatFamily agiRingStat = ItemStatFamily::Agility;
        bool isWeapon = false;
        bool isRanged = false;

        // Mage rolling bot vs Rogue bot: Rogue fits better!
        CHECK(MemberFitsBetter(ItemStatFamily::Spell, SPEC_PRIORITY_CLASS_MAGE, "frost",
                               ItemStatFamily::Agility, SPEC_PRIORITY_CLASS_ROGUE, "combat",
                               agiRingStat, isWeapon, isRanged, false) == true);

        // Rogue rolling bot vs Mage bot: Mage does NOT fit better
        CHECK(MemberFitsBetter(ItemStatFamily::Agility, SPEC_PRIORITY_CLASS_ROGUE, "combat",
                               ItemStatFamily::Spell, SPEC_PRIORITY_CLASS_MAGE, "frost",
                               agiRingStat, isWeapon, isRanged, false) == false);

        // Mage rolling bot vs real player Rogue: Rogue player fits better!
        CHECK(MemberFitsBetter(ItemStatFamily::Spell, SPEC_PRIORITY_CLASS_MAGE, "frost",
                               ItemStatFamily::None, SPEC_PRIORITY_CLASS_ROGUE, "",
                               agiRingStat, isWeapon, isRanged, true) == true);

        std::cout << "  [PASS] Agility ring: rogue vs mage\n";
    }

    // 9. Scenario: no better member -> upgrade NEED stays
    {
        // Hunter with agility dagger when group only has Casters and Prot Warrior (no Rogue)
        ItemStatFamily daggerStat = ItemStatFamily::Agility;
        CHECK(MemberFitsBetter(ItemStatFamily::Agility, SPEC_PRIORITY_CLASS_HUNTER, "marks",
                               ItemStatFamily::Tank, SPEC_PRIORITY_CLASS_WARRIOR, "prot",
                               daggerStat, true, false, false) == false);

        CHECK(MemberFitsBetter(ItemStatFamily::Agility, SPEC_PRIORITY_CLASS_HUNTER, "marks",
                               ItemStatFamily::Spell, SPEC_PRIORITY_CLASS_PRIEST, "holy",
                               daggerStat, true, false, false) == false);

        CHECK(MemberFitsBetter(ItemStatFamily::Agility, SPEC_PRIORITY_CLASS_HUNTER, "marks",
                               ItemStatFamily::Spell, SPEC_PRIORITY_CLASS_MAGE, "frost",
                               daggerStat, true, false, false) == false);

        std::cout << "  [PASS] No better member -> upgrade NEED stays\n";
    }

    // 10. Scenario: pure stamina item -> upgrade NEED stays
    {
        ItemStatFamily pureStamStat = ItemStatFamily::None;
        CHECK(MemberFitsBetter(ItemStatFamily::Tank, SPEC_PRIORITY_CLASS_WARRIOR, "prot",
                               ItemStatFamily::Agility, SPEC_PRIORITY_CLASS_ROGUE, "combat",
                               pureStamStat, false, false, false) == false);

        CHECK(MemberFitsBetter(ItemStatFamily::Spell, SPEC_PRIORITY_CLASS_MAGE, "frost",
                               ItemStatFamily::Strength, SPEC_PRIORITY_CLASS_WARRIOR, "arms",
                               pureStamStat, false, false, false) == false);

        std::cout << "  [PASS] Pure stamina item -> upgrade NEED stays\n";
    }

    // 11. Scenario: two members of same spec roll as equal peers
    {
        ItemStatFamily agiDagger = ItemStatFamily::Agility;
        CHECK(MemberFitsBetter(ItemStatFamily::Agility, SPEC_PRIORITY_CLASS_ROGUE, "combat",
                               ItemStatFamily::Agility, SPEC_PRIORITY_CLASS_ROGUE, "combat",
                               agiDagger, true, false, false) == false);

        std::cout << "  [PASS] Same-spec members roll as equal peers\n";
    }

    // 12. Ranged weapon tie-break: Hunter beats Rogue / Warrior
    {
        ItemStatFamily agiBow = ItemStatFamily::Agility;
        CHECK(MemberFitsBetter(ItemStatFamily::Agility, SPEC_PRIORITY_CLASS_ROGUE, "combat",
                               ItemStatFamily::Agility, SPEC_PRIORITY_CLASS_HUNTER, "marks",
                               agiBow, true, true, false) == true);

        CHECK(MemberFitsBetter(ItemStatFamily::Agility, SPEC_PRIORITY_CLASS_HUNTER, "marks",
                               ItemStatFamily::Agility, SPEC_PRIORITY_CLASS_ROGUE, "combat",
                               agiBow, true, true, false) == false);

        std::cout << "  [PASS] Ranged weapon tie-break: Hunter beats Rogue/Warrior\n";
    }

    // 13. Owner addendum: priority only blocks when the priority member actually wants the item
    {
        // Item: Agility item (dagger or leather/ring) that warrior could use as an upgrade
        ItemStatFamily agiStat = ItemStatFamily::Agility;

        // Rogue bot present, but item is NOT an upgrade for that rogue (memberWantsItem = false):
        // Warrior is NOT blocked -> MemberFitsBetter returns false -> warrior may NEED
        CHECK(MemberFitsBetter(ItemStatFamily::Strength, SPEC_PRIORITY_CLASS_WARRIOR, "arms",
                               ItemStatFamily::Agility, SPEC_PRIORITY_CLASS_ROGUE, "combat",
                               agiStat, true, false, false, false) == false);

        // Rogue bot present and item IS an upgrade for that rogue (memberWantsItem = true):
        // Warrior IS blocked -> MemberFitsBetter returns true -> warrior steps down to GREED
        CHECK(MemberFitsBetter(ItemStatFamily::Strength, SPEC_PRIORITY_CLASS_WARRIOR, "arms",
                               ItemStatFamily::Agility, SPEC_PRIORITY_CLASS_ROGUE, "combat",
                               agiStat, true, false, false, true) == true);

        // Real player Rogue greeded or passed (memberWantsItem = false):
        // Warrior is NOT blocked -> MemberFitsBetter returns false -> warrior may NEED
        CHECK(MemberFitsBetter(ItemStatFamily::Strength, SPEC_PRIORITY_CLASS_WARRIOR, "arms",
                               ItemStatFamily::None, SPEC_PRIORITY_CLASS_ROGUE, "",
                               agiStat, true, false, true, false) == false);

        // Real player Rogue hasn't voted yet or wants it (memberWantsItem = true):
        // Warrior IS blocked -> MemberFitsBetter returns true -> warrior steps down to GREED
        CHECK(MemberFitsBetter(ItemStatFamily::Strength, SPEC_PRIORITY_CLASS_WARRIOR, "arms",
                               ItemStatFamily::None, SPEC_PRIORITY_CLASS_ROGUE, "",
                               agiStat, true, false, true, true) == true);

        std::cout << "  [PASS] Owner addendum: unwanting member does not block\n";
    }

    std::cout << "All TortoiseBots loot spec priority policy tests passed successfully!\n";
    return 0;
}
