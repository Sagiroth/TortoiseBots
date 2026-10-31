// Standalone check for ai/playerbot/WarriorWwStancePolicy.h:
//   g++ -std=c++17 -Wall -Wextra tools/test_warrior_ww_stance_policy.cpp -o /tmp/test_warrior_ww && /tmp/test_warrior_ww
#include "../ai/playerbot/WarriorWwStancePolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using namespace ai;

int main()
{
    std::cout << "Starting TortoiseBots warrior whirlwind-stance policy tests...\n";

    // Already in Berserker Stance the bar is untouched: current rage gates.
    CHECK(ShouldWhirlwindStanceDance(true, true, 25, 0, false, true));
    CHECK(ShouldWhirlwindStanceDance(true, true, 100, 0, false, true));
    CHECK(!ShouldWhirlwindStanceDance(true, true, 24, 0, false, true));
    CHECK(!ShouldWhirlwindStanceDance(false, true, 100, 5, false, true)); // on cooldown: no dance
    std::cout << "  [PASS] whirlwind in-stance gates on current rage and cooldown\n";

    // Outside Berserker Stance only the Tactical Mastery retention survives
    // the swap (5 per rank: 0/5/10/15/20/25). Whirlwind costs 25, so below
    // rank 5 no pre-swap bar can pay for it after the swap: even 100 rage
    // must not arm the dance (the round-1 hole: it gated on current rage).
    for (unsigned rank = 0; rank <= 4; ++rank)
        CHECK(!ShouldWhirlwindStanceDance(true, false, 100, rank, false, true));
    std::cout << "  [PASS] whirlwind outside-stance never dances below rank 5, however full the bar\n";

    // Rank 5 keeps 25: rage just above/below the cost decides.
    CHECK(ShouldWhirlwindStanceDance(true, false, 25, 5, false, true));
    CHECK(ShouldWhirlwindStanceDance(true, false, 100, 5, false, true));
    CHECK(!ShouldWhirlwindStanceDance(true, false, 24, 5, false, true));
    CHECK(!ShouldWhirlwindStanceDance(false, false, 100, 5, false, true)); // on cooldown: no dance
    std::cout << "  [PASS] whirlwind outside-stance gates on post-swap rage at rank 5\n";

    // Raid: a single-target Whirlwind is no better than Mortal Strike from
    // Battle Stance, and the round trip costs two casts. Only dance when
    // two or more mobs are in melee; already in Berserker Stance the cast
    // costs no swap and stays allowed.
    CHECK(!ShouldWhirlwindStanceDance(true, false, 100, 5, true, false)); // raid, single target, Battle
    CHECK(ShouldWhirlwindStanceDance(true, false, 100, 5, true, true));   // raid, 2+ in melee
    CHECK(ShouldWhirlwindStanceDance(true, true, 25, 0, true, false));    // raid, single, already Berserker
    CHECK(!ShouldWhirlwindStanceDance(true, true, 24, 0, true, false));   // still needs the rage
    CHECK(!ShouldWhirlwindStanceDance(false, true, 100, 0, true, false)); // on cooldown
    CHECK(ShouldWhirlwindStanceDance(true, false, 100, 5, false, false)); // dungeon single: unchanged
    std::cout << "  [PASS] raid whirlwind does not dance on a single target\n";

    // Pummel in stance: current rage against the 10 cost, plus a live cast.
    CHECK(ShouldPummelStanceDance(true, true, 10, 0, true));
    CHECK(!ShouldPummelStanceDance(true, true, 9, 0, true));
    CHECK(!ShouldPummelStanceDance(true, true, 100, 0, false)); // nothing to interrupt
    CHECK(!ShouldPummelStanceDance(false, true, 100, 3, true)); // on cooldown: no dance
    std::cout << "  [PASS] pummel in-stance gates on current rage, cooldown and cast\n";

    // Pummel outside stance at each retention rank: the swap keeps at most
    // 5/10/15/20/25, so rank 0-1 can never pay 10 however full the bar.
    CHECK(!ShouldPummelStanceDance(true, false, 100, 0, true));
    CHECK(!ShouldPummelStanceDance(true, false, 100, 1, true));
    // Rank 2 keeps exactly 10: just above/below the cost decides.
    CHECK(ShouldPummelStanceDance(true, false, 10, 2, true));
    CHECK(ShouldPummelStanceDance(true, false, 100, 2, true));
    CHECK(!ShouldPummelStanceDance(true, false, 9, 2, true));
    // Ranks 3-5 keep 15+: the 10 cost survives whenever the bar holds it.
    for (unsigned rank = 3; rank <= 5; ++rank)
    {
        CHECK(ShouldPummelStanceDance(true, false, 10, rank, true));
        CHECK(!ShouldPummelStanceDance(true, false, 9, rank, true));
    }
    CHECK(!ShouldPummelStanceDance(true, false, 100, 5, false)); // nothing to interrupt
    CHECK(!ShouldPummelStanceDance(false, false, 100, 5, true)); // on cooldown: no dance
    std::cout << "  [PASS] pummel outside-stance gates on post-swap rage at each rank\n";

    CHECK(WARRIOR_WHIRLWIND_RAGE_COST == 25);
    CHECK(WARRIOR_PUMMEL_RAGE_COST == 10);
    std::cout << "  [PASS] cost constants pinned\n";

    std::cout << "All TortoiseBots warrior whirlwind-stance policy tests passed.\n";
    return 0;
}
