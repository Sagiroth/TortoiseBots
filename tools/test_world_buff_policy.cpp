// Standalone regression test for issue #492 (stages 1+3): pure world-buff
// policy behind the capital-recruiter branch (runtime/WorldBuffPolicy.h +
// runtime/WorldBuffService.h):
//   - branch gating (flag x bot x level),
//   - capital-recruiter routing (6 entries) and the recruiter-caster filter,
//   - price math (solo / 5 stacked / 40-man, all four tiers),
//   - purchase -> unlock-quest routing for both factions,
//   - aura -> quest map (DM any-one, Sayge any-of-8, Songflower),
//   - boss -> credit map (Ony/Nef only; Rend/Hakkar direct, Gyth excluded),
//   - teleport snapshot/restore/strip table incl. Upper Kara 814,
//   - service price pairs, spell lists (DM triple, Sayge strip-others),
//     Sayge submenu picks.
//
// Build and run (from the repo root):
//   g++ -std=c++17 -Wall -Wextra tools/test_world_buff_policy.cpp -o /tmp/test_world_buff_policy
//   /tmp/test_world_buff_policy

#include "../runtime/WorldBuffPolicy.h"
#include "../runtime/WorldBuffService.h"
#include "../runtime/WorldBuffRaidKeeper.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace TortoiseBots;

static int checks = 0;
#define CHECK(cond) do { \
    ++checks; \
    if (!(cond)) { \
        std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        std::exit(1); \
    } \
} while (0)

static void TestGating()
{
    // Enabled real player at 60 sees the branch; 59 does not.
    CHECK(ShouldShowWorldBuffs(true, false, 60, 60) == true);
    CHECK(ShouldShowWorldBuffs(true, false, 59, 60) == false);
    CHECK(ShouldShowWorldBuffs(true, false, 80, 60) == true);
    // Flag off: nobody sees it, even at 60.
    CHECK(ShouldShowWorldBuffs(false, false, 60, 60) == false);
    // Pool bots only ever see the hire wizard.
    CHECK(ShouldShowWorldBuffs(true, true, 60, 60) == false);
}

static void TestRecruiterRouting()
{
    CHECK(IsCapitalRecruiter(95017) == true); // SW
    CHECK(IsCapitalRecruiter(95006) == true); // IF
    CHECK(IsCapitalRecruiter(95012) == true); // Darnassus
    CHECK(IsCapitalRecruiter(95025) == true); // Org
    CHECK(IsCapitalRecruiter(95018) == true); // UC
    CHECK(IsCapitalRecruiter(95019) == true); // TB
    // The other 61 inn recruiters keep the hire wizard directly.
    CHECK(IsCapitalRecruiter(95000) == false);
    CHECK(IsCapitalRecruiter(95001) == false);
    CHECK(IsCapitalRecruiter(95067) == false);
    CHECK(IsCapitalRecruiter(12345) == false);
}

static void TestRecruiterCasterFilter()
{
    // Auras whose caster is any hire recruiter never credit an aura quest:
    // otherwise a purchase would unlock the buff for the whole group.
    CHECK(IsHireRecruiterEntry(95000) == true);
    CHECK(IsHireRecruiterEntry(95017) == true);
    CHECK(IsHireRecruiterEntry(95067) == true);
    CHECK(IsHireRecruiterEntry(94999) == false);
    CHECK(IsHireRecruiterEntry(95068) == false);
    CHECK(IsHireRecruiterEntry(0) == false);
}

