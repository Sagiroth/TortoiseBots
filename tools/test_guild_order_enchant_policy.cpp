// Standalone test for runtime/GuildOrderEnchantPolicy.h: the slot token and
// alias rules, the Enchanting-row filter, the req-skill rule, the catalogue
// sort and the item-fit check.
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra tools/test_guild_order_enchant_policy.cpp -o /tmp/test_guild_order_enchant_policy
//   /tmp/test_guild_order_enchant_policy
#include "../runtime/GuildOrderEnchantPolicy.h"

#include <cstdio>
#include <string>

using namespace TortoiseBots::GuildOrders;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { ++failures; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

static EnchantEntry Entry(char const* name, uint32_t reqSkill)
{
    EnchantEntry entry;
    entry.name = name;
    entry.lowerName = name;
    entry.reqSkill = reqSkill;
    return entry;
}

int main()
{
    // Slot tokens round-trip; unknown parses to nothing.
    CHECK(std::string(EnchantSlotToken(EnchantSlot::Bracer)) == "bracer");
    CHECK(std::string(EnchantSlotToken(EnchantSlot::TwoHandWeapon)) == "2hweapon");
    CHECK(std::string(EnchantSlotToken(EnchantSlot::Unknown)) == "-");
    EnchantSlot slot = EnchantSlot::Unknown;
    CHECK(ParseEnchantSlot("cloak", slot) && slot == EnchantSlot::Cloak);
    CHECK(ParseEnchantSlot("2hweapon", slot) && slot == EnchantSlot::TwoHandWeapon);
    CHECK(!ParseEnchantSlot("head", slot));
    CHECK(!ParseEnchantSlot("", slot));

    // Slot derivation from the spell's equipped-item masks (values from the
    // live Spell.dbc: every catalogue spell falls in one of these groups).
    CHECK(EnchantSlotForMasks(4, 31, 512) == EnchantSlot::Bracer);
    CHECK(EnchantSlotForMasks(4, 31, (1 << 5) | (1 << 20)) == EnchantSlot::Chest);
    CHECK(EnchantSlotForMasks(4, 31, 256) == EnchantSlot::Boots);
    CHECK(EnchantSlotForMasks(4, 30, 65536) == EnchantSlot::Cloak);
    CHECK(EnchantSlotForMasks(4, 31, 1024) == EnchantSlot::Gloves);
    CHECK(EnchantSlotForMasks(4, 64, 0) == EnchantSlot::Shield);
    CHECK(EnchantSlotForMasks(4, 0, 16384) == EnchantSlot::Shield);
    CHECK(EnchantSlotForMasks(2, 189939, 0) == EnchantSlot::Weapon);
    CHECK(EnchantSlotForMasks(2, 1378, 0) == EnchantSlot::TwoHandWeapon);
    CHECK(EnchantSlotForMasks(4, 31, 0) == EnchantSlot::Unknown);

    // Search-text aliases; anything else passes through untouched.
    CHECK(EnchantSlotAlias("wrist") == "bracer");
    CHECK(EnchantSlotAlias("feet") == "boots");
    CHECK(EnchantSlotAlias("back") == "cloak");
    CHECK(EnchantSlotAlias("hands") == "gloves");
    CHECK(EnchantSlotAlias("2h") == "2hweapon");
    CHECK(EnchantSlotAlias("stamina") == "stamina");

    // Row filter: only Enchanting (333) permanent enchants pass; oils, wands
    // and rods (create-item) fall out, as do rank/learn rows and other lines.
    CHECK(IsCatalogueEnchantRow(333, true, false, false, 0));
    CHECK(!IsCatalogueEnchantRow(333, true, true, false, 0));   // oil/wand/rod
    CHECK(!IsCatalogueEnchantRow(333, false, false, false, 0)); // no enchant effect
    CHECK(!IsCatalogueEnchantRow(164, true, false, false, 0));  // blacksmithing
    CHECK(!IsCatalogueEnchantRow(333, true, false, true, 0));   // skill step
    CHECK(!IsCatalogueEnchantRow(333, true, false, false, 36)); // learn spell
    CHECK(!IsCatalogueEnchantRow(333, true, false, false, 44)); // skill step effect

    // Required skill mirrors the Professions missing-recipe line: the lower
    // of the trainer requirement and the recipe item's RequiredSkillRank.
    // Trainer-taught enchants take the trainer value (Minor Stamina 50,
    // Lesser Stamina 130, Stamina 170); formula-taught ones take the item
    // rank (Greater Stamina 245, Superior Stamina 300); both present takes
    // the lower; neither gives 0.
    CHECK(EnchantReqSkillCombine(true, 50, false, 0) == 50);
    CHECK(EnchantReqSkillCombine(true, 130, false, 0) == 130);
    CHECK(EnchantReqSkillCombine(true, 170, false, 0) == 170);
    CHECK(EnchantReqSkillCombine(false, 0, true, 245) == 245);
    CHECK(EnchantReqSkillCombine(false, 0, true, 300) == 300);
    CHECK(EnchantReqSkillCombine(true, 200, true, 245) == 200);
    CHECK(EnchantReqSkillCombine(true, 250, true, 245) == 245);
    CHECK(EnchantReqSkillCombine(false, 0, false, 0) == 0);

    // Sort: required skill ascending, then name.
    CHECK(EnchantSortLess(Entry("b", 100), Entry("a", 200)));
    CHECK(EnchantSortLess(Entry("a", 100), Entry("b", 100)));
    CHECK(!EnchantSortLess(Entry("b", 100), Entry("a", 100)));

    // Item fit: bracer enchant admits bracers of any armor subclass the mask
    // names, and refuses boots; the mask decides, not the slot token.
    EnchantTargetFacts bracer{ 4, 31, 512, true, 0 };
    CHECK(EnchantFitsItem(bracer, { 4, 1, 9 }));
    CHECK(EnchantFitsItem(bracer, { 4, 4, 9 }));
    CHECK(!EnchantFitsItem(bracer, { 4, 1, 8 }));
    CHECK(!EnchantFitsItem(bracer, { 2, 7, 13 }));

    // Shield enchants (class armor, shield subclass mask, no inventory mask)
    // admit shields and refuse weapons.
    EnchantTargetFacts shield{ 4, 64, 0, true, 0 };
    CHECK(EnchantFitsItem(shield, { 4, 6, 14 }));
    CHECK(!EnchantFitsItem(shield, { 2, 7, 13 }));

    // 2H-only weapon enchants admit two-handers and refuse one-handers.
    EnchantTargetFacts twoHand{ 2, 1378, 0, true, 0 };
    CHECK(EnchantFitsItem(twoHand, { 2, 8, 17 }));
    CHECK(!EnchantFitsItem(twoHand, { 2, 7, 13 }));

    // One-hand weapon enchants (plain weapon mask) admit both: the blade can
    // go on either kind.
    EnchantTargetFacts weapon{ 2, 189939, 0, true, 0 };
    CHECK(EnchantFitsItem(weapon, { 2, 7, 13 }));
    CHECK(EnchantFitsItem(weapon, { 2, 8, 17 }));

    // The 13419 data exception: weapon-class spell still fits a cloak.
    EnchantTargetFacts minorAgi{ 2, 196083, 0, true, 13419 };
    CHECK(EnchantFitsItem(minorAgi, { 4, 1, 16 }));
    EnchantTargetFacts plainWeapon{ 2, 196083, 0, true, 9999 };
    CHECK(!EnchantFitsItem(plainWeapon, { 4, 1, 16 }));

    if (failures == 0)
        std::printf("test_guild_order_enchant_policy ok\n");
    return failures ? 1 : 0;
}
