// Standalone check for ai/playerbot/BossTacticsPolicy.h:
//   g++ -std=c++17 -Wall -Wextra tools/test_boss_tactics_policy.cpp -o /tmp/test_boss_tactics_policy && /tmp/test_boss_tactics_policy
#include "../ai/playerbot/BossTacticsPolicy.h"
#include "../ai/playerbot/BombRunoutPolicy.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <iostream>
#include <map>
#include <utility>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using namespace ai;

static bool Near(float a, float b) { return std::fabs(a - b) < 0.01f; }

static BossTacticRow Row(std::uint32_t map, std::uint32_t boss, BossTacticKind kind, std::uint32_t target = 0,
                         std::uint32_t spell = 0, std::uint8_t priority = 0, float param = 0.0f)
{
    BossTacticRow row;
    row.map = map;
    row.bossEntry = boss;
    row.kind = kind;
    row.targetEntry = target;
    row.spellId = spell;
    row.priority = priority;
    row.param = param;
    return row;
}

static BossTacticRow Gated(BossTacticRow row, std::uint8_t hpMin, std::uint8_t hpMax, std::uint32_t botAura = 0)
{
    row.hpMin = hpMin;
    row.hpMax = hpMax;
    row.botAura = botAura;
    return row;
}

static ActiveBossRows Active(const std::vector<BossTacticRow>& rows)
{
    ActiveBossRows out;
    for (const BossTacticRow& row : rows)
        out.push_back(&row);
    return out;
}

static BossTacticRow DispelRow(std::uint32_t spell, std::uint8_t priority, TacticMode mode, float param = 0.0f)
{
    BossTacticRow row = Row(309, 11380, BossTacticKind::DispelPriority, 0, spell, priority, param);
    row.mode = mode;
    return row;
}

// The 1.12 dispel types (SpellEntry::Dispel): magic 1, curse 2, disease 3, poison 4. The policy header takes the raw
// number so it stays free of core includes, exactly like the loader passes the row's columns.
static const std::uint32_t kMagic = 1;
static const std::uint32_t kCurse = 2;
static const std::uint32_t kPoison = 4;

static AuraOnMember Aura(std::uint32_t spellId, std::uint32_t dispelType)
{
    AuraOnMember aura;
    aura.spellId = spellId;
    aura.dispelType = dispelType;
    return aura;
}

// t1106b: a Delusions carrier turns on its Shade out to the hold reach (40 yd), melee and ranged alike; the old
// 15/30 yd first override left 49 % of cursed ranged DPS on Jin'do with a Shade within 5 yd. A bot without the curse
// has no gated row (the fight scan drops it), so it never takes a Shade.
static void TestCarrierPicksItsShadeAt35Yd()
{
    std::vector<BossTacticRow> rows;
    BossTacticRow shade = Row(309, 11380, BossTacticKind::KillFirst, 14986);
    shade.botAura = 24306;
    rows.push_back(shade);
    rows.push_back(Row(309, 11380, BossTacticKind::KillFirst, 15112, 0, 2));
    std::vector<BossTacticRow> uncursed(rows.begin() + 1, rows.end());

    ActiveBossRows active = Active(rows);
    CHECK(HasCarrierKillFirstRow(active));
    CHECK(KillFirstPickModeFor(true, true, true, HasCarrierKillFirstRow(active)) == KillFirstPickMode::Carrier);
    // The carrier picks its Shade at 35 yd, melee and ranged alike (there is one reach).
    std::vector<TacticCandidate> shades(1);
    shades[0].entry = 14986;
    shades[0].distance = 35.0f;
    shades[0].pickable = true;
    CHECK(PickCarrierOverride(active, shades, kCarrierHoldReachYd) == 0);
    // A free DPS takes it too, and the Shade outranks the totem.
    CHECK(PickKillFirst(active, shades) == 0);
    // No curse: no gated row, no carrier mode, and no pick of a Shade the bot could not hurt.
    ActiveBossRows plain = Active(uncursed);
    CHECK(!HasCarrierKillFirstRow(plain));
    CHECK(KillFirstPickModeFor(true, true, true, HasCarrierKillFirstRow(plain)) == KillFirstPickMode::None);
    CHECK(PickCarrierOverride(plain, shades, kCarrierHoldReachYd) == -1);
    CHECK(PickKillFirst(plain, shades) == -1);
}

// t1106b: the kill-first no-switch row's reason and the direct-damage-only rule a needs_aura subject (the Shade) has:
// a DoT cast at it and the pet sent at it are refused (BossTacticsKillFirstAction, DotCastAtTarget, CanPetAttack).
static void TestCarrierNoSwitchReasonAndDirectOnly()
{
    CHECK(CarrierNoSwitchReason(false, true, true, 5.0f, kCarrierHoldReachYd) == CarrierNoSwitch::None);
    CHECK(CarrierNoSwitchReason(true, false, false, 0.0f, kCarrierHoldReachYd) == CarrierNoSwitch::NoShade);
    CHECK(CarrierNoSwitchReason(true, true, false, 5.0f, kCarrierHoldReachYd) == CarrierNoSwitch::NotAttackable);
    CHECK(CarrierNoSwitchReason(true, true, true, 41.0f, kCarrierHoldReachYd) == CarrierNoSwitch::BeyondReach);
    CHECK(CarrierNoSwitchReason(true, true, true, 39.0f, kCarrierHoldReachYd) == CarrierNoSwitch::None);
    CHECK(AuraGatedDirectOnlyDamage(true, 24306));
    CHECK(!AuraGatedDirectOnlyDamage(false, 24306));
    CHECK(!AuraGatedDirectOnlyDamage(true, 0));
}

// t1106k F3 a: the kill-first switch row's since_ms is the server-clock time from this bot's first sight of the Shade
// to the switch. Unknown first sight reads 0 (the row never invents a delay), and the 32-bit clock wrap is safe.
static void TestCarrierSwitchSinceMs()
{
    CHECK(CarrierSwitchSinceMs(1000, 2300) == 1300);
    CHECK(CarrierSwitchSinceMs(1000, 1000) == 0);
    CHECK(CarrierSwitchSinceMs(0, 5000) == 0);
    CHECK(CarrierSwitchSinceMs(0xFFFFFF00u, 0x00000100u) == 0x200u); // the clock wrapped between sight and switch
}

// t1106k R3 L3: a Shade missed by one pass (or two) keeps its first-sight record; it goes only after kShadeSeenGraceMs.
static void TestShadeSeenGrace()
{
    CHECK(kShadeSeenGraceMs == 3000);
    CHECK(!ShadeSeenExpired(10000, 10000));
    CHECK(!ShadeSeenExpired(10000, 11000));  // one missed 1 s pass: kept (the old code erased here)
    CHECK(!ShadeSeenExpired(10000, 13000));
    CHECK(ShadeSeenExpired(10000, 13001));
    CHECK(!ShadeSeenExpired(0xFFFFFF00u, 0x00000100u)); // clock wrap
}

// t1106b round 2: "current target" can already name the pick while the live victim is another unit (or none), and the
// old current-target-only test left the trigger off for the rest of the fight. The trigger must fire and the action
// must re-attack the pick, not only log.
static void TestKillFirstReattacksAStalePick()
{
    CHECK(!KillFirstNeedsAttack(false, false, false)); // no pick: nothing to attack
    CHECK(KillFirstNeedsAttack(true, false, false));   // another current target
    CHECK(KillFirstNeedsAttack(true, false, true));    // another current target, already swinging at the pick
    CHECK(KillFirstNeedsAttack(true, true, false));    // bookkeeping says pick, live victim another unit or none
    CHECK(!KillFirstNeedsAttack(true, true, true));    // already on it
}

static TacticCandidate Cand(std::uint32_t entry, float distance, bool pickable = true)
{
    TacticCandidate c;
    c.entry = entry;
    c.distance = distance;
    c.pickable = pickable;
    return c;
}

static CarrierScanFacts ScanHit(bool unitFound, bool eligible, float distanceYd, bool los, bool validAttack)
{
    CarrierScanFacts f;
    f.unitFound = unitFound;
    f.gridEligible = eligible;
    f.distanceYd = distanceYd;
    f.los = los;
    f.validAttack = validAttack;
    return f;
}

// walk 53: a Delusions carrier's attackers list held only Jin'do while a Shade stood beside it (295 of 357 no-switch
// rows read `not_read`). The carrier override finds nothing there; the grid fallback takes the nearest Shade that
// passes every check, and leaves the nearer one behind a wall out.
static void TestCarrierScanTakesGridShadeTheAttackersListMissed()
{
    std::vector<BossTacticRow> rows;
    BossTacticRow shade = Row(309, 11380, BossTacticKind::KillFirst, 14986);
    shade.botAura = 24306;
    rows.push_back(shade);
    ActiveBossRows active = Active(rows);
    // The attackers list of that carrier: Jin'do alone, pickable, 5 yd.
    std::vector<TacticCandidate> candidates{Cand(11380, 5.0f, true)};
    CHECK(PickCarrierOverride(active, candidates, kCarrierHoldReachYd) == -1);

    // The grid read of the carrier row's entry: a Shade at 12 yd and one at 8 yd without line of sight.
    std::vector<CarrierScanFacts> hits;
    hits.push_back(ScanHit(true, true, 12.0f, true, true));
    hits.push_back(ScanHit(true, true, 8.0f, false, true));
    CHECK(CarrierScanRefuses(hits[0], kCarrierHoldReachYd) == CarrierScanRefusal::None);
    CHECK(CarrierScanRefuses(hits[1], kCarrierHoldReachYd) == CarrierScanRefusal::NoLos);
    CHECK(CarrierScanPick(hits, kCarrierHoldReachYd) == 0);
}

// Each check alone, then the order the checks run in, then the reach's edge.
static void TestCarrierScanRefusal()
{
    CHECK(CarrierScanRefuses(ScanHit(false, true, 10.0f, true, true), 40.0f) == CarrierScanRefusal::NoUnit);
    CHECK(CarrierScanRefuses(ScanHit(true, false, 10.0f, true, true), 40.0f) == CarrierScanRefusal::Ineligible);
    CHECK(CarrierScanRefuses(ScanHit(true, true, 41.0f, true, true), 40.0f) == CarrierScanRefusal::BeyondReach);
    CHECK(CarrierScanRefuses(ScanHit(true, true, 10.0f, false, true), 40.0f) == CarrierScanRefusal::NoLos);
    CHECK(CarrierScanRefuses(ScanHit(true, true, 10.0f, true, false), 40.0f) == CarrierScanRefusal::InvalidTarget);
    CHECK(CarrierScanRefuses(ScanHit(true, true, 10.0f, true, true), 40.0f) == CarrierScanRefusal::None);

    CHECK(CarrierScanRefuses(ScanHit(false, false, 41.0f, false, false), 40.0f) == CarrierScanRefusal::NoUnit);
    CHECK(CarrierScanRefuses(ScanHit(true, false, 41.0f, false, false), 40.0f) == CarrierScanRefusal::Ineligible);
    CHECK(CarrierScanRefuses(ScanHit(true, true, 41.0f, false, false), 40.0f) == CarrierScanRefusal::BeyondReach);
    CHECK(CarrierScanRefuses(ScanHit(true, true, 10.0f, false, false), 40.0f) == CarrierScanRefusal::NoLos);

    CHECK(CarrierScanRefuses(ScanHit(true, true, 40.0f, true, true), 40.0f) == CarrierScanRefusal::None);
    CHECK(CarrierScanRefuses(ScanHit(true, true, 40.01f, true, true), 40.0f) == CarrierScanRefusal::BeyondReach);
    CHECK(CarrierScanRefuses(ScanHit(true, true, -1.0f, true, true), 40.0f) == CarrierScanRefusal::BeyondReach);
}

static void TestCarrierScanPick()
{
    std::vector<CarrierScanFacts> none;
    CHECK(CarrierScanPick(none, 40.0f) == -1); // TestCarrierScanPickEmpty
    std::vector<CarrierScanFacts> refused;
    refused.push_back(ScanHit(true, false, 5.0f, true, true));
    refused.push_back(ScanHit(true, true, 45.0f, false, false));
    CHECK(CarrierScanPick(refused, 40.0f) == -1); // TestCarrierScanPickAllRefused
    std::vector<CarrierScanFacts> eligible;
    eligible.push_back(ScanHit(true, true, 22.0f, true, true));
    eligible.push_back(ScanHit(true, true, 9.0f, true, false));
    CHECK(CarrierScanPick(eligible, 40.0f) == 0); // the nearer hit refused, the far one alone
    eligible[1].validAttack = true;
    CHECK(CarrierScanPick(eligible, 40.0f) == 1); // TestCarrierScanPickNearest
}

static void TestCarrierScanReasonWord()
{
    CHECK(std::strcmp(CarrierScanReasonWord(CarrierScanRefusal::None), "none") == 0);
    CHECK(std::strcmp(CarrierScanReasonWord(CarrierScanRefusal::NoUnit), "no_unit") == 0);
    CHECK(std::strcmp(CarrierScanReasonWord(CarrierScanRefusal::Ineligible), "ineligible") == 0);
    CHECK(std::strcmp(CarrierScanReasonWord(CarrierScanRefusal::BeyondReach), "beyond_reach") == 0);
    CHECK(std::strcmp(CarrierScanReasonWord(CarrierScanRefusal::NoLos), "no_los") == 0);
    CHECK(std::strcmp(CarrierScanReasonWord(CarrierScanRefusal::InvalidTarget), "invalid_target") == 0);
}

// Codex round 3: a free DPS carrier takes the Shade the scan found over a Powerful Healing Ward in view; every other
// case keeps the ordinary pick.
static void TestCarrierScanTakesPrecedence()
{
    using M = KillFirstPickMode;
    // A carrier, the key on, a ward the ordinary pick took, a Shade the scan found: the Shade.
    CHECK(CarrierScanTakesPrecedence(true, true, M::Free, true, false));
    CHECK(CarrierScanTakesPrecedence(true, true, M::Carrier, true, false));
    // Not a carrier: the ward stays (the uncursed raid's job).
    CHECK(!CarrierScanTakesPrecedence(false, true, M::Free, true, false));
    // Key off: the ward stays.
    CHECK(!CarrierScanTakesPrecedence(true, false, M::Free, true, false));
    // A player's order is never overridden.
    CHECK(!CarrierScanTakesPrecedence(true, true, M::None, true, false));
    // No Shade found: the ordinary pick stays.
    CHECK(!CarrierScanTakesPrecedence(true, true, M::Free, false, false));
    // The ordinary pick already is a Shade: it stays.
    CHECK(!CarrierScanTakesPrecedence(true, true, M::Carrier, true, true));

    // The walk shape: the free pick takes a ward (entry 14987) while a carrier row for 14986 is open.
    std::vector<BossTacticRow> rows;
    BossTacticRow shade = Row(309, 11380, BossTacticKind::KillFirst, 14986);
    shade.botAura = 24306;
    rows.push_back(shade);
    rows.push_back(Row(310, 11380, BossTacticKind::KillFirst, 14987));
    ActiveBossRows active = Active(rows);
    std::vector<TacticCandidate> candidates{Cand(14987, 10.0f, true)};
    int const ordinary = PickKillFirst(active, candidates);
    CHECK(ordinary == 0); // the ward wins the ordinary free pick
    CHECK(HasCarrierKillFirstRow(active));
    CHECK(CarrierScanTakesPrecedence(HasCarrierKillFirstRow(active), true, M::Free, true,
                                     candidates[ordinary].entry == 14986));
}

// The no-switch row's word from the scan tally: a player's order first, then the most common refusal (the enum order
// breaks a tie), then why there was no hit at all.
static void TestCarrierScanWord()
{
    // TestCarrierScanWordPlayerOrderWins: the order beats a tally full of refusals.
    CarrierScanTally ordered;
    ordered.playerOrder = true;
    ordered.hits = 2;
    ordered.byCode[static_cast<int>(CarrierScanRefusal::NoLos)] = 2;
    CHECK(std::strcmp(CarrierScanWord(ordered, true, true), "player_order") == 0);

    // TestCarrierScanWordMostCommon and TestCarrierScanWordTieBreak.
    CarrierScanTally mixed;
    mixed.hits = 5;
    mixed.byCode[static_cast<int>(CarrierScanRefusal::None)] = 4; // hits that passed are never the word
    mixed.byCode[static_cast<int>(CarrierScanRefusal::NoLos)] = 3;
    mixed.byCode[static_cast<int>(CarrierScanRefusal::BeyondReach)] = 1;
    CHECK(std::strcmp(CarrierScanWord(mixed, true, true), "no_los") == 0);
    mixed.byCode[static_cast<int>(CarrierScanRefusal::BeyondReach)] = 3;
    CHECK(std::strcmp(CarrierScanWord(mixed, true, true), "beyond_reach") == 0); // equal: the earlier code
    mixed.byCode[static_cast<int>(CarrierScanRefusal::Ineligible)] = 3;
    CHECK(std::strcmp(CarrierScanWord(mixed, true, true), "ineligible") == 0);

    // TestCarrierScanWordNotPickable: no refused hit, so the unit the fight holds decides.
    CarrierScanTally empty;
    CHECK(std::strcmp(CarrierScanWord(empty, false, false), "no_unit") == 0);
    CHECK(std::strcmp(CarrierScanWord(empty, true, false), "not_pickable") == 0);
    CHECK(std::strcmp(CarrierScanWord(empty, true, true), "beyond_reach") == 0);
    CarrierScanTally passed;
    passed.hits = 1;
    passed.byCode[static_cast<int>(CarrierScanRefusal::None)] = 1;
    CHECK(std::strcmp(CarrierScanWord(passed, true, true), "not_pickable") == 0);
}

// A tank never taunts a needs_aura subject (the Shade of Jin'do) or a taunt-immune creature; the key off leaves every
// taunt as it was.
static void TestTauntGate()
{
    // TauntGateFor(keyOn, requiredAura, botHasAura, tauntImmune, holdsBoss)
    CHECK(TauntGateFor(false, 24306, false, true, false) == TauntGate::Allow);   // TestTauntGateOffAllows
    CHECK(TauntGateFor(false, 24306, true, true, true) == TauntGate::Allow);
    CHECK(TauntGateFor(true, 24306, false, false, false) == TauntGate::NeedsAura);  // TestTauntGateNeedsAura: no aura, refused
    CHECK(TauntGateFor(true, 24306, true, false, false) == TauntGate::Allow);       // TestTauntGateCarrierAllowed: a cursed off-tank may
    CHECK(TauntGateFor(true, 0, false, true, false) == TauntGate::TauntImmune);     // TestTauntGateTauntImmune
    CHECK(TauntGateFor(true, 0, true, true, false) == TauntGate::TauntImmune);      // the aura does not lift immunity
    CHECK(TauntGateFor(true, 24306, true, true, false) == TauntGate::TauntImmune);  // immune stays refused for a carrier
    CHECK(TauntGateFor(true, 24306, false, true, false) == TauntGate::NeedsAura);   // no aura: the aura word comes first
    CHECK(TauntGateFor(true, 0, false, false, false) == TauntGate::Allow);
    // TestTauntGateHoldingBoss (1c): a cursed main tank that is the boss's victim does not taunt the Shade.
    CHECK(TauntGateFor(true, 24306, true, false, true) == TauntGate::HoldingBoss);
    CHECK(TauntGateFor(true, 24306, true, true, true) == TauntGate::HoldingBoss);   // holding_boss comes before taunt_immune
    CHECK(TauntGateFor(true, 24306, false, false, true) == TauntGate::NeedsAura);   // an uncursed tank: needs_aura first
    CHECK(TauntGateFor(true, 24306, true, false, false) == TauntGate::Allow);       // a cursed off-tank off the boss
    CHECK(TauntGateFor(false, 24306, true, false, true) == TauntGate::Allow);       // key off: allow
    CHECK(TauntGateFor(true, 0, false, false, true) == TauntGate::Allow);           // holding a boss is no rule for a plain target
    CHECK(std::strcmp(TauntGateWord(TauntGate::Allow), "allow") == 0);
    CHECK(std::strcmp(TauntGateWord(TauntGate::NeedsAura), "needs_aura") == 0);
    CHECK(std::strcmp(TauntGateWord(TauntGate::TauntImmune), "taunt_immune") == 0);
    CHECK(std::strcmp(TauntGateWord(TauntGate::HoldingBoss), "holding_boss") == 0);
}

// 1b: the refusal row is throttled per (bot, gate word); a taunt_immune row at t=0 must not block a needs_aura row at 1 s.
static void TestTauntGateLogThrottlePerWord()
{
    TauntGateLogTimes last{};
    constexpr std::uint32_t gap = 5000;
    CHECK(TauntGateLogDueFor(last, TauntGate::TauntImmune, 1000, gap));        // first row of the word
    CHECK(TauntGateLogDueFor(last, TauntGate::NeedsAura, 2000, gap));          // another word, 1 s later: not blocked
    CHECK(!TauntGateLogDueFor(last, TauntGate::TauntImmune, 3000, gap));       // same word inside the gap
    CHECK(!TauntGateLogDueFor(last, TauntGate::NeedsAura, 6999, gap));
    CHECK(TauntGateLogDueFor(last, TauntGate::TauntImmune, 6000, gap));        // gap over
    CHECK(TauntGateLogDueFor(last, TauntGate::NeedsAura, 7000, gap));
    CHECK(TauntGateLogDueFor(last, TauntGate::HoldingBoss, 7001, gap));        // the new word has its own slot
    CHECK(!TauntGateLogDueFor(last, TauntGate::HoldingBoss, 7002, gap));
    TauntGateLogTimes other{};
    CHECK(TauntGateLogDueFor(other, TauntGate::TauntImmune, 7003, gap));       // another bot has its own array
}

// 2b: a carrier on its Shade keeps it (the "not dps target active" trigger stays quiet); 2c: the kill-first check
// interval is 1 s under the hold.
static void TestCarrierHold()
{
    // CarrierHoldsCurrent(keyOn, currentIsNeedsAura, botHasRequiredAura)
    CHECK(CarrierHoldsCurrent(true, true, true));      // a carrier on a Shade, dps target Jin'do: the trigger does not fire
    CHECK(!CarrierHoldsCurrent(true, false, false));   // a non-carrier on Jin'do: the trigger fires
    CHECK(!CarrierHoldsCurrent(true, false, true));    // a carrier on Jin'do (not a Shade): fires
    CHECK(!CarrierHoldsCurrent(true, true, false));    // an uncursed bot on a Shade (cannot hurt it): today's rule
    CHECK(!CarrierHoldsCurrent(false, true, true));    // key off: today
    CHECK(!CarrierHoldsCurrent(false, false, false));
    // KillFirstCheckSeconds(totemFocus, carrierHold)
    CHECK(KillFirstCheckSeconds(false, false) == 2);   // today
    CHECK(KillFirstCheckSeconds(true, false) == 1);    // TotemFocus
    CHECK(KillFirstCheckSeconds(false, true) == 1);    // CarrierHold, whatever TotemFocus says
    CHECK(KillFirstCheckSeconds(true, true) == 1);
}

// Fix round: the hold never beats a hostile player target, and only suppresses the dps-target comparison.
static void TestNotDpsTargetFires()
{
    // NotDpsTargetFires(enemyKnown, targetIsEnemy, holdsCurrent, dpsKnown, targetIsDps)
    CHECK(NotDpsTargetFires(true, false, true, true, false));    // carrier on a Shade, enemy player known: fires as before
    CHECK(NotDpsTargetFires(true, false, false, false, false));  // enemy known, target is not it: fires
    CHECK(!NotDpsTargetFires(true, true, true, true, false));    // already on the enemy player: quiet
    CHECK(!NotDpsTargetFires(false, false, true, true, false));  // carrier on a Shade, dps target Jin'do: the hold keeps it
    CHECK(NotDpsTargetFires(false, false, false, true, false));  // non-carrier on Jin'do's side: fires (today)
    CHECK(!NotDpsTargetFires(false, false, false, true, true));  // on the dps target: quiet
    CHECK(!NotDpsTargetFires(false, false, false, false, false));  // nothing to compare: quiet
}

// Fix round: the kill-first interval follows the keys at every check, so an already-built trigger flips between 1 s
// and 2 s on a `.reload config`.
static void TestKillFirstCheckDueFlips()
{
    std::int64_t last = 0;
    // Keys off: the base interval-2 rule. First check is due, then every second tick is skipped.
    CHECK(KillFirstCheckDue(false, false, 100, last));
    CHECK(!KillFirstCheckDue(false, false, 101, last));
    CHECK(KillFirstCheckDue(false, false, 102, last));
    CHECK(!KillFirstCheckDue(false, false, 103, last));
    // The key flips on (same trigger, same `last`): every second is due now.
    CHECK(KillFirstCheckDue(false, true, 103, last));
    CHECK(KillFirstCheckDue(false, true, 104, last));
    CHECK(KillFirstCheckDue(false, true, 105, last));
    CHECK(KillFirstCheckDue(true, false, 106, last));            // TotemFocus alone does the same
    // The key flips off again: two seconds from the last due check.
    CHECK(!KillFirstCheckDue(false, false, 107, last));
    CHECK(KillFirstCheckDue(false, false, 108, last));
    // Same second asked twice with the key on: still due (the base interval-1 rule asks every tick).
    CHECK(KillFirstCheckDue(false, true, 108, last));
    CHECK(KillFirstCheckDue(false, true, 108, last));
}

static SoleCandidate Sole(std::uint64_t guid, int hpPct, bool inCombat, bool eligible = true)
{
    SoleCandidate c;
    c.guid = guid;
    c.hpPct = hpPct;
    c.inCombat = inCombat;
    c.eligible = eligible;
    return c;
}

static LinkedMember Linked(std::uint64_t guid, int hpPct, bool attackable = true, std::uint32_t entry = 0)
{
    LinkedMember m;
    m.guid = guid;
    m.entry = entry;
    m.hpPct = hpPct;
    m.attackable = attackable;
    return m;
}

// The core's raise rule over a list of deaths, each (entry, second it died): the chosen raiser is the first living of
// Lor'Khan (11347) -> Zath (11348) -> Thekal (14599), a corpse's first check is 7 s after its death, a raiser that
// died by then moves the check 8 s on, and the second check raises the corpse if any sibling still lives. Returns the
// number of corpses restored (0: the trio stayed down).
static int ThekalRaises(const std::vector<std::pair<std::uint32_t, int>>& deaths)
{
    std::uint32_t const entries[3] = {11347, 11348, 14599};
    bool alive[3] = {true, true, true};
    int raiser[3] = {-1, -1, -1};
    int deadline[3] = {-1, -1, -1};
    bool extended[3] = {false, false, false};
    auto firstAlive = [&alive]()
    {
        for (int i = 0; i < 3; ++i)
            if (alive[i])
                return i;
        return -1;
    };
    int raises = 0;
    for (int s = 0; s <= 90; ++s)
    {
        for (const std::pair<std::uint32_t, int>& died : deaths)
            if (died.second == s)
                for (int i = 0; i < 3; ++i)
                    if (entries[i] == died.first && alive[i])
                    {
                        alive[i] = false;
                        raiser[i] = firstAlive();
                        deadline[i] = s + (raiser[i] < 0 ? 10 : 7);
                    }
        for (int i = 0; i < 3; ++i)
        {
            if (deadline[i] != s || alive[i])
                continue;
            if (raiser[i] >= 0 && alive[raiser[i]])
            {
                ++raises;
                alive[i] = true;
                deadline[i] = -1;
            }
            else if (!extended[i])
            {
                extended[i] = true;
                deadline[i] = s + 8;
            }
            else if (firstAlive() >= 0)
            {
                ++raises;
                alive[i] = true;
                deadline[i] = -1;
            }
            else
                deadline[i] = -1;
        }
    }
    return raises;
}

// gate_hold (brief C): DPS spare a boss at or below its health gate while a second unit still fights. The modal case:
// Onyxia 10184 at 42 % with Cindarion 50143 up: the row spares her while more than `priority` blockers fight.
static void GateHold_HoldsSparedAtGateWhileBlockerFights()
{
    BossTacticRow row = Row(249, 10184, BossTacticKind::GateHold, 50143, 0, 0, 42.0f);
    row.windowSec = 200.0f;
    const std::vector<BossTacticRow> rows = { row };
    const ActiveBossRows active = Active(rows);
    const auto fighters = [](std::uint32_t n)
    {
        return [n](const BossTacticRow&) -> std::uint32_t { return n; };
    };
    CHECK(ParseBossTacticKind("gate_hold") == BossTacticKind::GateHold);
    CHECK(static_cast<int>(BossTacticKind::GateHold) == 26);
    CHECK(GateHoldRadius(0.0f) == kGateHoldRadiusYd);
    CHECK(GateHoldRadius(200.0f) == 200.0f);
    CHECK(GateHeld(active, 10184, 42, fighters(1)));     // at the gate, one blocker fighting: spared
    CHECK(GateHeld(active, 10184, 20, fighters(1)));
    CHECK(!GateHeld(active, 10184, 42, fighters(0)));    // no blocker fighting: the boss is free
    CHECK(!GateHeld(active, 50143, 42, fighters(1)));    // the blocker is never spared itself
}

static void GateHold_EvadedBlockerReleases()
{
    // The count keeps only living, in-combat, non-evading blockers. An evaded Cindarion is out of combat, evading and
    // back in the air (unselectable): he must release the hold so the raid can finish Onyxia.
    CHECK(!CountsAsFightingBlocker(false, true, true));
    CHECK(!CountsAsFightingBlocker(false, false, true));   // out of combat and unselectable: not a fight
    CHECK(CountsAsFightingBlocker(true, false, true));     // unselectable but fighting: still a blocker
    CHECK(!CountsAsFightingBlocker(true, true, false));    // evading: never
    BossTacticRow row = Row(249, 10184, BossTacticKind::GateHold, 50143, 0, 0, 42.0f);
    row.windowSec = 200.0f;
    const std::vector<BossTacticRow> rows = { row };
    const auto count = [](bool inCombat, bool evading, bool notSelectable) -> std::uint32_t
    { return CountsAsFightingBlocker(inCombat, evading, notSelectable) ? 1u : 0u; };
    const std::uint32_t evaded = count(false, true, true);
    CHECK(evaded == 0);
    CHECK(!GateHeld(Active(rows), 10184, 42, [evaded](const BossTacticRow&) { return evaded; }));
    const std::uint32_t fighting = count(true, false, false);
    CHECK(fighting == 1);
    CHECK(GateHeld(Active(rows), 10184, 42, [fighting](const BossTacticRow&) { return fighting; }));
}

static void GateHold_AboveGateNotHeld()
{
    BossTacticRow row = Row(249, 10184, BossTacticKind::GateHold, 50143, 0, 0, 42.0f);
    row.windowSec = 200.0f;
    const std::vector<BossTacticRow> rows = { row };
    const auto fighters = [](const BossTacticRow&) -> std::uint32_t { return 1; };
    CHECK(!GateHeld(Active(rows), 10184, 43, fighters));   // one point above the gate: full damage
    CHECK(!GateHeld(Active(rows), 10184, 100, fighters));
    CHECK(GateHeld(Active(rows), 10184, 42, fighters));    // the gate is inclusive
    // The row is about its own boss_entry; its target_entry is the blocker, never spared itself.
    CHECK(!GateHeld(Active(rows), 50143, 20, fighters));
    CHECK(!GateHeld(ActiveBossRows(), 10184, 20, fighters));
    // priority is how many blockers may still fight (0 = not one more).
    BossTacticRow budget = row;
    budget.priority = 1;
    const std::vector<BossTacticRow> budgetRows = { budget };
    const auto n = [](std::uint32_t count)
    { return [count](const BossTacticRow&) -> std::uint32_t { return count; }; };
    CHECK(!GateHeld(Active(budgetRows), 10184, 42, n(1)));
    CHECK(GateHeld(Active(budgetRows), 10184, 42, n(2)));
    // The picker rule: a free DPS bot drops the spared entry, a tank or healer never does.
    const auto countNear = [](std::uint32_t) -> std::uint32_t { return 0; };
    CHECK(DpsLeavesAlone(true, Active(rows), 10184, 42, countNear, fighters));
    CHECK(!DpsLeavesAlone(true, Active(rows), 10184, 43, countNear, fighters));
    CHECK(!DpsLeavesAlone(false, Active(rows), 10184, 42, countNear, fighters));
}

