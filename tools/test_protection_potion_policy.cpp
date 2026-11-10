#include "../ai/playerbot/ProtectionPotionPolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::BossProtectionPotion;
using ai::ProtectionPotion;
using ai::ProtectionPotionForSchool;
using ai::PotionRoleBit;
using ai::RowPotionDue;
using ai::SpreadRowPotion;
using ai::kPotionRoleHealer;
using ai::kPotionRoleMelee;
using ai::kPotionRoleRanged;
using ai::kPotionRoleTank;

static void TestPotionForSchool()
{
    // spell_template `school` is the index: 2 fire, 3 nature, 5 shadow.
    CHECK(ProtectionPotionForSchool(2).itemId == 13457);
    CHECK(ProtectionPotionForSchool(2).buffSpellId == 17543);
    CHECK(ProtectionPotionForSchool(3).itemId == 13458);
    CHECK(ProtectionPotionForSchool(3).buffSpellId == 17546);
    CHECK(ProtectionPotionForSchool(5).itemId == 13459);
    CHECK(ProtectionPotionForSchool(5).buffSpellId == 17548);
    // Physical, holy, frost, arcane and an unknown column: no Greater potion here.
    for (std::uint32_t school : { 0u, 1u, 4u, 6u, 7u })
    {
        CHECK(ProtectionPotionForSchool(school).itemId == 0);
        CHECK(ProtectionPotionForSchool(school).buffSpellId == 0);
    }
}

static void TestPotionRoleBit()
{
    // A tank stays a tank even when it also counts as ranged; a healer likewise.
    CHECK(PotionRoleBit(true, false, false) == kPotionRoleTank);
    CHECK(PotionRoleBit(true, false, true) == kPotionRoleTank);
    CHECK(PotionRoleBit(true, true, false) == kPotionRoleTank);
    CHECK(PotionRoleBit(false, true, false) == kPotionRoleHealer);
    CHECK(PotionRoleBit(false, true, true) == kPotionRoleHealer);
    CHECK(PotionRoleBit(false, false, true) == kPotionRoleRanged);
    CHECK(PotionRoleBit(false, false, false) == kPotionRoleMelee);
}

static void TestSpreadRowPotionShadowForMaskedRole()
{
    // The named case: Hakkar's row, Corrupted Blood's school 5, mask 7 (tanks and both damage roles).
    const ProtectionPotion ranged = SpreadRowPotion(true, 5, 7, kPotionRoleRanged);
    CHECK(ranged.itemId == 13459);
    CHECK(ranged.buffSpellId == 17548);
    CHECK(SpreadRowPotion(true, 5, 7, kPotionRoleTank).itemId == 13459);
    CHECK(SpreadRowPotion(true, 5, 7, kPotionRoleMelee).itemId == 13459);
    // Healers keep the potion cooldown for mana potions.
    CHECK(SpreadRowPotion(true, 5, 7, kPotionRoleHealer).itemId == 0);
    CHECK(SpreadRowPotion(true, 5, 7, kPotionRoleHealer).buffSpellId == 0);
    // Every row before this change carries priority 0: nobody drinks.
    CHECK(SpreadRowPotion(true, 5, 0, kPotionRoleTank).itemId == 0);
    // The key off answers nothing, whatever the row says.
    CHECK(SpreadRowPotion(false, 5, 7, kPotionRoleRanged).itemId == 0);
    CHECK(SpreadRowPotion(false, 5, 7, kPotionRoleTank).itemId == 0);
    // A non-shadow row keeps its own school, and a spell with no school gives nothing.
    CHECK(SpreadRowPotion(true, 2, 7, kPotionRoleMelee).itemId == 13457);
    CHECK(SpreadRowPotion(true, 3, 7, kPotionRoleRanged).itemId == 13458);
    CHECK(SpreadRowPotion(true, 0, 255, kPotionRoleRanged).itemId == 0);
}

static void TestRowPotionDue()
{
    // A potion, no aura, healthy enough and level 60: due.
    CHECK(RowPotionDue(true, false, 80, 50, 60));
    // The aura is still up: wait for it to fall.
    CHECK(!RowPotionDue(true, true, 80, 50, 60));
    // Below the low line a healing potion goes first; at the line the protection potion is due.
    CHECK(!RowPotionDue(true, false, 49, 50, 60));
    CHECK(RowPotionDue(true, false, 50, 50, 60));
    // The Greater potions need level 48.
    CHECK(!RowPotionDue(true, false, 80, 50, 47));
    CHECK(RowPotionDue(true, false, 80, 50, 48));
    // No potion, no drink.
    CHECK(!RowPotionDue(false, false, 80, 50, 60));
    CHECK(!RowPotionDue(false, true, 80, 50, 60));
}

int main()
{
    std::cout << "Starting TortoiseBots protection potion policy tests...\n";

    // Fire: MC fire bosses, Onyxia, Razorgore, Vaelastrasz.
    for (std::uint32_t entry : { 11982u, 12056u, 11988u, 11502u, 10184u, 12435u, 13020u })
    {
        CHECK(BossProtectionPotion(entry).itemId == 13457);
        CHECK(BossProtectionPotion(entry).buffSpellId == 17543);
    }

    // Shadow: Lucifron, Gehennas, Nefarian.
    for (std::uint32_t entry : { 12118u, 12259u, 11583u })
    {
        CHECK(BossProtectionPotion(entry).itemId == 13459);
        CHECK(BossProtectionPotion(entry).buffSpellId == 17548);
    }

    // Nature: Princess Huhuran, Viscidus.
    for (std::uint32_t entry : { 15509u, 15299u })
    {
        CHECK(BossProtectionPotion(entry).itemId == 13458);
        CHECK(BossProtectionPotion(entry).buffSpellId == 17546);
    }

    // Trash and unlisted bosses get nothing (no potion between pulls).
    CHECK(BossProtectionPotion(0).itemId == 0);
    CHECK(BossProtectionPotion(11673).itemId == 0); // Ancient Core Hound (MC trash)
    CHECK(BossProtectionPotion(12057).itemId == 0); // Garr: the fire is on his Firesworn adds
    CHECK(BossProtectionPotion(14834).itemId == 0); // Hakkar: mixed schools, not listed; his row answers instead

    // The spread row's own answer (DungeonRunBossProtectionPotion).
    TestPotionForSchool();
    TestPotionRoleBit();
    TestSpreadRowPotionShadowForMaskedRole();
    TestRowPotionDue();

    std::cout << "Protection potion policy tests passed.\n";
    return 0;
}
