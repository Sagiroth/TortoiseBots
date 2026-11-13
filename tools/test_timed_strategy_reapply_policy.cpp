#include "../runtime/policy/infra/TimedStrategyReapplyPolicy.h"

#include <cstdlib>
#include <iostream>
#include <cstdint>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using TortoiseBots::TimedReapplyInputs;
using TortoiseBots::TimedReapplyHoldsRole;

namespace
{
    bool Holds(uint8_t forcedRole, bool inGroup)
    {
        return TimedReapplyHoldsRole({forcedRole, inGroup});
    }
}

int main()
{
    std::cout << "Starting TortoiseBots timed strategy re-apply tests...\n";

    // (1) The issue #1605 incident: the LFT fill handed a pool priest the
    // healer role (forcedRole 2) and she sits in the real player's dungeon
    // group. The timed re-apply must not run: its "+dps" is the priest's
    // shadow alias and would drop the heal kit mid-run.
    CHECK(Holds(1, true)); // tank fill in a group
    CHECK(Holds(2, true)); // healer fill in a group - the reported case
    CHECK(Holds(4, true)); // damage fill in a group
    std::cout << "  [PASS] grouped bot holding an LFT role keeps its kit\n";

    // (2) Same bot, same role, no group: the pool roulette applies as before
    // (the role only counts while a group is there to honour it).
    CHECK(!Holds(1, false));
    CHECK(!Holds(2, false));
    CHECK(!Holds(4, false));
    std::cout << "  [PASS] solo bot with a role still re-rolls\n";

    // (3) A grouped free bot with no forced role keeps today's behaviour:
    // the timed re-apply is what gives it its pool kit.
    CHECK(!Holds(0, true));
    std::cout << "  [PASS] grouped bot without a role still re-rolls\n";

    // (4) Neither condition: unchanged.
    CHECK(!Holds(0, false));
    std::cout << "  [PASS] masterless unforced bot still re-rolls\n";

    std::cout << "TortoiseBots timed strategy re-apply tests passed.\n";
    return 0;
}
