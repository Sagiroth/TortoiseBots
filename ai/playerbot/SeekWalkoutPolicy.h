#pragma once

// The seek walk-out pull (Hakkar, ai_boss_tactic kinds seek_aura + anchor): the Sons of Hakkar patrol ~78 yd from his
// spawn, past both the tanks' reach and the ranged shot from the anchor's edge, so no Son ever came to the raid and
// every Blood Siphon healed him. When nothing else can pull, ONE ranged damage dealer leaves the anchor, walks to a
// stand within its shot range of the nearest Son out of combat, shoots it once and walks straight back into the tanks'
// circle; the Son follows it there and the off-tank takes it. Only that bot, and only while its task runs, is exempt
// from the anchor.
//
// Pure: the glue (UpdateSeekWalkout, FindSeekWalkoutPuller and the walk-out branch of BossTacticsSeekTankAction in
// strategy/actions/BossTacticsActions.cpp) reads the game state. Tested in tools/test_seek_walkout_policy.cpp.

#include "BossAnchorPolicy.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace ai
{
    constexpr std::uint32_t kSeekWalkoutTimeoutMs = 25000; // the task ends after this, shot or not
    constexpr float kSeekWalkoutSearchYd = 100.0f;         // the Sons it may walk to, around the anchor's centre
    constexpr float kSeekWalkoutShotMarginYd = 2.0f;       // the stand sits this far inside the shot's reach
    constexpr float kSeekWalkoutBackInsetYd = 5.0f;        // the walk back ends this far inside the tanks' radius
    constexpr float kSeekWalkoutBackSlackYd = 2.0f;        // ... and counts as arrived this far short of that point

    // The puller's rank: a hunter (the longest shot, a pet to tank the Son's first hits), then a mage, warlock or
    // shadow priest, then any other ranged damage dealer. None: no puller.
    enum class WalkoutRole : std::uint8_t
    {
        Hunter = 0,
        Caster = 1,
        Ranged = 2,
        None = 3,
    };

    inline WalkoutRole WalkoutRoleOf(bool hunter, bool caster, bool rangedDps)
    {
        if (hunter)
            return WalkoutRole::Hunter;
        if (caster)
            return WalkoutRole::Caster;
        if (rangedDps)
            return WalkoutRole::Ranged;
        return WalkoutRole::None;
    }

    struct WalkoutCandidate
    {
        std::uint64_t guid = 0;
        WalkoutRole role = WalkoutRole::None;
        bool alive = false;
        bool tank = false;
        bool healer = false;
        bool bossVictim = false; // the boss's current victim
        bool topThreat = false;  // one of the two highest on the boss's threat list
        bool charmed = false;
        bool insane = false;     // carries Cause Insanity
        float yd = 0.0f;         // to the Son (or the anchor's centre when no Son is known)
    };

    inline bool WalkoutEligible(const WalkoutCandidate& c)
    {
        return c.alive && c.role != WalkoutRole::None && !c.tank && !c.healer && !c.bossVictim && !c.topThreat &&
            !c.charmed && !c.insane;
    }

    // The puller: the best role among the eligible, then the nearest, then the lowest guid (every bot's read picks the
    // same one). -1: nobody.
    inline int PickWalkoutPuller(const std::vector<WalkoutCandidate>& candidates)
    {
        int pick = -1;
        for (int i = 0; i < static_cast<int>(candidates.size()); ++i)
        {
            const WalkoutCandidate& c = candidates[i];
            if (!WalkoutEligible(c))
                continue;
            if (pick < 0)
            {
                pick = i;
                continue;
            }
            const WalkoutCandidate& best = candidates[pick];
            if (c.role != best.role ? c.role < best.role : (c.yd != best.yd ? c.yd < best.yd : c.guid < best.guid))
                pick = i;
        }
        return pick;
    }

    // The shot's reach: a hunter's 35 yd, a wand's or a spell's 30 yd, less the margin.
    inline float WalkoutShotRangeYd(bool hunter)
    {
        return (hunter ? 35.0f : 30.0f) - kSeekWalkoutShotMarginYd;
    }

    // The stand the puller shoots from: where it stands when the Son is within `rangeYd`, else the point `rangeYd`
    // from the Son on the line toward the bot (the nearest point in reach).
    inline void WalkoutShotPoint(float sonX, float sonY, float botX, float botY, float rangeYd, float& x, float& y)
    {
        float const dx = botX - sonX;
        float const dy = botY - sonY;
        float const d = std::sqrt(dx * dx + dy * dy);
        if (d <= rangeYd || d < 0.01f)
        {
            x = botX;
            y = botY;
            return;
        }
        x = sonX + dx / d * rangeYd;
        y = sonY + dy / d * rangeYd;
    }

    // The walk back: straight toward the centre, to the tanks' radius less the inset, so the Son that follows comes
    // inside the tanks' circle where the off-tank may take it.
    inline void WalkoutBackPoint(const BossAnchorCircle& tank, float botX, float botY, float& x, float& y)
    {
        float const dx = botX - tank.x;
        float const dy = botY - tank.y;
        float const d = std::sqrt(dx * dx + dy * dy);
        float const reach = std::max(0.0f, tank.radius - kSeekWalkoutBackInsetYd);
        if (d <= reach || d < 0.01f)
        {
            x = botX;
            y = botY;
            return;
        }
        x = tank.x + dx / d * reach;
        y = tank.y + dy / d * reach;
    }

    inline bool WalkoutBackArrived(const BossAnchorCircle& tank, float botX, float botY)
    {
        float const dx = botX - tank.x;
        float const dy = botY - tank.y;
        return std::sqrt(dx * dx + dy * dy) <=
            std::max(0.0f, tank.radius - kSeekWalkoutBackInsetYd) + kSeekWalkoutBackSlackYd;
    }

    // The fight's one walk-out task (per map instance): its puller and Son, the start, whether the shot landed (the Son
    // joined the fight), the seek cycle it spent and every Son already walked to.
    struct SeekWalkoutState
    {
        std::uint64_t puller = 0;
        std::uint64_t son = 0;
        std::uint32_t startMs = 0;
        bool shot = false;
        bool cycleSpent = false;
        std::uint32_t lastCycle = 0;
        std::vector<std::uint64_t> walkedSons;
    };

    inline bool WalkoutActive(const SeekWalkoutState& s)
    {
        return s.puller != 0;
    }

    // A new task: none running, none yet in this seek cycle (one per Blood Siphon), and never twice for one Son.
    inline bool WalkoutMayStart(const SeekWalkoutState& s, std::uint32_t cycleNo, std::uint64_t son)
    {
        if (WalkoutActive(s) || son == 0)
            return false;
        if (s.cycleSpent && s.lastCycle == cycleNo)
            return false;
        return std::find(s.walkedSons.begin(), s.walkedSons.end(), son) == s.walkedSons.end();
    }

    inline void WalkoutStart(SeekWalkoutState& s, std::uint64_t puller, std::uint64_t son, std::uint32_t nowMs,
                             std::uint32_t cycleNo)
    {
        s.puller = puller;
        s.son = son;
        s.startMs = nowMs;
        s.shot = false;
        s.cycleSpent = true;
        s.lastCycle = cycleNo;
        s.walkedSons.push_back(son);
    }

    inline void WalkoutNoteShot(SeekWalkoutState& s)
    {
        if (WalkoutActive(s))
            s.shot = true;
    }

    inline void WalkoutFinish(SeekWalkoutState& s)
    {
        s.puller = 0;
        s.son = 0;
        s.startMs = 0;
        s.shot = false;
    }

    enum class WalkoutEnd : std::uint8_t
    {
        None = 0,
        Back = 1,    // shot, and the puller is back inside the tanks' circle
        Timeout = 2, // 25 s
        Abort = 3,   // the puller can no longer pull (dead, charmed, the boss's victim, gone) or the Son is gone unshot
    };

    inline WalkoutEnd WalkoutCheck(const SeekWalkoutState& s, std::uint32_t nowMs, bool pullerOk, bool sonOk,
                                   bool backInside)
    {
        if (!WalkoutActive(s))
            return WalkoutEnd::None;
        if (!pullerOk || (!s.shot && !sonOk))
            return WalkoutEnd::Abort;
        if (s.shot && backInside)
            return WalkoutEnd::Back;
        if (nowMs - s.startMs >= kSeekWalkoutTimeoutMs)
            return WalkoutEnd::Timeout;
        return WalkoutEnd::None;
    }

    inline const char* WalkoutEndName(WalkoutEnd end)
    {
        switch (end)
        {
            case WalkoutEnd::Back: return "back";
            case WalkoutEnd::Timeout: return "timeout";
            case WalkoutEnd::Abort: return "abort";
            default: return "none";
        }
    }

    // Whether the anchor lets this bot go: it is the running task's puller and the task is inside its time. The
    // exemption lapses at the timeout even before a read closes the task.
    inline bool WalkoutExempts(const SeekWalkoutState& s, std::uint64_t bot, std::uint32_t nowMs)
    {
        return WalkoutActive(s) && s.puller == bot && nowMs - s.startMs < kSeekWalkoutTimeoutMs;
    }
}