static void GateHold_RowShape()
{
    BossTacticRow good = Row(249, 10184, BossTacticKind::GateHold, 50143, 0, 0, 42.0f);
    good.windowSec = 200.0f;
    {
        BossTacticTable table;
        CHECK(table.Add(good));
        CHECK(!table.Add(good));                       // same key
        table.Seal();
        CHECK(table.HasGateHoldMap(249));
        CHECK(!table.HasGateHoldMap(250));
        BossTacticRow defaultRadius = good;
        defaultRadius.windowSec = 0.0f;                // 0 means the 200 yd default
        BossTacticTable other;
        CHECK(other.Add(defaultRadius));
    }
    const std::vector<BossTacticRow> refused = [&good]()
    {
        std::vector<BossTacticRow> rows;
        BossTacticRow noTarget = good; noTarget.targetEntry = 0;
        BossTacticRow selfTarget = good; selfTarget.targetEntry = good.bossEntry;
        BossTacticRow spell = good; spell.spellId = 33439;
        BossTacticRow lowGate = good; lowGate.param = 0.0f;
        BossTacticRow highGate = good; highGate.param = 100.0f;
        BossTacticRow shortRadius = good; shortRadius.windowSec = 49.0f;
        BossTacticRow dbDefaultRadius = good; dbDefaultRadius.windowSec = 7.0f;
        BossTacticRow wideRadius = good; wideRadius.windowSec = 251.0f;
        BossTacticRow band = good; band.hpMax = 50;
        BossTacticRow mode = good; mode.mode = TacticMode::UntilTank;
        BossTacticRow spot = good; spot.spotX = 1.0f;
        BossTacticRow cycle = good; cycle.cycleSec = 30.0f;
        rows.push_back(noTarget);
        rows.push_back(selfTarget);
        rows.push_back(spell);
        rows.push_back(lowGate);
        rows.push_back(highGate);
        rows.push_back(shortRadius);
        rows.push_back(dbDefaultRadius);
        rows.push_back(wideRadius);
        rows.push_back(band);
        rows.push_back(mode);
        rows.push_back(spot);
        rows.push_back(cycle);
        return rows;
    }();
    for (const BossTacticRow& bad : refused)
    {
        BossTacticTable table;
        CHECK(!table.Add(bad));
    }
}
static TankPostIn TankPostWaiting()
{
    TankPostIn in;
    in.live = true;
    in.started = true;
    in.tankCanAct = true;
    in.subjectFound = true;
    in.subjectAlive = true;
    in.subjectSelectable = false; // a second boss still in the air
    in.subjectEvading = false;
    in.subjectToSpotYd = 30.0f;
    in.arrivalRadiusYd = 15.0f;
    in.botToSpotYd = 25.0f;
    in.holdingOtherHostile = false;
    in.windowExpired = false;
    return in;
}

// The case that fails without the kind: nothing moves the slot tank to the post, so it stays on the boss it cannot
// reach while the awaited unit is unselectable overhead.
static void TestTankPost_SlotTankWalksToSpotWhileSubjectUnselectable()
{
    TankPostIn in = TankPostWaiting();
    CHECK(TankPostStep(in) == TankPostDecision::Walk);
    // At the spot (within 3 yd) it holds.
    in.botToSpotYd = 2.0f;
    CHECK(TankPostStep(in) == TankPostDecision::Hold);
    in.botToSpotYd = 3.0f;
    CHECK(TankPostStep(in) == TankPostDecision::Hold);
    in.botToSpotYd = 3.5f;
    CHECK(TankPostStep(in) == TankPostDecision::Walk);
    // Within the arrival radius but still unselectable: still not arrived.
    in.botToSpotYd = 1.0f;
    in.subjectToSpotYd = 2.0f;
    CHECK(TankPostStep(in) == TankPostDecision::Hold);
    // Selectable (combat is not asked): engage.
    in.subjectSelectable = true;
    CHECK(TankPostStep(in) == TankPostDecision::Engage);
    // Another hostile on a group member keeps the tank on it and the post waits, on the ground and after arrival.
    in.holdingOtherHostile = true;
    CHECK(TankPostStep(in) == TankPostDecision::Hold);
    in.subjectSelectable = false;
    CHECK(TankPostStep(in) == TankPostDecision::Hold);
}

// The case that fails if the combat requirement comes back: a unit that is alive, selectable and in range but not in
// combat yet is arrived, so the posted tank engages it. (TankPostUnit and TankPostIn carry no combat flag any more.)
static void TestTankPost_EngagesSelectableOutOfCombatSubject()
{
    TankPostIn in = TankPostWaiting();
    in.subjectSelectable = true;
    in.subjectToSpotYd = 2.0f;
    in.botToSpotYd = 1.0f;
    CHECK(TankPostArrived(true, true, true, 2.0f, 15.0f));
    CHECK(TankPostStep(in) == TankPostDecision::Engage);
    // The not-selectable case still holds.
    in.subjectSelectable = false;
    CHECK(TankPostStep(in) == TankPostDecision::Hold);
}

// The mutation case: dropping the selectable check from TankPostArrived makes an unselectable unit "arrived", so this
// check fails.
static void TestTankPost_UnselectableSubjectNotArrived()
{
    CHECK(!TankPostArrived(true, true, false, 1.0f, 15.0f));
    CHECK(!TankPostArrived(true, false, true, 1.0f, 15.0f));
    CHECK(!TankPostArrived(true, true, true, 16.0f, 15.0f));
    CHECK(!TankPostArrived(false, true, true, 1.0f, 15.0f));
    CHECK(TankPostArrived(true, true, true, 15.0f, 15.0f));

    // The grid pick: the nearest living unit to the spot; a dead one only when none lives. Three units: a dead one on
    // the spot, a living one 20 yd away and a living one 5 yd away.
    {
        std::vector<TankPostUnit> units(3);
        units[0].guid = 1;
        units[0].x = 0.0f;
        units[0].y = 0.0f;
        units[0].z = 0.0f;
        units[0].alive = false;
        units[1].guid = 2;
        units[1].x = 20.0f;
        units[1].y = 0.0f;
        units[1].z = 0.0f;
        units[1].alive = true;
        units[2].guid = 3;
        units[2].x = 5.0f;
        units[2].y = 0.0f;
        units[2].z = 0.0f;
        units[2].alive = true;
        CHECK(PickTankPostUnit(units, 0.0f, 0.0f, 0.0f) == 2);
        units[2].alive = false;
        units[1].alive = false;
        CHECK(PickTankPostUnit(units, 0.0f, 0.0f, 0.0f) == 0);  // the nearest, now that none lives
        CHECK(PickTankPostUnit(std::vector<TankPostUnit>(), 0.0f, 0.0f, 0.0f) == -1);
    }

    // The step closes a held post on each of the row's ends, and does nothing before a post exists.
    TankPostIn in = TankPostWaiting();
    in.subjectSelectable = true;
    in.subjectToSpotYd = 2.0f;
    in.botToSpotYd = 1.0f;
    CHECK(TankPostStep(in) == TankPostDecision::Engage);
    in.subjectAlive = false;
    CHECK(TankPostStep(in) == TankPostDecision::End);
    in.subjectAlive = true;
    in.subjectEvading = true;
    CHECK(TankPostStep(in) == TankPostDecision::End);
    in.subjectEvading = false;
    in.windowExpired = true;
    CHECK(TankPostStep(in) == TankPostDecision::End);
    in.windowExpired = false;
    in.tankCanAct = false;
    CHECK(TankPostStep(in) == TankPostDecision::End);
    in.tankCanAct = true;
    in.live = false; // the band closed
    CHECK(TankPostStep(in) == TankPostDecision::End);
    in.started = false;
    CHECK(TankPostStep(in) == TankPostDecision::None);
    in.live = true;
    in.subjectFound = false;
    CHECK(TankPostStep(in) == TankPostDecision::None);
    in.started = true;
    CHECK(TankPostStep(in) == TankPostDecision::End);
}

static CastDangerZone Zone(float x, float y, float z, float radius)
{
    CastDangerZone zone;
    zone.x = x;
    zone.y = y;
    zone.z = z;
    zone.radius = radius;
    return zone;
}