static void TestPrices()
{
    // Standard pair 10g+2g: solo buyer pays base + 1 x per-person.
    CHECK(WorldBuffTotalPrice(100000, 20000, 1) == 120000);
    // Buyer + 4 bots stacked at the NPC: 10g + 5 x 2g = 20g (spec table).
    CHECK(WorldBuffTotalPrice(100000, 20000, 5) == 200000);
    // Full 40-man raid: 10g + 40 x 2g = 90g (spec table).
    CHECK(WorldBuffTotalPrice(100000, 20000, 40) == 900000);
    // Sayge 6g+1g solo: 7g.
    CHECK(WorldBuffTotalPrice(60000, 10000, 1) == 70000);
    // Songflower 4g+1g solo: 5g; 5 stacked: 9g (spec table).
    CHECK(WorldBuffTotalPrice(40000, 10000, 5) == 90000);
    // Silithyst 2g+40s solo: 2.4g; 5 stacked: 4g (spec table).
    CHECK(WorldBuffTotalPrice(20000, 4000, 1) == 24000);
    CHECK(WorldBuffTotalPrice(20000, 4000, 5) == 40000);
    // Empty head count still charges the buyer alone, never zero.
    CHECK(WorldBuffTotalPrice(100000, 20000, 0) == 120000);

    CHECK(PriceTierForPurchase(kWorldBuffBuyRally) == WorldBuffPriceTier::Standard);
    CHECK(PriceTierForPurchase(kWorldBuffBuyDmPack) == WorldBuffPriceTier::Standard);
    CHECK(PriceTierForPurchase(kWorldBuffBuySayge) == WorldBuffPriceTier::Sayge);
    CHECK(PriceTierForPurchase(kWorldBuffBuySongflower) == WorldBuffPriceTier::Songflower);
    CHECK(PriceTierForPurchase(kWorldBuffBuySilithyst) == WorldBuffPriceTier::Silithyst);

    CHECK(IsValidWorldBuffPurchase(kWorldBuffBuyRally) == true);
    CHECK(IsValidWorldBuffPurchase(kWorldBuffBuySilithyst) == true);
    CHECK(IsValidWorldBuffPurchase(0) == false);
    CHECK(IsValidWorldBuffPurchase(8) == false);
    CHECK(IsValidWorldBuffPurchase(255) == false);
}

static void TestPurchaseQuests()
{
    // Every purchase maps to exactly one unlock quest per faction.
    CHECK(PurchaseUnlockQuest(kWorldBuffBuyRally, false) == 90000);
    CHECK(PurchaseUnlockQuest(kWorldBuffBuyRally, true) == 90001);
    CHECK(PurchaseUnlockQuest(kWorldBuffBuyWarchief, false) == 90002);
    CHECK(PurchaseUnlockQuest(kWorldBuffBuyWarchief, true) == 90003);
    CHECK(PurchaseUnlockQuest(kWorldBuffBuyZandalar, false) == 90004);
    CHECK(PurchaseUnlockQuest(kWorldBuffBuyZandalar, true) == 90005);
    CHECK(PurchaseUnlockQuest(kWorldBuffBuyDmPack, false) == 90006);
    CHECK(PurchaseUnlockQuest(kWorldBuffBuyDmPack, true) == 90007);
    CHECK(PurchaseUnlockQuest(kWorldBuffBuySayge, false) == 90008);
    CHECK(PurchaseUnlockQuest(kWorldBuffBuySayge, true) == 90009);
    CHECK(PurchaseUnlockQuest(kWorldBuffBuySongflower, false) == 90010);
    CHECK(PurchaseUnlockQuest(kWorldBuffBuySongflower, true) == 90011);
    CHECK(PurchaseUnlockQuest(kWorldBuffBuySilithyst, false) == 90012);
    CHECK(PurchaseUnlockQuest(kWorldBuffBuySilithyst, true) == 90013);
    CHECK(PurchaseUnlockQuest(0, false) == 0);
}

