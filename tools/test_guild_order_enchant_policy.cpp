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

    // Match: name words plus slot-token and alias words. "bracer" matches by
    // slot even when the name says nothing about it; "wrist" aliases there.
    EnchantEntry stamina;
    stamina.name = "Enchant Bracer - Stamina";
    stamina.lowerName = "enchant bracer - stamina";
    stamina.slot = EnchantSlot::Bracer;
    CHECK(EnchantSearchMatches(stamina, { "stamina" }));
    CHECK(EnchantSearchMatches(stamina, { "bracer", "stamina" }));
    CHECK(!EnchantSearchMatches(stamina, { "cloak", "stamina" }));
    CHECK(EnchantSearchMatches(stamina, { "wrist" }));
    CHECK(EnchantSearchMatches(stamina, { "wrist", "stamina" }));
    CHECK(!EnchantSearchMatches(stamina, { "q" }));
    CHECK(!EnchantSearchMatches(stamina, { "stamina", "cloak" }));

    // Verdict: known + skilled is ok; known but too low is noskill; unknown
    // is norecipe; zero requirement is always usable.
    CHECK(EnchantSearchVerdict(true, 300, 250) == OrderReason::None);
    CHECK(EnchantSearchVerdict(true, 100, 250) == OrderReason::NoSkill);
    CHECK(EnchantSearchVerdict(false, 300, 250) == OrderReason::NoRecipe);
    CHECK(EnchantSearchVerdict(true, 0, 0) == OrderReason::None);
    CHECK(std::string(EnchantVerdictToken(OrderReason::None)) == "ok");
    CHECK(std::string(EnchantVerdictToken(OrderReason::NoSkill)) == "noskill");

    // Wire rows: the per-match line, the begin/end framing, the reagents.
    CHECK(FormatEnchantRow(7428, "Enchant Bracer - Stamina", EnchantSlot::Bracer, 265,
        OrderReason::None, "10940:1:3") == "TBM:ORDER_ENCHANT|7428|Enchant Bracer - Stamina|bracer|265|ok|10940:1:3");
    CHECK(FormatEnchantRow(7428, "Enchant Bracer - Stamina", EnchantSlot::Bracer, 265,
        OrderReason::NoSkill, "") == "TBM:ORDER_ENCHANT|7428|Enchant Bracer - Stamina|bracer|265|noskill|-");
    CHECK(FormatEnchantsBegin(2) == "TBM:ORDER_ENCHANTS_BEGIN|2");
    CHECK(FormatEnchantsEnd() == "TBM:ORDER_ENCHANTS_END");
    EnchantSearchReagent reagents[] = { { 12811, 1, 1 }, { 11137, 2, 0 } };
    CHECK(FormatEnchantReagents(reagents, 2) == "12811:1:1;11137:2:0");
    CHECK(FormatEnchantReagents(nullptr, 0) == "-");
    CHECK(FormatEnchantReagents(reagents, 0) == "-");

    // Spell token: a bare id or a shift-clicked spell link.
    uint32_t spellId = 0;
    CHECK(ParseEnchantSpellToken("7428", spellId) && spellId == 7428);
    CHECK(ParseEnchantSpellToken("|cffffffff|Hspell:7428|h[Stamina]|h|r", spellId) && spellId == 7428);
    CHECK(!ParseEnchantSpellToken("0", spellId));
    CHECK(!ParseEnchantSpellToken("enchant", spellId));
    CHECK(!ParseEnchantSpellToken("", spellId));
    CHECK(!ParseEnchantSpellToken("12345678901", spellId));

    // Give verdicts: unknown or off-catalogue is norecipe, low skill is
    // noskill, keep is refused, anything else may be stored.
    CHECK(EnchantGiveVerdict({ true, true, true, false }) == OrderReason::None);
    CHECK(EnchantGiveVerdict({ false, true, true, false }) == OrderReason::NoRecipe);
    CHECK(EnchantGiveVerdict({ true, false, true, false }) == OrderReason::NoRecipe);
    CHECK(EnchantGiveVerdict({ true, true, false, false }) == OrderReason::NoSkill);
    CHECK(EnchantGiveVerdict({ true, true, true, true }) == OrderReason::Enchant);
    CHECK(std::string(EnchantGiveRefusal(OrderReason::NoSkill)) ==
        "The bot's Enchanting skill is too low for that enchant.");
    CHECK(std::string(EnchantGiveRefusal(OrderReason::Enchant)) ==
        "Keep orders are not available for enchants.");
    CHECK(std::string(EnchantGiveRefusal(OrderReason::NoRecipe)) == "The bot does not know that enchant.");

    // Hours and gold cap share the farm/craft shapes: 1-24 or inf, copper.
    uint32_t hours = 99;
    CHECK(ParseEnchantHours("inf", hours) && hours == 0);
    CHECK(ParseEnchantHours("INF", hours) && hours == 0);
    CHECK(ParseEnchantHours("5", hours) && hours == 5);
    CHECK(!ParseEnchantHours("0", hours));
    CHECK(!ParseEnchantHours("25", hours));
    CHECK(!ParseEnchantHours("abc", hours));
    CHECK(IsEnchantHoursToken("5") && IsEnchantHoursToken("24"));
    CHECK(!IsEnchantHoursToken("inf") && !IsEnchantHoursToken(""));
    uint32_t cap = 0;
    CHECK(ParseEnchantGoldCap("0", cap) && cap == 0);
    CHECK(ParseEnchantGoldCap("4294967295", cap) && cap == 4294967295u);
    CHECK(!ParseEnchantGoldCap("", cap));
    CHECK(!ParseEnchantGoldCap("4294967296", cap));
    CHECK(!ParseEnchantGoldCap("abc", cap));

    if (failures == 0)
        std::printf("test_guild_order_enchant_policy ok\n");
    return failures ? 1 : 0;
}
