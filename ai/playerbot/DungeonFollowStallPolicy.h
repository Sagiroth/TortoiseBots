#pragma once

// Pure rules for rescuing a stalled core follow inside a dungeon.
//
// FollowAction hands following to the core's follow generator and then trusts
// it: while that generator runs on the leader, the follow action is not
// useful and is never asked again (below 60 yd). Inside a dungeon the
// generator can stand still for good. With a partial path (the bot on a
// navmesh island, a raised step the mesh does not link to the floor) it
// stops while the leader is in sight and otherwise walks to the end of the
// partial path, which is where the bot already stands. The bot then shows
// "moving" with no motion and only "check values" runs; the step-down off a
// ledge, which lives in the follow path, never got its turn. Uldaman
// (2026-10-12): all four bots stood 50 s at (-257,96), on the edge of a small
// island 4 yd above the floor, 34-36 yd from their leader.
//
// The watch below notices that: out of combat, inside a dungeon, the bot is
// behind (beyond its follow range plus a slack) and has not moved a yard for
// two seconds while nothing holds it in place (eating, casting, rooted). It
// then latches a rescue until the bot is back within range: the follow action
// becomes useful again, tries the ledge step-down, and walks the navmesh path
// to the leader with the module's own MoveTo (as mod-playerbots' follow does
// with a path to its formation point), at most once per two seconds, backing
// off to eight seconds while no walk can be started. The rescue yields while
// the bot is held (eating, drinking, casting, rooted), latched or not, and
// after five walks in a row that could not start it lets go for a minute: a
// bot below a platform its leader stands on cannot be rescued, and planning a
// path every eight seconds for it is wasted work.
//
// No core includes; tested in tools/test_dungeon_follow_stall_policy.cpp.

#include <cstdint>

namespace ai
{
    // The bot counts as behind beyond its follow range plus this.
    constexpr float kDungeonFollowStallSlackYd = 3.0f;

    // Moving less than this (2D) counts as standing still.
    constexpr float kDungeonFollowStallMoveYd = 1.0f;

    // Standing still behind a running core follow this long is a stall.
    constexpr uint32_t kDungeonFollowStallMs = 2000;

    // Observations further apart than this start a fresh stand-still clock:
    // the bot was doing something else (another action, a walk's wait).
    constexpr uint32_t kDungeonFollowFeedGapMs = 1000;

    // At most one path walk per this interval while rescuing...
    constexpr uint32_t kDungeonFollowWalkIntervalMs = 2000;

    // ...doubling per walk that could not be started, up to this.
    constexpr uint32_t kDungeonFollowWalkMaxIntervalMs = 8000;

    // This many walks in a row that could not start end the rescue...
    constexpr uint32_t kDungeonFollowMaxFailedWalks = 5;

    // ...and keep it off for this long before the watch may latch again.
    constexpr uint32_t kDungeonFollowGiveUpMs = 60000;

    // Inside a dungeon (raids included) and out of combat only: in a fight the
    // reach actions own positioning, and outside dungeons the core follow does
    // not have the dungeon-only refusals.
    inline bool UseDungeonFollowWatch(bool inDungeon, bool inCombat)
    {
        return inDungeon && !inCombat;
    }

    struct DungeonFollowStallWatch
    {
        bool seen = false;
        float anchorX = 0.0f;
        float anchorY = 0.0f;
        uint32_t anchorMs = 0;
        uint32_t lastSeenMs = 0;
        bool rescuing = false;
        bool walked = false;
        uint32_t lastWalkMs = 0;
        uint32_t failedWalks = 0;
        bool givenUp = false;
        uint32_t givenUpMs = 0;

        void Reset() { *this = DungeonFollowStallWatch(); }

        // Feed the bot's position and its distance to the leader. `held` is
        // anything that keeps a bot still on purpose (sitting, casting,
        // rooted). Returns true while the rescue is on and may act: a held
        // bot is left alone even while the rescue stays latched.
        bool Observe(uint32_t nowMs, float x, float y, float leaderDistance, float followRange, bool held)
        {
            if (leaderDistance <= followRange + kDungeonFollowStallSlackYd)
            {
                Reset(); // caught up: the core follow takes over again
                return false;
            }

            if (givenUp)
            {
                if (nowMs - givenUpMs < kDungeonFollowGiveUpMs)
                    return false;
                Reset(); // a minute later: watch afresh
            }

            float const dx = x - anchorX;
            float const dy = y - anchorY;
            bool const moved = dx * dx + dy * dy > kDungeonFollowStallMoveYd * kDungeonFollowStallMoveYd;
            bool const gap = !seen || nowMs - lastSeenMs > kDungeonFollowFeedGapMs;
            seen = true;
            lastSeenMs = nowMs;

            if (gap || moved || held)
            {
                anchorX = x;
                anchorY = y;
                anchorMs = nowMs;
                return rescuing && !held;
            }

            if (!rescuing && nowMs - anchorMs >= kDungeonFollowStallMs)
                rescuing = true;

            return rescuing;
        }

        uint32_t WalkInterval() const
        {
            uint32_t interval = kDungeonFollowWalkIntervalMs;
            for (uint32_t i = 0; i < failedWalks && interval < kDungeonFollowWalkMaxIntervalMs; ++i)
                interval *= 2;
            return interval < kDungeonFollowWalkMaxIntervalMs ? interval : kDungeonFollowWalkMaxIntervalMs;
        }

        // True when the rescue may plan a path walk now.
        bool WalkDue(uint32_t nowMs) const
        {
            return rescuing && (!walked || nowMs - lastWalkMs >= WalkInterval());
        }

        // A walk was planned; `started` says whether it launched a move.
        // The fifth failure in a row ends the rescue for kDungeonFollowGiveUpMs.
        void Walked(uint32_t nowMs, bool started)
        {
            walked = true;
            lastWalkMs = nowMs;
            if (started)
            {
                failedWalks = 0;
                return;
            }

            if (++failedWalks >= kDungeonFollowMaxFailedWalks)
            {
                rescuing = false;
                walked = false;
                failedWalks = 0;
                givenUp = true;
                givenUpMs = nowMs;
            }
        }
    };
}