static void TestAuraMap()
{
    // DM: any ONE of the three guard buffs counts.
    CHECK(AuraUnlockQuest(22817, false) == 90006);
    CHECK(AuraUnlockQuest(22818, false) == 90006);
    CHECK(AuraUnlockQuest(22820, true) == 90007);
    // Sayge: any of the 8 fortunes counts, both factions.
    uint32_t saygeCount = 0;
    WorldBuffSaygeVariant const* variants = SaygeVariants(saygeCount);
    CHECK(saygeCount == 8);
    for (uint32_t i = 0; i < saygeCount; ++i)
    {
        CHECK(AuraUnlockQuest(variants[i].spellId, false) == 90008);
        CHECK(AuraUnlockQuest(variants[i].spellId, true) == 90009);
        for (uint32_t j = i + 1; j < saygeCount; ++j)
            CHECK(variants[i].spellId != variants[j].spellId);
    }
    // Songflower.
    CHECK(AuraUnlockQuest(15366, false) == 90010);
    CHECK(AuraUnlockQuest(15366, true) == 90011);
    // Purchasable raid buffs are not aura unlocks; unknown auras map to 0.
    CHECK(AuraUnlockQuest(22888, false) == 0);
    CHECK(AuraUnlockQuest(16609, true) == 0);
    CHECK(AuraUnlockQuest(24425, false) == 0);
    CHECK(AuraUnlockQuest(29534, true) == 0);
    CHECK(AuraUnlockQuest(0, false) == 0);
}

static void TestBossMap()
{
    // Onyxia OR Nefarian feed the invisible Rally credit 95100.
    CHECK(BossKillCreditEntry(10184) == 95100);
    CHECK(BossKillCreditEntry(11583) == 95100);
    // Rend and Hakkar quests use direct kill objectives: no translation.
    CHECK(BossKillCreditEntry(10429) == 0);
    CHECK(BossKillCreditEntry(14834) == 0);
    // Gyth (dies with Rend) must never credit: it is not the quest target.
    CHECK(BossKillCreditEntry(10339) == 0);
    CHECK(BossKillCreditEntry(0) == 0);
}

static void TestTeleportTable()
{
    // Every core strip map needs a snapshot, and so does Upper Kara 814
    // (the gap the module covers itself).
    CHECK(NeedsBuffSnapshot(409) == true);  // Molten Core
    CHECK(NeedsBuffSnapshot(469) == true);  // Blackwing Lair
    CHECK(NeedsBuffSnapshot(814) == true);   // Upper Karazhan
    CHECK(NeedsBuffSnapshot(0) == false);    // Eastern Kingdoms
    CHECK(NeedsBuffSnapshot(1) == false);    // Kalimdor
    CHECK(NeedsBuffSnapshot(530) == false);  // Outland (not a strip map)
    // Keep ON restores on core strip maps only; 814 needs no restore
    // (core never stripped it).
    CHECK(ShouldRestoreAfterTeleport(true, 409) == true);
    CHECK(ShouldRestoreAfterTeleport(true, 814) == false);
    CHECK(ShouldRestoreAfterTeleport(false, 409) == false);
    CHECK(ShouldRestoreAfterTeleport(true, 0) == false);
    // Keep OFF strips Upper Kara; strip maps are already handled by core.
    CHECK(ShouldStripUpperKara(false, 814) == true);
    CHECK(ShouldStripUpperKara(true, 814) == false);
    CHECK(ShouldStripUpperKara(false, 409) == false);
    CHECK(ShouldStripUpperKara(false, 0) == false);
}

