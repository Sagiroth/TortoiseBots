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
    constexpr float kSeekWalkoutThreatShare = 0.9f; // a puller at this share of the main tank's threat stops
    constexpr int kSeekWalkoutMoveFails = 2;        // this many refused moves in a row abort the task

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
        bool charmed = false;
        bool insane = false;     // carries Cause Insanity
        bool canShoot = true;    // its shot can be cast at the Son now (spell or wand known, ammo, mana, not silenced)
        float yd = 0.0f;         // to the Son (or the anchor's centre when no Son is known)
    };

    inline bool WalkoutEligible(const WalkoutCandidate& c)
    {
        return c.alive && c.role != WalkoutRole::None && !c.tank && !c.healer && !c.bossVictim && !c.highThreat &&
            !c.charmed && !c.insane && c.canShoot && c.inside && c.pathOk;
    }

    // Whether a bot's threat on the boss is close enough to the main tank's to turn him: kSeekWalkoutThreatShare of it or
    // more. Without a main-tank reference (dead, charmed) only the victim rule speaks. Being in the top two is not
    // enough: the off-tanks hold little threat on Hakkar, so a damage dealer is nearly always second.
    inline bool WalkoutThreatTooHigh(float botThreat, float mainTankThreat)
    {
        return mainTankThreat > 0.0f && botThreat >= kSeekWalkoutThreatShare * mainTankThreat;
    }

    // Why the running task's puller cannot go on (the abort's logged reason), or null: dead, charmed, under Cause
    // Insanity, the boss's victim, or at his main tank's threat.
    inline const char* WalkoutPullerProblem(bool alive, bool charmed, bool insane, bool bossVictim, bool highThreat)
    {
        if (!alive)
            return "dead";
        if (charmed)
            return "charmed";
        if (insane)
            return "insanity";
        if (bossVictim)
            return "victim";
        if (highThreat)
            return "threat";
        return nullptr;
    }

    inline bool WalkoutPullerUsable(bool alive, bool charmed, bool insane, bool bossVictim, bool highThreat)
    {
        return WalkoutPullerProblem(alive, charmed, insane, bossVictim, highThreat) == nullptr;
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
        int moveFails = 0;
        bool lastMoveOk = false;
        // The marks the running task undoes when its cast fails (WalkoutAbortUnspent).
        bool prevCycleSpent = false;
        std::uint32_t prevLastCycle = 0;
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

    // A new task: none running, none yet in this seek cycle (one per Blood Siphon), and never twice for one Son.
    inline bool WalkoutMayStart(const SeekWalkoutState& s, std::uint32_t cycleNo, std::uint64_t son)
    {
        if (WalkoutBusy(s) || son == 0)
            return false;
        if (s.cycleSpent && s.lastCycle == cycleNo)
            return false;
        return std::find(s.walkedSons.begin(), s.walkedSons.end(), son) == s.walkedSons.end();
    }

    inline void WalkoutStart(SeekWalkoutState& s, std::uint64_t puller, std::uint64_t son, std::uint32_t nowMs,
                             std::uint32_t cycleNo)
    {
        s.prevCycleSpent = s.cycleSpent;
        s.prevLastCycle = s.lastCycle;
        s.puller = puller;
        s.son = son;
        s.startMs = nowMs;
        s.shot = false;
        s.moveFails = 0;
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

    // The task ends: a Back end is inside already; a timeout or an abort leaves the puller outside, so its walk back
    // starts (exempt for kSeekWalkoutReturnMs at most).
    inline void WalkoutFinish(SeekWalkoutState& s, WalkoutEnd end, std::uint32_t nowMs)
    {
        if (end == WalkoutEnd::Timeout || end == WalkoutEnd::Abort)
        {
            s.returner = s.puller;
            s.returnStartMs = nowMs;
        }
        s.puller = 0;
        s.son = 0;
        s.startMs = 0;
        s.shot = false;
    }

    // The cast failed at the stand: abort, and undo the task's marks so the Son and the cycle are still free.
    inline void WalkoutAbortUnspent(SeekWalkoutState& s, std::uint32_t nowMs)
    {
        if (!WalkoutActive(s))
            return;
        std::vector<std::uint64_t>::iterator const it = std::find(s.walkedSons.begin(), s.walkedSons.end(), s.son);
        if (it != s.walkedSons.end())
            s.walkedSons.erase(it);
        s.cycleSpent = s.prevCycleSpent;
        s.lastCycle = s.prevLastCycle;
        WalkoutFinish(s, WalkoutEnd::Abort, nowMs);
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
