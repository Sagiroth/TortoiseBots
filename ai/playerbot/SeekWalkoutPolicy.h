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
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ai
{
    constexpr std::uint32_t kSeekWalkoutTimeoutMs = 25000; // the task ends after this, shot or not
    constexpr float kSeekWalkoutSearchYd = 100.0f;         // the Sons it may walk to, around the anchor's centre
    constexpr float kSeekWalkoutShotMarginYd = 2.0f;       // the stand sits this far inside the shot's reach
    constexpr float kSeekWalkoutBackInsetYd = 5.0f;        // the walk back ends this far inside the tanks' radius
    constexpr float kSeekWalkoutBackSlackYd = 2.0f;        // ... and counts as arrived this far short of that point
    constexpr std::uint32_t kSeekWalkoutReturnMs = 15000;  // a timed-out or aborted puller walks back this long, exempt
    constexpr std::uint32_t kSeekWalkoutStaleMs = 120000;  // a fight's record nobody touched this long is dropped
    constexpr std::uint32_t kSeekWalkoutPetHoldMs = 120000; // a held pet's stance older than this is dropped
    constexpr float kSeekWalkoutMinStandZ = 46.5f;  // the stand's floor: above Hakkar's 45.8 evade line, with margin
    constexpr float kSeekWalkoutStandStepYd = 2.0f; // the stand search's step, out from the anchor's edge
    constexpr float kSeekWalkoutThreatShare = 0.7f; // a bot at this share of the main tank's threat is not picked
    constexpr float kSeekWalkoutAbortThreatShare = 1.0f; // a running puller at this share of the top tank's aborts
    constexpr int kSeekWalkoutMoveFails = 3;        // this many refused moves in a row abort the task
    constexpr std::uint32_t kSeekWalkoutReplanMs = 1000;  // the stand is re-planned from the Son's live spot this often
    constexpr std::uint32_t kSeekWalkoutNoStandMs = 5000; // this long straight without any stand aborts ("no stand")
    constexpr std::uint32_t kSeekWalkoutExcludeMs = 60000; // a puller whose moves failed is left out of the picks
    constexpr std::uint32_t kSeekWalkoutSiphonLeadMs = 75000; // a walk-out starts only this close to the next siphon
    constexpr int kSeekWalkoutMaxPerCycle = 2;             // walk-outs started in one seek cycle, the retry included
    constexpr std::uint32_t kSeekWalkoutRetryMinMs = 25000; // a failed walk-out reopens the cycle only with more left

    // The one cast the puller opens the Son with, by class id: the hunter's Auto Shot (spell 75), the wand's Shoot
    // (spell 5019) for a mage, warlock or priest, a druid's Wrath (5176 at rank 1), a shaman's Lightning Bolt (403 at
    // rank 1). Empty: the class has no ranged opener here.
    inline const char* WalkoutShotSpell(std::uint8_t classId)
    {
        switch (classId)
        {
            case 3: return "auto shot";
            case 5:
            case 8:
            case 9: return "shoot";
            case 11: return "wrath";
            case 7: return "lightning bolt";
            default: return "";
        }
    }

    // The puller's rank: a hunter (the longest shot), then a mage, warlock or shadow priest, then any other ranged
    // damage dealer. None: no puller. The pet stays held at the puller's heels the whole task (it never takes the Son).
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
        bool highThreat = false; // its threat on the boss is at kSeekWalkoutThreatShare of the main tank's or more
        bool inside = true;      // it stands inside the anchor (a bot left out on a ledge is never sent)
        bool pathOk = true;      // it has a navmesh path to the stand
        bool excluded = false;   // its last task's moves failed less than kSeekWalkoutExcludeMs ago
        bool charmed = false;
        bool insane = false;     // carries Cause Insanity
        bool canShoot = true;    // its shot can be cast at the Son now (spell or wand known, ammo, mana, not silenced)
        float yd = 0.0f;         // to the Son (or the anchor's centre when no Son is known)
    };

    inline bool WalkoutEligible(const WalkoutCandidate& c)
    {
        return c.alive && c.role != WalkoutRole::None && !c.tank && !c.healer && !c.bossVictim && !c.highThreat &&
            !c.charmed && !c.insane && c.canShoot && c.inside && c.pathOk && !c.excluded;
    }

    // Whether a bot's threat on the boss is too close to the main tank's to be sent: kSeekWalkoutThreatShare of it or
    // more (the pick's margin; a running task aborts at kSeekWalkoutAbortThreatShare of the top tank's,
    // WalkoutRunningThreatTooHigh). Without a main-tank reference (dead, charmed) only the victim rule speaks.
    inline bool WalkoutThreatTooHigh(float botThreat, float mainTankThreat)
    {
        return mainTankThreat > 0.0f && botThreat >= kSeekWalkoutThreatShare * mainTankThreat;
    }

    // The live top tank's threat on the boss this tick: the highest of the living tanks' (`tankThreats`), never a cached
    // main tank, so a tank swap moves the reference with it. 0 with no tank on the list.
    inline float WalkoutTopTankThreat(const std::vector<float>& tankThreats)
    {
        float top = 0.0f;
        for (float const threat : tankThreats)
            if (threat > top)
                top = threat;
        return top;
    }

    // Whether the running task's puller has caught the top tank (t1105h review): its threat at
    // kSeekWalkoutAbortThreatShare of the live top tank's or more. The next swing or DoT tick hands it the boss, so the
    // task aborts (`threat`) before the boss turns to it. With no tank reference only the victim rule speaks.
    inline bool WalkoutRunningThreatTooHigh(float botThreat, float topTankThreat)
    {
        return topTankThreat > 0.0f && botThreat >= kSeekWalkoutAbortThreatShare * topTankThreat;
    }

    // Why the running task's puller cannot go on (the abort's logged reason), or null: dead, charmed, under Cause
    // Insanity, the boss's victim (the real danger: he turns toward it, off the platform), or its threat level with the
    // live top tank's (`threatHigh`, WalkoutRunningThreatTooHigh).
    inline const char* WalkoutPullerProblem(bool alive, bool charmed, bool insane, bool bossVictim,
        bool threatHigh = false)
    {
        if (!alive)
            return "dead";
        if (charmed)
            return "charmed";
        if (insane)
            return "insanity";
        if (bossVictim)
            return "victim";
        if (threatHigh)
            return "threat";
        return nullptr;
    }

    inline bool WalkoutPullerUsable(bool alive, bool charmed, bool insane, bool bossVictim)
    {
        return WalkoutPullerProblem(alive, charmed, insane, bossVictim) == nullptr;
    }

    // The eligible candidates, best first: the role, then the nearest, then the lowest guid (every reader ranks them
    // alike). The glue asks the costly path question in this order and takes the first that passes.
    inline std::vector<int> RankWalkoutPullers(const std::vector<WalkoutCandidate>& candidates)
    {
        std::vector<int> order;
        for (int i = 0; i < static_cast<int>(candidates.size()); ++i)
            if (WalkoutEligible(candidates[i]))
                order.push_back(i);
        std::sort(order.begin(), order.end(), [&candidates](int a, int b)
        {
            const WalkoutCandidate& x = candidates[a];
            const WalkoutCandidate& y = candidates[b];
            if (x.role != y.role)
                return x.role < y.role;
            if (x.yd != y.yd)
                return x.yd < y.yd;
            return x.guid < y.guid;
        });
        return order;
    }

    // The puller: the first of the ranking. -1: nobody.
    inline int PickWalkoutPuller(const std::vector<WalkoutCandidate>& candidates)
    {
        const std::vector<int> order = RankWalkoutPullers(candidates);
        return order.empty() ? -1 : order.front();
    }

    // The stand search's candidates: from the anchor's edge toward the Son, kSeekWalkoutStandStepYd apart, up to (not
    // onto) the Son. Empty when the Son stands inside the circle (the anchored shot pulls it).
    inline std::vector<std::pair<float, float>> WalkoutStandCandidates(const BossAnchorCircle& c, float sonX, float sonY)
    {
        std::vector<std::pair<float, float>> out;
        float const dx = sonX - c.x;
        float const dy = sonY - c.y;
        float const d = std::sqrt(dx * dx + dy * dy);
        if (d <= c.radius)
            return out;
        for (float r = c.radius; r < d; r += kSeekWalkoutStandStepYd)
            out.emplace_back(c.x + dx / d * r, c.y + dy / d * r);
        return out;
    }

    // Whether a candidate is the stand: a floor read there at or above kSeekWalkoutMinStandZ (the pullers that walked
    // down the slope to the Son's height died there), within the shot's range of the Son, and in sight of it.
    inline bool WalkoutStandOk(bool floorFound, float floorZ, float toSonYd, float rangeYd, bool sight)
    {
        return floorFound && floorZ >= kSeekWalkoutMinStandZ && toSonYd <= rangeYd && sight;
    }

    // The shot's reach: a hunter's 35 yd, a wand's or a spell's 30 yd, less the margin.
    inline float WalkoutShotRangeYd(bool hunter)
    {
        return (hunter ? 35.0f : 30.0f) - kSeekWalkoutShotMarginYd;
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
        // The stand the task walks to (the platform search), and its refused moves in a row.
        float standX = 0.0f, standY = 0.0f, standZ = 0.0f;
        std::uint32_t standMs = 0;       // the stand's last re-plan
        std::uint32_t noStandSinceMs = 0; // the re-plans have found no stand since then (0: they found one)
        int moveFails = 0;
        // The last refused move: the mover's reason, the path type and length to the stand (the abort row says them).
        std::uint32_t failReason = 0;
        std::uint32_t failPathType = 0;
        float failPathYd = 0.0f;
        bool lastMoveOk = false;
        // Pullers left out of the picks until a time (their moves failed): guid, until.
        std::vector<std::pair<std::uint64_t, std::uint32_t>> excluded;
        // The marks the running task undoes when its cast fails (WalkoutAbortUnspent).
        bool prevCycleSpent = false;
        std::uint32_t prevLastCycle = 0;
        // The walk-outs started in cycle `countedCycle`, and when the running task's siphon is due (its start plus the
        // due time then; equal to the start when unknown, which never reopens the cycle).
        std::uint32_t countedCycle = 0xFFFFFFFFu;
        int cycleStarts = 0;
        std::uint32_t siphonDueMs = 0;
        // The walk back after a timeout or an abort: that puller, exempt until it is inside or kSeekWalkoutReturnMs.
        std::uint64_t returner = 0;
        std::uint32_t returnStartMs = 0;
    };

    inline bool WalkoutActive(const SeekWalkoutState& s)
    {
        return s.puller != 0;
    }

    inline bool WalkoutReturning(const SeekWalkoutState& s, std::uint32_t nowMs)
    {
        return s.returner != 0 && nowMs - s.returnStartMs < kSeekWalkoutReturnMs;
    }

    // A task or its walk back holds the fight's one slot.
    inline bool WalkoutBusy(const SeekWalkoutState& s)
    {
        return WalkoutActive(s) || s.returner != 0;
    }

    // The seek cycle still takes a walk-out: not spent by one (a failed one may reopen it, WalkoutRetryAllowed), and
    // fewer than kSeekWalkoutMaxPerCycle started in it.
    inline bool WalkoutCycleOpen(const SeekWalkoutState& s, std::uint32_t cycleNo)
    {
        if (s.cycleSpent && s.lastCycle == cycleNo)
            return false;
        return s.countedCycle != cycleNo || s.cycleStarts < kSeekWalkoutMaxPerCycle;
    }

    // A new task: none running, the cycle open (WalkoutCycleOpen), and never twice for one living Son (a walked Son
    // that died is forgotten, WalkoutForgetGoneSons, so its respawn may be walked to again).
    inline bool WalkoutMayStart(const SeekWalkoutState& s, std::uint32_t cycleNo, std::uint64_t son)
    {
        if (WalkoutBusy(s) || son == 0)
            return false;
        if (!WalkoutCycleOpen(s, cycleNo))
            return false;
        return std::find(s.walkedSons.begin(), s.walkedSons.end(), son) == s.walkedSons.end();
    }

    // `dueInMs`: the next Blood Siphon's due time from now (0: unknown or due), kept for the retry rule.
    inline void WalkoutStart(SeekWalkoutState& s, std::uint64_t puller, std::uint64_t son, std::uint32_t nowMs,
                             std::uint32_t cycleNo, std::uint32_t dueInMs = 0)
    {
        s.prevCycleSpent = s.cycleSpent;
        s.prevLastCycle = s.lastCycle;
        if (s.countedCycle != cycleNo)
        {
            s.countedCycle = cycleNo;
            s.cycleStarts = 0;
        }
        ++s.cycleStarts;
        s.siphonDueMs = nowMs + dueInMs;
        s.puller = puller;
        s.son = son;
        s.startMs = nowMs;
        s.shot = false;
        s.moveFails = 0;
        s.standMs = nowMs;
        s.noStandSinceMs = 0;
        s.cycleSpent = true;
        s.lastCycle = cycleNo;
        s.walkedSons.push_back(son);
    }

    // One move of the task: a refused one counts, a good one resets the count. True: kSeekWalkoutMoveFails in a row,
    // the task aborts ("move failed").
    inline bool WalkoutNoteMove(SeekWalkoutState& s, bool moved)
    {
        s.lastMoveOk = moved;
        s.moveFails = moved ? 0 : s.moveFails + 1;
        return s.moveFails >= kSeekWalkoutMoveFails;
    }

    // The puller whose moves failed sits out kSeekWalkoutExcludeMs, so the next pick takes another bot.
    inline void WalkoutExclude(SeekWalkoutState& s, std::uint64_t bot, std::uint32_t nowMs)
    {
        for (std::pair<std::uint64_t, std::uint32_t>& entry : s.excluded)
        {
            if (entry.first == bot)
            {
                entry.second = nowMs + kSeekWalkoutExcludeMs;
                return;
            }
        }
        s.excluded.emplace_back(bot, nowMs + kSeekWalkoutExcludeMs);
    }

    inline bool WalkoutExcluded(const SeekWalkoutState& s, std::uint64_t bot, std::uint32_t nowMs)
    {
        for (const std::pair<std::uint64_t, std::uint32_t>& entry : s.excluded)
            if (entry.first == bot && static_cast<std::int32_t>(entry.second - nowMs) > 0)
                return true;
        return false;
    }

    // A retry (a second walk-out in the cycle) starts only while the siphon is still more than kSeekWalkoutRetryMinMs
    // away, read when it starts against the deadline stored by the cycle's first walk-out: the failed puller's walk
    // back (up to kSeekWalkoutReturnMs) runs between the end that reopened the cycle and this start.
    inline bool WalkoutRetryStartInTime(const SeekWalkoutState& s, std::uint32_t cycleNo, std::uint32_t nowMs)
    {
        if (s.countedCycle != cycleNo || s.cycleStarts < 1)
            return true;
        return static_cast<std::int32_t>(s.siphonDueMs - nowMs) > static_cast<std::int32_t>(kSeekWalkoutRetryMinMs);
    }

    // A new task for this puller: WalkoutMayStart, the bot not sitting out its exclusion (a scan cached before the
    // exclusion may still name it), a retry still in time (WalkoutRetryStartInTime), and the next Blood Siphon due in
    // kSeekWalkoutSiphonLeadMs or less (`dueInMs`, 0 once
    // due): the dose window's whole length, so the Son is back and burned before the siphon (the 12 s non-tank burn,
    // SeekAuraPolicy.h kSeekNonTankBurnMs, keeps it off the healers meanwhile).
    inline bool WalkoutMayStartFor(const SeekWalkoutState& s, std::uint32_t cycleNo, std::uint64_t son,
                                   std::uint64_t bot, std::uint32_t nowMs, std::uint32_t dueInMs)
    {
        return dueInMs <= kSeekWalkoutSiphonLeadMs && WalkoutMayStart(s, cycleNo, son) &&
            !WalkoutExcluded(s, bot, nowMs) && WalkoutRetryStartInTime(s, cycleNo, nowMs);
    }

    // The Son the walk-out goes for: nearest first (`yd`), the first that has a stand (`hasStand(i)`, asked lazily in
    // that order, so a Son without one never blocks the rest). -1: none has one.
    template <typename HasStand>
    inline int PickWalkoutSon(const std::vector<float>& yd, HasStand hasStand)
    {
        std::vector<int> order;
        for (int i = 0; i < static_cast<int>(yd.size()); ++i)
            order.push_back(i);
        std::sort(order.begin(), order.end(), [&yd](int a, int b) { return yd[a] != yd[b] ? yd[a] < yd[b] : a < b; });
        for (int i : order)
            if (hasStand(i))
                return i;
        return -1;
    }

    // Whether the stand is due a re-plan from the Son's live spot (kSeekWalkoutReplanMs since the last).
    inline bool WalkoutReplanDue(const SeekWalkoutState& s, std::uint32_t nowMs)
    {
        return nowMs - s.standMs >= kSeekWalkoutReplanMs;
    }

    // One re-plan: a found stand replaces the old one and restarts the no-stand clock; none keeps the old stand and
    // runs the clock. True: kSeekWalkoutNoStandMs straight without any stand, the task aborts ("no stand").
    inline bool WalkoutNoteStand(SeekWalkoutState& s, bool found, float x, float y, float z, std::uint32_t nowMs)
    {
        s.standMs = nowMs;
        if (found)
        {
            s.standX = x;
            s.standY = y;
            s.standZ = z;
            s.noStandSinceMs = 0;
            return false;
        }
        if (s.noStandSinceMs == 0)
            s.noStandSinceMs = nowMs;
        return nowMs - s.noStandSinceMs >= kSeekWalkoutNoStandMs;
    }

    inline void WalkoutNoteShot(SeekWalkoutState& s)
    {
        if (WalkoutActive(s))
            s.shot = true;
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

    // A failed walk-out (a timeout, no stand, a refused cast or move) leaves the cycle open for one more only while
    // the siphon is more than kSeekWalkoutRetryMinMs away (a later Son is not back and burned before it) and fewer
    // than kSeekWalkoutMaxPerCycle started in the cycle.
    inline bool WalkoutRetryAllowed(const SeekWalkoutState& s, std::uint32_t nowMs)
    {
        return s.cycleStarts < kSeekWalkoutMaxPerCycle &&
            static_cast<std::int32_t>(s.siphonDueMs - nowMs) > static_cast<std::int32_t>(kSeekWalkoutRetryMinMs);
    }

    // The task ends: a Back end is inside already; a timeout or an abort leaves the puller outside, so its walk back
    // starts (exempt for kSeekWalkoutReturnMs at most). A timeout undoes the task's cycle mark when a retry is allowed
    // (its Son stays walked, so the retry goes to another one). True: the cycle was reopened.
    inline bool WalkoutFinish(SeekWalkoutState& s, WalkoutEnd end, std::uint32_t nowMs)
    {
        bool reopened = false;
        if (end == WalkoutEnd::Timeout && WalkoutActive(s) && WalkoutRetryAllowed(s, nowMs))
        {
            s.cycleSpent = s.prevCycleSpent;
            s.lastCycle = s.prevLastCycle;
            reopened = true;
        }
        if (end == WalkoutEnd::Timeout || end == WalkoutEnd::Abort)
        {
            s.returner = s.puller;
            s.returnStartMs = nowMs;
        }
        s.puller = 0;
        s.son = 0;
        s.startMs = 0;
        s.shot = false;
        return reopened;
    }

    // No pull happened (the cast or the moves failed, or no stand): abort with the Son free again, and the cycle too
    // when a retry is allowed (WalkoutRetryAllowed).
    inline void WalkoutAbortUnspent(SeekWalkoutState& s, std::uint32_t nowMs)
    {
        if (!WalkoutActive(s))
            return;
        std::vector<std::uint64_t>::iterator const it = std::find(s.walkedSons.begin(), s.walkedSons.end(), s.son);
        if (it != s.walkedSons.end())
            s.walkedSons.erase(it);
        if (WalkoutRetryAllowed(s, nowMs))
        {
            s.cycleSpent = s.prevCycleSpent;
            s.lastCycle = s.prevLastCycle;
        }
        WalkoutFinish(s, WalkoutEnd::Abort, nowMs);
    }

    // The shot could not be cast at the stand: abort unspent, and the puller sits out the picks like a move fail, so the
    // retry goes to another bot (the cached scan would hand it the same puller, Son and stand, to fail again).
    inline void WalkoutCastFailed(SeekWalkoutState& s, std::uint64_t bot, std::uint32_t nowMs)
    {
        if (!WalkoutActive(s))
            return;
        WalkoutAbortUnspent(s, nowMs);
        WalkoutExclude(s, bot, nowMs);
    }

    // Walked Sons that died (or left the map) are forgotten, the running task's own Son excepted: the two Sons near the
    // platform respawn after 60 s with the same guid, and the respawn is the next walk-out's Son.
    template <typename IsGone>
    inline void WalkoutForgetGoneSons(SeekWalkoutState& s, IsGone isGone)
    {
        s.walkedSons.erase(std::remove_if(s.walkedSons.begin(), s.walkedSons.end(),
            [&](std::uint64_t son) { return son != s.son && isGone(son); }), s.walkedSons.end());
    }

    inline void WalkoutReturnDone(SeekWalkoutState& s)
    {
        s.returner = 0;
        s.returnStartMs = 0;
    }

    // The puller's pet stance while a task holds it passive at the puller's heels: its react state and its command
    // (stay or follow). Kept per puller, outside the fight's record, so a record dropped on any path (the boss's death
    // or combat exit, a stale sweep, a re-pull) never takes the stance with it.
    struct WalkoutPetStance
    {
        std::uint64_t petGuid = 0; // the pet it was taken from: no other pet gets it
        std::uint8_t react = 0;
        std::uint8_t command = 0;
        std::uint32_t savedMs = 0;
    };

    // Saved once: a second hold of the same puller keeps the first stance (the pet is passive by then).
    inline void WalkoutSavePetStance(std::unordered_map<std::uint64_t, WalkoutPetStance>& held, std::uint64_t bot,
                                     std::uint64_t petGuid, std::uint8_t react, std::uint8_t command,
                                     std::uint32_t nowMs)
    {
        if (held.find(bot) != held.end())
            return;
        WalkoutPetStance stance;
        stance.petGuid = petGuid;
        stance.react = react;
        stance.command = command;
        stance.savedMs = nowMs;
        held.emplace(bot, stance);
    }

    // Whether the stance goes back onto the bot's current pet: only the pet it was taken from (a dismissed and
    // resummoned pet, or a relog's new one, has another guid).
    inline bool WalkoutPetStanceApplies(const WalkoutPetStance& stance, std::uint64_t currentPetGuid)
    {
        return currentPetGuid != 0 && stance.petGuid == currentPetGuid;
    }

    // Drops every stance older than kSeekWalkoutPetHoldMs (a puller that logged out or was dismissed mid-hold never
    // releases its own); the number dropped.
    inline std::size_t WalkoutSweepPetHolds(std::unordered_map<std::uint64_t, WalkoutPetStance>& held,
                                            std::uint32_t nowMs)
    {
        std::size_t dropped = 0;
        for (auto it = held.begin(); it != held.end();)
        {
            if (nowMs - it->second.savedMs >= kSeekWalkoutPetHoldMs)
            {
                it = held.erase(it);
                ++dropped;
            }
            else
                ++it;
        }
        return dropped;
    }

    inline bool WalkoutTakePetStance(std::unordered_map<std::uint64_t, WalkoutPetStance>& held, std::uint64_t bot,
                                     WalkoutPetStance& out)
    {
        auto const it = held.find(bot);
        if (it == held.end())
            return false;
        out = it->second;
        held.erase(it);
        return true;
    }

    // Whether the bot's held pet goes back to its stance: its instance has no record (dropped), or the record's task or
    // walk back is no longer this bot's.
    inline bool WalkoutPetRelease(const SeekWalkoutState* record, std::uint64_t bot)
    {
        if (!record)
            return true;
        return !(WalkoutActive(*record) && record->puller == bot) && record->returner != bot;
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

    // Whether the anchor lets this bot go: it is the running task's puller inside the task's time, or the puller of a
    // timed-out or aborted task inside its walk back. Both lapse on their own clock, before any read closes them.
    inline bool WalkoutExempts(const SeekWalkoutState& s, std::uint64_t bot, std::uint32_t nowMs)
    {
        if (WalkoutActive(s) && s.puller == bot && nowMs - s.startMs < kSeekWalkoutTimeoutMs)
            return true;
        return s.returner == bot && WalkoutReturning(s, nowMs);
    }
}