static void TestRaidKeeper()
{
    // The 18-spell strip set matches live instance_buff_removal: all seven
    // buffs plus 18968/26393/28681, and never Silithyst (29534).
    uint32_t count = 0;
    RaidStripSpells(count);
    CHECK(count == 18);
    CHECK(IsRaidStripSpell(22888) == true);
    CHECK(IsRaidStripSpell(16609) == true);
    CHECK(IsRaidStripSpell(24425) == true);
    CHECK(IsRaidStripSpell(22817) == true);
    CHECK(IsRaidStripSpell(22818) == true);
    CHECK(IsRaidStripSpell(22820) == true);
    CHECK(IsRaidStripSpell(15366) == true);
    CHECK(IsRaidStripSpell(18968) == true);
    CHECK(IsRaidStripSpell(26393) == true);
    CHECK(IsRaidStripSpell(28681) == true);
    CHECK(IsRaidStripSpell(23735) == true);
    CHECK(IsRaidStripSpell(23769) == true);
    CHECK(IsRaidStripSpell(29534) == false);
    CHECK(IsRaidStripSpell(0) == false);
    // Snapshot store: take consumes, empty snapshot erases, logout (no
    // take) drops the buffs by design.
    WorldBuffRaidKeeper keeper;
    std::vector<WorldBuffSnapshotEntry> out;
    CHECK(keeper.Take(123, out) == false);
    keeper.Snapshot(123, { { 22888, 7000000, 7200000 } });
    CHECK(keeper.Size() == 1);
    CHECK(keeper.Take(123, out) == true);
    CHECK(out.size() == 1 && out[0].spellId == 22888);
    CHECK(out[0].remainMs == 7000000 && out[0].maxMs == 7200000);
    CHECK(keeper.Take(123, out) == false);
    keeper.Snapshot(7, {});
    CHECK(keeper.Size() == 0);
    keeper.Snapshot(7, { { 16609, 1000, 3600000 } });
    keeper.Clear(7);
    CHECK(keeper.Size() == 0);
}

static void TestServicePrices()
{
    // Config pairs flow through unchanged: standard 10g+2g, Sayge 6g+1g,
    WorldBuffPricePair pair;
    CHECK(WorldBuffPrices(1, pair, 100000, 20000, 60000, 10000, 40000, 10000, 20000, 4000) == true);
    CHECK(pair.baseCopper == 100000 && pair.perPersonCopper == 20000);
    CHECK(WorldBuffPrices(4, pair, 100000, 20000, 60000, 10000, 40000, 10000, 20000, 4000) == true);
    CHECK(pair.baseCopper == 100000 && pair.perPersonCopper == 20000);
    CHECK(WorldBuffPrices(5, pair, 100000, 20000, 60000, 10000, 40000, 10000, 20000, 4000) == true);
    CHECK(pair.baseCopper == 60000 && pair.perPersonCopper == 10000);
    CHECK(WorldBuffPrices(6, pair, 100000, 20000, 60000, 10000, 40000, 10000, 20000, 4000) == true);
    CHECK(pair.baseCopper == 40000 && pair.perPersonCopper == 10000);
    CHECK(WorldBuffPrices(7, pair, 100000, 20000, 60000, 10000, 40000, 10000, 20000, 4000) == true);
    CHECK(pair.baseCopper == 20000 && pair.perPersonCopper == 4000);
    CHECK(WorldBuffPrices(0, pair, 100000, 20000, 60000, 10000, 40000, 10000, 20000, 4000) == false);
    CHECK(WorldBuffPrices(8, pair, 100000, 20000, 60000, 10000, 40000, 10000, 20000, 4000) == false);
}

