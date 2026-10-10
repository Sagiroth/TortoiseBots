// Standalone regression test for the druid out-of-combat rebirth port
// (DRU-1): burn Rebirth out of combat only when someone is dead, we know
// the spell, are alive and out of combat, and no living resurrecting class
// (priest/paladin/shaman) can rez instead.
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra tools/test_ooc_rebirth_policy.cpp -o /tmp/test_ooc_rebirth
//   /tmp/test_ooc_rebirth

#include "../ai/playerbot/OocRebirthPolicy.h"

#include <cstdio>
#include <cstdlib>

using namespace ai;

static int checks = 0;
#define CHECK(cond) do { \
    ++checks; \
    if (!(cond)) { \
        std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        std::exit(1); \
    } \
} while (0)

static OocRebirthState Base()
{
    // Dead member, trained and alive druid, out of combat, nobody better.
    return OocRebirthState{true, true, true, false, false};
}

int main()
{
    // Base case fires.
    CHECK(ShouldCastOocRebirth(Base()));

    // Nobody dead: quiet.
    CHECK(!ShouldCastOocRebirth(OocRebirthState{false, true, true, false, false}));

    // Rebirth unknown (low level / untrained): quiet.
    CHECK(!ShouldCastOocRebirth(OocRebirthState{true, false, true, false, false}));

    // Druid itself dead: quiet.
    CHECK(!ShouldCastOocRebirth(OocRebirthState{true, true, false, false, false}));

    // In combat: quiet (combat `rebirth` row owns that case).
    CHECK(!ShouldCastOocRebirth(OocRebirthState{true, true, true, true, false}));

    // A living priest/paladin/shaman in the group: their normal rez wins.
    CHECK(!ShouldCastOocRebirth(OocRebirthState{true, true, true, false, true}));

    std::printf("ooc rebirth policy: OK (%d checks)\n", checks);
    return 0;
}
