#include "../runtime/AltBotPolicy.h"

#include <cassert>
#include <iostream>

using namespace TortoiseBots;

static void TestStart()
{
    assert(CheckAltBotStart(true, true, false, true) == AltBotStartRefusal::None);
    // Config off: a kicked alt stays a companion (G48 as decided on #872).
    assert(CheckAltBotStart(false, true, false, true) == AltBotStartRefusal::FeatureOff);
    // Pool and hired characters are never alt-bots.
    assert(CheckAltBotStart(true, false, false, true) == AltBotStartRefusal::NotOwnedAlt);
    assert(CheckAltBotStart(true, true, false, false) == AltBotStartRefusal::NotInWorld);
    // Hardcore is refused.
    assert(CheckAltBotStart(true, true, true, true) == AltBotStartRefusal::Hardcore);
}

static void TestDisplace()
{
    std::vector<PoolDisplaceCandidate> none;
    assert(PickPoolBotToDisplace(none, 7) == 0);

    std::vector<PoolDisplaceCandidate> c(5);
    c[0] = {10, 5, true, false, false, false};   // grouped
    c[1] = {11, 3, false, true, false, false};   // in combat
    c[2] = {12, 2, false, false, true, false};   // in an instance
    c[3] = {13, 1, false, false, false, true};   // has a master
    c[4] = {14, 30, false, false, false, false}; // the only free one
    assert(PickPoolBotToDisplace(c, 0) == 14);
    assert(PickPoolBotToDisplace(c, 99) == 14);

    // Lowest level wins; ties are picked by the roll.
    c.push_back({15, 8, false, false, false, false});
    c.push_back({16, 8, false, false, false, false});
    assert(PickPoolBotToDisplace(c, 0) == 15);
    assert(PickPoolBotToDisplace(c, 1) == 16);
    assert(PickPoolBotToDisplace(c, 2) == 15);

    for (PoolDisplaceCandidate& x : c)
        x.grouped = true;
    assert(PickPoolBotToDisplace(c, 0) == 0);
}

static void TestTarget()
{
    assert(PoolTargetWithAlts(500, 0) == 500);
    assert(PoolTargetWithAlts(500, 3) == 497);
    assert(PoolTargetWithAlts(2, 5) == 0);
}

static AltBotLocation Base()
{
    AltBotLocation loc;
    loc.inWorld = true;
    loc.alive = true;
    loc.haveGrave = true;
    loc.graveSameMap = true;
    return loc;
}

static void TestStartExit()
{
    // Grouped: stays put (F42).
    AltBotLocation in = Base();
    in.instanceableMap = true;
    in.dungeonMap = true;
    in.haveExit = true;
    assert(PickAltBotStartExit(in, true) == AltBotExitTarget::None);
    // Ordinary map with no group anywhere odd: no relocation.
    assert(PickAltBotStartExit(Base(), false) == AltBotExitTarget::None);
    // Dungeon exit preferred over the graveyard.
    assert(PickAltBotStartExit(in, false) == AltBotExitTarget::InstanceExit);
    // Dungeon without a usable exit: the graveyard.
    in.haveExit = false;
    assert(PickAltBotStartExit(in, false) == AltBotExitTarget::Graveyard);
    // Dungeon with no graveyard row at all: stay (caller scatter is per-map).
    in.haveGrave = false;
    assert(PickAltBotStartExit(in, false) == AltBotExitTarget::None);
    // BG: always the graveyard, never the instance exit path.
    AltBotLocation bg = Base();
    bg.inBattleGround = true;
    bg.instanceableMap = true;
    bg.dungeonMap = true;
    bg.haveExit = true;
    assert(PickAltBotStartExit(bg, false) == AltBotExitTarget::Graveyard);
    // Transport: graveyard when one is known.
    AltBotLocation boat = Base();
    boat.onTransport = true;
    assert(PickAltBotStartExit(boat, false) == AltBotExitTarget::Graveyard);
    // Not in world or dead: no relocation.
    AltBotLocation out = in;
    out.inWorld = false;
    assert(PickAltBotStartExit(out, false) == AltBotExitTarget::None);
}

