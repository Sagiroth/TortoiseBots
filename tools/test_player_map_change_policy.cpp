// Standalone regression test for the map-change summon sweep's bot filter
// (host/BotPlayerAdapter::OnMapChanged): a real player who leaves a dungeon
// brings the bots still inside out, and leaves every other bot alone.
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra tools/test_player_map_change_policy.cpp -o /tmp/test_player_map_change_policy
//   /tmp/test_player_map_change_policy

#include "../host/PlayerMapChangePolicy.h"

#include <cstdio>

using TortoiseBots::PlayerMapChange::BotLeftInDungeon;

static int g_failed = 0;
#define CHECK(cond) do { if (!(cond)) { std::fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_failed; } } while (0)

int main()
{
    // The bot is still inside the dungeon the master left: it is summoned out.
    CHECK(BotLeftInDungeon(true, true));

    // The bot is already outside with its master: nothing to do. (The inverted
    // form - the bug this pins - returned true here and summoned every bot
    // that was already out.)
    CHECK(!BotLeftInDungeon(true, false));

    // Between maps (a far teleport in flight) there is no map to read.
    CHECK(!BotLeftInDungeon(false, false));

    if (g_failed)
    {
        std::fprintf(stderr, "%d check(s) failed\n", g_failed);
        return 1;
    }
    std::printf("test_player_map_change_policy: all checks passed\n");
    return 0;
}
