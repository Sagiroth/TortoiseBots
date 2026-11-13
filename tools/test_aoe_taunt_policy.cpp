#include "../ai/playerbot/policy/combat/AoeTauntPolicy.h"

#include <cstdlib>
#include <iostream>
#include <vector>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using namespace ai;

static std::vector<LooseAddCandidate> Pack(unsigned loose, unsigned tankVictims, unsigned strangers, unsigned far)
{
    std::vector<LooseAddCandidate> pack;
    for (unsigned i = 0; i < loose; ++i)
        pack.push_back({true, false, 5.0f});
    for (unsigned i = 0; i < tankVictims; ++i)
        pack.push_back({true, true, 5.0f});
    for (unsigned i = 0; i < strangers; ++i)
        pack.push_back({false, false, 5.0f});
    for (unsigned i = 0; i < far; ++i)
        pack.push_back({true, false, 10.1f});
    return pack;
}

int main()
{
    std::cout << "Starting TortoiseBots AoE taunt policy tests...\n";

    CHECK(!ShouldAoeTaunt(0));
    CHECK(!ShouldAoeTaunt(2));
    CHECK(ShouldAoeTaunt(3));
    CHECK(ShouldAoeTaunt(4));

    // Two loose adds in reach do not spend the 10-min cooldown.
    CHECK(CountLooseAddsInReach(Pack(2, 0, 0, 0)) == 2);
    CHECK(!ShouldAoeTaunt(CountLooseAddsInReach(Pack(2, 0, 0, 0))));

    // Three loose adds in reach arm the shout.
    CHECK(CountLooseAddsInReach(Pack(3, 0, 0, 0)) == 3);
    CHECK(ShouldAoeTaunt(CountLooseAddsInReach(Pack(3, 0, 0, 0))));

    // A mob on another tank, a non-group victim and a mob at 10.1 yd
    // (radius index 13 = 10 yd) are not loose adds.
    CHECK(CountLooseAddsInReach(Pack(3, 1, 1, 1)) == 3);
    CHECK(CountLooseAddsInReach(Pack(2, 1, 1, 1)) == 2);
    CHECK(!ShouldAoeTaunt(CountLooseAddsInReach(Pack(2, 1, 1, 1))));

    // A mob at exactly 10.0 yd is still in reach.
    std::vector<LooseAddCandidate> edge = {{true, false, 10.0f}, {true, false, 10.0f}, {true, false, 10.0f}};
    CHECK(CountLooseAddsInReach(edge) == 3);
    CHECK(ShouldAoeTaunt(CountLooseAddsInReach(edge)));
    CHECK(LooseAddsStillNeedAoeTaunt(edge));

    // The failing case: the same pack, re-walked now, after every victim moved
    // onto a tank within the trigger value's one-second cache window. The old
    // cached "trigger active" read would still return the true that queued the
    // shout; the shared cast-time verdict answers from today's victims and
    // both area-taunt actions stand down.
    std::vector<LooseAddCandidate> settled = {{true, true, 5.0f}, {true, true, 5.0f}, {true, true, 5.0f}};
    CHECK(CountLooseAddsInReach(settled) == 0);
    CHECK(!LooseAddsStillNeedAoeTaunt(settled));
    std::cout << "  [PASS] a pack settled onto tanks stands the AoE taunt down at cast time\n";

    // Pure policy constants pinned: 3+ loose adds inside 10 yd.
    CHECK(kAoeTauntRadiusYd == 10.0f);
    CHECK(kAoeTauntMinLooseAdds == 3);
    std::cout << "  [PASS] loose-add counting gates the AoE taunt\n";

    std::cout << "All TortoiseBots AoE taunt policy tests passed.\n";
    return 0;
}