static void TestSweepDue()
{
    assert(!AltBotSweepDue(4999, 0));
    assert(AltBotSweepDue(5000, 0));
    assert(AltBotSweepDue(9000, 4000));
    assert(!AltBotSweepDue(8999, 4000));
    // 32-bit wrap: 0xFFFFFF00 -> 0x1500 is 0x1600 ms past (due);
    // -> 0x1000 is only 0x1100 ms (not due).
    assert(AltBotSweepDue(0x1500, 0xFFFFFF00));
    assert(!AltBotSweepDue(0x1000, 0xFFFFFF00));
}

static void TestLeaveInstance()
{
    // Solo alt forgotten in a dungeon: leaves.
    assert(ShouldAltBotLeaveInstance(true, true, true, false, true, false, false));
    // Grouped but nobody else on its map: leaves.
    assert(ShouldAltBotLeaveInstance(true, true, true, false, true, true, false));
    // Party mate on the same map: stays.
    assert(!ShouldAltBotLeaveInstance(true, true, true, false, true, true, true));
    // Not autonomous, dead, teleporting, or not in a dungeon: stays.
    assert(!ShouldAltBotLeaveInstance(false, true, true, false, true, false, false));
    assert(!ShouldAltBotLeaveInstance(true, false, true, false, true, false, false));
    assert(!ShouldAltBotLeaveInstance(true, true, false, false, true, false, false));
    assert(!ShouldAltBotLeaveInstance(true, true, true, true, true, false, false));
    assert(!ShouldAltBotLeaveInstance(true, true, true, false, false, false, false));
}

static void TestSummary()
{
    AltBotSessionCounters c;
    c.startLevel = 12;
    c.endLevel = 14;
    c.quests = 1;
    c.deaths = 2;
    c.startMoney = 10000;
    c.endMoney = 35050;
    assert(FormatAltBotSummary("Alt", c) ==
        "Alt played on its own: level 12 -> 14, 1 quest, 2 deaths, gold +2g 50s 50c.");

    c.endLevel = 12;
    c.quests = 0;
    c.deaths = 1;
    c.endMoney = 9990;
    assert(FormatAltBotSummary("Alt", c) ==
        "Alt played on its own: level 12, 0 quests, 1 death, gold -10c.");
    assert(FormatAltBotMoney(0) == "+0c");
    assert(FormatAltBotMoney(100) == "+1s 0c");
}

static void TestAwaySummary()
{
    AltBotAwayPoint from;
    from.level = 23;
    from.money = 10000;
    AltBotAwayPoint to = from;
    to.level = 26;
    to.money = 10000 + 12 * 10000 + 300;
    to.quests = 8;
    to.deaths = 2;
    AltBotAwayDiff diff = DiffAltBotAway(from, to);
    assert(diff.fromLevel == 23 && diff.toLevel == 26);
    assert(diff.money == 12 * 10000 + 300);
    assert(diff.quests == 8 && diff.deaths == 2);
    std::string line = FormatAltBotAwayLine("Bob", diff, {"[Sword]", "[Helm]"}, "Stranglethorn Vale", "questing");
    assert(line ==
        "Bob: level 23 -> 26, +12g 3s 0c, 8 quests, died twice, equipped [Sword] [Helm], now in Stranglethorn Vale, questing.");
    AltBotAwayDiff flat = DiffAltBotAway(from, from);
    assert(FormatAltBotAwayLine("Bob", flat, {}, "Elwynn Forest", "grinding") ==
        "Bob: no change, now in Elwynn Forest, grinding.");
    assert(FormatAltBotAwayLine("Bob", flat, {}, "", "") == "Bob: no change.");
    AltBotAwayPoint lower = from;
    lower.money = 5000;
    AltBotAwayDiff spent = DiffAltBotAway(from, lower);
    assert(spent.money == -5000 && spent.quests == 0 && spent.deaths == 0);
    assert(FormatAltBotAwayLine("Bob", spent, {}, "Durotar", "fighting") ==
        "Bob: -50s 0c, now in Durotar, fighting.");
    assert(std::string(AltBotAwayActivityWord(true, true, true, true, true, true)) == "dead");
    assert(std::string(AltBotAwayActivityWord(false, true, true, true, true, true)) == "fighting");
    assert(std::string(AltBotAwayActivityWord(false, false, true, true, true, true)) == "questing");
    assert(std::string(AltBotAwayActivityWord(false, false, false, true, true, false)) == "travelling");
    assert(std::string(AltBotAwayActivityWord(false, false, false, false, true, false)) == "in town");
    assert(std::string(AltBotAwayActivityWord(false, false, false, false, false, true)) == "resting");
    assert(std::string(AltBotAwayActivityWord(false, false, false, false, false, false)) == "grinding");
}


