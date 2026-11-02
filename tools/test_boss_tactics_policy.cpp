// Standalone check for ai/playerbot/BossTacticsPolicy.h:
//   g++ -std=c++17 -Wall -Wextra tools/test_boss_tactics_policy.cpp -o /tmp/test_boss_tactics_policy && /tmp/test_boss_tactics_policy
#include "../ai/playerbot/BossTacticsPolicy.h"

#include <cstdlib>
#include <limits>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using namespace ai;

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

static ActiveBossRows Active(const std::vector<BossTacticRow>& rows)
{
    ActiveBossRows out;
    for (const BossTacticRow& row : rows)
        out.push_back(&row);
    return out;
}

static TacticCandidate Cand(std::uint32_t entry, float distance, bool pickable = true)
{
    TacticCandidate c;
    c.entry = entry;
    c.distance = distance;
    c.pickable = pickable;
    return c;
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

static LinkedMember Linked(std::uint64_t guid, int hpPct, bool attackable = true)
{
    LinkedMember m;
    m.guid = guid;
    m.hpPct = hpPct;
    m.attackable = attackable;
    return m;
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
                if (!DpsLeavesAlone(true, r, entry, count))
                    return entry;
            return 0;
        };
        const std::vector<std::uint32_t> both = { 15111, 15146 };
        CHECK(firstUnheld(active, both, countOf(15146, 1)) == 15146);
        CHECK(firstUnheld(active, { 15111 }, countOf(15146, 1)) == 0);
        CHECK(firstUnheld(ActiveBossRows(), both, countOf(15146, 1)) == 15111);
        CHECK(firstUnheld(active, both, countOf(15146, 0)) == 15111);       // no blocker: the Servant is free again
        CHECK(!DpsLeavesAlone(false, active, 15111, countOf(15146, 1)));    // a tank or healer is not a free DPS bot

        // The fallback still skips an unattackable or too-far candidate, like PickKillFirst.
        CHECK(PickNearestUnheld(active, { Cand(15146, 41.0f) }, countOf(15146, 0)) == -1);
        CHECK(PickNearestUnheld(active, { Cand(15146, 5.0f, false) }, countOf(15146, 0)) == -1);
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
    }

    // The group: the anchor and every partner its rows list, resolved from either.
    const std::vector<BossTacticRow> thekal = {
        Row(309, 14599, BossTacticKind::LinkedDeath, 11347, 0, 1, 3.0f),
        Row(309, 14599, BossTacticKind::LinkedDeath, 11348, 0, 1, 3.0f),
    };
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
        CHECK(FindLinkedDeathGroup(thekalRows, 11348, group));  // a partner resolves the same group
        CHECK(group.entries.size() == 3);
        CHECK(!FindLinkedDeathGroup(thekalRows, 11361, group));  // the tiger is no member
        CHECK(!FindLinkedDeathGroup(ActiveBossRows(), 14599, group));
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

    std::cout << "All boss tactics policy tests passed.\n";
    return 0;
}