static void TestServiceSpells()
{
    std::vector<uint32_t> apply;
    std::vector<uint32_t> strip;
    // Singles.
    CHECK(WorldBuffSpells(1, 0, apply, strip) == true && apply.size() == 1 && apply[0] == 22888);
    CHECK(WorldBuffSpells(2, 0, apply, strip) == true && apply.size() == 1 && apply[0] == 16609);
    CHECK(WorldBuffSpells(3, 0, apply, strip) == true && apply.size() == 1 && apply[0] == 24425);
    CHECK(WorldBuffSpells(6, 0, apply, strip) == true && apply.size() == 1 && apply[0] == 15366);
    CHECK(WorldBuffSpells(7, 0, apply, strip) == true && apply.size() == 1 && apply[0] == 29534);
    // DM pack: one purchase lands all three guard buffs.
    CHECK(WorldBuffSpells(4, 0, apply, strip) == true && apply.size() == 3);
    CHECK(apply[0] == 22817 && apply[1] == 22818 && apply[2] == 22820);
    CHECK(strip.empty() == true);
    // Sayge: chosen variant applies, the other 7 strip first (single active).
    CHECK(WorldBuffSpells(5, 23768, apply, strip) == true);
    CHECK(apply.size() == 1 && apply[0] == 23768);
    CHECK(strip.size() == 7);
    for (uint32_t id : strip)
        CHECK(id != 23768 && IsSaygeFortuneAura(id) == true);
    // Unknown variant or purchase fails closed (menu re-shown, no charge).
    CHECK(WorldBuffSpells(5, 0, apply, strip) == false);
    CHECK(WorldBuffSpells(5, 22888, apply, strip) == false);
    CHECK(WorldBuffSpells(0, 0, apply, strip) == false);
    CHECK(WorldBuffSpells(9, 0, apply, strip) == false);
    // Submenu: 8 distinct known variants.
    uint32_t count = 0;
    WorldBuffSaygePick const* picks = WorldBuffSaygePicks(count);
    CHECK(count == 8);
    for (uint32_t i = 0; i < count; ++i)
    {
        CHECK(IsKnownSaygeChoice(picks[i].spellId) == true);
        CHECK(IsSaygeFortuneAura(picks[i].spellId) == true);
        for (uint32_t j = i + 1; j < count; ++j)
            CHECK(picks[i].spellId != picks[j].spellId);
    }
    CHECK(IsKnownSaygeChoice(0) == false);
    CHECK(IsKnownSaygeChoice(22888) == false);
}

static void TestSaygeActionEncoding()
{
    // Standard buys pass the buy index directly; saygePick is 0.
    for (uint8_t buy = 1; buy <= 7; ++buy)
    {
        CHECK(DecodeBuyIndex(buy) == buy);
        CHECK(DecodeSaygePick(buy) == 0);
    }
    // Sayge submenu actions pack (pick << 8) | buy.
    for (uint32_t pick = 1; pick <= 8; ++pick)
    {
        uint32_t action = EncodeSaygeAction(kWorldBuffBuySayge, pick);
        CHECK(DecodeBuyIndex(action) == kWorldBuffBuySayge);
        CHECK(DecodeSaygePick(action) == pick);
    }
}

static void TestAuraUnlockFastCheck()
{
    // Exactly 12 tracked auras: Songflower, 3 DM tribute guard buffs, 8 Sayge fortunes.
    CHECK(IsAuraUnlockSpell(15366) == true);
    CHECK(IsAuraUnlockSpell(22817) == true);
    CHECK(IsAuraUnlockSpell(22818) == true);
    CHECK(IsAuraUnlockSpell(22820) == true);
    CHECK(IsAuraUnlockSpell(23735) == true);
    CHECK(IsAuraUnlockSpell(23736) == true);
    CHECK(IsAuraUnlockSpell(23737) == true);
    CHECK(IsAuraUnlockSpell(23738) == true);
    CHECK(IsAuraUnlockSpell(23766) == true);
    CHECK(IsAuraUnlockSpell(23767) == true);
    CHECK(IsAuraUnlockSpell(23768) == true);
    CHECK(IsAuraUnlockSpell(23769) == true);
    // Non-unlock spells return false.
    CHECK(IsAuraUnlockSpell(22888) == false);
    CHECK(IsAuraUnlockSpell(16609) == false);
    CHECK(IsAuraUnlockSpell(24425) == false);
    CHECK(IsAuraUnlockSpell(29534) == false);
    CHECK(IsAuraUnlockSpell(0) == false);
}

int main()
{
    TestGating();
    TestRecruiterRouting();
    TestRecruiterCasterFilter();
    TestPrices();
    TestPurchaseQuests();
    TestAuraMap();
    TestBossMap();
    TestTeleportTable();
    TestRaidKeeper();
    TestServicePrices();
    TestServiceSpells();
    TestSaygeActionEncoding();
    TestAuraUnlockFastCheck();
    std::printf("world buff policy: %d checks passed\n", checks);
    return 0;
}
