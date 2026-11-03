#pragma once

// Game-state translation of the ai_boss_stand rows (BossStandPolicy.h): where
// a bot stands while a boss is at an anchor and casting the row's window
// spell. The decisions themselves are pure and tested on their own
// (tools/test_boss_stand_policy.cpp); the translation is
// FindBossStand in strategy/actions/BossTacticsActions.cpp.
//
// Declarations only, so the reach actions and the reactive boss dodge can ask
// without pulling in the action headers.

class PlayerbotAI;
class Player;

namespace ai
{
    struct BossStandTarget
    {
        bool valid = false;  // a stand row applies to this bot and seats it
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;      // the row's floor; the mover snaps it to the ground again
    };

    // The seat of `bot` among the bots of its group with the same role, or
    // invalid: no row applies (map, strategy, boss, role, window), or every
    // seat is taken. Invalid changes nothing for the bot.
    BossStandTarget FindBossStand(PlayerbotAI* ai, Player* bot);
}