static void TestProtections()
{
    // B9: autonomous economy is off; companions keep today's behaviour.
    assert(AltBotMayUseEconomy(true) == false);
    assert(AltBotMayUseEconomy(false) == true);
    // B10: greys and whites sell; soulbound/equipped never do.
    assert(AltBotMayVendorItem(0, false, false) == true);
    assert(AltBotMayVendorItem(1, false, false) == true);
    assert(AltBotMayVendorItem(2, false, false) == false);
    assert(AltBotMayVendorItem(0, true, false) == false);
    assert(AltBotMayVendorItem(1, false, true) == false);
    // B10: repair is paid without the gold cheat.
    assert(AltBotRepairIsPaid(false) == true);
    assert(AltBotRepairIsPaid(true) == false);
    // B11: spells, repair, flight only.
    assert(AltBotMaySpendGoldOn(AltBotBuyNeed::TrainerSpell) == true);
    assert(AltBotMaySpendGoldOn(AltBotBuyNeed::Repair) == true);
    assert(AltBotMaySpendGoldOn(AltBotBuyNeed::FlightPath) == true);
    assert(AltBotMaySpendGoldOn(AltBotBuyNeed::Mount) == false);
    assert(AltBotMaySpendGoldOn(AltBotBuyNeed::Bag) == false);
    assert(AltBotMaySpendGoldOn(AltBotBuyNeed::Respec) == false);
    assert(AltBotMaySpendGoldOn(AltBotBuyNeed::OtherVendorBuy) == false);
    // B12: autonomous alts never auto-spend talents.
    assert(AltBotMayAutoSpendTalents(true) == false);
    assert(AltBotMayAutoSpendTalents(false) == true);
    // B13: best score wins, earliest on ties.
    assert(AltBotQuestRewardChoice({{0, 10}, {1, 30}, {2, 20}}) == 1);
    assert(AltBotQuestRewardChoice({{0, 30}, {1, 30}}) == 0);
    assert(AltBotQuestRewardChoice({}) == 0);
    // B14: no room for the old piece means no equip.
    assert(AltBotMayAutoEquip(false) == false);
    assert(AltBotMayAutoEquip(true) == true);
    // B16: drop boost is a pool property.
    assert(AltBotMayGetDropBoost(true) == true);
    assert(AltBotMayGetDropBoost(false) == false);
    // C: Need demotes to Greed for alts; others pass through.
    assert(AltBotGroupLootVote(2, true) == 1);
    assert(AltBotGroupLootVote(1, true) == 1);
    assert(AltBotGroupLootVote(0, true) == 0);
    assert(AltBotGroupLootVote(2, false) == 2);
    // B15: item cheat must be on for the alt.
    assert(AltBotItemCheatCovered(true) == true);
    assert(AltBotItemCheatCovered(false) == false);
    // Footprint: no hearth rebind while autonomous.
    assert(AltBotMayRebindHearth(true) == false);
    assert(AltBotMayRebindHearth(false) == true);
    // Footprint: 10 free quest slots; only abandon own quests.
    assert(AltBotMayAcceptQuest(14) == true);
    assert(AltBotMayAcceptQuest(15) == false);
    assert(AltBotMayAcceptQuest(24) == false);
    assert(AltBotMayAbandonQuest(true, true) == true);
    assert(AltBotMayAbandonQuest(true, false) == false);
    assert(AltBotMayAbandonQuest(false, false) == true);
    // Footprint: rep-lowering kills are never picked solo.
    assert(AltBotMayPickTarget(true) == false);
    assert(AltBotMayPickTarget(false) == true);
}

