#include "../runtime/policy/hire/ClaimedBotPolicy.h"

#include <cassert>
#include <iostream>

using namespace TortoiseBots;

static void TestEligibility()
{
    // Normal wandering bot eligible
    assert(MayClaimBot(true, true, false, false));

    // Already in a guild -> rejected
    assert(!MayClaimBot(false, true, false, false));

    // Not a pool bot (e.g. real player or already personal alt) -> rejected
    assert(!MayClaimBot(true, false, false, false));

    // Already claimed -> rejected
    assert(!MayClaimBot(true, true, true, false));

    // Opposing faction -> rejected by default or when cross-faction guilds are off
    assert(!MayClaimBot(true, true, false, true));
    assert(!MayClaimBot(true, true, false, true, false, false));

    // Opposing faction -> allowed when cross-faction guilds are enabled
    assert(MayClaimBot(true, true, false, true, false, true));

    // Hired bot -> rejected even with cross-faction guilds enabled
    assert(!MayClaimBot(true, true, false, false, true));
    assert(!MayClaimBot(true, true, false, true, true, true));
}

static void TestSelfEquip()
{
    // Unclaimed bots can self-equip at any level regardless of the claimed setting
    assert(MayClaimedBotSelfEquip(10, false, true));
    assert(MayClaimedBotSelfEquip(60, false, true));
    assert(MayClaimedBotSelfEquip(60, false, false));

    // Claimed bots: leveling can self-equip regardless of the level-60 setting
    assert(MayClaimedBotSelfEquip(1, true, true));
    assert(MayClaimedBotSelfEquip(25, true, true));
    assert(MayClaimedBotSelfEquip(59, true, true));
    assert(MayClaimedBotSelfEquip(1, true, false));
    assert(MayClaimedBotSelfEquip(59, true, false));

    // Claimed bots at max level (60+): allowed by default, locked when disabled
    assert(MayClaimedBotSelfEquip(60, true));
    assert(MayClaimedBotSelfEquip(60, true, true));
    assert(MayClaimedBotSelfEquip(61, true, true));
    assert(!MayClaimedBotSelfEquip(60, true, false));
    assert(!MayClaimedBotSelfEquip(61, true, false));
}

static void TestPlayerOverridePin()
{
    assert(ShouldPinPlayerEquippedItem(true, true));
    assert(!ShouldPinPlayerEquippedItem(true, false));
    assert(!ShouldPinPlayerEquippedItem(false, true));
    assert(!ShouldPinPlayerEquippedItem(false, false));
    assert(ShouldPinPlayerEquippedItem(false, true, true));
    assert(!ShouldPinPlayerEquippedItem(false, false, true));
}

static void TestKeepDisplacedGear()
{
    assert(ShouldKeepDisplacedClaimedGear(true));
    assert(!ShouldKeepDisplacedClaimedGear(false));
}

static void TestAutoVendor()
{
    // Unclaimed bots can auto-vendor any quality
    assert(MayClaimedBotAutoVendor(30, kQualityPoor, false));
    assert(MayClaimedBotAutoVendor(30, kQualityRare, false));
    assert(MayClaimedBotAutoVendor(60, kQualityEpic, false));

    // Claimed bots below 60: can vendor grey/white/green leveling junk
    assert(MayClaimedBotAutoVendor(30, kQualityPoor, true));
    assert(MayClaimedBotAutoVendor(30, kQualityCommon, true));
    assert(MayClaimedBotAutoVendor(30, kQualityUncommon, true));

    // Claimed bots below 60: Rare and Epic are preserved in bags!
    assert(!MayClaimedBotAutoVendor(30, kQualityRare, true));
    assert(!MayClaimedBotAutoVendor(30, kQualityEpic, true));
    assert(!MayClaimedBotAutoVendor(30, kQualityLegendary, true));

    // Claimed bots at level 60: never auto-vendor anything!
    assert(!MayClaimedBotAutoVendor(60, kQualityPoor, true));
    assert(!MayClaimedBotAutoVendor(60, kQualityCommon, true));
    assert(!MayClaimedBotAutoVendor(60, kQualityUncommon, true));
    assert(!MayClaimedBotAutoVendor(60, kQualityRare, true));
    assert(!MayClaimedBotAutoVendor(60, kQualityEpic, true));
}

static void TestResetExemption()
{
    assert(IsClaimedBotExemptFromReset(true));
    assert(!IsClaimedBotExemptFromReset(false));
}

int main()
{
    TestEligibility();
    TestSelfEquip();
    TestPlayerOverridePin();
    TestKeepDisplacedGear();
    TestAutoVendor();
    TestResetExemption();

    std::cout << "claimed bot policy: all checks passed\n";
    return 0;
}
