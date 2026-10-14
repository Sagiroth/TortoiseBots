// Standalone check for ai/playerbot/BossTacticsPolicy.h:
//   g++ -std=c++17 -Wall -Wextra tools/test_boss_tactics_policy.cpp -o /tmp/test_boss_tactics_policy && /tmp/test_boss_tactics_policy
#include "../ai/playerbot/BossTacticsPolicy.h"

#include <cstdlib>
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

int main()
{
    std::cout << "Starting TortoiseBots boss tactics policy tests...\n";

    // Kind text: exactly the three lowercase spellings the table holds.
    CHECK(ParseBossTacticKind("interrupt") == BossTacticKind::Interrupt);
    CHECK(ParseBossTacticKind("kill_first") == BossTacticKind::KillFirst);
    CHECK(ParseBossTacticKind("behind") == BossTacticKind::Behind);
    CHECK(ParseBossTacticKind("Interrupt") == BossTacticKind::None);
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

    std::cout << "All boss tactics policy tests passed.\n";
    return 0;
}