static void TestStrangerCommands()
{
    // Combat movement and orders: allowed for strangers.
    assert(CheckAltBotStrangerCommand("follow") == AltBotStrangerVerdict::Allowed);
    assert(CheckAltBotStrangerCommand("  Attack  skull") == AltBotStrangerVerdict::Allowed);
    assert(CheckAltBotStrangerCommand("pull my target") == AltBotStrangerVerdict::Allowed);
    assert(CheckAltBotStrangerCommand("tank attack") == AltBotStrangerVerdict::Allowed);
    assert(CheckAltBotStrangerCommand("stay") == AltBotStrangerVerdict::Allowed);
    assert(CheckAltBotStrangerCommand("guard") == AltBotStrangerVerdict::Allowed);
    assert(CheckAltBotStrangerCommand("formation") == AltBotStrangerVerdict::Allowed);
    // Read-only info: allowed.
    assert(CheckAltBotStrangerCommand("stats") == AltBotStrangerVerdict::Allowed);
    assert(CheckAltBotStrangerCommand("c") == AltBotStrangerVerdict::Allowed);
    assert(CheckAltBotStrangerCommand("where") == AltBotStrangerVerdict::Allowed);
    // Blocked for non-owners: trade, selling, destroying, equipping,
    // talents, quest abandon, logout, guild kick, hearth bind, and any
    // manipulation outside combat (Issue #872 B8).
    assert(CheckAltBotStrangerCommand("trade") == AltBotStrangerVerdict::OwnerOnly);
    assert(CheckAltBotStrangerCommand("sell") == AltBotStrangerVerdict::OwnerOnly);
    assert(CheckAltBotStrangerCommand("destroy") == AltBotStrangerVerdict::OwnerOnly);
    assert(CheckAltBotStrangerCommand("equip") == AltBotStrangerVerdict::OwnerOnly);
    assert(CheckAltBotStrangerCommand("talents") == AltBotStrangerVerdict::OwnerOnly);
    assert(CheckAltBotStrangerCommand("drop") == AltBotStrangerVerdict::OwnerOnly);
    assert(CheckAltBotStrangerCommand("logout") == AltBotStrangerVerdict::OwnerOnly);
    assert(CheckAltBotStrangerCommand("gk") == AltBotStrangerVerdict::OwnerOnly);
    assert(CheckAltBotStrangerCommand("guild leave") == AltBotStrangerVerdict::OwnerOnly);
    assert(CheckAltBotStrangerCommand("home") == AltBotStrangerVerdict::OwnerOnly);
    assert(CheckAltBotStrangerCommand("grind") == AltBotStrangerVerdict::OwnerOnly);
    assert(CheckAltBotStrangerCommand("co +defensive") == AltBotStrangerVerdict::OwnerOnly);
    assert(CheckAltBotStrangerCommand("hire") == AltBotStrangerVerdict::OwnerOnly);
    assert(CheckAltBotStrangerCommand("invite") == AltBotStrangerVerdict::OwnerOnly);
    assert(CheckAltBotStrangerCommand("leave") == AltBotStrangerVerdict::OwnerOnly);
    // Unknown commands fail closed.
    assert(CheckAltBotStrangerCommand("") == AltBotStrangerVerdict::OwnerOnly);
    assert(CheckAltBotStrangerCommand("frobnicate") == AltBotStrangerVerdict::OwnerOnly);
    // `.bot cmd` verbs mirror the chat list.
    assert(CheckAltBotStrangerOrder("attack") == AltBotStrangerVerdict::Allowed);
    assert(CheckAltBotStrangerOrder("loot") == AltBotStrangerVerdict::OwnerOnly);
    assert(CheckAltBotStrangerOrder("revive") == AltBotStrangerVerdict::OwnerOnly);
}

static void TestGuildInvite()
{
    assert(AltBotMayAcceptGuildInvite(true, false));
    assert(AltBotMayAcceptGuildInvite(false, true));
    assert(!AltBotMayAcceptGuildInvite(false, false));
    // Group invites need no gate: anyone may invite the alt like a pool bot.
    assert(AltBotMayAcceptGroupInvite());
}

static void TestDeletionGuards()
{
    // G49: a character on a non-registered (player) account is excluded from
    // the pool reset; a registered pool account is not an alt.
    assert(AltBotExcludedFromPoolReset(false));
    assert(!AltBotExcludedFromPoolReset(true));
    // G50: a character with no hire ledger row never reaches hire deletion.
    assert(AltBotExcludedFromHireDeletion(false));
    assert(!AltBotExcludedFromHireDeletion(true));
}

int main()
{
    TestStart();
    TestDisplace();
    TestTarget();
    TestSummary();
    TestAwaySummary();
    TestStartExit();
    TestSweepDue();
    TestLeaveInstance();
    TestProtections();
    TestStrangerCommands();
    TestGuildInvite();
    TestDeletionGuards();
    std::cout << "test_alt_bot_policy: OK" << std::endl;
    return 0;
}
