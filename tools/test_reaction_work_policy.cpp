// Standalone regression test for the reaction pass gate
// (runtime/ReactionWorkPolicy.h): a queued chat command forces the pass even
// for a masterless, idle bot.
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra tools/test_reaction_work_policy.cpp -o /tmp/test_reaction_work
//   /tmp/test_reaction_work

#include "../runtime/ReactionWorkPolicy.h"

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

int main()
{
    // A hirer is the master: every pass runs (the hire-time check answers at once).
    CHECK(ReactionPassForced(true, 0));
    CHECK(ReactionPassForced(true, 3));
    // A bound hire has no player master: an idle bot with an empty queue may skip.
    CHECK(!ReactionPassForced(false, 0));
    // ... but a waiting `talents` / `spells` order must be read (Ragefire and Uldaman, 2026-10-17).
    CHECK(ReactionPassForced(false, 1));
    CHECK(ReactionPassForced(false, 5));

    std::printf("test_reaction_work_policy: %d checks passed\n", checks);
    return 0;
}