int main()
{
    std::cout << "Starting TortoiseBots boss tactics policy tests...\n";
    TestCarrierPicksItsShadeAt35Yd();
    TestCarrierNoSwitchReasonAndDirectOnly();
    TestCarrierSwitchSinceMs();
    TestShadeSeenGrace();
    TestKillFirstReattacksAStalePick();
    TestCarrierScanTakesGridShadeTheAttackersListMissed();
    TestCarrierScanRefusal();
    TestCarrierScanPick();
    TestCarrierScanReasonWord();
    TestCarrierScanWord();
    TestCarrierScanTakesPrecedence();
    TestTauntGate();
    TestTauntGateLogThrottlePerWord();
    TestCarrierHold();
    TestNotDpsTargetFires();
    TestKillFirstCheckDueFlips();
    GateHold_HoldsSparedAtGateWhileBlockerFights();
    GateHold_EvadedBlockerReleases();
    GateHold_AboveGateNotHeld();
    GateHold_RowShape();
    TestTankPost_SlotTankWalksToSpotWhileSubjectUnselectable();
    TestTankPost_UnselectableSubjectNotArrived();
    TestTankPost_EngagesSelectableOutOfCombatSubject();

    // A boss that vanishes (Arlokk: invisible + not selectable, so it leaves the attackers list while it stays alive and
    // in combat) keeps its rows active while its adds fight on; the rows end with the fight or the grace window.
    {
        std::vector<BossTacticRow> arlokk;
        arlokk.push_back(Row(309, 14515, BossTacticKind::KillFirst, 14515, 0, 1));
        arlokk.push_back(Row(309, 14515, BossTacticKind::KillFirst, 15101, 0, 2));
        const std::uint64_t guid = 0xF130003893000001ULL;
        BossPresenceMemory memory;
        // Engaged and visible: the scan sees her, and remembers the unit and her health.
        memory.Note(14515, guid, 80, 1000);
        // Vanished at 36 s: only prowlers in the attackers list. Her entry is still in the fight.
        const std::vector<BossPresenceMemory::Seen> gone = memory.Absent({15101}, 36000, true);
        CHECK(gone.size() == 1);
        CHECK(gone[0].entry == 14515 && gone[0].guid == guid && gone[0].hpPct == 80);
        // With her rows active (the scan keeps them), the prowler is the pick: she is not a candidate.
        std::vector<TacticCandidate> prowlerOnly(1);
        prowlerOnly[0].entry = 15101;
        prowlerOnly[0].distance = 12.0f;
        prowlerOnly[0].pickable = true;
        CHECK(PickKillFirst(Active(arlokk), prowlerOnly) == 0);
        // Without them (the scan before the fix) the prowlers run free.
        CHECK(PickKillFirst(Active({}), prowlerOnly) == -1);
        // The remembered unit must still be alive, in combat, not evading, near the bot.
        CHECK(VanishedBossStillInFight(true, true, false, true, 25.0f));
        CHECK(!VanishedBossStillInFight(false, true, false, true, 25.0f));  // gone from the map
        CHECK(!VanishedBossStillInFight(true, false, false, true, 25.0f)); // dead
        CHECK(!VanishedBossStillInFight(true, true, true, true, 25.0f));   // running home
        CHECK(!VanishedBossStillInFight(true, true, false, false, 25.0f)); // out of combat
        CHECK(!VanishedBossStillInFight(true, true, false, true, 120.0f)); // far from this bot
        // Back in view: not absent any more.
        memory.Note(14515, guid, 70, 50000);
        CHECK(memory.Absent({14515, 15101}, 50500, true).empty());
        // Second vanish at 145 s: the scan noted her every tick until it, so the memory carries the newest sighting.
        memory.Note(14515, guid, 70, 144500);
        const std::vector<BossPresenceMemory::Seen> again = memory.Absent({15101}, 145000, true);
        CHECK(again.size() == 1 && again[0].hpPct == 70);
        // The fight ends (no attackers left): the memory is dropped, and stays dropped.
        CHECK(memory.Absent({}, 146000, false).empty());
        CHECK(memory.Absent({15101}, 146500, true).empty());
        // Past the grace window the rows end even with adds still up.
        memory.Note(14515, guid, 60, 200000);
        CHECK(memory.Absent({15101}, 200000 + kBossVanishGraceMs, true).size() == 1);
        CHECK(memory.Absent({15101}, 200000 + kBossVanishGraceMs + 1, true).empty());
        CHECK(memory.Absent({15101}, 200000 + kBossVanishGraceMs + 2, true).empty());
    }

    // The three Hakkar kinds. A seek_aura row carries the source, the aura, the cycle clock and the spot the source is
    // held at; the loader refuses a row that cannot answer the seek rules, and the cycle columns belong to that kind.
    {
        BossTacticRow seek = Row(309, 14834, BossTacticKind::SeekAura, 11357, 24321, 30, 4.0f);
        seek.cycleSec = 90.0f;
        seek.windowSec = 80.0f;
        seek.spotX = -11802.0f;
        seek.spotY = -1683.0f;
        seek.spotZ = 52.9f;
        {
            BossTacticTable table;
            CHECK(!table.Add(seek));                 // no cycle_spell
            seek.cycleSpell = 24324;
            CHECK(table.Add(seek));
        }
        {
            BossTacticRow noSpot = seek;
            noSpot.spotX = noSpot.spotY = noSpot.spotZ = 0.0f;
            BossTacticTable table;
            CHECK(!table.Add(noSpot));               // no spot
            CHECK(!noSpot.HasSpot());
            CHECK(seek.HasSpot());
        }
        {
            BossTacticRow badParam = seek;
            badParam.param = 10.5f;
            BossTacticTable table;
            CHECK(!table.Add(badParam));             // gather radius above 10 yd
            badParam.param = 0.0f;
            CHECK(!table.Add(badParam));
        }
        {
            BossTacticRow highPriority = seek;
            highPriority.priority = 100;
            BossTacticTable table;
            CHECK(!table.Add(highPriority));         // hold percent above 99
        }
        {
            BossTacticRow slowCycle = seek;
            slowCycle.cycleSec = 0.0f;
            BossTacticTable table;
            CHECK(!table.Add(slowCycle));            // the cycle length is the window's ceiling
            slowCycle.cycleSec = 601.0f;
            CHECK(!table.Add(slowCycle));
            slowCycle.cycleSec = 90.0f;
            slowCycle.windowSec = 90.5f;
            CHECK(!table.Add(slowCycle));            // the window cannot outlast the cycle
        }

        // A pull gate names the boss itself (target_entry 0), its stake spell and the seconds the run waits.
        BossTacticRow gate = Row(309, 14834, BossTacticKind::PullGate, 0, 24692, 0, 30.0f);
        {
            BossTacticRow withTarget = gate;
            withTarget.targetEntry = 11357;
            BossTacticTable table;
            CHECK(!table.Add(withTarget));
            CHECK(table.Add(gate));
        }
        {
            BossTacticRow noSpell = gate;
            noSpell.spellId = 0;
            BossTacticTable table;
            CHECK(!table.Add(noSpell));
            noSpell.spellId = 24692;
            noSpell.param = 301.0f;
            CHECK(!table.Add(noSpell));
            noSpell.param = 0.0f;
            CHECK(!table.Add(noSpell));
        }

        // The spread row is a spacing in yards, 5 to 30, and names no target.
        BossTacticRow spread = Row(309, 14834, BossTacticKind::Spread, 0, 24328, 0, 12.0f);
        {
            BossTacticRow narrow = spread;
            narrow.param = 4.9f;
            BossTacticTable table;
            CHECK(!table.Add(narrow));
            narrow.param = 30.5f;
            CHECK(!table.Add(narrow));
            narrow.param = 5.0f;
            CHECK(table.Add(narrow));
        }
        {
            BossTacticRow withTarget = spread;
            withTarget.targetEntry = 11357;
            BossTacticTable table;
            CHECK(!table.Add(withTarget));
        }

        // The cycle and spot columns belong to seek_aura alone.
        {
            BossTacticRow interrupt = Row(309, 14834, BossTacticKind::Interrupt, 0, 24324);
            interrupt.cycleSpell = 24324;
            BossTacticTable table;
            CHECK(!table.Add(interrupt));
            interrupt.cycleSpell = 0;
            interrupt.cycleSec = 90.0f;
            CHECK(!table.Add(interrupt));
            interrupt.cycleSec = 0.0f;
            interrupt.spotZ = 52.9f;
            CHECK(!table.Add(interrupt));
            interrupt.spotZ = 0.0f;
            CHECK(table.Add(interrupt));
        }

        // A gate is a fight-scan question, and the run leader's own kinds are not asked (a pull gate is already the
        // run-shape decision).
        {
            BossTacticRow gated = Gated(seek, 0, 50);
            BossTacticTable table;
            CHECK(!table.Add(gated));
        }

        // Before the seal the kind maps read empty; after it, each map's own kinds are flagged.
        {
            BossTacticTable table;
            CHECK(table.Add(seek));
            CHECK(table.Add(gate));
            CHECK(table.Add(spread));
            CHECK(!table.HasSeekAuraMap(309) && !table.HasPullGateMap(309) && !table.HasSpreadMap(309));
            table.Seal();
            CHECK(table.HasSeekAuraMap(309));
            CHECK(table.HasPullGateMap(309));
            CHECK(table.HasSpreadMap(309));
            CHECK(!table.HasSeekAuraMap(531));
            CHECK(!table.HasSpreadMap(531));
        }
    }

    // seek_pull (walk 51): the seek source, the most walk-outs in one cycle, the kill lead and the last-start window.
    {
        BossTacticRow pull = Row(309, 14834, BossTacticKind::SeekPull, 11357, 0, 3, 45.0f);
        pull.windowSec = 60.0f;
        {
            BossTacticTable table;
            CHECK(table.Add(pull));
            CHECK(table.SeekPullRow(309, 14834, 11357) == nullptr); // not sealed
            table.Seal();
            const BossTacticRow* found = table.SeekPullRow(309, 14834, 11357);
            CHECK(found != nullptr);
            CHECK(found->priority == 3);
            CHECK(Near(found->param, 45.0f));
            CHECK(Near(found->windowSec, 60.0f));
            CHECK(table.SeekPullRow(309, 14834, 11358) == nullptr);
            CHECK(table.SeekPullRow(531, 14834, 11357) == nullptr);
        }
        {
            BossTacticRow bad = pull;
            bad.priority = 0;
            BossTacticTable table;
            CHECK(!table.Add(bad));
            bad.priority = 6;
            CHECK(!table.Add(bad));
            bad.priority = 1;
            CHECK(table.Add(bad));
        }
        {
            BossTacticRow bad = pull;
            bad.param = 5.0f;
            BossTacticTable table;
            CHECK(!table.Add(bad));
            bad.param = 86.0f;
            CHECK(!table.Add(bad));
        }
        {
            BossTacticRow bad = pull;
            bad.windowSec = 90.0f;
            BossTacticTable table;
            CHECK(!table.Add(bad));
            bad.windowSec = 9.0f;
            CHECK(!table.Add(bad));
        }
        {
            BossTacticRow bad = pull;
            bad.spellId = 24321;
            BossTacticTable table;
            CHECK(!table.Add(bad));
        }
        {
            BossTacticRow bad = pull;
            bad.spotZ = 54.0f;
            BossTacticTable table;
            CHECK(!table.Add(bad));
        }
        {
            BossTacticRow bad = pull;
            bad.cycleSec = 90.0f;
            BossTacticTable table;
            CHECK(!table.Add(bad));
        }
        {
            BossTacticRow bad = pull;
            bad.targetEntry = 0;
            BossTacticTable table;
            CHECK(!table.Add(bad));
        }
        {
            BossTacticRow bad = Gated(pull, 0, 50);
            BossTacticTable table;
            CHECK(!table.Add(bad));
        }
    }

    // Kind text: exactly the lowercase spellings the table holds.
    CHECK(ParseBossTacticKind("interrupt") == BossTacticKind::Interrupt);
    CHECK(ParseBossTacticKind("kill_first") == BossTacticKind::KillFirst);
    CHECK(ParseBossTacticKind("behind") == BossTacticKind::Behind);
    CHECK(ParseBossTacticKind("avoid_cast") == BossTacticKind::AvoidCast);
    CHECK(ParseBossTacticKind("tank_hold") == BossTacticKind::TankHold);
    CHECK(ParseBossTacticKind("hold_kill") == BossTacticKind::HoldKill);
    CHECK(ParseBossTacticKind("one_at_a_time") == BossTacticKind::OneAtATime);
    CHECK(ParseBossTacticKind("Interrupt") == BossTacticKind::None);
    CHECK(ParseBossTacticKind("Hold_Kill") == BossTacticKind::None);
    CHECK(ParseBossTacticKind("Tank_Hold") == BossTacticKind::None);
    CHECK(ParseBossTacticKind("One_At_A_Time") == BossTacticKind::None);
    CHECK(ParseBossTacticKind("one-at-a-time") == BossTacticKind::None);
    CHECK(ParseBossTacticKind("") == BossTacticKind::None);
    CHECK(ParseBossTacticKind("dispel") == BossTacticKind::None);
    CHECK(ParseBossTacticKind("dps_hold_after") == BossTacticKind::DpsHoldAfter);
    CHECK(ParseBossTacticKind("dispel_priority") == BossTacticKind::DispelPriority);
    CHECK(ParseBossTacticKind("flee_on_aura") == BossTacticKind::FleeOnAura);
    CHECK(ParseBossTacticKind("freeze_on_aura") == BossTacticKind::FreezeOnAura);
    CHECK(ParseBossTacticKind("taunt_after_cast") == BossTacticKind::TauntAfterCast);
    CHECK(ParseBossTacticKind("swap_on_aura") == BossTacticKind::SwapOnAura);
    // The values continue at 9, so a stored row never changes meaning; taunt_after_cast is the longest kind name and
    // still fits varchar(16).
    CHECK(static_cast<int>(BossTacticKind::DpsHoldAfter) == 9);
    CHECK(static_cast<int>(BossTacticKind::DispelPriority) == 10);
    CHECK(static_cast<int>(BossTacticKind::FleeOnAura) == 11);
    CHECK(static_cast<int>(BossTacticKind::FreezeOnAura) == 12);
    CHECK(static_cast<int>(BossTacticKind::TauntAfterCast) == 13);
    CHECK(static_cast<int>(BossTacticKind::SwapOnAura) == 14);
    CHECK(ParseBossTacticKind("use_object") == BossTacticKind::UseObject);
    CHECK(static_cast<int>(BossTacticKind::UseObject) == 15);
    CHECK(ParseBossTacticKind("seek_aura") == BossTacticKind::SeekAura);
    CHECK(ParseBossTacticKind("pull_gate") == BossTacticKind::PullGate);
    CHECK(ParseBossTacticKind("spread") == BossTacticKind::Spread);
    CHECK(ParseBossTacticKind("tank_post") == BossTacticKind::TankPost);
    CHECK(static_cast<int>(BossTacticKind::SeekAura) == 16);
    CHECK(static_cast<int>(BossTacticKind::PullGate) == 17);
    CHECK(static_cast<int>(BossTacticKind::Spread) == 18);
    CHECK(static_cast<int>(BossTacticKind::GateHold) == 26);
    CHECK(static_cast<int>(BossTacticKind::TankPost) == 27);

    // Mode words belong to their kind only; an empty word is the kind's own behaviour, a foreign word is Invalid.
    CHECK(ParseTacticMode(BossTacticKind::DispelPriority, "") == TacticMode::Default);
    CHECK(ParseTacticMode(BossTacticKind::DispelPriority, "now") == TacticMode::Now);
    CHECK(ParseTacticMode(BossTacticKind::DispelPriority, "clear") == TacticMode::Clear);
    CHECK(ParseTacticMode(BossTacticKind::DispelPriority, "never") == TacticMode::Never);
    CHECK(ParseTacticMode(BossTacticKind::FleeOnAura, "away") == TacticMode::Away);
    CHECK(ParseTacticMode(BossTacticKind::FleeOnAura, "to_tank") == TacticMode::ToTank);
    CHECK(ParseTacticMode(BossTacticKind::DpsHoldAfter, "engage") == TacticMode::Engage);
    CHECK(ParseTacticMode(BossTacticKind::DpsHoldAfter, "blink") == TacticMode::Blink);
    CHECK(ParseTacticMode(BossTacticKind::DpsHoldAfter, "until_tank") == TacticMode::UntilTank);
    CHECK(ParseTacticMode(BossTacticKind::TauntAfterCast, "until_tank") == TacticMode::UntilTank);
    CHECK(ParseTacticMode(BossTacticKind::UseObject, "") == TacticMode::Default);
    CHECK(ParseTacticMode(BossTacticKind::UseObject, "tank_anchor") == TacticMode::TankAnchor);
    CHECK(ParseTacticMode(BossTacticKind::FleeOnAura, "tank_anchor") == TacticMode::Invalid);
    CHECK(ParseTacticMode(BossTacticKind::UseObject, "never") == TacticMode::Invalid);
    CHECK(ParseTacticMode(BossTacticKind::DispelPriority, "to_tank") == TacticMode::Invalid);
    CHECK(ParseTacticMode(BossTacticKind::DpsHoldAfter, "clear") == TacticMode::Invalid);
    CHECK(ParseTacticMode(BossTacticKind::FleeOnAura, "never") == TacticMode::Invalid);
    CHECK(ParseTacticMode(BossTacticKind::TauntAfterCast, "now") == TacticMode::Invalid);
    CHECK(ParseTacticMode(BossTacticKind::Interrupt, "now") == TacticMode::Invalid);
    CHECK(ParseTacticMode(BossTacticKind::Interrupt, "") == TacticMode::Default);
    CHECK(ParseTacticMode(BossTacticKind::AvoidCast, "") == TacticMode::Default);
    CHECK(ParseTacticMode(BossTacticKind::AvoidCast, "all") == TacticMode::All);
    CHECK(ParseTacticMode(BossTacticKind::Interrupt, "all") == TacticMode::Invalid);
    CHECK(ParseTacticMode(BossTacticKind::KillFirst, "all") == TacticMode::Invalid);
    // Loader: nothing is visible before the seal, and the first row of a key wins.
    {
        BossTacticTable table;
        CHECK(table.Add(Row(70, 6910, BossTacticKind::Interrupt, 0, 16006)));
        CHECK(table.Add(Row(70, 6910, BossTacticKind::Interrupt, 0, 15117)));
        CHECK(!table.Add(Row(70, 6910, BossTacticKind::Interrupt, 0, 16006, 9)));  // same key
        CHECK(table.Add(Row(70, 7228, BossTacticKind::Behind, 0, 8374, 0, 10.0f)));
        CHECK(table.Size() == 3);
        CHECK(!table.HasMap(70));                  // not sealed yet
        CHECK(table.Rows(70, 6910) == nullptr);
        table.Seal();
        CHECK(table.Sealed());
        CHECK(!table.Add(Row(70, 7023, BossTacticKind::KillFirst, 7209, 0, 1)));  // refused after the seal
        CHECK(table.Size() == 3);
        CHECK(table.HasMap(70));
        CHECK(!table.HasMap(1));
        const std::vector<BossTacticRow>* rows = table.Rows(70, 6910);
        CHECK(rows && rows->size() == 2);
        CHECK((*rows)[0].spellId == 16006);
        CHECK(table.Rows(70, 6911) == nullptr);
        CHECK(table.Rows(71, 6910) == nullptr);    // same entry on another map
    }

    // Loader: unusable rows are refused.
    {
        BossTacticTable table;
        CHECK(!table.Add(Row(0, 6910, BossTacticKind::Interrupt, 0, 16006)));          // no map
        CHECK(!table.Add(Row(70, 0, BossTacticKind::Interrupt, 0, 16006)));            // no boss
        CHECK(!table.Add(Row(70, 6910, BossTacticKind::None, 0, 16006)));              // unknown kind
        CHECK(!table.Add(Row(70, 6910, BossTacticKind::Interrupt, 0, 0)));             // interrupt without a spell
        CHECK(!table.Add(Row(70, 7228, BossTacticKind::Behind, 0, 8374, 0, 0.0f)));    // behind without a radius
        CHECK(!table.Add(Row(70, 7228, BossTacticKind::Behind, 0, 8374, 0, -3.0f)));
        CHECK(!table.Add(Row(309, 15111, BossTacticKind::HoldKill)));                  // hold_kill without a blocker
        CHECK(!table.Add(Row(309, 15111, BossTacticKind::OneAtATime, 15146)));         // one_at_a_time with a target
        CHECK(table.Size() == 0);
        table.Seal();
        CHECK(!table.HasMap(70));  // an empty sealed table has no map: the tactics stay off
    }

    // Row gates: a boss health band and a bot aura. The band is inclusive, and with the master key off a gated row is
    // closed (today's behaviour, before the columns existed); an ungated row stays open either way.
    {
        BossTacticRow band = Gated(Row(531, 15263, BossTacticKind::DpsHoldAfter, 0, 0, 0, 3.0f), 0, 75);
        CHECK(RowGateOpen(band, true, 75, false));    // the band's edge is inside
        CHECK(!RowGateOpen(band, true, 76, false));   // 76 % is out of a [0, 75] band
        CHECK(RowGateOpen(band, true, 0, false));
        CHECK(!RowGateOpen(band, false, 75, false));  // key off: a gated row is closed
        BossTacticRow lowBand = Gated(Row(531, 15263, BossTacticKind::DpsHoldAfter, 0, 0, 0, 3.0f), 76, 100);
        CHECK(!RowGateOpen(lowBand, true, 75, false));
        CHECK(RowGateOpen(lowBand, true, 76, false));
        CHECK(RowGateOpen(lowBand, true, 100, false));
        BossTacticRow plain = Row(70, 6910, BossTacticKind::Interrupt, 0, 16006);
        CHECK(RowGateOpen(plain, false, 50, false));  // no gate: the key changes nothing
        BossTacticRow aura = Gated(Row(309, 11380, BossTacticKind::KillFirst, 14986, 0, 0), 0, 100, 24306);
        CHECK(!RowGateOpen(aura, true, 100, false));  // the bot does not carry Delusions
        CHECK(RowGateOpen(aura, true, 100, true));
        CHECK(!RowGateOpen(aura, false, 100, true));  // key off
        CHECK(RowGateSet(band));
        CHECK(!RowGateSet(plain));
    }

    // Row gates: the band reads one unit of the boss entry, never whichever attacker the scan met first. Of the
    // same-entry attackers it is the highest max health (the real boss over its clones and adds), the highest current
    // %, then the lowest guid, so both orders of the same fight open or close a row alike.
    {
        BossTacticRow band = Gated(Row(309, 15263, BossTacticKind::DpsHoldAfter, 0, 0, 0, 3.0f), 0, 75);
        BossGateUnit boss;
        boss.maxHealth = 200000;
        boss.hpPct = 80;  // the real boss is out of the band, so the row stays closed
        boss.guid = 200;
        BossGateUnit clone;
        clone.maxHealth = 50000;
        clone.hpPct = 10;  // a first-seen pick would open the phase row on the clone
        clone.guid = 100;
        std::vector<BossGateUnit> bossFirst;
        bossFirst.push_back(boss);
        bossFirst.push_back(clone);
        std::vector<BossGateUnit> cloneFirst;
        cloneFirst.push_back(clone);
        cloneFirst.push_back(boss);
        const int bossPick = PickBossGateUnit(bossFirst);
        const int clonePick = PickBossGateUnit(cloneFirst);
        CHECK(bossPick == 0);
        CHECK(clonePick == 1);
        CHECK(bossFirst[bossPick].hpPct == cloneFirst[clonePick].hpPct);
        CHECK(!RowGateOpen(band, true, bossFirst[bossPick].hpPct, false));
        CHECK(!RowGateOpen(band, true, cloneFirst[clonePick].hpPct, false));
        // A first-seen pick would have opened the row on the clone: the case the authoritative pick exists for.
        CHECK(RowGateOpen(band, true, cloneFirst[0].hpPct, false));
        // Equal max health: the highest current % decides, whichever came first.
        BossGateUnit woundedBoss = boss;
        woundedBoss.maxHealth = clone.maxHealth;
        woundedBoss.hpPct = 40;
        std::vector<BossGateUnit> woundedFirst;
        woundedFirst.push_back(woundedBoss);
        woundedFirst.push_back(clone);  // 10 %
        CHECK(PickBossGateUnit(woundedFirst) == 0);
        std::vector<BossGateUnit> woundedLast;
        woundedLast.push_back(clone);
        woundedLast.push_back(woundedBoss);
        CHECK(PickBossGateUnit(woundedLast) == 1);
        // Equal max health and health: the lowest guid decides.
        BossGateUnit older = clone;
        older.guid = 50;
        std::vector<BossGateUnit> guidOrder;
        guidOrder.push_back(clone);  // guid 100
        guidOrder.push_back(older);  // guid 50
        CHECK(PickBossGateUnit(guidOrder) == 1);
        std::vector<BossGateUnit> guidOrderReversed;
        guidOrderReversed.push_back(older);
        guidOrderReversed.push_back(clone);
        CHECK(PickBossGateUnit(guidOrderReversed) == 0);
        CHECK(PickBossGateUnit(std::vector<BossGateUnit>()) == -1);
    }
    // Loader: the band is a percentage range, the duplicate key includes it, and only a scan-read kind may gate.
    {
        BossTacticTable table;
        CHECK(table.Add(Gated(Row(531, 15263, BossTacticKind::DpsHoldAfter, 0, 0, 0, 3.0f), 0, 75)));
        CHECK(table.Add(Gated(Row(531, 15263, BossTacticKind::DpsHoldAfter, 0, 0, 0, 3.0f), 0, 50)));  // another band
        CHECK(!table.Add(Gated(Row(531, 15263, BossTacticKind::DpsHoldAfter, 0, 0, 0, 3.0f), 0, 50)));  // same key
        CHECK(!table.Add(Gated(Row(531, 15263, BossTacticKind::DpsHoldAfter, 0, 0, 0, 3.0f), 101, 100)));  // > 100
        CHECK(!table.Add(Gated(Row(531, 15263, BossTacticKind::DpsHoldAfter, 0, 0, 0, 3.0f), 80, 70)));    // inverted
        CHECK(!table.Add(Gated(Row(309, 15111, BossTacticKind::HoldKill, 15146), 0, 90)));       // read from the table
        CHECK(!table.Add(Gated(Row(309, 15146, BossTacticKind::TankHold), 0, 100, 24306)));
        CHECK(!table.Add(Gated(Row(309, 15111, BossTacticKind::OneAtATime), 50, 100)));
        CHECK(!table.Add(Gated(Row(309, 14599, BossTacticKind::LinkedDeath, 11347, 0, 1, 2.0f), 90, 100)));
        CHECK(table.Size() == 2);
    }

    // Loader: the new kinds' own shape checks.
    {
        BossTacticTable table;
        CHECK(!table.Add(Row(309, 11380, BossTacticKind::DpsHoldAfter, 0, 0, 0, 0.0f)));   // no seconds
        CHECK(!table.Add(Row(309, 11380, BossTacticKind::DpsHoldAfter, 0, 0, 0, 31.0f)));  // longer than 30 s
        CHECK(table.Add(Row(309, 11380, BossTacticKind::DpsHoldAfter, 0, 0, 0, 3.0f)));
        CHECK(!table.Add(Row(309, 11380, BossTacticKind::DispelPriority)));                 // no aura
        BossTacticRow hex = Row(309, 11380, BossTacticKind::DispelPriority, 0, 17172);
        hex.mode = TacticMode::Now;
        CHECK(table.Add(hex));
        BossTacticRow wrongMode = Row(309, 11380, BossTacticKind::DispelPriority, 0, 24306);
        wrongMode.mode = ParseTacticMode(wrongMode.kind, "away");                            // flee's word
        CHECK(wrongMode.mode == TacticMode::Invalid);
        CHECK(!table.Add(wrongMode));
        CHECK(!table.Add(Row(309, 11382, BossTacticKind::FleeOnAura)));                     // no aura
        CHECK(!table.Add(Row(309, 14515, BossTacticKind::FleeOnAura, 0, 24210, 0, -1.0f)));
        CHECK(!table.Add(Row(309, 11382, BossTacticKind::FreezeOnAura)));                   // no aura
        CHECK(!table.Add(Row(309, 15082, BossTacticKind::TauntAfterCast, 16290, 0, 0, 0.0f)));  // no deadline
        CHECK(!table.Add(Row(309, 15082, BossTacticKind::TauntAfterCast, 16290, 0, 1, 2.0f)));  // no trigger
        BossTacticRow blink = Row(309, 15082, BossTacticKind::TauntAfterCast, 16290, 0, 1, 2.0f);
        blink.mode = TacticMode::Blink;
        CHECK(table.Add(blink));
        CHECK(!table.Add(Row(309, 15932, BossTacticKind::SwapOnAura, 0, 25646, 1, 5.0f)));  // one tank
        CHECK(table.Add(Row(309, 15932, BossTacticKind::SwapOnAura, 0, 25646, 2, 5.0f)));
        BossTacticRow noWindow = Row(309, 14599, BossTacticKind::LinkedDeath, 11347, 0, 1, 2.0f);
        noWindow.windowSec = 0.0f;
        CHECK(!table.Add(noWindow));
    }

    // Loader: a use_object row names the clicked object, a clicker role of free DPS, healer or non-tank, a boss
    // range of 0-60 yd, and a window of 0 or at least 10 s on top of an aura trigger. The column default of 7 s is
    // refused, so a row must state its own window.
    {
        BossTacticTable table;
        auto useObject = [](std::uint32_t target, std::uint32_t spell, std::uint8_t priority, float param, float window)
        {
            BossTacticRow row = Row(509, 15339, BossTacticKind::UseObject, target, spell, priority, param);
            row.windowSec = window;
            return row;
        };
        CHECK(!table.Add(useObject(0, 25176, 0, 25.0f, 38.0f)));       // no object
        CHECK(!table.Add(useObject(180619, 25176, 0, 25.0f, 7.0f)));   // the column default: too short
        CHECK(!table.Add(useObject(180619, 0, 0, 25.0f, 0.0f)));       // neither aura nor timer
        CHECK(!table.Add(useObject(180619, 25176, 3, 25.0f, 38.0f)));  // no such role
        CHECK(!table.Add(useObject(180619, 25176, 0, 0.0f, 38.0f)));
        CHECK(!table.Add(useObject(180619, 25176, 0, 61.0f, 38.0f)));
        CHECK(table.Add(useObject(180619, 25176, 0, 25.0f, 38.0f)));
        CHECK(!table.Add(useObject(180619, 25176, 0, 25.0f, 38.0f)));  // same key
        CHECK(table.Size() == 1);
        CHECK(!table.HasUseObjectMap(509));                            // not sealed yet
        table.Seal();
        CHECK(table.HasUseObjectMap(509));
        CHECK(!table.HasUseObjectMap(309));
        CHECK(table.HasKindFor(509, 15339, BossTacticKind::UseObject));
        CHECK(!table.HasKindFor(509, 15339, BossTacticKind::Interrupt));
        CHECK(!table.HasKindFor(509, 15341, BossTacticKind::UseObject));
        CHECK(!table.HasKindFor(309, 14599, BossTacticKind::UseObject));
    }

    // Loader: tank_anchor is the use_object mode word, and a foreign word is Invalid.
    {
        BossTacticTable table;
        BossTacticRow anchored = Row(509, 15339, BossTacticKind::UseObject, 180619, 25176, 0, 25.0f);
        anchored.windowSec = 0.0f;  // an aura-only row: no timer
        anchored.mode = ParseTacticMode(anchored.kind, "tank_anchor");
        CHECK(anchored.mode == TacticMode::TankAnchor);
        CHECK(table.Add(anchored));
        BossTacticRow never = Row(509, 15340, BossTacticKind::UseObject, 180619, 25176, 0, 25.0f);
        never.windowSec = 38.0f;
        never.mode = ParseTacticMode(never.kind, "never");             // a foreign word
        CHECK(never.mode == TacticMode::Invalid);
        CHECK(!table.Add(never));
    }

    // Loader: a tank_post row names the awaited unit, a tank slot 1-7, a spot, an arrival radius of 5-40 yd and a
    // window of 10-120 s. No spell, no mode word, no cycle columns; its health band is the kind's own arming gate.
    {
        BossTacticTable table;
        auto tankPost = [](std::uint32_t target, std::uint32_t spell, std::uint8_t priority, float param,
                           float window, bool spot)
        {
            BossTacticRow row = Row(249, 10184, BossTacticKind::TankPost, target, spell, priority, param);
            row.windowSec = window;
            if (spot)
            {
                row.spotX = -17.0f;
                row.spotY = -212.75f;
                row.spotZ = -88.3f;
            }
            return row;
        };
        CHECK(!table.Add(tankPost(0, 0, 1, 15.0f, 60.0f, true)));       // no awaited unit
        CHECK(!table.Add(tankPost(50143, 33439, 1, 15.0f, 60.0f, true)));  // a spell
        CHECK(!table.Add(tankPost(50143, 0, 0, 15.0f, 60.0f, true)));   // slot 0 is the main tank, not a post
        CHECK(!table.Add(tankPost(50143, 0, 8, 15.0f, 60.0f, true)));   // no such slot
        CHECK(!table.Add(tankPost(50143, 0, 1, 15.0f, 60.0f, false)));  // no spot
        CHECK(!table.Add(tankPost(50143, 0, 1, 4.0f, 60.0f, true)));    // radius below 5
        CHECK(!table.Add(tankPost(50143, 0, 1, 41.0f, 60.0f, true)));   // radius above 40
        CHECK(!table.Add(tankPost(50143, 0, 1, 15.0f, 9.0f, true)));    // window below 10
        CHECK(!table.Add(tankPost(50143, 0, 1, 15.0f, 121.0f, true)));  // window above 120
        BossTacticRow modeWord = tankPost(50143, 0, 1, 15.0f, 60.0f, true);
        modeWord.mode = ParseTacticMode(modeWord.kind, "until_tank");
        CHECK(modeWord.mode == TacticMode::Invalid);
        CHECK(!table.Add(modeWord));
        CHECK(table.Add(Gated(tankPost(50143, 0, 1, 15.0f, 60.0f, true), 41, 66)));  // the band arms it
        CHECK(table.Add(tankPost(50143, 0, 2, 15.0f, 60.0f, true)));    // another slot, another key
        CHECK(table.Size() == 2);
        CHECK(!table.HasTankPostMap(249));                              // not sealed yet
        table.Seal();
        CHECK(table.HasTankPostMap(249));
        CHECK(!table.HasTankPostMap(309));
        CHECK(table.HasKindFor(249, 10184, BossTacticKind::TankPost));
        CHECK(!table.HasKindFor(249, 10184, BossTacticKind::Interrupt));
        CHECK(!table.HasKindFor(309, 10184, BossTacticKind::TankPost));
    }

    // Activation gate: needs rows on the map, then the opt-in or a commander binding.
    CHECK(!BossTacticsWanted(false, false, true));   // default install, normal player's bots: unchanged
    CHECK(BossTacticsWanted(true, false, true));     // AiPlayerbot.DungeonTactics = 1
    CHECK(BossTacticsWanted(false, true, true));     // commander-bound group
    CHECK(BossTacticsWanted(true, true, true));
    CHECK(!BossTacticsWanted(true, true, false));    // map without rows
    CHECK(!BossTacticsWanted(false, true, false));

    // Subject: the row's target, or the boss itself.
    CHECK(TacticSubjectEntry(Row(70, 7023, BossTacticKind::KillFirst, 7209)) == 7209);
    CHECK(TacticSubjectEntry(Row(70, 6910, BossTacticKind::Interrupt, 0, 16006)) == 6910);
    CHECK(TacticSubjectEntry(Row(509, 15339, BossTacticKind::UseObject, 180619, 25176, 0, 25.0f)) == 15339);

    // Interrupt matching: spell and caster both have to match; only interrupt rows count.
    {
        const std::vector<BossTacticRow> rows = {
            Row(70, 6910, BossTacticKind::Interrupt, 0, 16006),
            Row(70, 6910, BossTacticKind::Interrupt, 6911, 5000),
            Row(70, 6910, BossTacticKind::KillFirst, 6911, 0, 1),
        };
        const ActiveBossRows active = Active(rows);
        CHECK(IsInterruptListed(active, 6910, 16006));
        CHECK(!IsInterruptListed(active, 6910, 15801));   // the filler bolt is not listed
        CHECK(!IsInterruptListed(active, 6911, 16006));   // another caster
        CHECK(IsInterruptListed(active, 6911, 5000));     // a listed add
        CHECK(!IsInterruptListed(active, 6911, 0));
        CHECK(!IsInterruptListed(ActiveBossRows(), 6910, 16006));
    }

    // Kill first: lowest priority wins, nearer breaks a tie, unlisted/unpickable/far ones are skipped.
    {
        const std::vector<BossTacticRow> rows = {
            Row(70, 6906, BossTacticKind::KillFirst, 6906, 0, 1),
            Row(70, 6906, BossTacticKind::KillFirst, 6907, 0, 2),
            Row(70, 6906, BossTacticKind::KillFirst, 6908, 0, 3),
            Row(70, 6907, BossTacticKind::KillFirst, 6906, 0, 1),  // duplicated under a second dwarf
            Row(70, 7023, BossTacticKind::KillFirst, 7209, 0, 1),
        };
        const ActiveBossRows active = Active(rows);

        // The three dwarves in the order they were found: Olaf, Eric, Baelog.
        std::vector<TacticCandidate> dwarves = { Cand(6908, 5), Cand(6907, 5), Cand(6906, 30) };
        CHECK(PickKillFirst(active, dwarves) == 2);          // Baelog, even from further away
        dwarves[2].pickable = false;                         // dead or not attackable
        CHECK(PickKillFirst(active, dwarves) == 1);          // Eric
        dwarves[1].distance = 41.0f;                         // out of engage range
        CHECK(PickKillFirst(active, dwarves) == 0);          // Olaf
        dwarves[0].pickable = false;
        CHECK(PickKillFirst(active, dwarves) == -1);

        // Same priority: the nearer one.
        const std::vector<TacticCandidate> shards = { Cand(7209, 20), Cand(7209, 8), Cand(7023, 3) };
        CHECK(PickKillFirst(active, shards) == 1);           // the boss itself is not listed

        // Not in this fight's rows: no pick.
        CHECK(PickKillFirst(active, { Cand(1234, 3) }) == -1);
        CHECK(PickKillFirst(active, {}) == -1);
        CHECK(PickKillFirst(ActiveBossRows(), shards) == -1);

        // Interrupt and behind rows never pick a target.
        const std::vector<BossTacticRow> other = { Row(70, 6910, BossTacticKind::Interrupt, 0, 16006) };
        CHECK(PickKillFirst(Active(other), { Cand(6910, 3) }) == -1);
    }

    // TestDelusionsCarrierPicksShadeOnHealer: a Delusions carrier's Shades of Jin'do 14986 are kill_first rank 1 and
    // gated by needs_aura (the caller marks a Shade it may not attack unpickable). Among the ones it may attack, the one
    // eating a healer comes first -- the Shades copy the boss's threat list, on which the healers' heal threat is top,
    // so a healer left with one dies -- and distance only breaks a tie between two Shades on healers, or two on others.
    {
        const std::vector<BossTacticRow> rows = {
            Row(309, 11380, BossTacticKind::NeedsAura, 14986, 24306),
            Row(309, 11380, BossTacticKind::KillFirst, 14986, 0, 1),
            Row(309, 11380, BossTacticKind::KillFirst, 15112, 0, 2),
        };
        const ActiveBossRows active = Active(rows);
        std::vector<TacticCandidate> shades = { Cand(14986, 4), Cand(14986, 14) };
        shades[1].victimHealer = true;
        CHECK(PickKillFirst(active, shades) == 1);    // the farther Shade on a healer over the near one on a dps
        shades[0].victimHealer = true;
        CHECK(PickKillFirst(active, shades) == 0);    // both on healers: the nearer
        shades[0].victimHealer = false;
        shades[1].victimHealer = false;
        CHECK(PickKillFirst(active, shades) == 0);    // neither: the nearer, as before
        // A Shade the aura gate refuses is not a candidate at all (IsValid makes it unpickable), healer victim or not.
        std::vector<TacticCandidate> gated = { Cand(14986, 30, false), Cand(14986, 38) };
        gated[0].victimHealer = true;
        CHECK(PickKillFirst(active, gated) == 1);
        // Priority still comes first: the lower-priority totem's healer victim does not outrank a Shade.
        std::vector<TacticCandidate> mixed = { Cand(14986, 20), Cand(15112, 3) };
        mixed[1].victimHealer = true;
        CHECK(PickKillFirst(active, mixed) == 0);
    }

    // TestKillFirstGridMergeKeepsTheHealerVictim: a Shade the attackers path found (victimHealer read from its victim)
    // and the grid read finds again is one candidate, not two: the grid's victim role merges into the existing one,
    // and either read saying "a healer" keeps the flag. Failing case before the merge: the far Shade on a healer lost
    // to the near one on a damage dealer, because the attackers path never set the flag and the grid hit was dropped.
    {
        const std::vector<BossTacticRow> rows = {
            Row(309, 11380, BossTacticKind::KillFirst, 14986, 0, 1),
        };
        const ActiveBossRows active = Active(rows);
        std::vector<TacticCandidate> shades = { Cand(14986, 4), Cand(14986, 14) };
        CHECK(PickKillFirst(active, shades) == 0);    // no read says healer yet: the nearer
        MergeVictimHealer(shades[1], true);           // the grid read of the same far Shade: its victim is a healer
        CHECK(shades[1].victimHealer);
        CHECK(PickKillFirst(active, shades) == 1);
        MergeVictimHealer(shades[1], false);          // a grid read that missed the victim never clears the flag
        CHECK(shades[1].victimHealer);
        MergeVictimHealer(shades[0], false);
        CHECK(!shades[0].victimHealer);
    }

    // hold_kill: DPS leave a boss entry alone while more than `priority` living blockers of it are near. The modal
    // case, "a Mad Servant is held while one Mad Voidwalker lives": the two Zul'Gurub Servant rows, budget 0, blockers
    // the Mad Voidwalker 15146 and the unselectable Portal of Madness 15141.
    {
        const std::vector<BossTacticRow> rows = {
            Row(309, 15111, BossTacticKind::HoldKill, 15146),
            Row(309, 15111, BossTacticKind::HoldKill, 15141),
        };
        const ActiveBossRows active = Active(rows);
        auto countOf = [](std::uint32_t entry, std::uint32_t n) {
            return [entry, n](std::uint32_t e) -> std::uint32_t { return e == entry ? n : 0; };
        };
        CHECK(KillHeld(active, 15111, countOf(15146, 1)));    // a Mad Servant is held while one Mad Voidwalker lives
        CHECK(KillHeld(active, 15111, countOf(15146, 2)));
        CHECK(KillHeld(active, 15111, countOf(15141, 1)));    // the unselectable portal counts like any blocker
        CHECK(!KillHeld(active, 15111, countOf(15146, 0)));   // nothing left: the Servants are free
        CHECK(!KillHeld(active, 15111, countOf(11340, 9)));   // a creature no row names
        CHECK(!KillHeld(active, 15146, countOf(15146, 1)));   // the Voidwalker is not held itself
        CHECK(!KillHeld(active, 11340, countOf(15146, 5)));
        CHECK(!KillHeld(ActiveBossRows(), 15111, countOf(15146, 1)));

        // The row's priority is the budget: a budget-2 row holds only once three blockers live.
        const std::vector<BossTacticRow> budget = { Row(309, 15111, BossTacticKind::HoldKill, 15146, 0, 2) };
        CHECK(!KillHeld(Active(budget), 15111, countOf(15146, 2)));
        CHECK(KillHeld(Active(budget), 15111, countOf(15146, 3)));

        // No other kind holds anything.
        const std::vector<BossTacticRow> others = {
            Row(309, 15111, BossTacticKind::KillFirst, 15111, 0, 1),
            Row(309, 15111, BossTacticKind::AvoidCast, 0, 24612, 0, 6.0f),
            Row(309, 15111, BossTacticKind::TankHold),
        };
        CHECK(!KillHeld(Active(others), 15111, countOf(15146, 9)));

        // The subject is the row's boss_entry and the blocker its target_entry: a row keyed the other way round holds
        // the other creature, not this one.
        const std::vector<BossTacticRow> swapped = { Row(309, 15146, BossTacticKind::HoldKill, 15111) };
        CHECK(!KillHeld(Active(swapped), 15111, countOf(15111, 1)));
        CHECK(KillHeld(Active(swapped), 15146, countOf(15111, 1)));
    }

    // The hold lives in the target choice too, not only in the action: a free DPS bot leaves a held entry alone
    // (DpsLeavesAlone), so the dps pickers drop it and with nothing unheld left the value returns null and the bot
    // waits. The action's nearest-attacker fallback drops a held candidate the same way (PickNearestUnheld).
    {
        const std::vector<BossTacticRow> rows = {
            Row(309, 15111, BossTacticKind::HoldKill, 15146),
            Row(309, 15111, BossTacticKind::HoldKill, 15141),
        };
        const ActiveBossRows active = Active(rows);
        auto countOf = [](std::uint32_t entry, std::uint32_t n) {
            return [entry, n](std::uint32_t e) -> std::uint32_t { return e == entry ? n : 0; };
        };

        // Candidates in pick order: a held Servant is never re-picked while a Voidwalker lives.
        const std::vector<TacticCandidate> servantThenVoidwalker = { Cand(15111, 5.0f), Cand(15146, 20.0f) };
        CHECK(PickNearestUnheld(active, servantThenVoidwalker, countOf(15146, 1)) == 1);   // the Voidwalker
        CHECK(PickNearestUnheld(active, { Cand(15111, 5.0f) }, countOf(15146, 1)) == -1);  // nothing unheld: no pick
        CHECK(PickNearestUnheld(ActiveBossRows(), servantThenVoidwalker, countOf(15146, 1)) == 0);  // no rows: Servant

        // The same rule with the role gate the hold-kill action uses: the first candidate a free DPS bot may attack.
        auto firstUnheld = [&](const ActiveBossRows& r, const std::vector<std::uint32_t>& entries,
                               const std::function<std::uint32_t(std::uint32_t)>& count) -> std::uint32_t
        {
            for (std::uint32_t entry : entries)
                if (!DpsLeavesAlone(true, r, entry, 100, count,
                        [](const BossTacticRow&) -> std::uint32_t { return 0; }))
                    return entry;
            return 0;
        };
        const std::vector<std::uint32_t> both = { 15111, 15146 };
        CHECK(firstUnheld(active, both, countOf(15146, 1)) == 15146);
        CHECK(firstUnheld(active, { 15111 }, countOf(15146, 1)) == 0);
        CHECK(firstUnheld(ActiveBossRows(), both, countOf(15146, 1)) == 15111);
        CHECK(firstUnheld(active, both, countOf(15146, 0)) == 15111);       // no blocker: the Servant is free again
        CHECK(!DpsLeavesAlone(false, active, 15111, 100, countOf(15146, 1),
            [](const BossTacticRow&) -> std::uint32_t { return 0; }));       // a tank or healer is not a free DPS bot

        // The fallback still skips an unattackable or too-far candidate, like PickKillFirst.
        CHECK(PickNearestUnheld(active, { Cand(15146, 41.0f) }, countOf(15146, 0)) == -1);
        CHECK(PickNearestUnheld(active, { Cand(15146, 5.0f, false) }, countOf(15146, 0)) == -1);
        // A seek_aura source the row holds is not a hold_kill entry: the extra argument skips it too, so a drop never
        // re-picks another Son (H2).
        const std::vector<TacticCandidate> sons = { Cand(11357, 3.0f), Cand(11359, 9.0f) };
        CHECK(PickNearestUnheld(active, sons, countOf(15146, 0)) == 0);
        CHECK(PickNearestUnheld(active, sons, countOf(15146, 0), 11357) == 1);
    }

    // A seek_aura row's dose window has to be shorter than its cycle (spec §4's open interval): equal is refused.
    {
        BossTacticRow row = Row(309, 14834, BossTacticKind::SeekAura, 11357, 24321, 30, 4.0f);
        row.cycleSpell = 24324;
        row.cycleSec = 90.0f;
        row.spotX = -11802.0f;
        row.spotY = -1683.0f;
        row.spotZ = 52.9f;
        row.windowSec = 90.0f;
        BossTacticTable table;
        CHECK(!table.Add(row));
        row.windowSec = 89.9f;
        CHECK(table.Add(row));
    }

    // "a Mad Servant that is not the sole pick is held with no Voidwalker alive": a one_at_a_time row paces an entry by
    // itself, without any blocker. The pick is the lowest health among the units of the entry that fight, the lowest
    // guid on a tie, so every bot picks the same one; every other unit of the entry waits, whether it fights yet or
    // not. No unit of the entry fighting means no pick, so nothing is held.
    {
        const std::vector<SoleCandidate> servants = { Sole(0x1002, 100, true), Sole(0x1003, 100, false),
                                                      Sole(0x1004, 73, true) };
        CHECK(PickSoleKill(servants) == 2);                       // the wounded one
        CHECK(!OneAtATimeHolds(0x1004, 0x1004));                  // the pick is never held
        CHECK(OneAtATimeHolds(0x1004, 0x1002));                   // another Servant that fights waits
        CHECK(OneAtATimeHolds(0x1004, 0x1003));                   // a Servant nobody has hit yet waits too
        CHECK(!OneAtATimeHolds(0, 0x1002));                       // no pick: nobody is paced
        CHECK(PickSoleKill({}) == -1);
        CHECK(PickSoleKill({ Sole(0x1002, 100, false) }) == -1);  // out of combat: never the pick
        // Tie on health: the lowest guid is the pick.
        const std::vector<SoleCandidate> tied = { Sole(0x2002, 100, true), Sole(0x2001, 100, true) };
        CHECK(PickSoleKill(tied) == 1);
        CHECK(!OneAtATimeHolds(0x2001, 0x2001));
        CHECK(OneAtATimeHolds(0x2001, 0x2002));
        // A wounded unit nobody fights does not take the pick from one that fights.
        const std::vector<SoleCandidate> waiting = { Sole(0x3001, 100, true), Sole(0x3002, 1, false) };
        CHECK(PickSoleKill(waiting) == 0);
        CHECK(OneAtATimeHolds(0x3001, 0x3002));

        // A crowd-controlled (ineligible) Servant is never the pick, however wounded: the raid does not converge on a sheep.
        const std::vector<SoleCandidate> sheeped = { Sole(0x4001, 100, true), Sole(0x4002, 1, true, false) };
        CHECK(PickSoleKill(sheeped) == 0);
        CHECK(PickSoleKill({ Sole(0x4002, 1, true, false) }) == -1);

        // "a held CC'd Servant 5 yd from the cast vetoes the area action": the held subject is chosen among the
        // fight-wide candidates (the pick, plus the held Servant that is crowd-controlled and so ineligible), by
        // distance from the cast, and never from the bot's collapsible attackers list (which this helper is not given).
        const std::vector<SoleCandidate> pile = { Sole(0x5001, 40, true), Sole(0x5002, 100, true, false) };
        const float kNear = 10.0f;
        CHECK(HeldSubjectIndex(pile, { 30.0f, 5.0f }, kNear, false) == 1);    // the CC'd Servant 5 yd away: vetoed
        CHECK(HeldSubjectIndex(pile, { 3.0f, 30.0f }, kNear, false) == -1);   // only the pick is near: nothing held
        CHECK(HeldSubjectIndex(pile, { 30.0f, 30.0f }, kNear, false) == -1);  // both out of reach
        CHECK(HeldSubjectIndex(pile, { 30.0f, 5.0f }, 1.0e30f, false) == 1);  // the attacker debuff has no distance bound
        CHECK(HeldSubjectIndex({ Sole(0x5002, 100, true, false) }, { 5.0f }, kNear, false) == -1);   // no pick: no pacing
        CHECK(HeldSubjectIndex(pile, { 30.0f, std::numeric_limits<float>::infinity() }, 1.0e30f, false) == -1);   // unresolved
        // A hold_kill budget that is over holds every candidate, the pick included.
        CHECK(HeldSubjectIndex(pile, { 3.0f, 30.0f }, kNear, true) == 0);
        CHECK(HeldSubjectIndex(pile, { 30.0f, 30.0f }, kNear, true) == -1);
        CHECK(HeldSubjectIndex({}, {}, kNear, true) == -1);

        // The row is about its own boss_entry (like a tank_hold row) and needs no blocker: a hold_kill row of the same
        // entry stays a rule of its own.
        CHECK(TacticSubjectEntry(Row(309, 15111, BossTacticKind::OneAtATime)) == 15111);
        BossTacticTable table;
        CHECK(table.Add(Row(309, 15111, BossTacticKind::OneAtATime)));
        CHECK(!table.Add(Row(309, 15111, BossTacticKind::OneAtATime)));           // same key
        CHECK(!table.Add(Row(309, 15111, BossTacticKind::OneAtATime, 15146)));    // a target_entry is refused
        CHECK(table.Add(Row(309, 15111, BossTacticKind::HoldKill, 15146)));
        table.Seal();
        const std::vector<BossTacticRow>* stored = table.Rows(309, 15111);
        CHECK(stored && stored->size() == 2);
        CHECK((*stored)[0].kind == BossTacticKind::OneAtATime);
    }

    // A blocker counts towards a hold_kill budget only while it fights, or when it can never fight at all (the
    // unselectable Portal of Madness); a loose Voidwalker that has not woken yet does not hold the Servant.
    {
        CHECK(!CountsAsBlocker(false, false));   // out of combat and selectable: an add on the way, not a blocker yet
        CHECK(CountsAsBlocker(true, false));     // fighting
        CHECK(CountsAsBlocker(false, true));     // the unselectable portal
        CHECK(CountsAsBlocker(true, true));
    }

    // The tank mark yields to a nearer tank_hold pickup: with a TankHold row on the mark's own entry and a pickup
    // nearer than the mark, the mark is dropped (the tournament's loose bucket takes the pickup first); otherwise,
    // and without the row, the mark is taken as before.
    {
        CHECK(!TankTakesMark(true, true, 8.0f, 30.0f));    // an off-tank takes the nearer loose Voidwalker
        CHECK(TankTakesMark(true, true, 30.0f, 30.0f));    // the same distance: the mark, not a coin toss
        CHECK(TankTakesMark(true, true, 40.0f, 30.0f));    // no nearer pickup
        CHECK(TankTakesMark(true, false, 0.0f, 30.0f));    // no pickup at all
        CHECK(TankTakesMark(false, true, 8.0f, 30.0f));    // the mark without a TankHold row: as before
        CHECK(TankTakesMark(false, false, 0.0f, 30.0f));
    }

    // Loader: a hold_kill row is keyed by boss + kind + blocker + spell, so the two Servant rows coexist; its
    // target_entry is the blocker, so one without a blocker is refused (like a tank_hold row that carries one).
    {
        BossTacticTable table;
        CHECK(table.Add(Row(309, 15111, BossTacticKind::HoldKill, 15146)));
        CHECK(table.Add(Row(309, 15111, BossTacticKind::HoldKill, 15141)));
        CHECK(!table.Add(Row(309, 15111, BossTacticKind::HoldKill, 15146)));   // same key
        CHECK(table.Add(Row(309, 15111, BossTacticKind::Interrupt, 0, 24612)));
        table.Seal();
        const std::vector<BossTacticRow>* rows = table.Rows(309, 15111);
        CHECK(rows && rows->size() == 3);
        CHECK(table.HasMap(309));
    }

    // Boss front: inside the radius and in the frontal half-plane only.
    {
        const float pi = 3.1415926536f;
        // Boss at the origin facing +x (0 rad).
        CHECK(InsideBossFront(0, 0, 0.0f, 5, 0, 10));         // dead ahead
        CHECK(InsideBossFront(0, 0, 0.0f, 4, 4, 10));         // front, 45 degrees off
        CHECK(!InsideBossFront(0, 0, 0.0f, 5 * std::cos(1.75f), 5 * std::sin(1.75f), 10));  // 100 degrees off: beside it, out
        CHECK(!InsideBossFront(0, 0, 0.0f, -5, 0, 10));       // behind
        CHECK(!InsideBossFront(0, 0, 0.0f, 12, 0, 10));       // ahead but outside the radius
        // Facing -x (pi rad): the same test with the heading wrapped.
        CHECK(InsideBossFront(0, 0, pi, -5, 0, 10));
        CHECK(!InsideBossFront(0, 0, pi, 5, 0, 10));
        // Facing 5.5 rad (equivalent to -0.78): a bot at angle -0.5 is in front.
        CHECK(InsideBossFront(0, 0, 5.5f, 5 * std::cos(-0.5f), 5 * std::sin(-0.5f), 10));
    }

    // Flank destination: lands outside the front, at the bot's own distance, on the nearer side.
    {
        float x = 0, y = 0;
        BossFlankDestination(0, 0, 0.0f, 6, 1, 3.0f, x, y);   // bot ahead and a little to +y
        CHECK(!InsideBossFront(0, 0, 0.0f, x, y, 20));
        CHECK(y > 0.0f);                                      // the +y side is nearer
        CHECK(std::fabs(std::sqrt(x * x + y * y) - std::sqrt(37.0f)) < 0.01f);
        BossFlankDestination(0, 0, 0.0f, 6, -1, 3.0f, x, y);
        CHECK(y < 0.0f);
        BossFlankDestination(0, 0, 0.0f, 0.5f, 0.0f, 3.0f, x, y);  // inside the minimum: pushed out to it
        CHECK(std::fabs(std::sqrt(x * x + y * y) - 3.0f) < 0.01f);
        CHECK(!InsideBossFront(0, 0, 0.0f, x, y, 20));
    }

    // avoid_cast: the text form, and a row needs the spell and a radius.
    {
        CHECK(ParseBossTacticKind("avoid_cast") == BossTacticKind::AvoidCast);
        BossTacticTable table;
        CHECK(!table.Add(Row(309, 14750, BossTacticKind::AvoidCast, 0, 0, 0, 13.0f)));      // no spell
        CHECK(!table.Add(Row(309, 14750, BossTacticKind::AvoidCast, 0, 24024, 0, 0.0f)));   // no radius
        CHECK(!table.Add(Row(309, 14750, BossTacticKind::AvoidCast, 0, 24024, 0, -1.0f)));
        CHECK(table.Add(Row(309, 14750, BossTacticKind::AvoidCast, 0, 24024, 0, 13.0f)));
        CHECK(!table.Add(Row(309, 14750, BossTacticKind::AvoidCast, 0, 24024, 0, 15.0f)));  // same key
        table.Seal();
        CHECK(table.HasMap(309));
    }

    // avoid_cast radius: only the listed spell of the listed caster; the widest row wins.
    {
        const std::vector<BossTacticRow> rows = {
            Row(309, 14750, BossTacticKind::AvoidCast, 0, 24024, 0, 13.0f),
            Row(309, 14750, BossTacticKind::Interrupt, 0, 16006),
            Row(309, 14517, BossTacticKind::AvoidCast, 14965, 23970, 0, 8.0f),
            Row(309, 14518, BossTacticKind::AvoidCast, 14965, 23970, 0, 11.0f),
        };
        const ActiveBossRows active = Active(rows);
        CHECK(AvoidCastRadius(active, 14750, 24024) == 13.0f);
        CHECK(AvoidCastRadius(active, 14750, 23511) == 0.0f);   // another spell of the same caster
        CHECK(AvoidCastRadius(active, 14751, 24024) == 0.0f);   // the same spell from another caster
        CHECK(AvoidCastRadius(active, 14750, 16006) == 0.0f);   // an interrupt row is not an avoid row
        CHECK(AvoidCastRadius(active, 14965, 23970) == 11.0f);  // the add named by two rows: the wider one
        CHECK(AvoidCastRadius(ActiveBossRows(), 14750, 24024) == 0.0f);
    }

    // avoid_cast through either cast slot: the Zul'Gurub Mad Voidwalker (15146) channels Consuming Shadows 24614, which
    // never shows up in the generic slot, so the channel has to be read too; with both slots listed the wider wins.
    {
        const std::vector<BossTacticRow> rows = {
            Row(309, 15146, BossTacticKind::AvoidCast, 0, 24614, 0, 8.0f),
            Row(309, 15146, BossTacticKind::AvoidCast, 0, 24616, 0, 3.0f),
            Row(309, 15111, BossTacticKind::AvoidCast, 0, 24612, 0, 6.0f),
        };
        const ActiveBossRows active = Active(rows);
        CHECK(ListedCastRadius(active, 15146, 0, 24614) == 8.0f);        // the channel alone
        CHECK(ListedCastRadius(active, 15146, 24611, 24614) == 8.0f);    // an unlisted generic does not hide the channel
        CHECK(ListedCastRadius(active, 15146, 24614, 0) == 8.0f);        // the generic alone
        CHECK(ListedCastRadius(active, 15146, 24616, 24614) == 8.0f);    // both listed: the wider radius wins
        CHECK(ListedCastRadius(active, 15146, 0, 24615) == 0.0f);        // the tick, not a listed cast
        CHECK(ListedCastRadius(active, 15146, 0, 0) == 0.0f);            // both slots empty
        CHECK(ListedCastRadius(active, 15111, 0, 24614) == 0.0f);        // another caster
        CHECK(ListedCastRadius(ActiveBossRows(), 15146, 0, 24614) == 0.0f);
    }

    // Who leaves: everyone inside the radius except a tank.
    {
        CHECK(MustLeaveCast(false, 13.0f, 2.0f));
        CHECK(MustLeaveCast(false, 13.0f, 12.9f));
        CHECK(!MustLeaveCast(false, 13.0f, 13.0f));   // on the edge is out
        CHECK(!MustLeaveCast(false, 13.0f, 20.0f));
        CHECK(!MustLeaveCast(true, 13.0f, 2.0f));     // the tank holding the caster
        CHECK(!MustLeaveCast(false, 0.0f, 2.0f));     // no row
    }

    // Escape points: on a circle a step past the radius around the caster, the bot's own bearing first,
    // then alternating sides; every point is outside the radius.
    {
        AvoidCastPoint points[kAvoidCastPoints];
        const int n = AvoidCastDestinations(10.0f, 20.0f, 14.0f, 20.0f, 13.0f, 0.0f, points);
        CHECK(n == kAvoidCastPoints);
        const float ring = 13.0f + kAvoidCastStepOut;
        CHECK(std::fabs(points[0].x - (10.0f + ring)) < 0.01f);   // straight away from the caster (+x)
        CHECK(std::fabs(points[0].y - 20.0f) < 0.01f);
        for (int i = 0; i < n; ++i)
        {
            const float dx = points[i].x - 10.0f;
            const float dy = points[i].y - 20.0f;
            CHECK(std::fabs(std::sqrt(dx * dx + dy * dy) - ring) < 0.01f);
            CHECK(!MustLeaveCast(false, 13.0f, std::sqrt(dx * dx + dy * dy)));
        }
        CHECK(points[1].y > 20.0f && points[2].y < 20.0f);        // 45 degrees to one side, then the other
        CHECK(std::fabs(points[1].y - 20.0f + (points[2].y - 20.0f)) < 0.01f);
        // A bot on top of the caster has no bearing: the fallback angle is used.
        AvoidCastDestinations(10.0f, 20.0f, 10.0f, 20.0f, 13.0f, 3.14159265f, points);
        CHECK(points[0].x < 10.0f - ring + 0.01f);
        CHECK(std::fabs(points[0].y - 20.0f) < 0.01f);
    }

    // One log line per bot per cast: the first walk-out of a cast is new, the ticks that follow within the gap are
    // not, and another caster, another spell or a long silence start a new cast.
    {
        AvoidCastLogState state;
        CHECK(AvoidCastIsNewCast(state, 0x1111, 24024, 100, 1000));                    // first sight
        CHECK(!AvoidCastIsNewCast(state, 0x1111, 24024, 100, 1500));                   // the next tick of the cast
        CHECK(!AvoidCastIsNewCast(state, 0x1111, 24024, 100, 3400));                   // a 3 s blast kept alive tick by tick
        CHECK(AvoidCastIsNewCast(state, 0x2222, 24024, 100, 3500));                    // another caster, same spell
        CHECK(AvoidCastIsNewCast(state, 0x2222, 23511, 100, 3600));                    // same caster, another spell
        CHECK(AvoidCastIsNewCast(state, 0x2222, 23511, 100, 3600 + kAvoidCastLogGapMs));  // the cast ended, a new one
        CHECK(!AvoidCastIsNewCast(state, 0x2222, 23511, 100, 3600 + kAvoidCastLogGapMs + 10));
        // Same caster and spell but a new Spell object (a recast inside the gap) is a new cast.
        CHECK(AvoidCastIsNewCast(state, 0x2222, 23511, 200, 3600 + kAvoidCastLogGapMs + 20));
        // Two casters at once, the nearest one alternating between ticks: one row each.
        AvoidCastLogState two;
        CHECK(AvoidCastIsNewCast(two, 0xA, 24024, 1, 10000));
        CHECK(AvoidCastIsNewCast(two, 0xB, 24024, 2, 10100));
        CHECK(!AvoidCastIsNewCast(two, 0xA, 24024, 1, 10200));
        CHECK(!AvoidCastIsNewCast(two, 0xB, 24024, 2, 10300));
        CHECK(!AvoidCastIsNewCast(two, 0xA, 24024, 1, 10400));
        // More live casts than slots evicts the one seen longest ago.
        AvoidCastLogState many;
        for (std::uint64_t g = 1; g <= kAvoidCastLogCasts; ++g)
            CHECK(AvoidCastIsNewCast(many, g, 24024, g, 20000 + 10 * static_cast<std::uint32_t>(g)));
        CHECK(AvoidCastIsNewCast(many, 99, 24024, 99, 20100));
        CHECK(AvoidCastIsNewCast(many, 1, 24024, 1, 20110));   // caster 1 was the evicted one
        // The millisecond clock wraps: 4294967000 -> 100 is 396 ms, still the same cast.
        AvoidCastLogState wrap;
        CHECK(AvoidCastIsNewCast(wrap, 7, 1, 100, 4294967000u));
        CHECK(!AvoidCastIsNewCast(wrap, 7, 1, 100, 100u));
    }

    // Who holds a caster: a tank stays in when the caster is on it, or — only for a caster whose entry has a
    // tank_hold row — when the caster is on a non-tank and this tank's own current target is the caster. The second
    // case is per caster and per tank: without the row, or from another tank, every tank leaves like anyone else.
    {
        CHECK(HoldsCaster(true, false, true, true, true));    // a tank on a tank_hold caster that is on a healer holds
        CHECK(!HoldsCaster(true, false, true, true, false));  // same tank, a caster without the row: walks out (Bat Rider)
        CHECK(!HoldsCaster(true, false, true, false, true));  // the row, but this tank's target is elsewhere: walks out
        CHECK(HoldsCaster(true, true, false, false, false));  // the tank the caster is on holds it, row or not
        CHECK(HoldsCaster(true, true, false, true, false));   // ...and targeting it makes no difference
        CHECK(!HoldsCaster(true, false, false, true, true));  // the caster is on nobody: the row keeps nobody in
        CHECK(!HoldsCaster(false, false, true, true, true));  // a non-tank never holds, row and target or not
        CHECK(!HoldsCaster(false, true, false, false, false));
        CHECK(!MustLeaveCast(HoldsCaster(true, false, true, true, true), 13.0f, 2.0f));   // the tank walks in, not out
        CHECK(MustLeaveCast(HoldsCaster(true, false, true, true, false), 13.0f, 2.0f));   // no row: the tank leaves too
        CHECK(MustLeaveCast(HoldsCaster(false, false, true, true, true), 13.0f, 2.0f));   // the healer still leaves
    }

    // F4-A: mode `all` clears every tank while the row's spell is cast. With the flag off the row holds like a
    // default row; with it on, no bot holds the caster. A default row never reads the flag.
    {
        CHECK(HoldsCaster(true, true, false, true, false, /*allRowCasting*/ true) == false);  // the Rider's tank walks out
        CHECK(HoldsCaster(true, true, false, false, false, true) == false);                  // ... even without target
        CHECK(HoldsCaster(true, false, true, true, true, true) == false);    // a tank_hold row yields to `all` too
        CHECK(HoldsCaster(false, true, false, false, false, true) == false); // a non-tank never held anyway
        CHECK(HoldsCaster(true, true, false, false, false, false) == true);  // default row: the tank still holds it
        CHECK(HoldsCaster(true, true, false, true, false) == true);          // the old call shape still holds
    }

    // AvoidCastAllRow: only an `all` row listing the cast slot's spell matches.
    {
        auto allRow = []()
        {
            BossTacticRow row = Row(309, 14750, BossTacticKind::AvoidCast, 0, 24024, 0, 13.0f);
            row.mode = TacticMode::All;
            return row;
        };
        const std::vector<BossTacticRow> rows = {
            allRow(),
            Row(309, 15146, BossTacticKind::AvoidCast, 0, 24614, 0, 8.0f),   // a default row: never an `all` row
        };
        const ActiveBossRows active = Active(rows);
        CHECK(AvoidCastAllRow(active, 14750, 24024, 0));       // the generic cast is the listed spell
        CHECK(AvoidCastAllRow(active, 14750, 0, 24024));       // ... or the channel
        CHECK(AvoidCastAllRow(active, 14750, 24024, 24614));   // either slot listing it is enough
        CHECK(!AvoidCastAllRow(active, 14750, 24611, 0));      // another spell of the same caster: not listed
        CHECK(!AvoidCastAllRow(active, 14751, 24024, 0));      // the same spell from another caster
        CHECK(!AvoidCastAllRow(active, 15146, 24614, 0));      // a default row never counts, spell or not
        CHECK(!AvoidCastAllRow(ActiveBossRows(), 14750, 24024, 0));
        BossTacticRow wrongKind = allRow();
        wrongKind.kind = BossTacticKind::Interrupt;
        CHECK(!AvoidCastAllRow(Active({wrongKind}), 14750, 24024, 0));  // an interrupt row is not an avoid row
        CHECK(!AvoidCastAllRow(active, 14750, 0, 0));          // both slots empty
        BossTacticTable table;
        BossTacticRow stored = allRow();
        CHECK(table.Add(stored));                              // the loader keeps the new mode word
        BossTacticRow foreign = Row(309, 14750, BossTacticKind::Interrupt, 0, 24024);
        foreign.mode = ParseTacticMode(foreign.kind, "all");
        CHECK(foreign.mode == TacticMode::Invalid);
    }
    // The tank_hold lookup: the row is keyed by the caster's entry (target_entry 0 -> the boss entry itself), so the
    // loader takes it, and an avoid_cast row is not a tank_hold.
    {
        BossTacticTable table;
        CHECK(table.Add(Row(309, 15146, BossTacticKind::TankHold)));
        CHECK(!table.Add(Row(309, 15146, BossTacticKind::TankHold)));   // same key
        CHECK(!table.Add(Row(309, 15146, BossTacticKind::TankHold, 15147)));  // a tank_hold row cannot name a target_entry
        table.Seal();
        CHECK(table.Rows(309, 15146) != nullptr);
        const std::vector<BossTacticRow> rows = {
            Row(309, 15146, BossTacticKind::TankHold),
            Row(309, 15146, BossTacticKind::AvoidCast, 0, 24614, 0, 8.0f),
        };
        const ActiveBossRows active = Active(rows);
        CHECK(HasTankHold(active, 15146));
        CHECK(!HasTankHold(active, 14750));   // the Bat Rider has no such row
        const std::vector<BossTacticRow> onlyAvoid = { Row(309, 15146, BossTacticKind::AvoidCast, 0, 24614, 0, 8.0f) };
        CHECK(!HasTankHold(Active(onlyAvoid), 15146));   // an avoid_cast row is not a tank_hold
        CHECK(!HasTankHold(ActiveBossRows(), 15146));
    }

    // The avoid_cast map flag: only a map with an avoid_cast row, and only after the seal.
    {
        BossTacticTable flags;
        CHECK(flags.Add(Row(70, 6910, BossTacticKind::Interrupt, 0, 8292, 0, 0.0f)));
        CHECK(flags.Add(Row(309, 14750, BossTacticKind::AvoidCast, 0, 24024, 0, 13.0f)));
        CHECK(!flags.HasAvoidCastMap(309));   // not before the seal
        flags.Seal();
        CHECK(flags.HasAvoidCastMap(309));
        CHECK(!flags.HasAvoidCastMap(70));    // rows, but no avoid_cast row
        CHECK(flags.HasMap(70));
        CHECK(!flags.HasAvoidCastMap(1));
    }

    // The hold map flag: only a map with a hold_kill or one_at_a_time row, only after the seal. A map with only
    // kill_first or interrupt rows is not one (the hold multiplier does no scan for heals, buffs or moves there).
    {
        BossTacticTable holds;
        CHECK(holds.Add(Row(70, 6910, BossTacticKind::Interrupt, 0, 8292, 0, 0.0f)));
        CHECK(holds.Add(Row(70, 6911, BossTacticKind::KillFirst)));
        CHECK(holds.Add(Row(309, 15111, BossTacticKind::OneAtATime)));
        CHECK(holds.Add(Row(429, 11488, BossTacticKind::HoldKill, 11489)));
        CHECK(!holds.HasHoldMap(309));        // not before the seal
        holds.Seal();
        CHECK(holds.HasHoldMap(309));
        CHECK(holds.HasHoldMap(429));
        CHECK(!holds.HasHoldMap(70));         // rows, but only interrupt and kill_first
        CHECK(holds.HasMap(70));
        CHECK(!holds.HasHoldMap(1));
    }

    // Zones: 3D, centre to centre, the edge is out.
    {
        std::vector<CastDangerZone> zones = {Zone(0.0f, 0.0f, 0.0f, 13.0f)};
        CHECK(InsideAnyCastZone(zones, 12.9f, 0.0f, 0.0f));
        CHECK(!InsideAnyCastZone(zones, 13.0f, 0.0f, 0.0f));
        CHECK(!InsideAnyCastZone(zones, 5.0f, 0.0f, 12.5f));   // 13.5 yd in 3D: a ledge above is out
        CHECK(!InsideAnyCastZone(std::vector<CastDangerZone>(), 0.0f, 0.0f, 0.0f));
    }

    // The walk-out's straight line: never deeper into its own blast, never through another one.
    {
        std::vector<CastDangerZone> one = {Zone(0.0f, 0.0f, 0.0f, 13.0f)};
        // A bot 10 yd east: straight out, 45 and 90 degrees are clear; 135 degrees would cross the caster.
        AvoidCastPoint ring[kAvoidCastPoints];
        AvoidCastDestinations(0.0f, 0.0f, 10.0f, 0.0f, 13.0f, 0.0f, ring);
        CHECK(AvoidCastChordClear(10.0f, 0.0f, ring[0].x, ring[0].y, one));
        CHECK(AvoidCastChordClear(10.0f, 0.0f, ring[1].x, ring[1].y, one));
        CHECK(AvoidCastChordClear(10.0f, 0.0f, ring[3].x, ring[3].y, one) ==
              (SegmentPointDistance2D(10.0f, 0.0f, ring[3].x, ring[3].y, 0.0f, 0.0f) >= 10.0f - kAvoidCastChordSlack));
        CHECK(!AvoidCastChordClear(10.0f, 0.0f, ring[5].x, ring[5].y, one));
        CHECK(!AvoidCastChordClear(10.0f, 0.0f, ring[6].x, ring[6].y, one));
        // A second rider 25 yd east of the first: straight out (east) runs through its blast.
        std::vector<CastDangerZone> two = {Zone(0.0f, 0.0f, 0.0f, 13.0f), Zone(25.0f, 0.0f, 0.0f, 13.0f)};
        CHECK(!AvoidCastChordClear(10.0f, 0.0f, ring[0].x, ring[0].y, two));
    }

    // The escapes: only clear chords, shortest walk first.
    {
        std::vector<CastDangerZone> one = {Zone(0.0f, 0.0f, 0.0f, 13.0f)};
        AvoidCastPoint out[kAvoidCastPoints];
        const int n = AvoidCastEscapes(0.0f, 0.0f, 10.0f, 0.0f, 13.0f, 0.0f, one, out);
        CHECK(n >= 3 && n < kAvoidCastPoints);
        CHECK(std::fabs(out[0].x - (13.0f + kAvoidCastStepOut)) < 0.01f && std::fabs(out[0].y) < 0.01f);
        float last = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            const float d = std::sqrt((out[i].x - 10.0f) * (out[i].x - 10.0f) + out[i].y * out[i].y);
            CHECK(d + 0.001f >= last);
            last = d;
            CHECK(out[i].x > -0.01f);   // nothing on the far side of the caster
        }
        // Boxed in by a second blast on the straight-out side: straight out is gone, the turns stay.
        std::vector<CastDangerZone> two = {Zone(0.0f, 0.0f, 0.0f, 13.0f), Zone(25.0f, 0.0f, 0.0f, 13.0f)};
        const int m = AvoidCastEscapes(0.0f, 0.0f, 10.0f, 0.0f, 13.0f, 0.0f, two, out);
        CHECK(m > 0 && m < n);
        for (int i = 0; i < m; ++i)
            CHECK(std::fabs(out[i].y) > 1.0f);
    }

    // The cast hold: a move whose destination stands in a blast is held; not the walk-out, not a move to the bot
    // itself, not a move with no destination, not one outside every blast.
    {
        std::vector<CastDangerZone> zones = {Zone(0.0f, 0.0f, 0.0f, 13.0f)};
        CHECK(CastHoldsMove(false, false, true, 3.0f, 0.0f, 0.0f, zones));       // a reach to the caster or beside it
        CHECK(CastHoldsMove(false, false, true, 0.0f, 12.0f, 0.0f, zones));      // a heal walk to the tank, a stay spot
        CHECK(!CastHoldsMove(false, false, true, 0.0f, 20.0f, 0.0f, zones));     // a follow to a master outside
        CHECK(!CastHoldsMove(true, false, true, 3.0f, 0.0f, 0.0f, zones));       // the walk-out itself
        CHECK(!CastHoldsMove(false, true, true, 3.0f, 0.0f, 0.0f, zones));       // a move to the bot itself
        CHECK(!CastHoldsMove(false, false, false, 3.0f, 0.0f, 0.0f, zones));
        CHECK(!CastHoldsMove(false, false, true, 3.0f, 0.0f, 0.0f, std::vector<CastDangerZone>()));  // the tank's held caster
    }

    // The walk names in the log row.
    {
        CHECK(std::string(AvoidCastWalkName(AvoidCastWalk::Started)) == "started");
        CHECK(std::string(AvoidCastWalkName(AvoidCastWalk::Deferred)) == "deferred");
        CHECK(std::string(AvoidCastWalkName(AvoidCastWalk::None)) == "none");
    }

    // linked_death: kind text (and its alias), and the loader's shape checks.
    CHECK(ParseBossTacticKind("linked_death") == BossTacticKind::LinkedDeath);
    CHECK(ParseBossTacticKind("together") == BossTacticKind::LinkedDeath);
    CHECK(ParseBossTacticKind("Linked_Death") == BossTacticKind::None);
    CHECK(ParseBossTacticKind("linked-death") == BossTacticKind::None);
    CHECK(ParseBossTacticKind("linkeddeath") == BossTacticKind::None);
    {
        BossTacticTable table;
        CHECK(!table.Add(Row(309, 14599, BossTacticKind::LinkedDeath, 0, 0, 1, 3.0f)));      // no partner
        CHECK(!table.Add(Row(309, 14599, BossTacticKind::LinkedDeath, 11347, 0, 1, 0.0f)));  // no hold percent
        CHECK(!table.Add(Row(309, 14599, BossTacticKind::LinkedDeath, 11347, 0, 1, 101.0f))); // over 100 %
        CHECK(table.Add(Row(309, 14599, BossTacticKind::LinkedDeath, 11347, 0, 1, 3.0f)));
        CHECK(table.Add(Row(309, 14599, BossTacticKind::LinkedDeath, 11348, 0, 1, 3.0f)));
        CHECK(table.Size() == 2);
        table.Seal();
        CHECK(table.HasMap(309));
        CHECK(table.HasLinkedDeathMap(309));
        CHECK(!table.HasLinkedDeathMap(70));
        CHECK(!table.HasHoldMap(309));  // a linked group is not a hold_kill budget: each map flag answers its own kind
        // The group is read from the anchor alone, so two partner rows name one anchor. A map without rows has none,
        // and an unsealed table has none either.
        CHECK(table.LinkedDeathGroups(309).size() == 1);
        CHECK(table.LinkedDeathGroups(309)[0].anchor == 14599);
        CHECK(table.LinkedDeathGroups(309)[0].group == 1);
        CHECK(table.LinkedDeathGroups(70).empty());

        BossTacticTable unsealed;
        CHECK(unsealed.Add(Row(309, 14599, BossTacticKind::LinkedDeath, 11347, 0, 1, 3.0f)));
        CHECK(unsealed.LinkedDeathGroups(309).empty());
    }

    // One anchor can carry several groups (its rows' priorities): each key is scanned, so a second group under the
    // same boss entry is not hidden by the first one's rows.
    {
        BossTacticTable table;
        CHECK(table.Add(Row(309, 14599, BossTacticKind::LinkedDeath, 11347, 0, 1, 2.0f)));
        CHECK(table.Add(Row(309, 14599, BossTacticKind::LinkedDeath, 11348, 0, 1, 2.0f)));
        CHECK(table.Add(Row(309, 14599, BossTacticKind::LinkedDeath, 11361, 0, 2, 5.0f)));
        table.Seal();
        CHECK(table.LinkedDeathGroups(309).size() == 2);
        CHECK(table.LinkedDeathGroups(309)[0].group == 1);
        CHECK(table.LinkedDeathGroups(309)[1].group == 2);
    }
    // A same-entry pair (the two Qiraji Gladiators 15324 in one hall): the rows' anchor is the entry the pair
    // shares. Two living units of that entry at 80 % and 60 %: the pick is the 80 % one; both at 5 % or under:
    // released, and the released pick burns the lowest.
    std::vector<BossTacticRow> glad = {
        Row(509, 15324, BossTacticKind::LinkedDeath, 15324, 0, 1, 5.0f),
    };
    glad[0].windowSec = 7.0f;
    const ActiveBossRows gladRows = Active(glad);
    {
        LinkedDeathGroup group;
        CHECK(FindLinkedDeathGroup(gladRows, 15324, group));
        CHECK(group.bossEntry == 15324);
        CHECK(group.groupId == 1);
        CHECK(group.entries.size() == 1);
        CHECK(group.entries[0] == 15324);
        std::vector<LinkedMember> members = { Linked(1, 80, true, 15324), Linked(2, 60, true, 15324) };
        CHECK(!LinkedDeathReleased(group.param, members));
        CHECK(PickLinkedDeath(group.param, members) == 0);  // the highest: 80 %
        members = { Linked(1, 60, true, 15324), Linked(2, 80, true, 15324) };
        CHECK(PickLinkedDeath(group.param, members) == 1);  // the highest now
        members = { Linked(1, 5, true, 15324), Linked(2, 4, true, 15324) };
        CHECK(LinkedDeathReleased(group.param, members));
        CHECK(PickLinkedDeathTarget(true, true, gladRows, 15324, members) == -1);
    }


    // The group: the anchor and every partner its rows list, resolved from either. Its burn order is the raiser chain:
    // Lor'Khan (raised by Zath), then Zath (raised by Thekal), then Thekal.
    std::vector<BossTacticRow> thekal = {
        Row(309, 14599, BossTacticKind::LinkedDeath, 11347, 0, 1, 3.0f),
        Row(309, 14599, BossTacticKind::LinkedDeath, 11348, 0, 1, 3.0f),
    };
    thekal[0].burnOrder = {11347, 11348, 14599};
    thekal[1].burnOrder = thekal[0].burnOrder;
    const ActiveBossRows thekalRows = Active(thekal);
    {
        LinkedDeathGroup group;
        CHECK(FindLinkedDeathGroup(thekalRows, 14599, group));
        CHECK(group.bossEntry == 14599);
        CHECK(group.groupId == 1);
        CHECK(group.param == 3.0f);
        CHECK(group.entries.size() == 3);
        CHECK(std::find(group.entries.begin(), group.entries.end(), 14599) != group.entries.end());
        CHECK(std::find(group.entries.begin(), group.entries.end(), 11347) != group.entries.end());
        CHECK(std::find(group.entries.begin(), group.entries.end(), 11348) != group.entries.end());
        CHECK(group.burnOrder == std::vector<std::uint32_t>({11347, 11348, 14599}));
        CHECK(FindLinkedDeathGroup(thekalRows, 11348, group));  // a partner resolves the same group
        CHECK(group.entries.size() == 3);
        CHECK(group.burnOrder.size() == 3);
        CHECK(!FindLinkedDeathGroup(thekalRows, 11361, group));  // the tiger is no member
        CHECK(!FindLinkedDeathGroup(ActiveBossRows(), 14599, group));
    }

    // The group's own window (`window_s`) is the max over its rows: a partner with 15 s gives the group 15 s, so the
    // Thekal trio keeps its 15 s script retry while a new group defaults to 7.
    {
        const std::vector<BossTacticRow> rows = {
            Row(309, 14599, BossTacticKind::LinkedDeath, 11347, 0, 1, 2.0f),
            Row(309, 14599, BossTacticKind::LinkedDeath, 11348, 0, 1, 2.0f),
        };
        std::vector<BossTacticRow> timed = rows;
        timed[0].windowSec = 7.0f;
        timed[1].windowSec = 15.0f;
        const ActiveBossRows active = Active(timed);
        LinkedDeathGroup group;
        CHECK(FindLinkedDeathGroup(active, 11347, group));
        CHECK(group.windowSec == 15.0f);
        CHECK(LinkedDeathWindowMs(group.windowSec) == 15000);
        const ActiveBossRows plain = Active(rows);
        CHECK(FindLinkedDeathGroup(plain, 11347, group));
        CHECK(group.windowSec == 7.0f);
    }

    // Equalise: the pick is the living member with the highest health fraction, never one at or below param while
    // another stands above it, and never one this bot cannot attack.
    {
        std::vector<LinkedMember> members = { Linked(1, 100), Linked(2, 80), Linked(3, 60) };
        CHECK(PickLinkedDeathTarget(true, true, thekalRows, 14599, members) == 0);
        members[0].hpPct = 40;
        CHECK(PickLinkedDeathTarget(true, true, thekalRows, 11347, members) == 1);  // the highest now
        members = { Linked(1, 3), Linked(2, 60), Linked(3, 40) };
        CHECK(PickLinkedDeathTarget(true, true, thekalRows, 14599, members) == 1);  // 3 % is held, 60 % is not
        CHECK(LinkedDeathHolds(3.0f, 3, members));   // at the param, another member above it
        CHECK(!LinkedDeathHolds(3.0f, 4, members));
        // A raid mark on a held member names a member that has to live to the burn, so target selection falls
        // through to the group's pick; a mark on an above-threshold member, on a non-member, or with the group
        // released stands as it is.
        CHECK(LinkedDeathHoldsMember(3.0f, members, 1));
        CHECK(!LinkedDeathHoldsMember(3.0f, members, 2));
        CHECK(!LinkedDeathHoldsMember(3.0f, members, 77));
        std::vector<LinkedMember> burnAll = { Linked(1, 3), Linked(2, 2), Linked(3, 1) };
        CHECK(!LinkedDeathHoldsMember(3.0f, burnAll, 1));
        members = { Linked(1, 70, false), Linked(2, 50), Linked(3, 40) };
        CHECK(PickLinkedDeathTarget(true, true, thekalRows, 14599, members) == 1);  // the highest is out of reach
    }

    // Release: every living member at or below param, or one member left, hands the pick back so all DPS burn the
    // lowest (the deaths come together then).
    {
        std::vector<LinkedMember> members = { Linked(1, 3), Linked(2, 2), Linked(3, 1) };
        CHECK(LinkedDeathReleased(3.0f, members));
        CHECK(PickLinkedDeathTarget(true, true, thekalRows, 14599, members) == -1);
        members = { Linked(1, 50) };
        CHECK(LinkedDeathReleased(3.0f, members));
        CHECK(PickLinkedDeathTarget(true, true, thekalRows, 14599, members) == -1);
        members = { Linked(1, 50), Linked(2, 3) };
        CHECK(!LinkedDeathReleased(3.0f, members));  // one above param: not released (the hold runs)
    }

    // The release burn follows the row's order: all DPS on the first living member of it, then the next. A member out
    // of reach is passed over, a living member the order does not name is never picked, and the order alone decides
    // (never the health, so a raised member at 100 % does not steal the burn).
    {
        std::vector<std::uint32_t> const order = {11347, 11348, 14599};
        std::vector<LinkedMember> members = { Linked(1, 2, true, 14599), Linked(2, 2, true, 11347),
                                              Linked(3, 2, true, 11348) };
        CHECK(PickLinkedDeathBurn(order, members) == 1);
        members[1].attackable = false;
        CHECK(PickLinkedDeathBurn(order, members) == 2);
        members[2].attackable = false;
        CHECK(PickLinkedDeathBurn(order, members) == 0);
        members[0].attackable = false;
        CHECK(PickLinkedDeathBurn(order, members) == -1);
        // The order's first living member, even when a member not in the order is the only other one alive.
        members = { Linked(9, 100, true, 11361) };
        CHECK(PickLinkedDeathBurn(order, members) == -1);
        CHECK(PickLinkedDeathBurn(std::vector<std::uint32_t>(), members) == -1);
    }

    // The release latch, by guid and instance-wide. A member counts as dead only when the observing bot finds that known
    // guid dead (or as a corpse); one merely not seen (out of range, not cached) never does. A death while the release is
    // armed (every known guid last seen at or below param % within kLinkedDeathSeenMs) latches the burn, kept on after a
    // heal or a raise; it clears after kLinkedDeathLatchMs, and every known member confirmed dead clears it at once.
    {
        const float p = 3.0f;
        const std::vector<std::uint64_t> none;
        const std::vector<LinkedMember> three = { Linked(1, 2), Linked(2, 2), Linked(3, 1) };
        const std::vector<LinkedMember> two = { Linked(1, 2), Linked(2, 2) };  // guid 3 out of this observer's range
        const std::vector<LinkedMember> healed = { Linked(1, 80), Linked(2, 2), Linked(3, 1) };
        const std::vector<LinkedMember> healedTwo = { Linked(1, 80), Linked(2, 2) };  // guid 3 dead, not living
        const std::vector<LinkedMember> nobody;
        const std::vector<std::uint64_t> dead3 = { 3 };
        const std::vector<std::uint64_t> dead12 = { 1, 2 };

        // Two observers alternately see 3 and 2 living members, no death: the release never latches.
        LinkedDeathLatch alt;
        CHECK(!LinkedDeathLatchTick(alt, p, three, none, 1000));                // A sees 3 living
        CHECK(LinkedDeathUnseen(alt, two) == std::vector<std::uint64_t>{3});     // B sees 2: 3 unseen, not dead
        CHECK(!LinkedDeathLatchTick(alt, p, two, none, 1100));                  // B's read: no death found
        CHECK(!LinkedDeathLatchTick(alt, p, three, none, 1200));                // A again
        CHECK(!LinkedDeathLatchTick(alt, p, two, none, 1300));                  // B again
        CHECK(!alt.latched);
        // A heal above param puts a member back: the group is held again, no latch to keep it released.
        CHECK(!LinkedDeathLatchTick(alt, p, healed, none, 1400));
        CHECK(!alt.latched);
        CHECK(!LinkedDeathReleased(3.0f, healed));
        CHECK(PickLinkedDeath(3.0f, healed) == 0);  // the hold: the highest member (guid 1 at 80 %)

        // A real death of a known member, found dead by the observer, latches it while armed.
        LinkedDeathLatch death;
        CHECK(!LinkedDeathLatchTick(death, p, three, none, 1000));
        CHECK(!LinkedDeathLatchTick(death, p, two, none, 2000));                // 3 not seen, not dead: no latch
        CHECK(LinkedDeathLatchTick(death, p, two, dead3, 3000));                // 3 found dead: latched
        CHECK(death.deadGuid == 3);
        CHECK(LinkedDeathLatchTick(death, p, healedTwo, none, 4000));           // a heal of 1 (guid 3 is a corpse)
        CHECK(LinkedDeathLatchTick(death, p, healedTwo, none, 3000 + kLinkedDeathLatchMs - 1));
        CHECK(!LinkedDeathLatchTick(death, p, healedTwo, none, 3000 + kLinkedDeathLatchMs)); // 15 s: back to the hold
        // The corpse counts once: it is not looked up again, and a later read that finds it still dead does not re-latch.
        CHECK(LinkedDeathUnseen(death, two).empty());
        CHECK(!LinkedDeathLatchTick(death, p, two, none, 19000));
        CHECK(!LinkedDeathLatchTick(death, p, two, dead3, 20000));
        CHECK(!death.latched);

        // Codex round 2: a bot sees two low members while a known healthy third is omitted (another observer saw it
        // healthy). One of the two dies: the release is not armed, so the burn does not latch.
        LinkedDeathLatch partial;
        CHECK(!LinkedDeathLatchTick(partial, p, { Linked(1, 2), Linked(2, 2), Linked(3, 100) }, none, 1000));
        CHECK(!LinkedDeathArmed(partial, p, 1000));                             // 3 known healthy: disarmed
        CHECK(!LinkedDeathLatchTick(partial, p, two, none, 1500));              // 3 omitted, still known healthy
        CHECK(!LinkedDeathLatchTick(partial, p, { Linked(2, 2) }, { 1 }, 2000)); // 1 found dead, 3 still healthy
        CHECK(!partial.latched);

        // All three seen low, then one dies while the other two are still low: armed, so the death latches.
        LinkedDeathLatch lowThenDeath;
        CHECK(!LinkedDeathLatchTick(lowThenDeath, p, three, none, 1000));
        CHECK(LinkedDeathArmed(lowThenDeath, p, 1000));
        CHECK(LinkedDeathLatchTick(lowThenDeath, p, two, dead3, 2000));
        CHECK(lowThenDeath.deadGuid == 3);

        // A known living member not seen for longer than kLinkedDeathSeenMs blocks the arming (fail safe: hold), so a
        // death in that read does not latch either.
        LinkedDeathLatch stale;
        CHECK(!LinkedDeathLatchTick(stale, p, three, none, 1000));
        CHECK(!LinkedDeathLatchTick(stale, p, { Linked(2, 2), Linked(3, 1) }, none, 1000 + kLinkedDeathSeenMs + 1));
        CHECK(!LinkedDeathArmed(stale, p, 1000 + kLinkedDeathSeenMs + 1));      // 1 last seen too long ago
        CHECK(!LinkedDeathLatchTick(stale, p, two, dead3, 1000 + kLinkedDeathSeenMs + 2));
        CHECK(!stale.latched);

        // Every known member confirmed dead clears the latch, whatever the clock.
        LinkedDeathLatch all;
        CHECK(!LinkedDeathLatchTick(all, p, three, none, 1000));
        CHECK(LinkedDeathLatchTick(all, p, two, dead3, 1500));
        CHECK(LinkedDeathUnseen(all, nobody) == dead12);
        CHECK(!LinkedDeathLatchTick(all, p, nobody, dead12, 1600));
        CHECK(!all.latched);
        CHECK(all.known.empty());

        // A death while the release is not armed never latches: the read before it has to be armed too.
        LinkedDeathLatch open;
        const std::vector<LinkedMember> oneHealthy = { Linked(1, 50), Linked(2, 2), Linked(3, 1) };
        CHECK(!LinkedDeathLatchTick(open, p, oneHealthy, none, 1000));          // 1 above param: not armed
        CHECK(!LinkedDeathArmed(open, p, 1000));
        CHECK(!LinkedDeathLatchTick(open, p, two, dead3, 1001));                // 1 low again, 3 dies in the same read
        CHECK(!open.latched);

        // A raise undoes the earlier death: the member living again counts as a death once more when it dies again.
        LinkedDeathLatch raise;
        CHECK(!LinkedDeathLatchTick(raise, p, three, none, 1000));
        CHECK(LinkedDeathLatchTick(raise, p, two, dead3, 2000));
        CHECK(!LinkedDeathLatchTick(raise, p, three, none, 2000 + kLinkedDeathLatchMs));  // 3 back: window over
        CHECK(LinkedDeathLatchTick(raise, p, two, dead3, 2000 + kLinkedDeathLatchMs + 1000)); // 3 dies again
    }

    // Encounter reset (Codex round 3): a stale guid must not block the arming forever. An evade (every visible member out
    // of combat at full health, every known guid of the scan) resets the known set, armed and latch, so respawned members under new guids arm on the
    // next pull; a quiet group (no member seen for kLinkedDeathEndMs) resets too; a guid unseen for kLinkedDeathDropMs
    // while the group is seen is forgotten (a despawn).
    {
        const float p = 3.0f;
        const std::vector<std::uint64_t> none;
        const std::vector<LinkedMember> nobody;
        const std::vector<LinkedMember> three = { Linked(1, 2), Linked(2, 2), Linked(3, 1) };
        const std::vector<LinkedMember> two = { Linked(1, 2), Linked(2, 2) };
        const std::vector<LinkedMember> idleFull = { Linked(1, 100, false), Linked(2, 100, false) };

        // Evade: 1 and 2 low and armed, then both leave combat at full health. The reset clears them.
        LinkedDeathLatch evade;
        CHECK(!LinkedDeathLatchTick(evade, p, { Linked(1, 2), Linked(2, 2) }, none, 1000));
        CHECK(LinkedDeathArmed(evade, p, 1000));
        CHECK(!LinkedDeathLatchTick(evade, p, nobody, none, 2000, idleFull));
        CHECK(evade.known.empty());
        CHECK(!evade.armed);
        CHECK(!evade.latched);
        // The respawn under new guids 4 and 5 arms on the next pull; the death of 4 latches the burn.
        CHECK(!LinkedDeathLatchTick(evade, p, { Linked(4, 2), Linked(5, 2) }, none, 60000));
        CHECK(LinkedDeathArmed(evade, p, 60000));
        CHECK(LinkedDeathLatchTick(evade, p, { Linked(5, 2) }, { 4 }, 61000));
        CHECK(evade.deadGuid == 4);

        // An idle unit below full health is a fight, not an evade: nothing resets, the known guids stay.
        LinkedDeathLatch wiped;
        CHECK(!LinkedDeathLatchTick(wiped, p, { Linked(1, 2), Linked(2, 2) }, none, 1000));
        CHECK(!LinkedDeathLatchTick(wiped, p, nobody, none, 2000, { Linked(1, 2, false), Linked(2, 100, false) }));
        CHECK(wiped.known.size() == 2);
        CHECK(!LinkedDeathArmed(wiped, p, 2000)); // 2 is idle at 100 %, a known sighting above param: disarmed

        // A known unit at full health that still fights (out of this bot's view, so not a member) is no evade either.
        LinkedDeathLatch stillFights;
        CHECK(!LinkedDeathLatchTick(stillFights, p, { Linked(1, 2), Linked(2, 2) }, none, 1000));
        std::vector<LinkedMember> fighting = { Linked(1, 100, false), Linked(2, 100, false) };
        fighting[1].inCombat = true;
        CHECK(!LinkedDeathLatchTick(stillFights, p, nobody, none, 2000, fighting));
        CHECK(stillFights.known.size() == 2);
        CHECK(!LinkedDeathEncounterEnded(stillFights, 2000));

        // One visible idle member at full health is no evade while another known member is unresolved and recent (it may
        // still fight out of view). The reset waits until every known guid's latest observation, from any observer, is
        // idle at full health.
        LinkedDeathLatch partial;
        CHECK(!LinkedDeathLatchTick(partial, p, { Linked(1, 2), Linked(2, 2) }, none, 1000));
        CHECK(!LinkedDeathLatchTick(partial, p, nobody, none, 2000, { Linked(1, 100, false) }));
        CHECK(partial.known.size() == 2);                                       // 2 unresolved, seen 1 s ago: no reset
        CHECK(!LinkedDeathEncounterEnded(partial, 2000));
        CHECK(!LinkedDeathLatchTick(partial, p, nobody, none, 3000, idleFull)); // all known idle at full health: reset
        CHECK(partial.known.empty());

        // Two observers, each seeing only one of the two idle members (their scans do not overlap): the sightings
        // combine, so the second observer's read resets the encounter even though neither scan saw the whole group.
        LinkedDeathLatch split;
        CHECK(!LinkedDeathLatchTick(split, p, { Linked(1, 2), Linked(2, 2) }, none, 1000));
        CHECK(!LinkedDeathLatchTick(split, p, nobody, none, 2000, { Linked(1, 100, false) }));
        CHECK(split.known.size() == 2);                                          // 2's latest sighting is the fight
        CHECK(!LinkedDeathEncounterEnded(split, 2000));
        CHECK(!LinkedDeathLatchTick(split, p, nobody, none, 2500, { Linked(2, 100, false) }));
        CHECK(split.known.empty());                                              // 1 and 2 idle across two reads

        // An idle sighting older than kLinkedDeathSeenMs is not fresh enough to end the encounter: the group may have
        // resumed out of view. The 30 s no-sighting timeout still applies to a member not seen at all.
        LinkedDeathLatch staleIdle;
        CHECK(!LinkedDeathLatchTick(staleIdle, p, three, none, 1000));
        CHECK(!LinkedDeathLatchTick(staleIdle, p, nobody, none, 25000, idleFull)); // 3 unseen 24 s: still the fight
        CHECK(staleIdle.known.size() == 3);
        CHECK(!LinkedDeathLatchTick(staleIdle, p, nobody, none, 31000, nobody));
        CHECK(staleIdle.known.size() == 3);   // 3 timed out, but 1 and 2's idle sighting is 6 s old: not an evade
        CHECK(!LinkedDeathLatchTick(staleIdle, p, nobody, none, 31500, idleFull));
        CHECK(staleIdle.known.empty());        // a fresh idle sighting resets

        // The newest observation wins: after one observer sees 1 idle, another sees 2 still fighting at full health.
        // 2's fighting sighting overrides any older idle one, so no reset; once 2 is seen out of combat it resets.
        LinkedDeathLatch fightingWins;
        CHECK(!LinkedDeathLatchTick(fightingWins, p, { Linked(1, 2), Linked(2, 2) }, none, 1000));
        CHECK(!LinkedDeathLatchTick(fightingWins, p, nobody, none, 2000, { Linked(1, 100, false) }));
        CHECK(!LinkedDeathLatchTick(fightingWins, p, { Linked(2, 100) }, none, 2500));  // 2 fights, 1 idle
        CHECK(fightingWins.known.size() == 2);
        CHECK(!LinkedDeathEncounterEnded(fightingWins, 2500));
        CHECK(!LinkedDeathLatchTick(fightingWins, p, nobody, none, 3000, { Linked(1, 100, false) }));
        CHECK(fightingWins.known.size() == 2);                                   // 2 last seen fighting: no reset
        CHECK(!LinkedDeathLatchTick(fightingWins, p, nobody, none, 3500, idleFull));
        CHECK(fightingWins.known.empty());                                       // 2 idle again: reset

        // An unresolved known member not seen for kLinkedDeathEndMs times out even while another member is idle.
        LinkedDeathLatch timedOut;
        CHECK(!LinkedDeathLatchTick(timedOut, p, { Linked(1, 2), Linked(2, 2) }, none, 1000));
        CHECK(!LinkedDeathLatchTick(timedOut, p, nobody, none, 1000 + kLinkedDeathEndMs - 1, { Linked(1, 100, false) }));
        CHECK(timedOut.known.size() == 2);                                      // 2 unseen 29.999 s: still the fight
        CHECK(!LinkedDeathLatchTick(timedOut, p, nobody, none, 1000 + kLinkedDeathEndMs, { Linked(1, 100, false) }));
        CHECK(timedOut.known.empty());

        // Quiet group: no member in combat or idle, none seen for kLinkedDeathEndMs: the fight is gone, reset.
        LinkedDeathLatch quiet;
        CHECK(!LinkedDeathLatchTick(quiet, p, two, none, 1000));
        CHECK(!LinkedDeathLatchTick(quiet, p, nobody, none, 1000 + kLinkedDeathEndMs - 1)); // 29 s: still the fight
        CHECK(quiet.known.size() == 2);
        CHECK(!LinkedDeathLatchTick(quiet, p, nobody, none, 1000 + kLinkedDeathEndMs));     // 30 s: ended
        CHECK(quiet.known.empty());

        // Despawn: guid 3 is never seen again while 1 and 2 are. Within kLinkedDeathSeenMs it is not blocking, then it
        // blocks the arming (fail safe), and only after kLinkedDeathDropMs is it forgotten.
        LinkedDeathLatch despawn;
        CHECK(!LinkedDeathLatchTick(despawn, p, three, none, 1000));
        CHECK(!LinkedDeathLatchTick(despawn, p, two, none, 6500));                 // 3 out of sight for 5.5 s
        CHECK(!LinkedDeathArmed(despawn, p, 6500));                                // blocks the arming (fail safe)
        CHECK(!LinkedDeathLatchTick(despawn, p, two, none, 1000 + kLinkedDeathDropMs - 1));
        CHECK(!LinkedDeathArmed(despawn, p, 1000 + kLinkedDeathDropMs - 1));       // still blocking at 59.999 s
        CHECK(!LinkedDeathLatchTick(despawn, p, two, none, 1000 + kLinkedDeathDropMs));
        CHECK(despawn.known.size() == 2);                                          // 3 forgotten at 60 s
        CHECK(LinkedDeathArmed(despawn, p, 1000 + kLinkedDeathDropMs));
        CHECK(LinkedDeathLatchTick(despawn, p, { Linked(2, 2) }, { 1 }, 1000 + kLinkedDeathDropMs + 1000));
        CHECK(despawn.deadGuid == 1);

        // Fail safe in a real fight: 3 unseen for 5.6 s while 1 dies in the same window. Not armed: no latch.
        LinkedDeathLatch failsafe;
        CHECK(!LinkedDeathLatchTick(failsafe, p, three, none, 1000));
        CHECK(!LinkedDeathLatchTick(failsafe, p, { Linked(2, 2) }, { 1 }, 6600));
        CHECK(!failsafe.latched);
    }

    // Write order is not observation order: each sighting carries the time its bot's scan was taken. Bot B scans at 2
    // (guid 1 fighting) and writes at 2; bot A scans at 1 (guids 1 and 2 idle at full health) and writes at 3. A's older
    // idle sighting of guid 1 must not overwrite B's newer fight, so the encounter does not end; guid 2's sighting is
    // newer than anything stored and is applied. A later idle sighting (scanned at 4) does end it.
    {
        const float p = 3.0f;
        const std::vector<std::uint64_t> none;
        const std::vector<LinkedMember> nobody;
        LinkedDeathLatch order;
        CHECK(!LinkedDeathLatchTick(order, p, { Linked(1, 2), Linked(2, 2) }, none, 0));
        std::vector<LinkedMember> fighting = { Linked(1, 100, false) };
        fighting[0].inCombat = true;
        CHECK(!LinkedDeathLatchTick(order, p, fighting, none, 2, 2));                  // B: scanned 2, written 2
        std::vector<LinkedMember> idleOld = { Linked(1, 100, false), Linked(2, 100, false) };
        CHECK(!LinkedDeathLatchTick(order, p, nobody, none, 3, 1, idleOld));           // A: scanned 1, written 3
        CHECK(order.known.size() == 2);                                                // no reset: guid 1 still fights
        CHECK(!LinkedDeathEncounterEnded(order, 3));
        CHECK(!LinkedDeathLatchTick(order, p, nobody, none, 4, 4, idleOld));           // A's newer idle: encounter over
        CHECK(order.known.empty());
    }

    // The latch's window comes from the group's rows (`window_s`): 7 s by default, Thekal's trio 15 s. The constant is
    // only the fallback for a group without a window, which the loader refuses.
    {
        CHECK(LinkedDeathWindowMs(7.0f) == 7000);
        CHECK(LinkedDeathWindowMs(15.0f) == 15000);
        CHECK(LinkedDeathWindowMs(0.0f) == kLinkedDeathLatchMs);
        const float p = 3.0f;
        const std::vector<std::uint64_t> none;
        const std::vector<LinkedMember> three = { Linked(1, 2), Linked(2, 2), Linked(3, 1) };
        const std::vector<LinkedMember> two = { Linked(1, 2), Linked(2, 2) };
        const std::vector<std::uint64_t> dead3 = { 3 };

        LinkedDeathLatch seven;
        CHECK(!LinkedDeathLatchTick(seven, p, three, none, 1000, {}, LinkedDeathWindowMs(7.0f)));
        CHECK(LinkedDeathLatchTick(seven, p, two, dead3, 3000, {}, LinkedDeathWindowMs(7.0f)));
        CHECK(LinkedDeathLatchTick(seven, p, two, none, 3000 + 6999, {}, LinkedDeathWindowMs(7.0f)));
        CHECK(!LinkedDeathLatchTick(seven, p, two, none, 3000 + 7000, {}, LinkedDeathWindowMs(7.0f)));  // 7 s: the hold

        LinkedDeathLatch fifteen;
        CHECK(!LinkedDeathLatchTick(fifteen, p, three, none, 1000, {}, LinkedDeathWindowMs(15.0f)));
        CHECK(LinkedDeathLatchTick(fifteen, p, two, dead3, 3000, {}, LinkedDeathWindowMs(15.0f)));
        CHECK(fifteen.latched);
        CHECK(LinkedDeathLatchTick(fifteen, p, two, none, 3000 + 7000, {}, LinkedDeathWindowMs(15.0f)));
        CHECK(LinkedDeathLatchTick(fifteen, p, two, none, 3000 + 14999, {}, LinkedDeathWindowMs(15.0f)));
        CHECK(!LinkedDeathLatchTick(fifteen, p, two, none, 3000 + 15000, {}, LinkedDeathWindowMs(15.0f)));
    }

    // The ordered burn at walk 27's numbers: the trio at the 2 % hold is 1 709 + 2 136 + 2 665 hp, so at 2.2-3.0 k
    // dps it is down ~2.2-3 s after the release, and the death order is the row's. The core's raiser rule (7 s, then
    // the 8 s fallback) then sees every corpse's chosen raiser die before its check: no restore. The raid's old
    // one-at-a-time pattern (each member from full health, ~60 s for Thekal) leaves the first corpse's raiser alive at
    // the 7 s check, so it is raised: that is the window the order exists for.
    {
        std::vector<std::uint32_t> const order = {11347, 11348, 14599};
        // Constructed in another order on purpose: an arbitrary pick (the first member of the list) would burn the
        // wrong entry first, which the death-order check below catches.
        std::vector<LinkedMember> members = { Linked(1, 2665, true, 14599), Linked(2, 1709, true, 11347),
                                              Linked(3, 2136, true, 11348) };
        std::vector<std::pair<std::uint32_t, int>> deaths;
        double at = 0.0;
        for (;;)
        {
            const int pick = PickLinkedDeathBurn(order, members);
            if (pick < 0)
                break;
            at += members[pick].hpPct / 2200.0; // the walk's measured low end: the slower run is the hard one
            deaths.push_back(std::make_pair(members[pick].entry, static_cast<int>(at + 0.5)));
            members[pick].attackable = false;
        }
        CHECK(deaths.size() == 3);
        CHECK(deaths[0].first == 11347 && deaths[1].first == 11348 && deaths[2].first == 14599);
        CHECK(deaths.back().second - deaths.front().second <= 7); // inside the first corpse's 7 s check
        CHECK(ThekalRaises(deaths) == 0);
        // The same trio at 3.0 k dps is quicker still.
        std::vector<LinkedMember> fast = { Linked(1, 2665, true, 14599), Linked(2, 1709, true, 11347),
                                           Linked(3, 2136, true, 11348) };
        std::vector<std::pair<std::uint32_t, int>> fastDeaths;
        at = 0.0;
        for (;;)
        {
            const int pick = PickLinkedDeathBurn(order, fast);
            if (pick < 0)
                break;
            at += fast[pick].hpPct / 3000.0;
            fastDeaths.push_back(std::make_pair(fast[pick].entry, static_cast<int>(at + 0.5)));
            fast[pick].attackable = false;
        }
        CHECK(ThekalRaises(fastDeaths) == 0);
        // A bad burn: the old one-at-a-time pattern, each member from full health (the observed 9-93 % kills), so
        // Thekal's first corpse waits 60 s for Lor'Khan's 7 s check.
        std::vector<std::pair<std::uint32_t, int>> bad = { {14599, 61}, {11347, 122}, {11348, 183} };
        CHECK(ThekalRaises(bad) > 0);
    }

    // Area veto: a held member at the threshold beside the action's target is in the blast, so the area cast or the
    // DoT is vetoed. `distance[1]` is the target itself (an above-threshold member), `distance[0]` the held one.
    {
        const float near = kHoldKillAoeNearYd;
        const float anywhere = std::numeric_limits<float>::max();
        std::vector<LinkedMember> held = { Linked(1, 3), Linked(2, 60), Linked(3, 50) };
        CHECK(LinkedDeathHeldIndex(3.0f, held, { 5.0f, 0.0f, 30.0f }, near) == 0);   // held next to the target
        CHECK(LinkedDeathHeldIndex(3.0f, held, { 30.0f, 0.0f, 5.0f }, near) == -1);  // held out of the area
        CHECK(LinkedDeathHeldIndex(3.0f, held, { 0.0f, 0.0f, 30.0f }, anywhere) == 0);  // the DoT lands on it
        CHECK(LinkedDeathHeldIndex(3.0f, held, { std::numeric_limits<float>::infinity(), 0.0f, 5.0f }, anywhere)
              == -1);  // unresolved: never in reach
        // Burn phase: every living member at or below param, so the release allows the AoE that kills them together.
        std::vector<LinkedMember> burn = { Linked(1, 3), Linked(2, 2), Linked(3, 1) };
        CHECK(LinkedDeathHeldIndex(3.0f, burn, { 5.0f, 0.0f, 5.0f }, near) == -1);
        // One member above param keeps the others held, even with no held member near this target.
        std::vector<LinkedMember> oneAbove = { Linked(1, 3), Linked(2, 2), Linked(3, 60) };
        CHECK(LinkedDeathHeldIndex(3.0f, oneAbove, { 50.0f, 50.0f, 0.0f }, near) == -1);
        CHECK(LinkedDeathHeldIndex(3.0f, oneAbove, { 5.0f, 50.0f, 0.0f }, near) == 0);
    }

    // Flag off (the key, or the tactics strategy): today's pick stands. A non-linked boss is untouched too.
    {
        std::vector<LinkedMember> members = { Linked(1, 100), Linked(2, 80), Linked(3, 60) };
        CHECK(PickLinkedDeathTarget(false, true, thekalRows, 14599, members) == -1);
        CHECK(PickLinkedDeathTarget(true, false, thekalRows, 14599, members) == -1);
        CHECK(PickLinkedDeathTarget(false, false, thekalRows, 14599, members) == -1);
        const std::vector<BossTacticRow> others = { Row(309, 14507, BossTacticKind::KillFirst) };
        CHECK(PickLinkedDeathTarget(true, true, Active(others), 14507, members) == -1);
        CHECK(PickLinkedDeathTarget(true, true, thekalRows, 11361, members) == -1);  // not a group member
    }

    // The burn window, as a tick loop: a second of the raid's damage to the pick. The hold takes every member down
    // to param and no further; once all are there the release lets them die, and they die inside the script's 7 s
    // resurrect timer (boss_thekal.cpp's RESSURECT_TIMER).
    {
        std::vector<LinkedMember> members = { Linked(1, 100), Linked(2, 100), Linked(3, 100) };
        int deathTick[3] = {0, 0, 0};
        for (int tick = 1; tick <= 600; ++tick)
        {
            if (!members[0].attackable && !members[1].attackable && !members[2].attackable)
                break;
            int pick = PickLinkedDeathTarget(true, true, thekalRows, 14599, members);
            if (pick >= 0)
                CHECK(members[pick].hpPct > 3);  // the hold: never damage a member at or below param
            else
            {
                // Released: the ordinary pick, the lowest health among the living.
                pick = -1;
                for (int i = 0; i < 3; ++i)
                    if (members[i].attackable && (pick < 0 || members[i].hpPct < members[pick].hpPct))
                        pick = i;
                if (pick < 0)
                    break;
            }
            members[pick].hpPct -= 1;
            if (members[pick].hpPct <= 0)
            {
                members[pick].hpPct = 0;
                members[pick].attackable = false;
                deathTick[pick] = tick;
            }
        }
        CHECK(!members[0].attackable && !members[1].attackable && !members[2].attackable);
        CHECK(deathTick[0] > 0 && deathTick[1] > 0 && deathTick[2] > 0);
        const int first = std::min(deathTick[0], std::min(deathTick[1], deathTick[2]));
        const int last = std::max(deathTick[0], std::max(deathTick[1], deathTick[2]));
        CHECK(last - first <= 7);
    }


    // dps_hold_after: one fight-wide state per subject and row, whose triggers (engage, band opening, a spell or aura
    // rising edge, a blink) restart a hold of `param` seconds. The hold is what the free DPS bots of the instance obey;
    // any bot's read feeds the same state, as the linked_death latch does.
    {
        // Engage: the first sighting of the subject among the attackers starts the hold, which runs its seconds and no
        // longer (released at 0 ms, still held after param would both be wrong).
        DpsHoldState engage;
        DpsHoldInputs in;
        in.engaged = true;
        in.engageEnabled = true;
        in.holdSec = 4.0f;
        DpsHoldTrigger fired = DpsHoldTrigger::None;
        CHECK(DpsHoldTick(engage, in, 1000, &fired));
        CHECK(fired == DpsHoldTrigger::Engage);
        CHECK(engage.startMs == 1000);
        CHECK(DpsHoldTick(engage, in, 1000 + 3999, nullptr));
        CHECK(!DpsHoldTick(engage, in, 1000 + 4000, nullptr));  // param seconds: released at the boundary
        CHECK(!DpsHoldTick(engage, in, 1000 + 9000, nullptr));  // the engage edge fires once, never again
        CHECK(fired == DpsHoldTrigger::Engage);
    }

    // A re-pull re-arms the opening hold: the evade or wipe (boss out of combat) resets the edge, so the next engage
    // holds again. No new edge without a reset, and an in-combat read at 100 % is not a reset.
    {
        DpsHoldState repull;
        DpsHoldInputs in;
        in.engaged = true;
        in.engageEnabled = true;
        in.holdSec = 5.0f;
        CHECK(DpsHoldTick(repull, in, 1000, nullptr));
        CHECK(repull.engageSeen);
        CHECK(!DpsHoldTick(repull, in, 1000 + 6000, nullptr));
        DpsHoldResetOnBossReset(repull, true, 1000 + 6500);
        CHECK(repull.engageSeen);                               // the fight is on: still spent
        CHECK(!DpsHoldTick(repull, in, 1000 + 7000, nullptr));
        DpsHoldResetOnBossReset(repull, false, 1000 + 7500);    // evade or wipe: the boss left combat
        CHECK(!repull.engageSeen);
        CHECK(DpsHoldTick(repull, in, 20000, nullptr));         // the re-pull holds again
        DpsHoldResetOnBossReset(repull, false, 25000);
        CHECK(!repull.engageSeen);
        CHECK(DpsHoldTick(repull, in, 30000, nullptr));
    }
    // A quick wipe and repull: nobody reads the state while the boss is out of combat (the scan walks only live
    // attackers), so the next read already sees him in combat. The read gap is the repull signal: the pull that follows a
    // silence longer than kDpsHoldRepullGapMs re-arms the opening hold; a gap inside a fight does not.
    {
        DpsHoldState wipe;
        DpsHoldInputs in;
        in.engaged = true;
        in.engageEnabled = true;
        in.holdSec = 5.0f;
        CHECK(DpsHoldTick(wipe, in, 1000, nullptr));            // pull 1
        CHECK(!DpsHoldTick(wipe, in, 8000, nullptr));           // spent, hold over
        // wipe + evade: no read between 8000 and 30000, then the repull reads the boss already in combat
        DpsHoldResetOnBossReset(wipe, true, 30000);
        CHECK(!wipe.engageSeen);
        DpsHoldTrigger fired = DpsHoldTrigger::None;
        CHECK(DpsHoldTick(wipe, in, 30000, &fired));            // the repull holds again
        CHECK(fired == DpsHoldTrigger::Engage);
        // reads inside the fight a few seconds apart do not reset
        DpsHoldResetOnBossReset(wipe, true, 30000 + kDpsHoldRepullGapMs);
        CHECK(wipe.engageSeen);
        CHECK(!DpsHoldTick(wipe, in, 30000 + kDpsHoldRepullGapMs, nullptr));
        // one ms past the gap is a repull
        DpsHoldResetOnBossReset(wipe, true, 30000 + kDpsHoldRepullGapMs + kDpsHoldRepullGapMs + 1);
        CHECK(!wipe.engageSeen);
    }
    // An in-combat boss back at full health (Blood Siphon) is not a reset: a running hold stays running and the spent
    // engage edge does not fire again (the old '|| hp >= 100' reset restarted the 5 s deadline on every read).
    {
        DpsHoldState mid;
        DpsHoldInputs in;
        in.engaged = true;
        in.engageEnabled = true;
        in.holdSec = 5.0f;
        CHECK(DpsHoldTick(mid, in, 1000, nullptr));             // deadline 6000
        for (std::uint32_t t = 2000; t <= 5000; t += 1000)
            DpsHoldResetOnBossReset(mid, true, t);                 // the boss reads 100 % in combat: nothing resets
        CHECK(mid.engageSeen);
        CHECK(mid.untilMs == 6000);
        CHECK(DpsHoldTick(mid, in, 5999, nullptr));             // still the original hold
        CHECK(!DpsHoldTick(mid, in, 6000, nullptr));            // released on time, not restarted
        DpsHoldResetOnBossReset(mid, true, 6500);
        CHECK(!DpsHoldTick(mid, in, 7000, nullptr));            // the spent edge does not re-arm
    }
    // A band opening restarts the hold: closed -> open is the edge, and a band that stays open does not extend it.
    {
        DpsHoldState band;
        DpsHoldInputs in;
        in.gateEnabled = true;
        in.gateOpen = false;
        in.holdSec = 3.0f;
        DpsHoldTrigger fired = DpsHoldTrigger::None;
        CHECK(!DpsHoldTick(band, in, 0, &fired));
        CHECK(fired == DpsHoldTrigger::None);
        in.gateOpen = true;
        CHECK(DpsHoldTick(band, in, 100, &fired));
        CHECK(fired == DpsHoldTrigger::Band);
        CHECK(DpsHoldTick(band, in, 2000, nullptr));               // still inside the first band's hold
        in.gateOpen = false;                                       // the band closed: 100 + 3000 = 3100
        CHECK(!DpsHoldTick(band, in, 2100, nullptr));              // the row is not open now
        CHECK(!DpsHoldTick(band, in, 3100, nullptr));              // and its hold does not come back
        in.gateOpen = true;
        CHECK(DpsHoldTick(band, in, 4000, &fired));                // reopening restarts it
        CHECK(fired == DpsHoldTrigger::Band);
        CHECK(DpsHoldTick(band, in, 6999, nullptr));
        CHECK(!DpsHoldTick(band, in, 7000, nullptr));
    }

    // A spell or aura is a rising edge only: a value that stays true (an aura that lasts the whole hold) must not
    // restart the hold on every read, or the hold would never end.
    {
        DpsHoldState spell;
        DpsHoldInputs in;
        in.castEnabled = true;
        in.holdSec = 3.0f;
        DpsHoldTrigger fired = DpsHoldTrigger::None;
        CHECK(!DpsHoldTick(spell, in, 0, nullptr));                 // nothing seen yet
        in.castOrAuraSeen = true;
        CHECK(DpsHoldTick(spell, in, 100, &fired));
        CHECK(fired == DpsHoldTrigger::Spell);
        CHECK(DpsHoldTick(spell, in, 3099, nullptr));               // still held, the signal is still true
        CHECK(!DpsHoldTick(spell, in, 3100, nullptr));              // level, not edge: released at param
    }

    // A read by a bot outside the row (its bot_aura closed) touches no shared edge: the spell an ineligible bot reads
    // first must not hide the rising edge from an eligible bot, and a band it sees closed must not reset the band edge.
    {
        DpsHoldState spell;
        DpsHoldInputs in;
        in.castEnabled = true;
        in.holdSec = 3.0f;
        DpsHoldTrigger fired = DpsHoldTrigger::None;
        in.gateOpen = false;
        in.botEligible = false;
        in.castOrAuraSeen = true;                                   // the ineligible bot reads the active spell
        CHECK(!DpsHoldTick(spell, in, 0, &fired));
        CHECK(fired == DpsHoldTrigger::None);
        in.gateOpen = true;
        in.botEligible = true;
        CHECK(DpsHoldTick(spell, in, 100, &fired));                 // the eligible bot sees the rising edge
        CHECK(fired == DpsHoldTrigger::Spell);

        DpsHoldState band;
        DpsHoldInputs band_in;
        band_in.gateEnabled = true;
        band_in.holdSec = 3.0f;
        band_in.gateOpen = true;
        CHECK(DpsHoldTick(band, band_in, 0, &fired));
        CHECK(fired == DpsHoldTrigger::Band);
        band_in.gateOpen = false;
        band_in.botEligible = false;                                // an ineligible bot reads the band closed
        CHECK(!DpsHoldTick(band, band_in, 500, &fired));
        band_in.gateOpen = true;
        band_in.botEligible = true;                                 // still open for the eligible bot: no new edge
        CHECK(DpsHoldTick(band, band_in, 1000, &fired));            // the first hold still runs to 3000
        CHECK(fired == DpsHoldTrigger::None);
    }

    // Eligibility is lost through the end of cast 1 and regained during cast 2: the hold starts on cast 2. The
    // ineligible read resets the bot's own edge, so the stale "seen" of cast 1 cannot hide cast 2's rising edge.
    {
        DpsHoldState spell;
        DpsHoldInputs in;
        in.castEnabled = true;
        in.holdSec = 3.0f;
        in.observer = 7;
        DpsHoldTrigger fired = DpsHoldTrigger::None;
        in.castOrAuraSeen = true;                                   // cast 1, the bot carries the aura
        CHECK(DpsHoldTick(spell, in, 0, &fired));
        CHECK(fired == DpsHoldTrigger::Spell);
        in.botEligible = false;                                     // the aura is lost; cast 1 ends meanwhile
        in.castOrAuraSeen = false;
        CHECK(!DpsHoldTick(spell, in, 4000, &fired));
        in.castOrAuraSeen = true;                                   // cast 2 starts, still no aura
        CHECK(!DpsHoldTick(spell, in, 8000, &fired));
        in.botEligible = true;                                      // the aura returns during cast 2
        CHECK(DpsHoldTick(spell, in, 9000, &fired));
        CHECK(fired == DpsHoldTrigger::Spell);
        CHECK(spell.startMs == 9000);
    }

    // The cast/aura edge is per observer: one bot seeing a cast does not hide its rising edge from another bot that
    // starts watching before the cast (a bot already seen true is not a restart for a bot joining mid-cast).
    {
        DpsHoldState spell;
        DpsHoldInputs a;
        a.castEnabled = true;
        a.holdSec = 3.0f;
        a.observer = 1;
        DpsHoldInputs b = a;
        b.observer = 2;
        DpsHoldTrigger fired = DpsHoldTrigger::None;
        CHECK(!DpsHoldTick(spell, a, 0, &fired));                   // A watches, no cast
        CHECK(!DpsHoldTick(spell, b, 0, &fired));                   // B watches, no cast
        a.castOrAuraSeen = true;
        b.castOrAuraSeen = true;
        CHECK(DpsHoldTick(spell, a, 100, &fired));                  // A's rising edge starts the hold
        CHECK(fired == DpsHoldTrigger::Spell);
        CHECK(spell.startMs == 100);
        CHECK(DpsHoldTick(spell, b, 200, &fired));                  // B's own rising edge promotes into the hold
        CHECK(fired == DpsHoldTrigger::Spell);
        CHECK(spell.startMs == 200);
        DpsHoldInputs c = a;                                        // C joins mid-cast: A and B see it, no restart
        c.observer = 3;
        CHECK(DpsHoldTick(spell, c, 300, &fired));
        CHECK(fired == DpsHoldTrigger::None);
        CHECK(spell.startMs == 200);
        CHECK(spell.observers.size() == 3);
    }

    // The observer list is bounded.
    {
        DpsHoldState spell;
        DpsHoldInputs in;
        in.castEnabled = true;
        in.holdSec = 3.0f;
        for (std::uint64_t g = 1; g <= 200; ++g)
        {
            in.observer = g;
            DpsHoldTick(spell, in, static_cast<std::uint32_t>(g), nullptr);
        }
        CHECK(spell.observers.size() <= kDpsHoldMaxObservers);
    }

    // The held-subject veto: any held unit within range vetoes, not only the last unit read.
    {
        CHECK(DpsHoldVetoIndex({true, false}, {5.0f, 5.0f}, 10.0f) == 0);    // first held and in range
        CHECK(DpsHoldVetoIndex({true, false}, {20.0f, 5.0f}, 10.0f) == -1);  // held but out of range; other not held
        CHECK(DpsHoldVetoIndex({false, true}, {5.0f, 5.0f}, 10.0f) == 1);
        CHECK(DpsHoldVetoIndex({}, {}, 10.0f) == -1);
    }

    // A blink is a jump of more than 10 yd between two reads at most 1500 ms apart.
    {
        CHECK(!DpsHoldBlinked(0.0f, 0.0f, 9.9f, 0.0f, 500));        // under 10 yd: no blink
        CHECK(DpsHoldBlinked(0.0f, 0.0f, 10.1f, 0.0f, 500));
        CHECK(!DpsHoldBlinked(0.0f, 0.0f, 12.0f, 0.0f, 2000));      // 12 yd over 2 s: too slow to be a blink
        CHECK(DpsHoldBlinked(0.0f, 0.0f, 0.0f, 11.0f, 1500));       // the 1500 ms read gap is inclusive
        CHECK(!DpsHoldBlinked(0.0f, 0.0f, 0.0f, 11.0f, 1501));
        CHECK(!DpsHoldBlinked(5.0f, 5.0f, 5.0f, 5.0f, 10));         // standing still
    }

    // A blink mode restarts the hold on a jump, and the position is updated on every read.
    {
        DpsHoldState blink;
        DpsHoldInputs in;
        in.blinkEnabled = true;
        in.holdSec = 3.0f;
        in.x = 0.0f;
        in.y = 0.0f;
        DpsHoldTrigger fired = DpsHoldTrigger::None;
        CHECK(!DpsHoldTick(blink, in, 0, nullptr));                 // no previous position: no blink
        in.x = 12.0f;
        CHECK(DpsHoldTick(blink, in, 100, &fired));
        CHECK(fired == DpsHoldTrigger::Blink);
        in.x = 12.0f;                                               // the same place on the next read
        CHECK(!DpsHoldTick(blink, in, 3100, nullptr));              // and no second blink from standing there
    }

    // until_tank keeps the hold after param while the subject is on nobody but a tank's counterpart, and the cap at
    // 3 * param ends it, so it can never deadlock. A tank victim releases it at param like any other hold.
    {
        DpsHoldState tank;
        DpsHoldInputs in;
        in.engaged = true;
        in.engageEnabled = true;
        in.untilTank = true;
        in.victimIsTank = false;
        in.holdSec = 4.0f;
        CHECK(DpsHoldTick(tank, in, 0, nullptr));
        CHECK(DpsHoldTick(tank, in, 11999, nullptr));               // the cap is 3 * 4 s = 12 s
        CHECK(!DpsHoldTick(tank, in, 12000, nullptr));

        DpsHoldState done;
        in.victimIsTank = true;
        CHECK(DpsHoldTick(done, in, 0, nullptr));
        CHECK(!DpsHoldTick(done, in, 4000, nullptr));               // on the tank: released at param
    }

    // dispel_priority: a `never` aura excludes the whole dispel type, not only its own id. Delusions 24306 (curse) must
    // stay on the Shade killer, so a decurser may not touch any other curse on that member either: the core picks a random
    // aura of the type.
    {
        std::vector<BossTacticRow> rows = { DispelRow(24306, 0, TacticMode::Never) };
        ActiveBossRows active = Active(rows);
        std::vector<AuraOnMember> auras = { Aura(24306, kCurse), Aura(9001, kCurse) };
        DispelVerdict const never = DispelMemberVerdict(active, auras, kCurse, 50.0f, 30.0f);
        CHECK(never.excluded);
        CHECK(!never.wait);
        CHECK(never.auraId == 24306);
        CHECK(never.mode == TacticMode::Never);
    }

    // A never aura of one type never blocks another type on the same member: a magic dispel still lands beside it.
    {
        std::vector<BossTacticRow> rows = { DispelRow(9001, 0, TacticMode::Never) };
        ActiveBossRows active = Active(rows);
        std::vector<AuraOnMember> auras = { Aura(9001, kCurse), Aura(9002, kCurse), Aura(9003, kMagic) };
        DispelVerdict const magic = DispelMemberVerdict(active, auras, kMagic, 50.0f, 30.0f);
        CHECK(!magic.excluded);
        CHECK(magic.rank == kUnlistedDispelRank);
    }

    // A `clear` row waits until the carrier is clear of every other living group member: the donated Grobbulus release.
    // The hard never-dispel list (28169) is exactly the opt-in case: no row or an un-clear carrier keeps it excluded, a
    // `clear` row with the carrier clear releases it.
    {
        std::vector<BossTacticRow> rows = { DispelRow(28169, 0, TacticMode::Clear, 30.0f) };
        ActiveBossRows active = Active(rows);
        std::vector<AuraOnMember> auras = { Aura(28169, kPoison) };
        DispelVerdict const close = DispelMemberVerdict(active, auras, kPoison, 12.0f, 30.0f);
        CHECK(close.excluded);
        CHECK(close.wait);
        CHECK(close.mode == TacticMode::Clear);
        DispelVerdict const clear = DispelMemberVerdict(active, auras, kPoison, 30.0f, 30.0f);
        CHECK(!clear.excluded);
        CHECK(!clear.wait);
        CHECK(clear.rank == 0);
        // param 0 falls back to the config distance, not to "always clear".
        std::vector<BossTacticRow> fallback = { DispelRow(28169, 0, TacticMode::Clear) };
        ActiveBossRows fallbackRows = Active(fallback);
        CHECK(DispelMemberVerdict(fallbackRows, auras, kPoison, 29.9f, 30.0f).excluded);
        CHECK(!DispelMemberVerdict(fallbackRows, auras, kPoison, 30.0f, 30.0f).excluded);
    }

    // Without any active row a hard never-dispel aura stays excluded: 28169 is never cured, exactly as before the rows.
    {
        ActiveBossRows const empty;
        std::vector<AuraOnMember> auras = { Aura(28169, kPoison) };
        DispelVerdict const verdict = DispelMemberVerdict(empty, auras, kPoison, 100.0f, 30.0f);
        CHECK(verdict.excluded);
        CHECK(verdict.auraId == 28169);
        // An ordinary aura without a row is not excluded, it just ranks last.
        std::vector<AuraOnMember> plain = { Aura(9005, kPoison) };
        DispelVerdict const unlisted = DispelMemberVerdict(empty, plain, kPoison, 100.0f, 30.0f);
        CHECK(!unlisted.excluded);
        CHECK(unlisted.rank == kUnlistedDispelRank);
        CHECK(DispelNeedsAllyDistance(empty, plain) == false);
    }

    // Rank first, tier second: a rank-0 carrier that needs a short walk is cured before a rank-2 carrier in cast range
    // that has already been claimed by another dispeller.
    {
        std::vector<BossTacticRow> rows = { DispelRow(9005, 2, TacticMode::Now), DispelRow(9006, 0, TacticMode::Now) };
        ActiveBossRows active = Active(rows);
        DispelVerdict const low = DispelMemberVerdict(active, { Aura(9006, kMagic) }, kMagic, 50.0f, 30.0f);
        DispelVerdict const high = DispelMemberVerdict(active, { Aura(9005, kMagic) }, kMagic, 5.0f, 30.0f);
        CHECK(low.rank == 0 && high.rank == 2);
        CHECK(DispelCandidateBetter(low.rank, 2, high.rank, 0));
        CHECK(!DispelCandidateBetter(high.rank, 0, low.rank, 2));
    }

    // On equal rank the tier decides, so two auras of the same priority still prefer the member already in reach.
    {
        CHECK(DispelCandidateBetter(0, 0, 0, 1));
        CHECK(!DispelCandidateBetter(0, 1, 0, 0));
        CHECK(DispelCandidateBetter(0, 2, 0, 3));
        CHECK(!DispelCandidateBetter(1, 0, 0, 3));
    }

    // A cure is judged by every type its spell removes, not only the one that triggered it: a poison-triggered Cleanse
    // (poison, disease, magic) on a member carrying a protected magic aura is held, while the same member under a
    // poison-only cure is not.
    {
        std::vector<BossTacticRow> rows = { DispelRow(9007, 0, TacticMode::Never) };
        ActiveBossRows active = Active(rows);
        std::vector<AuraOnMember> auras = { Aura(9007, kMagic) };
        std::vector<std::uint32_t> const cleanse = { kPoison, 3, kMagic };
        std::vector<std::uint32_t> const poisonOnly = { kPoison };
        DispelVerdict const held = DispelCureVerdict(active, auras, cleanse, 50.0f, 30.0f);
        CHECK(held.excluded);
        CHECK(held.auraId == 9007);
        CHECK(held.mode == TacticMode::Never);
        CHECK(!DispelCureVerdict(active, auras, poisonOnly, 50.0f, 30.0f).excluded);
        // No type excluded: the default verdict, so the cure runs.
        CHECK(!DispelCureVerdict(active, { Aura(9008, kPoison) }, cleanse, 50.0f, 30.0f).excluded);
    }

    // The cast-time case: Cleanse chosen for a magic Hex on a member that also carries Poisonous Blood 24321, whose
    // dispel_priority row is never. The core resolves one of Cleanse's dispel types at random, so the poison can be
    // picked and the aura stripped: the cure is held even though the triggering type (magic) is not protected.
    {
        std::vector<BossTacticRow> rows = { DispelRow(24321, 0, TacticMode::Never) };
        ActiveBossRows active = Active(rows);
        std::vector<AuraOnMember> auras = { Aura(24321, kPoison), Aura(24053, kMagic) };
        std::vector<std::uint32_t> const cleanse = { kMagic, kPoison, 3 };
        DispelVerdict const held = DispelCureVerdict(active, auras, cleanse, 20.0f, 30.0f);
        CHECK(held.excluded);
        CHECK(held.auraId == 24321);
        CHECK(held.mode == TacticMode::Never);
        // Without the poison aura the same Cleanse runs on the Hex.
        CHECK(!DispelCureVerdict(active, { Aura(24053, kMagic) }, cleanse, 20.0f, 30.0f).excluded);
    }

    // The last gate at cast time places its target: PartyMemberToDispel offers a non-summoned pet (a hunter pet has no
    // Player), so a pet is judged by its owner's group exactly like a member. A pet with a never aura is held, a pet
    // with an allowed aura is not, and a unit the gate cannot place is refused whatever its auras (fail closed).
    {
        std::vector<BossTacticRow> rows = { DispelRow(24306, 0, TacticMode::Never) };
        ActiveBossRows active = Active(rows);
        std::vector<std::uint32_t> const curse = { kCurse };
        // Hunter pet with a never aura: blocked.
        CHECK(DispelCastVerdict(DispelTargetKind::GroupPet, active, { Aura(24306, kCurse) }, curse, 50.0f, 30.0f).excluded);
        // Hunter pet with an allowed aura: not blocked.
        CHECK(!DispelCastVerdict(DispelTargetKind::GroupPet, active, { Aura(9001, kCurse) }, curse, 50.0f, 30.0f).excluded);
        // A unit that is neither the bot, a group member nor a group pet: blocked.
        CHECK(DispelCastVerdict(DispelTargetKind::Unknown, active, {}, curse, 50.0f, 30.0f).excluded);
        CHECK(DispelCastVerdict(DispelTargetKind::Unknown, active, { Aura(9001, kCurse) }, curse, 50.0f, 30.0f).excluded);
        // The same allowed aura on a group member stays allowed, so only the unknown target fails closed.
        CHECK(!DispelCastVerdict(DispelTargetKind::GroupMember, active, { Aura(9001, kCurse) }, curse, 50.0f, 30.0f).excluded);
    }

    // freeze_on_aura: nothing but the freeze itself runs, the heals included (healing threat reaches every enemy in
    // combat with the healer, and Mandokir's Gaze allows no threat at all).
    {
        // Failing case: a heal action runs while the aura is on.
        CHECK(!FreezeHoldsAction(true, false));
        CHECK(FreezeHoldsAction(true, true));
        CHECK(FreezeHoldsAction(false, false));
        CHECK(FreezeHoldsAction(false, true));
    }

    // The freeze action's per-tick still: a lost-control state (fear, confuse, knockback) is never touched, so it
    // never cancels the generator that owns the bot; otherwise the edge stops attack and cast, a generator on top is
    // dropped, and an unfinished spline is stopped even when the generator is already idle.
    {
        // Rising edge: stop the attack and the cast, drop the generator, stop the walk.
        FreezeStill edge = FreezeStillTick(true, false, true, true);
        CHECK(edge.attackStop);
        CHECK(edge.dropGenerator);
        CHECK(edge.stopSpline);
        // A later tick with a chase or follow generator on top: dropped, its spline stopped.
        FreezeStill chase = FreezeStillTick(false, false, false, false);
        CHECK(!chase.attackStop);
        CHECK(chase.dropGenerator);
        CHECK(chase.stopSpline);
        // The generator is gone yet the spline still walks (a chase's Finalize does not stop it): still stopped.
        FreezeStill walking = FreezeStillTick(false, false, true, false);
        CHECK(!walking.attackStop);
        CHECK(!walking.dropGenerator);
        CHECK(walking.stopSpline);
        // Nothing to do.
        FreezeStill still = FreezeStillTick(false, false, true, true);
        CHECK(!still.attackStop && !still.dropGenerator && !still.stopSpline);
        // Fearing, confused or knocked back: the generator that runs it stays, the spline is not cut mid-flight.
        FreezeStill lost = FreezeStillTick(true, true, false, false);
        CHECK(!lost.attackStop && !lost.dropGenerator && !lost.stopSpline);
    }

    // The fight scan's cache answers only inside an AI update: an explicit command runs between updates where the
    // tick does not move, so its second scan re-scans instead of reusing the update's (possibly dead) units.
    {
        std::uint32_t const tick = 7;
        CHECK(FightScanCacheHit(true, true, true, tick, tick, true)); // the update's own second scan
        // No update running (the run order's scan after the update returned): never a hit.
        CHECK(!FightScanCacheHit(false, true, true, tick, tick, true));
        // Another bot, another update, another table: never a hit either.
        CHECK(!FightScanCacheHit(true, false, true, tick, tick, true));
        CHECK(!FightScanCacheHit(true, true, true, tick, tick + 1, true));
        CHECK(!FightScanCacheHit(true, true, true, tick, tick, false));
    }

    // flee_on_aura, healer reach: an end point more than kFleeHealerReachYd from the only healer is not accepted.
    {
        DebuffAnchor healer;
        healer.x = 0.0f;
        healer.y = 0.0f;
        CHECK(FleePointHealable(0.0f, 0.0f, &healer, 1, kFleeHealerReachYd));
        CHECK(FleePointHealable(35.0f, 0.0f, &healer, 1, kFleeHealerReachYd));
        CHECK(FleePointHealable(0.0f, 36.0f, &healer, 1, kFleeHealerReachYd));
        // The failing case: 40 yd away is accepted (mutation: a 60 yd reach must fail here).
        CHECK(!FleePointHealable(40.0f, 0.0f, &healer, 1, kFleeHealerReachYd));
        CHECK(!FleePointHealable(0.0f, -40.0f, &healer, 1, kFleeHealerReachYd));
        // No living healer: nothing can pass the reach test, and the action falls back to today's escape.
        CHECK(!FleePointHealable(0.0f, 0.0f, nullptr, 0, kFleeHealerReachYd));
    }

    // flee_on_aura, the step: the old overload is the no-healer case, so its answer never changes; a healer within
    // reach of a later clear step moves the carrier one step further out to keep it reachable.
    {
        DebuffAnchor raid[4];
        raid[0].x = 5.0f;  raid[0].y = 0.0f;
        raid[1].x = -5.0f; raid[1].y = 0.0f;
        raid[2].x = 0.0f;  raid[2].y = 5.0f;
        raid[3].x = 0.0f;  raid[3].y = -5.0f;
        float cx, cy, nearest;
        std::size_t nearestIdx;
        CHECK(BombRunoutCentre(8.0f, 0.0f, raid, 4, cx, cy, nearest, nearestIdx));
        const float oldStep = BombRunoutStep(8.0f, 0.0f, 0.0f, nearest, 30.0f, raid, 4);
        CHECK(Near(oldStep, 29.0f));
        CHECK(Near(BombRunoutStep(8.0f, 0.0f, 0.0f, nearest, 30.0f, raid, 4, nullptr, 0), oldStep));

        // A healer behind the raid is out of reach of every step: the old answer stands (an escape still escapes).
        DebuffAnchor behind;
        behind.x = -40.0f;
        behind.y = 0.0f;
        CHECK(Near(BombRunoutStep(8.0f, 0.0f, 0.0f, nearest, 30.0f, raid, 4, &behind, 1), oldStep));

        // A healer at (75, 0): the old first clear step lands 38 yd away and fails the reach test, the next step
        // lands 36 yd away. The healer-aware search takes that one.
        DebuffAnchor far;
        far.x = 75.0f;
        far.y = 0.0f;
        const float healStep = BombRunoutStep(8.0f, 0.0f, 0.0f, nearest, 30.0f, raid, 4, &far, 1);
        CHECK(healStep > oldStep);
        CHECK(Near(healStep, 31.0f));
        CHECK(FleePointHealable(8.0f + healStep, 0.0f, &far, 1, kFleeHealerReachYd));
        CHECK(!FleePointHealable(8.0f + oldStep, 0.0f, &far, 1, kFleeHealerReachYd));
        CHECK(DebuffEscapeClearance(8.0f + healStep, 0.0f, raid, 4) >= 30.0f + kBombRunoutMarginYd);
    }

    // flee_on_aura, the angle order: angle 0 (east) is clear from 29 yd but never within reach of the healer at (30, 40);
    // angle 0.5 is clear and healable. Pass 1 must pick the later healable angle (index 1), not the earlier clear one.
    {
        DebuffAnchor raid[4];
        raid[0].x = 5.0f;  raid[0].y = 0.0f;
        raid[1].x = -5.0f; raid[1].y = 0.0f;
        raid[2].x = 0.0f;  raid[2].y = 5.0f;
        raid[3].x = 0.0f;  raid[3].y = -5.0f;
        DebuffAnchor healer;
        healer.x = 30.0f;
        healer.y = 40.0f;
        float cx, cy, nearest;
        std::size_t nearestIdx;
        CHECK(BombRunoutCentre(8.0f, 0.0f, raid, 4, cx, cy, nearest, nearestIdx));
        const float offsets[] = { 0.0f, 0.5f, -0.5f, 1.0f, -1.0f };
        const std::size_t nOffsets = sizeof(offsets) / sizeof(offsets[0]);

        const BombRunoutSweep east = SweepBombRunout(8.0f, 0.0f, 0.0f, nearest, 30.0f, raid, 4, &healer, 1);
        CHECK(east.clear >= 0.0f);
        CHECK(east.healable < 0.0f);
        const BombRunoutSweep northEast = SweepBombRunout(8.0f, 0.0f, 0.5f, nearest, 30.0f, raid, 4, &healer, 1);
        CHECK(northEast.healable >= 0.0f);
        CHECK(FleePointHealable(8.0f + std::cos(0.5f) * northEast.healable, std::sin(0.5f) * northEast.healable,
            &healer, 1, kFleeHealerReachYd));

        // Everything accepted: the healable angle wins at once; the clear angle 0 is never offered.
        std::size_t offered = 0;
        std::size_t lastIdx = 99;
        float lastStep = -1.0f;
        int picked = PickRunoutAngle(8.0f, 0.0f, offsets, nOffsets, 0.0f, nearest, 30.0f, raid, 4, &healer, 1,
            [&](std::size_t i, float step) { ++offered; lastIdx = i; lastStep = step; return true; });
        CHECK(picked == 1);
        CHECK(offered == 1);
        CHECK(lastIdx == 1);
        CHECK(Near(lastStep, northEast.healable));

        // The healable end point is refused (not pathable): pass 2 takes angle 0's clear, unhealable step, the old answer.
        std::size_t idx0Step = 0;
        float angle0Step = -1.0f;
        picked = PickRunoutAngle(8.0f, 0.0f, offsets, nOffsets, 0.0f, nearest, 30.0f, raid, 4, &healer, 1,
            [&](std::size_t i, float step) {
                if (i == 1) return false;
                if (i == 0) { ++idx0Step; angle0Step = step; return true; }
                return false;
            });
        CHECK(picked == 0);
        CHECK(idx0Step == 1);
        CHECK(Near(angle0Step, east.clear));

        // No healer: the old first-accepted order, so angle 0 is taken as before.
        picked = PickRunoutAngle(8.0f, 0.0f, offsets, nOffsets, 0.0f, nearest, 30.0f, raid, 4, nullptr, 0,
            [&](std::size_t, float) { return true; });
        CHECK(picked == 0);
    }

    // The group's final death writes one window diagnostic: first_ms 0 (the episode start), last_ms the span from
    // the first death to the last, window_ms the group's window, inside whether the group died within it.
    {
        const float p = 3.0f;
        const std::vector<std::uint64_t> none;
        const std::vector<LinkedMember> three = { Linked(1, 2), Linked(2, 2), Linked(3, 1) };
        const std::vector<LinkedMember> two = { Linked(1, 2), Linked(2, 2) };

        LinkedDeathLatch inside;
        LinkedDeathWindowEvent event;
        CHECK(!LinkedDeathLatchTick(inside, p, three, none, 500, {}, 7000, &event));
        CHECK(!event.fired);
        CHECK(LinkedDeathLatchTick(inside, p, two, { 3 }, 1000, {}, 7000, &event));  // first death
        CHECK(!event.fired);
        LinkedDeathLatchTick(inside, p, { Linked(1, 2) }, { 2 }, 4000, {}, 7000, &event);
        CHECK(!event.fired);                                  // the group is not all dead yet
        CHECK(!LinkedDeathLatchTick(inside, p, {}, { 1 }, 5000, {}, 7000, &event));
        CHECK(event.fired);                                   // the final death
        CHECK(event.firstMs == 0);
        CHECK(event.lastMs == 4000);                          // 1000 -> 5000
        CHECK(event.windowMs == 7000);
        CHECK(event.inside);

        // A final death past the window reports outside; a later empty read does not repeat the event.
        LinkedDeathLatch outside;
        CHECK(!LinkedDeathLatchTick(outside, p, three, none, 500, {}, 7000, &event));
        CHECK(LinkedDeathLatchTick(outside, p, two, { 3 }, 1000, {}, 7000, &event));
        LinkedDeathLatchTick(outside, p, { Linked(1, 2) }, { 2 }, 4000, {}, 7000, &event);
        CHECK(!event.fired);                                  // one member still lives
        CHECK(!LinkedDeathLatchTick(outside, p, {}, { 1 }, 10000, {}, 7000, &event));
        CHECK(event.fired);
        CHECK(event.lastMs == 9000);                          // 1000 -> 10000, past the 7 s window
        CHECK(!event.inside);
        CHECK(!LinkedDeathLatchTick(outside, p, {}, none, 11000, {}, 7000, &event));
        CHECK(!event.fired);
    }

    // The dps_hold_after telemetry rows follow their holds. A row of a hold that was dropped (a dead subject, a stale row)
    // or whose map instance is gone is erased, whichever bot logged it; the rows of a live hold in a live instance stay,
    // one per bot. Before the fix nothing pruned the rows, so every old instance and subject kept its rows for good.
    {
        auto key = [](std::uint32_t mapId, std::uint32_t instanceId, std::uint64_t subject) {
            return DpsHoldKey(mapId, instanceId, subject, 11583u, 15263u, std::uint8_t(0), std::uint8_t(100));
        };
        auto instanceAlive = [](std::uint32_t mapId, std::uint32_t instanceId) {
            return !(mapId == 309u && instanceId == 3u);  // instance 3 has been torn down
        };
        DpsHoldKey const live = key(309u, 7u, 100u);
        DpsHoldKey const oldInstance = key(309u, 3u, 100u);
        DpsHoldKey const deadSubject = key(309u, 7u, 200u);
        std::map<DpsHoldKey, DpsHoldState> holds;
        holds[live];
        holds[oldInstance];  // still in the hold map: only the instance is gone
        // deadSubject is not in `holds`: its hold was dropped when the subject died.
        std::map<std::pair<std::uint64_t, DpsHoldKey>, std::uint32_t> log;
        log[std::make_pair(std::uint64_t{1}, live)] = 5000;
        log[std::make_pair(std::uint64_t{2}, live)] = 5000;
        log[std::make_pair(std::uint64_t{1}, oldInstance)] = 4000;
        log[std::make_pair(std::uint64_t{1}, deadSubject)] = 3000;

        CHECK(PruneDpsHoldLog(log, holds, instanceAlive) == 2);
        CHECK(log.size() == 2);
        CHECK(log.count(std::make_pair(std::uint64_t{1}, live)) == 1);  // a live hold keeps each bot's row
        CHECK(log.count(std::make_pair(std::uint64_t{2}, live)) == 1);
        CHECK(log.count(std::make_pair(std::uint64_t{1}, oldInstance)) == 0);
        CHECK(log.count(std::make_pair(std::uint64_t{1}, deadSubject)) == 0);

        // The live hold goes stale: its rows follow, and a second prune finds nothing left to do.
        holds.erase(live);
        CHECK(PruneDpsHoldLog(log, holds, instanceAlive) == 2);
        CHECK(log.empty());
        CHECK(PruneDpsHoldLog(log, holds, instanceAlive) == 0);
    }

    // The auto-cc exclusions the fight scan does not build: a kill_first unit (its target_entry, or the boss itself
    // when 0) and a linked_death group's partner are never held; a freeze_on_aura row's aura is the one a bot can
    // carry that freezes it. Both indexes are keyed by map and empty before the seal.
    {
        BossTacticTable table;
        BossTacticRow spawn = Row(309, 14510, BossTacticKind::KillFirst, 15041, 0, 1);
        CHECK(table.Add(spawn));
        BossTacticRow illusion = Row(309, 15083, BossTacticKind::KillFirst, 15163, 0, 1);
        CHECK(table.Add(illusion));
        BossTacticRow partner = Row(309, 11492, BossTacticKind::LinkedDeath, 11348, 0, 1, 50);
        CHECK(table.Add(partner));
        BossTacticRow bossItself = Row(309, 14834, BossTacticKind::KillFirst, 0, 0, 0);
        CHECK(table.Add(bossItself));
        BossTacticRow gaze = Row(309, 11382, BossTacticKind::FreezeOnAura, 0, 24314);
        CHECK(table.Add(gaze));
        // A seek_aura source is dragged to the spot and burned there: a CC'd Son cannot be dragged (M4).
        BossTacticRow seekSource = Row(309, 14834, BossTacticKind::SeekAura, 11357, 24321, 30, 4.0f);
        seekSource.cycleSpell = 24324;
        seekSource.cycleSec = 90.0f;
        seekSource.spotX = -11802.0f;
        seekSource.spotY = -1683.0f;
        seekSource.spotZ = 52.9f;
        seekSource.windowSec = 80.0f;
        CHECK(table.Add(seekSource));
        // Before the seal a reader sees nothing.
        CHECK(table.NoCcEntries(309).empty() && table.FreezeAuras(309).empty());
        table.Seal();
        CHECK(table.NoCcEntries(309).count(15041) == 1);  // Mar'li's Spawn
        CHECK(table.NoCcEntries(309).count(15163) == 1);  // Nightmare Illusion
        CHECK(table.NoCcEntries(309).count(11348) == 1);  // the linked_death partner
        CHECK(table.NoCcEntries(309).count(14834) == 1);  // a targetless kill_first is its boss entry
        CHECK(table.NoCcEntries(309).count(11357) == 1);  // the seek_aura source (Son of Hakkar)
        CHECK(table.NoCcEntries(309).size() == 5);
        CHECK(table.SeekTargets(309).count(11357) == 1);  // the third off-tank's pickup rule reads it
        CHECK(table.SeekTargets(531).empty());
        CHECK(table.NoCcEntries(531).empty());            // another map's rows are not smeared in
        CHECK(table.FreezeAuras(309).size() == 1 && table.FreezeAuras(309)[0] == 24314);
        CHECK(table.FreezeAuras(531).empty());
    }

    // TauntAfterTankIndexWraps / TauntAfterDue (taunt_after_cast, mode until_tank): a row names the tank that takes the
    // subject back after its victim stops being a tank. The index is the first ALIVE tank at or after the row's
    // `priority`, wrapping to the front; the deadline runs from the rising edge and nothing is taken back while the
    // victim is the assigned tank.
    {
        const std::vector<bool> gap = { true, false, true };
        CHECK(TauntAfterTankIndex(gap, 1) == 2);          // no wrap: the dead first off-tank keeps the job
        const std::vector<bool> onlyLast = { false, false, true };
        CHECK(TauntAfterTankIndex(onlyLast, 2) == 2);
        const std::vector<bool> onlyFirst = { true, false, false };
        CHECK(TauntAfterTankIndex(onlyFirst, 1) == 0);    // wraps
        CHECK(TauntAfterTankIndex(onlyFirst, 0) == 0);
        CHECK(TauntAfterTankIndex(std::vector<bool>{}, 0) == -1);
        CHECK(TauntAfterTankIndex(std::vector<bool>{ false, false }, 1) == -1);
        CHECK(TauntAfterTankIndex(onlyFirst, -1) == -1);

        CHECK(TauntAfterDue(true, 0, 10000, false));      // the rising edge itself
        CHECK(TauntAfterDue(true, 1, 10000, false));
        CHECK(TauntAfterDue(true, 9999, 10000, false));
        CHECK(TauntAfterDue(true, 10000, 10000, false));  // the deadline instant is the window's last one
        CHECK(!TauntAfterDue(true, 10001, 10000, false)); // one ms later the window has run out
        CHECK(!TauntAfterDue(false, 1, 10000, false));    // no episode
        CHECK(!TauntAfterDue(true, 1, 10000, true));      // the assigned tank already has it

        // A creature-charmed tank yields its AI, so the until_tank order counts it as gone (M5).
        CHECK(TauntAfterTankUsable(false, false));
        CHECK(TauntAfterTankUsable(true, true));          // a player MC leaves the bot's AI in the core, not gone
        CHECK(!TauntAfterTankUsable(true, false));        // a creature's charm

        // The window's state machine: the rising edge opens it, a tank has it back as regained and the episode ends,
        // and at the deadline the run-out is reported once while the next window opens at once.
        {
            TauntEpisodeRead edge = TauntEpisodeTick(true, true, false, false, 0, 10000, false);
            CHECK(edge.starts && edge.open && !edge.missed && edge.step == TauntEpisodeStep::Due);
            TauntEpisodeRead due = TauntEpisodeTick(true, true, false, true, 5000, 10000, false);
            CHECK(!due.starts && due.open && !due.missed && due.step == TauntEpisodeStep::Due);
            // The assigned tank has it back: the episode ends, the next charm starts clean.
            TauntEpisodeRead assigned = TauntEpisodeTick(true, false, true, true, 5000, 10000, true);
            CHECK(!assigned.open && assigned.step == TauntEpisodeStep::Regained);
            // The deadline instant is still the old window's taunt; only the next millisecond reports the run-out
            // and opens the new window.
            TauntEpisodeRead last = TauntEpisodeTick(true, true, false, true, 10000, 10000, false);
            CHECK(!last.starts && last.open && !last.missed && last.step == TauntEpisodeStep::Due);
            TauntEpisodeRead miss = TauntEpisodeTick(true, true, false, true, 10001, 10000, false);
            CHECK(miss.starts && miss.open && miss.missed && miss.step == TauntEpisodeStep::Due);
            TauntEpisodeRead regained = TauntEpisodeTick(true, false, true, true, 4000, 10000, false);
            CHECK(regained.step == TauntEpisodeStep::Regained && !regained.open);
            TauntEpisodeRead between = TauntEpisodeTick(true, false, false, true, 4000, 10000, false);
            CHECK(between.open && between.step == TauntEpisodeStep::None); // a pet or a transition: the clock runs
        }

        // The window's one boundary: at the deadline instant the holder is unchanged and its window still taunts; one
        // millisecond later the next window opens on the next tank. The writer, the taunt and its run-out row all read
        // this same instant.
        {
            std::vector<std::uint64_t> const guids = {11, 22, 33};
            std::vector<bool> const usable = {true, true, true};
            std::vector<bool> const available = {true, true, true};
            std::vector<bool> const canTaunt = {true, true, true};
            std::vector<bool> const canHold = {true, true, true};
            bool open = true;
            bool taunted = true; // the holder cast its taunt and still lost the subject
            std::uint32_t startMs = 0;
            int holderSlot = 1;
            auto const read = [&](std::uint32_t now) {
                std::uint32_t const sinceMs = now - startMs;
                bool const opening = !open || TauntDeadlinePassed(sinceMs, 10000);
                int const wanted = (opening && open && taunted) ? holderSlot + 1 : 0;
                bool const holderAvailable = holderSlot >= 0 && usable[holderSlot] && available[holderSlot];
                int const assigned = TauntWindowAssign(opening, holderAvailable, holderSlot, wanted, usable, canTaunt,
                    canHold);
                TauntEpisodeRead const out = TauntEpisodeTick(true, true, false, open, sinceMs, 10000, false);
                if (out.starts)
                {
                    startMs = now;
                    taunted = false;
                }
                open = out.open;
                holderSlot = assigned;
                return out;
            };
            TauntEpisodeRead const deadline = read(10000);
            CHECK(holderSlot == 1); // still the old window's holder
            CHECK(!deadline.missed && deadline.step == TauntEpisodeStep::Due);
            TauntEpisodeRead const after = read(10001);
            CHECK(holderSlot == 2); // the next window hands it to the next tank
            CHECK(after.starts && after.missed);
        }

        // A holder that loses control mid-window (a stun, a fear, a hex, a silence, a polymorph) hands the window
        // over at once instead of keeping it and wasting its deadline; a holder whose taunt is merely on cooldown
        // after its cast keeps it, since its availability is not its readiness (TauntAfterTankAvailable).
        {
            std::vector<bool> const usable = {true, true, true};
            std::vector<bool> const canTaunt = {false, true, true};
            std::vector<bool> const canHold = {false, true, true};
            CHECK(TauntWindowAssign(false, false, 0, 1, usable, canTaunt, canHold) == 1); // the holder lost control
            CHECK(TauntWindowAssign(false, true, 0, 1, usable, canTaunt, canHold) == 0);  // it holds: the window is its
            CHECK(TauntWindowAssign(false, false, -1, 1, usable, canTaunt, canHold) == 1); // its slot is gone
            CHECK(TauntWindowAssign(true, true, 0, 2, usable, canTaunt, canHold) == 2); // a new window picks afresh
            // Availability ignores the taunt's cooldown; readiness (canHold) reads it only to pick a new holder.
            CHECK(TauntAfterTankAvailable(true, false, true));    // ready or cooling: the holder still holds
            CHECK(!TauntAfterTankAvailable(false, false, true));  // stunned, feared, confused, hexed or charmed
            CHECK(!TauntAfterTankAvailable(true, true, true));    // silenced or pacified
            CHECK(!TauntAfterTankAvailable(true, false, false));  // no taunt known
            CHECK(TauntAfterTankCanHold(true, false, true, true));
            CHECK(!TauntAfterTankCanHold(true, false, true, false)); // on cooldown: no pick
        }

        // The holder's own taunt cooldown never drops it: the scans right after its cast keep the window on it and
        // no rival becomes due, a hex hands the window over at once, and the next window opens with a fresh pick.
        {
            std::vector<std::uint64_t> const guids = {11, 22, 33};
            std::vector<bool> const usable = {true, true, true};
            std::vector<bool> inControl = {true, true, true};
            std::vector<bool> const silenced = {false, false, false};
            std::vector<bool> tauntReady = {true, true, true}; // the holder's taunt cools from its cast at 2 s
            bool open = false;
            std::uint32_t startMs = 0;
            std::uint32_t advancedMs = 0;
            bool taunted = false; // the holder cast its taunt at 2 s
            std::uint64_t holder = 0;
            auto const scan = [&](std::uint32_t now) {
                std::vector<bool> canTaunt(guids.size(), false);
                std::vector<bool> canHold(guids.size(), false);
                std::vector<bool> available(guids.size(), false);
                for (std::size_t i = 0; i < guids.size(); ++i)
                {
                    canTaunt[i] = TauntAfterTankCanTaunt(inControl[i], silenced[i], true, tauntReady[i], true,
                        false, true);
                    canHold[i] = TauntAfterTankCanHold(inControl[i], silenced[i], true, tauntReady[i]);
                    available[i] = TauntAfterTankAvailable(inControl[i], silenced[i], true);
                }
                unsigned acts = 0; // the tanks the glue would make act in this scan
                for (int reader = 0; reader < static_cast<int>(guids.size()); ++reader)
                {
                    int const holderSlot = TankSlotOf(guids, holder);
                    int const owner = TauntWindowOwner(usable, available, holderSlot, 0);
                    if (TauntWindowAdvance(owner, reader, now, advancedMs, 1000))
                    {
                        advancedMs = now;
                        std::uint32_t const sinceMs = open ? now - startMs : 0;
                        bool const opening = !open || TauntDeadlinePassed(sinceMs, 10000);
                        int const wanted = (opening && open && taunted && holderSlot >= 0) ? holderSlot + 1 : 0;
                        bool const holds = holderSlot >= 0 && usable[holderSlot] && available[holderSlot];
                        int const assigned = TauntWindowAssign(opening, holds, holderSlot, wanted, usable, canTaunt,
                            canHold);
                        TauntEpisodeRead const read = TauntEpisodeTick(true, true, false, open, sinceMs, 10000, false);
                        if (read.starts)
                        {
                            startMs = now;
                            taunted = false;
                        }
                        open = read.open;
                        holder = guids[assigned];
                    }
                    if (open && holder == guids[reader] && available[reader] && TauntAfterDue(true, now - startMs, 10000, false))
                        acts |= 1u << reader;
                }
                return acts;
            };
            std::uint64_t holderAfterCast = 0;
            std::uint64_t holderAfterHex = 0;
            std::uint64_t holderNextWindow = 0;
            for (std::uint32_t now = 0; now <= 11000; now += 100)
            {
                if (now == 2000)
                {
                    taunted = true;          // its taunt landed: the window stays open on it
                    tauntReady[0] = false;   // and the taunt is on cooldown
                }
                if (now == 5000)
                    inControl[0] = false;    // hexed mid-window
                if (now == 5200)
                    inControl[0] = true;
                unsigned const acts = scan(now);
                CHECK(__builtin_popcount(acts) <= 1); // never two tanks due at once
                if (now == 5000)
                    CHECK(holder == guids[1] && acts == (1u << 1)); // the hex hands it over in the SAME scan
                if (now >= 2100 && now < 5000)
                {
                    CHECK(holder == guids[0]); // the cooling holder keeps the window
                    CHECK(acts == (1u << 0));
                    holderAfterCast = holder;
                }
                if (now >= 5100 && now < 10100)
                {
                    CHECK(holder == guids[1]); // the hex handed it over at once
                    CHECK(acts == (1u << 1));
                    holderAfterHex = holder;
                }
                if (now == 10100 || now == 11000)
                {
                    CHECK(holder == guids[2]); // the next window opened with a fresh pick
                    holderNextWindow = holder;
                }
            }
            CHECK(holderAfterCast == guids[0]);
            CHECK(holderAfterHex == guids[1]);
            CHECK(holderNextWindow == guids[2]);
        }

        // Jin'do: one window per tank-hold left the row dead after its first miss, and a wiped threat list never gave
        // the tank another. The window re-arms every `param` seconds while the subject stays on a non-tank: 40 s at a
        // 10 s deadline is four windows, each one due to a tank that can taunt it.
        {
            bool open = false;
            std::uint32_t startMs = 0;
            int windows = 0;
            int dueTicks = 0;
            for (std::uint32_t now = 0; now < 40000; now += 1000)
            {
                std::uint32_t const since = open ? now - startMs : 0;
                TauntEpisodeRead const read = TauntEpisodeTick(true, true, false, open, since, 10000, false);
                if (read.starts)
                {
                    ++windows;
                    startMs = now;
                }
                if (read.step == TauntEpisodeStep::Due)
                    ++dueTicks;
                open = read.open;
            }
            CHECK(windows == 4);
            CHECK(dueTicks == 40); // every second of the 40 sits in an open window
        }

        // The window's tank: one in melee of the subject, with it as its target and not its victim counts before a
        // tank that cannot taunt now; then one that can cast its taunt at all; the row's own tank is the pick only
        // when none can.
        CHECK(TauntAfterTankPick({true, true}, {false, true}, {false, true}, 0) == 1);
        CHECK(TauntAfterTankPick({true, true}, {true, false}, {true, true}, 1) == 0);   // wraps to the front
        CHECK(TauntAfterTankPick({true, true}, {false, false}, {false, false}, 1) == 1); // none can: the row's tank
        CHECK(TauntAfterTankPick({true, false}, {true, true}, {true, true}, 0) == 0);   // a dead or charmed tank: never
        CHECK(TauntAfterTankPick({}, {}, {}, 0) == -1);
        CHECK(TauntAfterTankPick({true}, {true}, {true}, -1) == -1);
        // The fallback is a tank that can cast its taunt now, not a hexed one: the main tank is hexed and the off-tank
        // is healthy but on another target, so the off-tank takes the window and walks in.
        {
            std::vector<bool> const usable = {true, true, true};
            std::vector<bool> const canTaunt = {false, false, false}; // nobody taunts that subject this instant
            std::vector<bool> const canHold = {false, true, false};   // the main tank is hexed, the off-tank is not
            CHECK(TauntAfterTankPick(usable, canTaunt, canHold, 0) == 1);
            CHECK(TauntAfterTankPick(usable, canTaunt, canHold, 1) == 1); // the row's own off-tank needs no walk
            std::vector<bool> const noneCanHold = {false, false, false};
            CHECK(TauntAfterTankPick(usable, canTaunt, noneCanHold, 2) == 2); // only then the row's tank as before
        }

        // A second Banish sends the main tank to the pit, so it is Jin'do's current victim: it must not be handed the
        // window's fallback while the off-tank can hold. As the only tank it stays the pick.
        {
            CHECK(!TauntAfterTankCanHold(true, false, true, true, true));  // its own victim
            CHECK(TauntAfterTankCanHold(true, false, true, true, false));
            CHECK(TauntAfterTankCanHold(true, false, true, true));         // default: not the victim
            std::vector<bool> const usable = {true, true};
            std::vector<bool> const canTaunt = {false, false};
            std::vector<bool> const canHold = {
                TauntAfterTankCanHold(true, false, true, true, true),   // slot 0: the victim
                TauntAfterTankCanHold(true, false, true, true, false)}; // slot 1: another tank, can hold
            CHECK(TauntAfterTankPick(usable, canTaunt, canHold, 0) == 1);
            std::vector<bool> const soleUsable = {true};
            std::vector<bool> const soleHold = {TauntAfterTankCanHold(true, false, true, true, true)};
            CHECK(TauntAfterTankPick(soleUsable, {false}, soleHold, 0) == 0); // the only tank stays pickable
        }

        // A tank that lost control (a stun, a fear, a confuse, a hex), is silenced, has no taunt, or whose taunt is on
        // cooldown cannot take the subject back now: it is skipped for a tank that can.
        CHECK(TauntAfterTankCanTaunt(true, false, true, true, true, false, true));
        CHECK(!TauntAfterTankCanTaunt(false, false, true, true, true, false, true)); // stunned, feared, confused or hexed
        CHECK(!TauntAfterTankCanTaunt(true, true, true, true, true, false, true));   // silenced or pacified
        CHECK(!TauntAfterTankCanTaunt(true, false, false, true, true, false, true)); // no taunt known
        CHECK(!TauntAfterTankCanTaunt(true, false, true, false, true, false, true)); // the taunt is on cooldown
        CHECK(!TauntAfterTankCanTaunt(true, false, true, true, false, false, true)); // not on the subject
        CHECK(!TauntAfterTankCanTaunt(true, false, true, true, true, true, true));   // its victim: no taunt needed
        CHECK(!TauntAfterTankCanTaunt(true, false, true, true, true, false, false)); // out of melee
        {
            // Jin'do: the hexed main tank and the silenced off-tank are skipped, the third takes the window; its taunt
            // is resisted, so the next window starts one past it (wrapping) instead of hammering it again.
            std::vector<bool> const usable = {true, true, true};
            std::vector<bool> const canTaunt = {
                TauntAfterTankCanTaunt(false, false, true, true, true, false, true),  // hexed
                TauntAfterTankCanTaunt(true, true, true, true, true, false, true),    // silenced
                TauntAfterTankCanTaunt(true, false, true, true, true, false, true)};  // ready
            std::vector<bool> const canHold = {
                TauntAfterTankCanHold(false, false, true, true),  // hexed: cannot cast
                TauntAfterTankCanHold(true, true, true, true),    // silenced: cannot cast
                TauntAfterTankCanHold(true, false, true, true)};  // ready: can
            CHECK(TauntAfterTankPick(usable, canTaunt, canHold, 0) == 2);
            CHECK(TauntAfterTankPick(usable, canTaunt, canHold, 3) == 2); // wraps past the last to the only one
            std::vector<bool> const allCan = {true, true, true};
            CHECK(TauntAfterTankPick(usable, allCan, allCan, 1) == 1); // a resisted window hands on to the next tank
        }

        // One tank advances a window's shared state: its holder while it can still take the taunt, else the order's
        // first usable tank; a holder nobody read for a second hands the advance to the reader. Three tanks reading
        // every tick write one row per window that ran out and one when a tank has the subject back.
        {
            std::vector<std::uint64_t> const guids = {11, 22, 33};
            std::vector<bool> const usable = {true, true, true};
            std::vector<bool> const available = {true, true, true};
            std::vector<bool> const canTaunt = {true, true, true};
            std::vector<bool> const canHold = {true, true, true};
            CHECK(TankSlotOf(guids, 22) == 1);
            CHECK(TankSlotOf(guids, 44) == -1); // left the order: the slot no longer names it
            CHECK(TankSlotOf(guids, 0) == -1);
            CHECK(TauntWindowOwner(usable, available, 1, 0) == 1);      // the holder keeps the advance
            CHECK(TauntWindowOwner(usable, available, -1, 2) == 2);     // none yet: the order's first usable tank
            CHECK(TauntWindowOwner({false, true, false}, {true, true, true}, 0, 1) == 1); // its holder died: the next
            CHECK(TauntWindowOwner({false, false, false}, {true, true, true}, 0, 0) == -1);
            // An incapacitated holder (stunned, hexed, silenced: not available) is no longer the writer: the next
            // usable AND available tank is, at once. With nobody available the first usable tank still is.
            CHECK(TauntWindowOwner(usable, {false, true, true}, 0, 0) == 1);
            CHECK(TauntWindowOwner(usable, {true, false, true}, 1, 0) == 0);
            CHECK(TauntWindowOwner(usable, {true, false, false}, 1, 1) == 0);  // wraps past the unavailable ones
            CHECK(TauntWindowOwner(usable, {false, false, false}, 1, 0) == 0);
            CHECK(TauntWindowOwner({true, true, false}, {false, false, true}, 0, 0) == 0); // available but unusable: no
            CHECK(TauntWindowAdvance(1, 1, 5000, 4000, 1000));          // the owner's own read
            CHECK(!TauntWindowAdvance(1, 2, 5000, 4500, 1000));         // a rival, the owner just read
            CHECK(TauntWindowAdvance(1, 2, 5000, 4000, 1000));          // the owner went quiet: anyone advances
            CHECK(TauntWindowAdvance(-1, 0, 5000, 4000, 1000));         // no usable owner: the reader advances

            bool open = false;
            std::uint32_t startMs = 0;
            std::uint32_t advancedMs = 0;
            std::uint64_t holder = 0;
            int missedRows = 0;
            int acts = 0;
            int regainedRows = 0;
            // 30 s on a non-tank at a 10 s deadline, every tank reading every 100 ms tick; the victim is a tank again
            // at 25 s.
            for (std::uint32_t now = 0; now < 30000; now += 100)
            {
                bool const victimIsTank = now >= 25000;
                int actsThisTick = 0;
                for (std::uint64_t const reader : guids)
                {
                    int const readerSlot = TankSlotOf(guids, reader);
                    int const holderSlot = TankSlotOf(guids, holder);
                    bool const holderAvailable = holderSlot >= 0 && usable[holderSlot] && available[holderSlot];
                    int const owner = TauntWindowOwner(usable, available, holderSlot, 0);
                    if (TauntWindowAdvance(owner, readerSlot, now, advancedMs, 1000))
                    {
                        advancedMs = now;
                        std::uint32_t const sinceMs = open ? now - startMs : 0;
                        bool const opening = !open || TauntDeadlinePassed(sinceMs, 10000);
                        int const wanted = (opening && open && holderSlot >= 0) ? holderSlot + 1 : 0;
                        int const assigned =
                            TauntWindowAssign(opening, holderAvailable, holderSlot, wanted, usable, canTaunt, canHold);
                        TauntEpisodeRead const read =
                            TauntEpisodeTick(true, !victimIsTank, victimIsTank, open, sinceMs, 10000, false);
                        if (read.missed)
                            ++missedRows;
                        if (read.starts)
                            startMs = now;
                        open = read.open;
                        holder = guids[assigned];
                        if (read.step == TauntEpisodeStep::Regained)
                        {
                            ++regainedRows;
                            open = false;
                            holder = 0;
                        }
                    }
                    if (open && holder == reader && !victimIsTank && TauntAfterDue(true, now - startMs, 10000, false))
                        ++actsThisTick;
                }
                CHECK(actsThisTick <= 1); // one holder acts, never two tanks at once
                acts += actsThisTick;
            }
            CHECK(missedRows == 2);  // 10 s and 20 s: one row for each window that ran out, not one per tank
            CHECK(regainedRows == 1); // 25 s: one row, from the single writer
            CHECK(acts == 250);      // every tick up to the regained 25 s has exactly one acting tank

        // TauntAfterImmuneSubjectIsIgnored: a subject the core makes immune to taunts (Hakkar: creature_template 14834
        // immunity_flags 6 keeps CREATURE_IMMUNITY_TAUNT) can never be taken back, so the row opens no episode there
        // and the tank spends no attack or GCD on it (walk 40: 108 TauntAfter rows, taunted=0).
        {
            TauntEpisodeRead immune = TauntEpisodeTick(false, true, false, false, 0, 10000, false);
            CHECK(!immune.starts && !immune.open && immune.step == TauntEpisodeStep::None);
            TauntEpisodeRead immuneOpen = TauntEpisodeTick(false, true, false, true, 1000, 10000, false);
            CHECK(!immuneOpen.starts && !immuneOpen.open && immuneOpen.step == TauntEpisodeStep::None);
            CHECK(TauntEpisodeTick(false, false, true, true, 1000, 10000, true).step ==
                  TauntEpisodeStep::None);
        }

        // The assigned tank is itself the subject's victim yet counts as a non-tank (banished, teleported or charmed
        // out of the tank role, and the pick fell back to it as the only tank left): it cannot taunt itself, but its
        // window must not restart on every read. It opens once, stays open while its deadline runs, and the run-out
        // is logged once as a miss when the deadline passes, as for any other window.
        {
            TauntEpisodeRead first = TauntEpisodeTick(true, true, false, false, 0, 10000, true);
            CHECK(first.starts && first.open && !first.missed);            // opens once
            for (std::uint32_t since = 0; since <= 10000; since += 1000)  // then every read keeps it
            {
                TauntEpisodeRead keep = TauntEpisodeTick(true, true, false, true, since, 10000, true);
                CHECK(!keep.starts && keep.open && !keep.missed && keep.step == TauntEpisodeStep::None);
            }
            TauntEpisodeRead ran = TauntEpisodeTick(true, true, false, true, 10001, 10000, true);
            CHECK(ran.starts && ran.open && ran.missed);                   // the run-out is a miss row, once
        }
        }

        // The widest live spread row wins, never narrower than the fallback.
        CHECK(WidestSpreadYd({10.0f}, 10.0f) == 10.0f);
        CHECK(WidestSpreadYd({10.0f, 12.0f, 11.0f}, 10.0f) == 12.0f);
        CHECK(WidestSpreadYd({6.0f}, 10.0f) == 10.0f);
        CHECK(WidestSpreadYd({}, 10.0f) == 10.0f);
    }

    // TestTauntAfterCastFiresOnTankBanish: Jin'do's Banish 24466 teleports a random player, the tank included, onto
    // the pit's trolls 47 yd west of his stand. The tank stays his victim, so the spell-less until_tank row never
    // fires (a victim that is still a tank is "not lost"); a row keyed to 24466 reads the tank's teleport instead:
    // its landing at the spell's destination, or a jump of its own position between two reads, latched until another
    // tank has the subject, the tank is back within kTauntBackYd, or kTauntLatchMaxMs pass (review of t1105c: Jin'do
    // follows the tank to the rim, about 21 yd from the landing, so a plain distance test closed the window in ~2 s).
    {
        auto read = [](TauntTeleportLatch& latch, std::uint64_t guid, bool tankRole, bool keyed, float yd, float jump,
                       bool atDestination, std::uint32_t nowMs)
        {
            TauntAfterVictimIn in;
            in.victimIsPlayer = true;
            in.victimIsTankRole = tankRole;
            in.spellKeyed = keyed;
            in.victimGuid = guid;
            in.ydFromSubject = yd;
            in.jumpYd = jump;
            in.atSpellDestination = atDestination;
            in.nowMs = nowMs;
            return TauntAfterVictim(in, latch);
        };
        // The spell-less row (spell 0) on a tank 47 yd away: the bug, no episode (today's rule).
        TauntTeleportLatch lessLatch;
        TauntAfterVictimRead const stillTank = read(lessLatch, 1, true, false, 47.0f, 47.0f, true, 1000);
        CHECK(!stillTank.onNonTank && stillTank.victimIsTank);
        // The keyed row on a tank that merely stands far from the subject (kiting, a knockback, pathing: no jump, not
        // at the destination) opens nothing: Codex MEDIUM, the configured spell must be what is seen.
        TauntTeleportLatch far;
        TauntAfterVictimRead const kiting = read(far, 1, true, true, 47.0f, 3.0f, false, 1000);
        CHECK(!kiting.onNonTank && kiting.victimIsTank && !far.open);
        // A tank landing at the destination: the episode opens, and the latch holds it while Jin'do follows the tank
        // to the rim (21.5 yd from the landing), the Opus MEDIUM.
        TauntTeleportLatch latch;
        TauntAfterVictimRead const banished = read(latch, 1, true, true, 47.0f, 47.0f, true, 1000);
        CHECK(banished.onNonTank && !banished.victimIsTank && latch.open);
        TauntEpisodeRead opened = TauntEpisodeTick(true, banished.onNonTank, banished.victimIsTank, false, 0,
            10000, false);
        CHECK(opened.starts && opened.open && opened.step == TauntEpisodeStep::Due);
        TauntAfterVictimRead const rim = read(latch, 1, true, true, 21.5f, 0.5f, true, 3000);
        CHECK(rim.onNonTank && !rim.victimIsTank && latch.open);
        TauntEpisodeRead continuing = TauntEpisodeTick(true, rim.onNonTank, rim.victimIsTank, true, 2000, 10000,
            false);
        CHECK(!continuing.starts && continuing.open && continuing.step == TauntEpisodeStep::Due);
        // Standing still at the destination does not re-open anything once the latch let go (the edge is spent).
        TauntAfterVictimRead const stays = read(latch, 1, true, true, 15.0f, 0.0f, true, 5000);
        CHECK(stays.onNonTank && latch.open); // 15 yd is still outside kTauntBackYd
        // The off-tank's taunt lands: another tank has Jin'do, the latch lets go and the episode closes as regained.
        TauntAfterVictimRead const backOnTank = read(latch, 2, true, true, 3.0f, 0.0f, false, 6000);
        CHECK(!backOnTank.onNonTank && backOnTank.victimIsTank && !latch.open);
        TauntEpisodeRead regained = TauntEpisodeTick(true, backOnTank.onNonTank, backOnTank.victimIsTank, true,
            5000, 10000, false);
        CHECK(regained.step == TauntEpisodeStep::Regained && !regained.open);
        // The banished tank walks back within kTauntBackYd of the subject on its own: the window closes.
        TauntTeleportLatch walkBack;
        CHECK(read(walkBack, 1, true, true, 47.0f, 47.0f, true, 1000).onNonTank);
        TauntAfterVictimRead const home = read(walkBack, 1, true, true, kTauntBackYd, 0.0f, true, 4000);
        CHECK(!home.onNonTank && home.victimIsTank && !walkBack.open);
        // The latch never holds longer than kTauntLatchMaxMs: a tank still 30 yd out after it is a plain tank again,
        // and standing at the destination (no new edge) does not re-open it.
        TauntTeleportLatch aged;
        CHECK(read(aged, 1, true, true, 47.0f, 47.0f, true, 1000).onNonTank);
        TauntAfterVictimRead const late = read(aged, 1, true, true, 30.0f, 0.0f, true, 1000 + kTauntLatchMaxMs + 1);
        CHECK(!late.onNonTank && late.victimIsTank && !aged.open);
        CHECK(!read(aged, 1, true, true, 30.0f, 0.0f, true, 1000 + kTauntLatchMaxMs + 2000).onNonTank);
        // A jump of the tank's own position between two reads opens it as well (a teleport to a spot the table has no
        // destination for), but a jump that leaves the tank next to the subject does not.
        TauntTeleportLatch jumped;
        CHECK(read(jumped, 1, true, true, 40.0f, 38.0f, false, 1000).onNonTank);
        TauntTeleportLatch nearJump;
        CHECK(!read(nearJump, 1, true, true, 4.0f, 38.0f, false, 1000).onNonTank);
        // A walking tank (below the jump) at 25 yd opens nothing.
        TauntTeleportLatch walking;
        CHECK(!read(walking, 1, true, true, 25.0f, kTauntTeleportYd, false, 1000).onNonTank);
        // A non-tank player (a DPS banished, or Hex's threat wipe) opens every row, keyed or not; a pet victim is
        // neither (the episode keeps its clock, as the transition above).
        TauntTeleportLatch dps;
        CHECK(read(dps, 5, false, false, 47.0f, 0.0f, false, 1000).onNonTank);
        CHECK(read(dps, 5, false, true, 47.0f, 0.0f, false, 1000).onNonTank);
        TauntAfterVictimIn pet;
        pet.victimIsPlayer = false;
        pet.spellKeyed = true;
        TauntTeleportLatch petLatch;
        TauntAfterVictimRead const petRead = TauntAfterVictim(pet, petLatch);
        CHECK(!petRead.onNonTank && !petRead.victimIsTank);
    }

    // needs_aura (the Shade of Jin'do 14986, Delusions 24306): only a bot carrying the row's aura may target the entry,
    // wherever it stands. AuraGateRefuses is the one rule the attack pickers, the taunt picks and the run guard read.
    CHECK(ParseBossTacticKind("needs_aura") == BossTacticKind::NeedsAura); // ParseNeedsAuraKind
    CHECK(static_cast<int>(BossTacticKind::NeedsAura) == 19);
    CHECK(AuraGateRefuses(true, 24306, false));   // NeedsAuraRefusesBotWithoutAura
    CHECK(!AuraGateRefuses(true, 24306, true));   // NeedsAuraAllowsCarrier
    CHECK(!AuraGateRefuses(false, 24306, false)); // NeedsAuraKeyOffAllowsAll
    CHECK(!AuraGateRefuses(true, 0, false));      // NeedsAuraNoRowAllowsAll
    CHECK(!CombatGateApplies(true, 24306, true));  // TestCarrierSkipsCombatGate
    CHECK(CombatGateApplies(true, 24306, false));  // TestUncursedKeepsCombatGate
    CHECK(CombatGateApplies(false, 24306, true));  // TestKeyOffKeepsCombatGate
    CHECK(CombatGateApplies(true, 0, true));       // TestNoRowKeepsCombatGate
    {
        BossTacticRow shade = Row(309, 11380, BossTacticKind::NeedsAura, 14986, 24306);
        {
            BossTacticRow noSpell = shade;
            noSpell.spellId = 0;
            BossTacticTable table;
            CHECK(!table.Add(noSpell));                 // NeedsAuraRowRefusedWithoutSpell
        }
        {
            BossTacticRow noTarget = shade;
            noTarget.targetEntry = 0;
            BossTacticTable table;
            CHECK(!table.Add(noTarget));                // NeedsAuraRowRefusedWithoutTarget
        }
        {
            BossTacticTable table;
            CHECK(!table.Add(Gated(shade, 0, 100, 24306))); // NeedsAuraRowRefusedWithGate (a bot aura)
            CHECK(!table.Add(Gated(shade, 20, 100)));       // ... or a health band
            CHECK(!table.Add(Gated(shade, 0, 90)));
        }
        {
            BossTacticRow withMode = shade;
            withMode.mode = TacticMode::Now;
            BossTacticTable table;
            CHECK(!table.Add(withMode));                // NeedsAuraRowRefusedWithMode
        }
        // The lookup is by map and entry, never by the row's boss: a Shade is gated wherever it stands.
        {
            BossTacticTable table;
            CHECK(table.RequiredAura(309, 14986) == 0); // not sealed yet
            CHECK(table.Add(shade));
            CHECK(table.Add(Row(309, 11380, BossTacticKind::KillFirst, 15112, 0, 1)));
            table.Seal();
            CHECK(table.RequiredAura(309, 14986) == 24306);
            CHECK(table.RequiredAura(309, 15112) == 0);  // an ungated entry
            CHECK(table.RequiredAura(309, 11380) == 0);  // the boss itself
            CHECK(table.RequiredAura(531, 14986) == 0);  // another map
            CHECK(table.NoCcEntries(309).count(14986) == 0); // not a crowd-control exclusion
        }
    }

    // The kill_first grid search's eligibility (the Shade/ward focus): alive, not evading, and part of our fight -- a
    // victim of ours, one of ours holding it, or the owner or summoner naming a unit of the fight. Being in combat is
    // no ticket in by itself: a creature of the entry brawling with an NPC or a player outside the group stays out.
    CHECK(KillFirstGridEligible(true, false, false, false, true));  // KillFirstGridAddsOwnedWard
    CHECK(KillFirstGridEligible(true, false, true, false, false));  // KillFirstGridAddsOneOnAMember
    CHECK(KillFirstGridEligible(true, false, false, true, false));  // ... and one the group holds
    CHECK(!KillFirstGridEligible(true, false, false, false, false)); // KillFirstGridSkipsForeignFight
    CHECK(!KillFirstGridEligible(false, false, true, false, false)); // dead
    CHECK(!KillFirstGridEligible(true, true, true, true, true));    // KillFirstGridSkipsEvading

    // T2 burn_mana: the loader's shape. target_entry 0 (the boss itself), param the start % (exclusive 0 and 100),
    // priority the most burners (0-10), spell_id unused and the kind takes no mode word.
    CHECK(ParseBossTacticKind("burn_mana") == BossTacticKind::BurnMana); // ParseBurnManaKind
    CHECK(static_cast<int>(BossTacticKind::BurnMana) == 20);
    {
        BossTacticRow burn = Row(509, 15340, BossTacticKind::BurnMana, 0, 0, 4, 10.0f);
        {
            BossTacticTable table;
            CHECK(table.Add(burn));                       // BurnManaRowAccepted
        }
        {
            BossTacticRow noTarget = burn;
            noTarget.targetEntry = 15527;
            BossTacticTable table;
            CHECK(!table.Add(noTarget));                  // BurnManaRowRefusedWithTarget
        }
        {
            BossTacticRow zeroStart = burn;
            zeroStart.param = 0.0f;
            BossTacticTable table;
            CHECK(!table.Add(zeroStart));                 // BurnManaRowRefusedAtZeroStart
        }
        {
            BossTacticRow fullStart = burn;
            fullStart.param = 100.0f;
            BossTacticTable table;
            CHECK(!table.Add(fullStart));                 // BurnManaRowRefusedAtFullStart
        }
        {
            BossTacticRow tooMany = burn;
            tooMany.priority = 11;
            BossTacticTable table;
            CHECK(!table.Add(tooMany));                   // BurnManaRowRefusedAboveTenBurners
        }
    }

    // T3 ranged_only: target_entry 0 (the boss) or an entry, param the vertical gap in [3, 40] yd, no mode word.
    CHECK(ParseBossTacticKind("ranged_only") == BossTacticKind::RangedOnly); // ParseRangedOnlyKind
    CHECK(static_cast<int>(BossTacticKind::RangedOnly) == 21);
    {
        BossTacticRow hold = Row(509, 15369, BossTacticKind::RangedOnly, 0, 0, 0, 8.0f);
        {
            BossTacticTable table;
            CHECK(table.Add(hold));                       // RangedOnlyRowAccepted
        }
        {
            BossTacticRow entryTarget = hold;
            entryTarget.targetEntry = 15934;
            BossTacticTable table;
            CHECK(table.Add(entryTarget));                // RangedOnlyRowAcceptsAnEntry
        }
        {
            BossTacticRow tooLow = hold;
            tooLow.param = 2.0f;
            BossTacticTable table;
            CHECK(!table.Add(tooLow));                    // RangedOnlyRowRefusedBelowThreeYards
        }
        {
            BossTacticRow tooHigh = hold;
            tooHigh.param = 41.0f;
            BossTacticTable table;
            CHECK(!table.Add(tooHigh));                   // RangedOnlyRowRefusedAboveFortyYards
        }
        // The hold is looked up by the held entry: the row's boss for target_entry 0, or its own target for an entry
        // row. The entry index is what the target pickers read on the hot path.
        {
            BossTacticTable table;
            CHECK(Near(table.RangedOnlyGap(509, 15369), 0.0f)); // not sealed yet
            CHECK(table.Add(hold));
            CHECK(table.Add(Row(509, 15369, BossTacticKind::RangedOnly, 15934, 0, 0, 12.0f)));
            table.Seal();
            CHECK(Near(table.RangedOnlyGap(509, 15369), 8.0f));   // RangedOnlyGapFromTheBossRow
            CHECK(Near(table.RangedOnlyGap(509, 15934), 12.0f));  // RangedOnlyGapFromAnEntryRow
            CHECK(Near(table.RangedOnlyGap(531, 15369), 0.0f));
        }
    }

    // T4 threat_successor: the boss itself (target_entry 0), no gate, and no mode word but `strict` (below). The row turns the damage
    // dealers' threat cap into the assigned off-tanks' highest (ThreatCapPolicy.h) and is read straight from the table
    // by ThreatValue::GetTankThreat, so a gate could never be answered.
    CHECK(ParseBossTacticKind("threat_successor") == BossTacticKind::ThreatSuccessor); // ParseThreatSuccessorKind
    CHECK(static_cast<int>(BossTacticKind::ThreatSuccessor) == 22);
    {
        BossTacticRow cap = Row(309, 14834, BossTacticKind::ThreatSuccessor);
        {
            BossTacticTable table;
            CHECK(table.Add(cap));                        // ThreatSuccessorRowAccepted
        }
        {
            BossTacticRow entryTarget = cap;
            entryTarget.targetEntry = 11357;
            BossTacticTable table;
            CHECK(!table.Add(entryTarget));               // ThreatSuccessorRowRefusedWithTarget
        }
        {
            BossTacticRow withMode = cap;
            withMode.mode = TacticMode::UntilTank;
            BossTacticTable table;
            CHECK(!table.Add(withMode));                  // ThreatSuccessorRowRefusedWithMode
        }
        {
            BossTacticRow withBand = cap;
            withBand.hpMin = 50;
            BossTacticTable table;
            CHECK(!table.Add(withBand));                  // ThreatSuccessorRowRefusedWithGate
        }
        // The reference reads the row by the target's own map and entry, never by the row's boss.
        {
            BossTacticTable table;
            CHECK(!table.HasThreatSuccessor(309, 14834)); // not sealed yet
            CHECK(table.Add(cap));
            table.Seal();
            CHECK(table.HasThreatSuccessor(309, 14834));  // ThreatSuccessorByEntry
            CHECK(!table.HasThreatSuccessor(309, 11357));
            CHECK(!table.HasThreatSuccessor(509, 14834)); // another map's boss of the same entry
        }
        // threat_successor_strict_mode_word (t1106m): `strict` is the kind's one mode word, the row's opt-in to
        // AiPlayerbot.DungeonRunThreatSuccessorStrict. Accepted on threat_successor only; every other kind refuses it
        // (ParseTacticMode reads it as Invalid there, and Add skips an Invalid row). A plain row is not strict.
        CHECK(static_cast<int>(TacticMode::Strict) == 13);
        CHECK(ParseTacticMode(BossTacticKind::ThreatSuccessor, "strict") == TacticMode::Strict);
        CHECK(ParseTacticMode(BossTacticKind::ThreatSuccessor, "") == TacticMode::Default);
        CHECK(ParseTacticMode(BossTacticKind::ThreatSuccessor, "all") == TacticMode::Invalid);
        for (BossTacticKind other : {BossTacticKind::AvoidCast, BossTacticKind::DispelPriority, BossTacticKind::DpsHoldAfter,
                 BossTacticKind::TankIgnore, BossTacticKind::RangedOnly, BossTacticKind::KillFirst})
            CHECK(ParseTacticMode(other, "strict") == TacticMode::Invalid); // StrictRefusedOnOtherKinds
        {
            BossTacticRow strictRow = cap;
            strictRow.bossEntry = 11380;
            strictRow.mode = TacticMode::Strict;
            BossTacticTable table;
            CHECK(table.Add(strictRow));                  // ThreatSuccessorStrictRowAccepted
            CHECK(table.Add(cap));                        // Hakkar's plain row
            CHECK(!table.HasStrictThreatSuccessor(309, 11380)); // not sealed yet
            table.Seal();
            CHECK(table.HasThreatSuccessor(309, 11380));
            CHECK(table.HasStrictThreatSuccessor(309, 11380));  // StrictRowOptsIn
            CHECK(table.HasThreatSuccessor(309, 14834));
            CHECK(!table.HasStrictThreatSuccessor(309, 14834)); // PlainRowIsNotStrict (Hakkar)
            CHECK(!table.HasStrictThreatSuccessor(509, 11380)); // another map's boss of the same entry
        }
        {
            // A strict word on another kind makes the whole row unusable.
            BossTacticRow strictAvoid = Row(309, 11380, BossTacticKind::AvoidCast);
            strictAvoid.spellId = 24306;
            strictAvoid.param = 10.0f;
            {
                BossTacticTable table;
                CHECK(table.Add(strictAvoid));            // the row itself is usable
            }
            strictAvoid.mode = ParseTacticMode(BossTacticKind::AvoidCast, "strict");
            BossTacticTable table;
            CHECK(!table.Add(strictAvoid));               // StrictRowRefusedOnAvoidCast
        }
        // The reset window (DungeonRunThreatResetWindow): a threat_successor row with cycle_spell carries the boss's
        // reset timer - param/window_s the gap bounds in seconds, priority the stop percent. Refused outside
        // 5 <= param < window_s <= 60 and 50 <= priority <= 100; a row without cycle_spell is today's plain cap.
        {
            BossTacticRow timed = cap;
            timed.cycleSpell = 24327;
            timed.param = 19.0f;
            timed.windowSec = 26.0f;
            timed.priority = 95;
            BossTacticTable table;
            CHECK(table.ThreatResetWindowOf(309, 14834) == nullptr); // not sealed
            CHECK(table.Add(timed));                                 // ThreatResetRowAccepted
            table.Seal();
            const ThreatResetWindow* const win = table.ThreatResetWindowOf(309, 14834);
            CHECK(win != nullptr);                                   // ThreatResetWindowOf
            CHECK(win->spell == 24327u);
            CHECK(win->minGapMs == 19000u && win->maxGapMs == 26000u);
            CHECK(win->stopPct == 95);
            CHECK(table.ThreatResetWindowOf(509, 14834) == nullptr); // another map's boss
            CHECK(table.ThreatResetWindowOf(309, 11357) == nullptr);
        }
        {
            BossTacticRow timed = cap;
            timed.cycleSpell = 24327;
            timed.param = 19.0f;
            timed.windowSec = 26.0f;
            timed.priority = 95;
            BossTacticTable table;
            timed.param = 30.0f;                                     // param above window_s
            CHECK(!table.Add(timed));
            timed.param = 19.0f;
            timed.windowSec = 61.0f;
            CHECK(!table.Add(timed));
            timed.windowSec = 19.0f;                                 // param == window_s
            CHECK(!table.Add(timed));
            timed.windowSec = 26.0f;
            timed.priority = 49;
            CHECK(!table.Add(timed));
            timed.priority = 101;
            CHECK(!table.Add(timed));
        }
        {
            // cycle_spell 0 is today's Hakkar row: no window.
            BossTacticTable table;
            CHECK(table.Add(cap));
            table.Seal();
            CHECK(table.ThreatResetWindowOf(309, 14834) == nullptr);
        }
    }

    // t1105h J1: a Delusions 24306 carrier turns on its Shade of Jin'do 14986 even under an explicit attack order (the
    // run's focus and the 3 s ward refocus give every DPS one). The scan already dropped the gated row for a bot without
    // the aura, so an open aura-gated kill_first row means "this bot carries it".
    {
        std::vector<BossTacticRow> jindo;
        BossTacticRow shade = Row(309, 11380, BossTacticKind::KillFirst, 14986, 0, 0);
        shade.botAura = 24306;
        jindo.push_back(shade);
        jindo.push_back(Row(309, 11380, BossTacticKind::KillFirst, 14987, 0, 1));
        jindo.push_back(Row(309, 11380, BossTacticKind::KillFirst, 15112, 0, 2));
        std::vector<BossTacticRow> uncursed(jindo.begin() + 1, jindo.end()); // the scan without the gated row
        CHECK(HasCarrierKillFirstRow(Active(jindo)));
        CHECK(!HasCarrierKillFirstRow(Active(uncursed)));
        // TestDelusionsCarrierPicksShadeOverOrder: a DPS under an order is in the carrier mode only while it carries.
        // Arguments: battle DPS, an explicit order, the order is the run's automation, a carrier row open.
        CHECK(KillFirstPickModeFor(true, false, false, false) == KillFirstPickMode::Free);
        CHECK(KillFirstPickModeFor(true, false, false, true) == KillFirstPickMode::Free);
        // Automated focus + curse: the carrier switches to the Shade.
        CHECK(KillFirstPickModeFor(true, true, true, true) == KillFirstPickMode::Carrier);
        // TestUncursedBotIgnoresShadeUnderOrder: the order holds for an uncursed DPS, as before.
        CHECK(KillFirstPickModeFor(true, true, true, false) == KillFirstPickMode::None);
        // Player focus + curse (Codex HIGH, t1105h review): the player's target wins, no override.
        CHECK(KillFirstPickModeFor(true, true, false, true) == KillFirstPickMode::None);
        // Tanks and healers keep their jobs, cursed or not.
        CHECK(KillFirstPickModeFor(false, false, false, true) == KillFirstPickMode::None);
        CHECK(KillFirstPickModeFor(false, true, true, true) == KillFirstPickMode::None);
        // The order is the automation's only while the run's own mark names the explicit target.
        CHECK(OrderIsAutomated(0x500, 0x500));
        CHECK(!OrderIsAutomated(0x500, 0x501));   // a player's order to another unit since
        CHECK(!OrderIsAutomated(0x500, 0));       // a player's order (the mark is cleared by every command commit)
        CHECK(!OrderIsAutomated(0, 0));           // no order at all
        // Codex HIGH (f621c7d96): a run attack held for a bot out of control and fired later is still the run's. The run
        // flag is recorded when the order is held (not read when it fires, outside the run's issuing), and the commit's
        // cleared mark is set again from it.
        {
            std::uint64_t const target = 0x700;
            // The deferred run attack: the commit cleared the mark, the held record says the run issued it.
            std::uint64_t const deferredMark = RunOrderMark(true, true, target, 0);
            CHECK(deferredMark == target);
            CHECK(KillFirstPickModeFor(true, true, OrderIsAutomated(target, deferredMark), true) ==
                KillFirstPickMode::Carrier);  // + curse: the carrier switches to its Shade
            // A player's order (immediate or held): the mark stays cleared, the player's target wins.
            std::uint64_t const playerMark = RunOrderMark(false, true, target, 0);
            CHECK(playerMark == 0);
            CHECK(KillFirstPickModeFor(true, true, OrderIsAutomated(target, playerMark), true) ==
                KillFirstPickMode::None);
            // An order the bot did not take leaves the mark as it was.
            CHECK(RunOrderMark(true, false, target, 0x123) == 0x123);
        }
        // The carrier pick: the nearest live Shade within the hold reach (40 yd, melee and ranged alike, t1106b), never
        // the ward the order names.
        std::vector<TacticCandidate> c(4);
        c[0].entry = 14987; c[0].distance = 2.0f; c[0].pickable = true;  // the ordered ward, nearest of all
        c[1].entry = 14986; c[1].distance = 25.0f; c[1].pickable = true; c[1].victimHealer = true;
        c[2].entry = 14986; c[2].distance = 18.0f; c[2].pickable = true;
        c[3].entry = 14986; c[3].distance = 3.0f; c[3].pickable = false; // dead or not attackable
        CHECK(PickCarrierOverride(Active(jindo), c, kCarrierHoldReachYd) == 2); // the nearer living Shade
        c[2].distance = 35.0f;
        c[1].distance = 39.0f;
        CHECK(PickCarrierOverride(Active(jindo), c, kCarrierHoldReachYd) == 2); // 35 yd, melee or ranged: in reach
        c[2].distance = 41.0f;
        CHECK(PickCarrierOverride(Active(jindo), c, kCarrierHoldReachYd) == 1); // past it: the other at 39 yd
        CHECK(Near(kCarrierHoldReachYd, 40.0f));
        c[2].distance = 4.0f;
        CHECK(PickCarrierOverride(Active(jindo), c, kCarrierHoldReachYd) == 2);
        CHECK(PickCarrierOverride(Active(uncursed), c, kCarrierHoldReachYd) == -1); // curse over: no override
        // An ungated kill_first row never makes a carrier pick (the ward stays the order's).
        std::vector<TacticCandidate> wardOnly(1, c[0]);
        CHECK(PickCarrierOverride(Active(jindo), wardOnly, kCarrierHoldReachYd) == -1);
        // TestZeroThreatShadePickedOverTotem: a fresh Shade sits at 0 threat for everyone, so a carrier that has not
        // hit it yet has no combat-gate ticket (inCombat false) and skips the gate; anyone else keeps it.
        for (bool const cursed : {true, false})
        {
            std::vector<TacticCandidate> zeroThreat(3);
            zeroThreat[0].entry = 14986; zeroThreat[0].distance = 6.0f;
            zeroThreat[0].pickable = !AuraGateRefuses(true, 24306, cursed) &&
                (false /*inCombat*/ || !CombatGateApplies(true, 24306, cursed));
            zeroThreat[1].entry = 14987; zeroThreat[1].distance = 3.0f; zeroThreat[1].pickable = true;
            zeroThreat[2].entry = 15112; zeroThreat[2].distance = 2.0f; zeroThreat[2].pickable = true;
            if (cursed)
            {
                CHECK(PickKillFirst(Active(jindo), zeroThreat) == 0);
                CHECK(PickCarrierOverride(Active(jindo), zeroThreat, kCarrierHoldReachYd) == 0);
            }
            else
            {
                CHECK(PickKillFirst(Active(uncursed), zeroThreat) == 1);
                CHECK(PickCarrierOverride(Active(uncursed), zeroThreat, kCarrierHoldReachYd) == -1);
            }
        }
    }

    // t1106c item 1: a cursed healer or off-tank strikes its own Shade while the raid is safe (CarrierStrikeAllowed).
    {
        // The lowest-member line is the caller's live AiPlayerbot.LowHealth, passed with the read (50 = the default).
        CarrierStrikeInput safe;
        safe.role = CarrierStrikeRole::Healer;
        safe.cursed = true;
        safe.shadeInReach = true;
        safe.holdsBoss = false;
        safe.threatSuccessor = false;
        safe.lowestMemberPct = 72;
        safe.tankPct = 80;
        safe.otherHealers = 3;
        safe.manaPct = 60;
        safe.dispelNowPending = false;
        CHECK(CarrierStrikeAllowed(safe, 50) == CarrierStrikeVerdict::Strike); // TestCursedHealerStrikesWhenRaidSafe
        {
            CarrierStrikeInput in = safe;
            in.lowestMemberPct = 49;
            CHECK(CarrierStrikeAllowed(in, 50) == CarrierStrikeVerdict::GuardLowMember); // TestCursedHealerYieldsBelowLowHealth
        }
        {
            // A live band below the member's health lets the strike through: the guard is the config's, not a literal.
            CarrierStrikeInput in = safe;
            in.lowestMemberPct = 45;
            CHECK(CarrierStrikeAllowed(in, 50) == CarrierStrikeVerdict::GuardLowMember); // TestLowHealthBandRefuses
            CHECK(CarrierStrikeAllowed(in, 40) == CarrierStrikeVerdict::Strike);         // TestLowHealthBandIsTheConfig
        }
        {
            CarrierStrikeInput in = safe;
            in.tankPct = 59;
            CHECK(CarrierStrikeAllowed(in, 50) == CarrierStrikeVerdict::GuardTank); // TestCursedHealerYieldsForTank
        }
        {
            CarrierStrikeInput in = safe;
            in.otherHealers = 1;
            CHECK(CarrierStrikeAllowed(in, 50) == CarrierStrikeVerdict::GuardHealers); // TestLastHealersKeepHealing
        }
        {
            CarrierStrikeInput in = safe;
            in.dispelNowPending = true;
            CHECK(CarrierStrikeAllowed(in, 50) == CarrierStrikeVerdict::GuardDispel); // TestDispelNowPendingWins
        }
        {
            CarrierStrikeInput in = safe;
            in.manaPct = 29;
            CHECK(CarrierStrikeAllowed(in, 50) == CarrierStrikeVerdict::GuardMana); // TestHealerManaFloor
        }
        {
            CarrierStrikeInput in = safe;
            in.role = CarrierStrikeRole::MainTank;
            CHECK(CarrierStrikeAllowed(in, 50) == CarrierStrikeVerdict::MainTank); // TestMainTankNeverStrikes
        }
        {
            CarrierStrikeInput in = safe;
            in.role = CarrierStrikeRole::OffTank;
            in.holdsBoss = true;
            CHECK(CarrierStrikeAllowed(in, 50) == CarrierStrikeVerdict::HoldsBoss); // TestOffTankOnBossNeverStrikes
        }
        {
            // A fight whose boss asks for a threat successor keeps its off-tank on the boss even when the boss is not on
            // it: with no off-tank holding, the damage dealers' cap falls to half the main tank's threat and the next
            // threat wipe (Jin'do's Hex) hands the boss to a damage dealer.
            CarrierStrikeInput in = safe;
            in.role = CarrierStrikeRole::OffTank;
            in.holdsBoss = false;
            in.threatSuccessor = true;
            CHECK(CarrierStrikeAllowed(in, 50) == CarrierStrikeVerdict::ThreatSuccessor); // TestOffTankOnSuccessorFightNeverStrikes
        }
        {
            // The rule is the tanks': a healer on the same fight still strikes, and the row only stops the off-tank.
            CarrierStrikeInput in = safe;
            in.threatSuccessor = true;
            CHECK(CarrierStrikeAllowed(in, 50) == CarrierStrikeVerdict::Strike); // TestHealerStrikesOnSuccessorFight
        }
        {
            CarrierStrikeInput in = safe;
            in.role = CarrierStrikeRole::OffTank;
            in.holdsBoss = false;
            CHECK(CarrierStrikeAllowed(in, 50) == CarrierStrikeVerdict::Strike); // TestOffTankStrikesWhenFree
        }
        {
            CarrierStrikeInput in = safe;
            in.cursed = false;
            CHECK(CarrierStrikeAllowed(in, 50) == CarrierStrikeVerdict::NotCarrier); // TestUncursedHealerNoStrike
        }
        {
            CarrierStrikeInput in = safe;
            in.role = CarrierStrikeRole::Dps;
            CHECK(CarrierStrikeAllowed(in, 50) == CarrierStrikeVerdict::Dps); // TestDpsLeftToKillFirst
        }
        // The fight-level read behind the refusal: the fight's open rows carry the kind (HasThreatSuccessorRow).
        {
            std::vector<BossTacticRow> none;
            BossTacticRow successor = Row(309, 11380, BossTacticKind::ThreatSuccessor);
            BossTacticRow first = Row(309, 11380, BossTacticKind::KillFirst, 14986);
            CHECK(!HasThreatSuccessorRow(Active(none))); // TestNoSuccessorRow
            CHECK(HasThreatSuccessorRow(Active(std::vector<BossTacticRow>{successor}))); // TestSuccessorRowRead
            CHECK(!HasThreatSuccessorRow(Active(std::vector<BossTacticRow>{first}))); // TestOtherRowIsNoSuccessor
        }
        // t1106c round 2: the other-healer count keeps only healers that can heal the raid (CarrierHealerCount).
        {
            CarrierHealer healer;
            healer.alive = true;
            healer.cursed = false;
            healer.charmed = false;
            healer.distanceYd = 20.0f;
            healer.dropYd = 0.0f;
            std::vector<CarrierHealer> healers(3, healer);
            CHECK(CarrierHealerCount(healers, 40.0f) == 3); // TestOtherHealerCount
            std::vector<CarrierHealer> one = healers;
            one[0].alive = false;
            CHECK(CarrierHealerCount(one, 40.0f) == 2); // TestDeadHealerNotCounted
            one = healers;
            one[0].cursed = true;
            CHECK(CarrierHealerCount(one, 40.0f) == 2); // TestCursedHealerNotCounted
            one = healers;
            one[0].charmed = true;
            CHECK(CarrierHealerCount(one, 40.0f) == 2); // TestCharmedHealerNotCounted
            one = healers;
            one[0].distanceYd = 41.0f;
            CHECK(CarrierHealerCount(one, 40.0f) == 2); // TestHealerBeyondReachNotCounted
            one = healers;
            one[0].distanceYd = 40.0f;
            CHECK(CarrierHealerCount(one, 40.0f) == 3); // TestHealerAtReachCounts
            one = healers;
            one[0].dropYd = 12.4f; // Jin'do's pit floor sits ~12 yd under the boss's
            CHECK(CarrierHealerCount(one, 40.0f) == 2); // TestHealerInThePitNotCounted
            one = healers;
            one[0].dropYd = kCarrierPitDropYd;
            CHECK(CarrierHealerCount(one, 40.0f) == 3); // TestHealerAtThePitLineCounts
            // The carrier can be the one in the pit: a healer on the room floor then reads the same ~12 yd above it, and
            // "above" is not "on the carrier's floor" either (TestCarrierInThePitCountsOnlyItsOwnFloor).
            one = healers;
            one[0].dropYd = -12.4f;
            CHECK(CarrierHealerCount(one, 40.0f) == 2); // TestCarrierInThePitLeavesTheRoomFloorOut
            one = healers;
            one[0].dropYd = -kCarrierPitDropYd;
            CHECK(CarrierHealerCount(one, 40.0f) == 3); // TestCarrierInThePitKeepsItsOwnFloor
        }
        // The strike spells: the class's shortest casts first, nothing for a class without one (melee or wand).
        CHECK(CarrierStrikeSpells(5) == std::vector<const char*>({"mind blast", "smite"})); // TestCarrierStrikeSpellsPriest
        CHECK(CarrierStrikeSpells(1).empty()); // TestCarrierStrikeSpellsWarrior
    }

    // t1105h J2: tank_ignore. Jin'do 11380 hands the Sacrificed Trolls 14826 to the damage dealers.
    {
        BossTacticRow ignore = Row(309, 11380, BossTacticKind::TankIgnore, 14826);
        CHECK(ParseBossTacticKind("tank_ignore") == BossTacticKind::TankIgnore);
        CHECK(ParseBossTacticKind("seek_pull") == BossTacticKind::SeekPull);
        CHECK(static_cast<int>(BossTacticKind::SeekPull) == 25);
        {
            BossTacticTable table;
            CHECK(table.TankIgnoreBosses(309, 14826).empty()); // not sealed
            CHECK(table.Add(ignore));
            table.Seal();
            CHECK(table.TankIgnoreBosses(309, 14826) == std::vector<std::uint32_t>{11380});
            CHECK(table.TankIgnoreBosses(309, 11380).empty());
            CHECK(table.TankIgnoreBosses(509, 14826).empty());
        }
        // The row's shape: a target, nothing else; read straight from the table, so no gate and no mode word.
        BossTacticRow noTarget = ignore; noTarget.targetEntry = 0;
        BossTacticRow spell = ignore; spell.spellId = 24306;
        BossTacticRow gated = ignore; gated.hpMax = 50;
        BossTacticRow aura = ignore; aura.botAura = 24306;
        BossTacticRow mode = ignore; mode.mode = TacticMode::UntilTank;
        for (const BossTacticRow& bad : {noTarget, spell, gated, aura, mode})
        {
            BossTacticTable table;
            CHECK(!table.Add(bad));
        }
        // TestTankNoTauntOnTankIgnoreEntry: a tank leaves the troll alone while Jin'do fights, a DPS never does.
        auto const engaged = [](std::uint32_t boss) { return boss == 11380; };
        auto const idle = [](std::uint32_t) { return false; };
        std::vector<std::uint32_t> const bosses{11380};
        CHECK(TankIgnoresUnit(true, bosses, engaged));
        CHECK(!TankIgnoresUnit(false, bosses, engaged));          // DPS still kill the trolls
        CHECK(!TankIgnoresUnit(true, bosses, idle));              // no Jin'do fight: a normal mob
        CHECK(!TankIgnoresUnit(true, std::vector<std::uint32_t>{}, engaged)); // no row
    }

    std::cout << "All boss tactics policy tests passed.\n";
    return 0;
}
