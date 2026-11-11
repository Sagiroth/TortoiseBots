// Standalone regression test for the reaction rows policy
// (ai/playerbot/ReactionRowsPolicy.h): AiPlayerbot.RaidReactions off must
// build no row at all (the reaction engine's trigger list is exactly what it
// was while `react` had no creator), and on must build the five universal raid
// rows with the priorities they carried.
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra tools/test_reaction_rows_policy.cpp -o /tmp/test_reaction_rows
//   /tmp/test_reaction_rows

#include "../ai/playerbot/ReactionRowsPolicy.h"

#include <cstdio>
#include <cstdlib>
#include <string>

using ai::kReactionRaidEmergency;
using ai::ReactionRaidRow;
using ai::ReactionRaidRows;

static int checks = 0;
#define CHECK(cond) do { \
    ++checks; \
    if (!(cond)) { \
        std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        std::exit(1); \
    } \
} while (0)

int main()
{
    // The priority base is ACTION_EMERGENCY (strategy/Strategy.h). The value
    // lives here because the policy header has no core includes; pinned so a
    // change of the engine's constant is noticed here too.
    CHECK(kReactionRaidEmergency == 90);

    // Key off (the shipped default): the strategy contributes nothing, so
    // every engine's trigger list is unchanged.
    CHECK(ReactionRaidRows(false).empty());

    // Key on: exactly the five universal raid rows, in registration order,
    // each with the priority it carries today.
    std::vector<ReactionRaidRow> const rows = ReactionRaidRows(true);
    CHECK(rows.size() == 5);

    struct Expected
    {
        const char* trigger;
        const char* action;
        int priority;
    };
    Expected const expected[] = {
        { "environmental hazard nearby", "move away from hazard", kReactionRaidEmergency + 5 },
        { "raid bomb debuff", "raid bomb runout", kReactionRaidEmergency + 6 },
        { "four horsemen mark", "four horsemen rotate", kReactionRaidEmergency + 6 },
        { "dragon breath risk", "dragon flank", kReactionRaidEmergency + 4 },
        { "raid spread needed", "raid spread", kReactionRaidEmergency + 3 },
    };

    for (std::size_t i = 0; i < rows.size(); ++i)
    {
        CHECK(std::string(rows[i].trigger) == expected[i].trigger);
        CHECK(std::string(rows[i].action) == expected[i].action);
        CHECK(rows[i].priority == expected[i].priority);
    }

    // The same read twice: the list is derived, not accumulated.
    CHECK(ReactionRaidRows(true).size() == 5);

    std::printf("test_reaction_rows_policy: %d checks passed\n", checks);
    return 0;
}
