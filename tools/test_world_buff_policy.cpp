// Standalone regression test for issue #492 (stage 1): pure world-buff
// policy behind the capital-recruiter branch (runtime/WorldBuffPolicy.h):
//   - branch gating (flag x bot x level),
//   - capital-recruiter routing (6 entries) and the recruiter-caster filter,
//   - price math (solo / 5 stacked / 40-man, all four tiers),
//   - purchase -> unlock-quest routing for both factions,
//   - aura -> quest map (DM any-one, Sayge any-of-8, Songflower),
//   - boss -> credit map (Ony/Nef only; Rend/Hakkar direct, Gyth excluded),
//   - teleport snapshot/restore/strip table incl. Upper Kara 814.
//
// Build and run (from the repo root):
//   g++ -std=c++17 -Wall -Wextra tools/test_world_buff_policy.cpp -o /tmp/test_world_buff_policy
//   /tmp/test_world_buff_policy

#include "../runtime/WorldBuffPolicy.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>

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
    std::printf("world buff policy: %d checks passed\n", checks);
    return 0;
}
