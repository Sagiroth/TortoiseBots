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
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
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
    constexpr int kSeekWalkoutMaxPerCycle = 1;             // walk-outs started in one seek cycle: no retry
    constexpr std::uint32_t kSeekWalkoutRetryMinMs = 25000; // a failed walk-out reopens the cycle only with more left
    constexpr std::uint32_t kSeekWalkoutShotStallMs = 6000; // at the stand this long without a shot aborts ("no shot")
    constexpr float kSeekWalkoutStandArriveYd = 3.0f;       // at the stand is this close to it
    constexpr float kSeekWalkoutPlatformYd = 60.0f;         // the ranged platform: this close to the anchor's centre

    // The pulled Son meets a tank (walk 53): the walk back ends where a waiting off-tank stands, not at the circle's
    // edge. The Son that chased the puller lands on that tank instead of on the ranged raid. Taunt and Growl are melee
    // range, so the puller has to bring the Son within the receiver's reach: the receiver takes it once it is this
    // close (Attack closes the last yards to the taunt's melee range), and the puller stops this close to the receiver.
    constexpr float kSeekMeetTakeYd = 8.0f;         // the receiver takes the Son once it is this close
    constexpr std::uint32_t kSeekMeetMaxMs = 20000; // the meet is over 20 s after the shot (walk 52: start to back
                                                    // median 18 s, p90 23 s, and the shot lands mid-task)

    // A seek_pull row's rules (walk 51): every Son is walked once per cycle, up to `maxPerCycle` walk-outs start a
    // cycle, each with `firstStartMinMs` left at the least, a failed one reopens the cycle, a shot that stalls at the
    // stand aborts, the pick compares with the live top tank and the stand search fans out and reaches a hunter's
    // range. The defaults are the old one-walk-out-per-cycle rules.
    struct WalkoutCycleRules
    {
        int maxPerCycle = kSeekWalkoutMaxPerCycle;
        std::uint32_t retryMinMs = kSeekWalkoutRetryMinMs;
        std::uint32_t firstStartMinMs = 0; // 0: no deadline (the old rule)
        std::uint32_t startLeadMs = kSeekWalkoutSiphonLeadMs; // the earliest start: this much to the due time
        std::uint32_t firstFightStartMs = 0; // 0: no gate (the old rule)
        bool perCycleWalked = false;
        bool reopenOnAbort = false;
        bool shotStall = false;
        bool pickTopTankThreat = false;
        bool standFan = false;
        bool hunterReachScan = false;
        bool platformPick = false;
        bool pickCastLater = false;
    };

    // The rules a seek_pull row arms: `priority` walk-outs a cycle, `windowSec` the last start and the retry floor,
    // `seekLeadSec` the seek_aura row's dose window (the earliest start, clamped to the walk-out's own bounds).
    inline WalkoutCycleRules WalkoutRulesFromRow(int priority, float windowSec, float seekLeadSec = 0.0f)
    {
        WalkoutCycleRules rules;
        rules.maxPerCycle = std::max(1, std::min(5, priority));
        std::uint32_t const windowMs = static_cast<std::uint32_t>(windowSec * 1000.0f);
        rules.retryMinMs = windowMs;
        rules.firstStartMinMs = windowMs;
        rules.firstFightStartMs = 19000; // the first Cause Insanity lands at +17-18 s
        rules.perCycleWalked = true;
        rules.reopenOnAbort = true;
        rules.shotStall = true;
        rules.pickTopTankThreat = true;
        rules.standFan = true;
        rules.hunterReachScan = true;
        rules.platformPick = true;
        rules.pickCastLater = true;
        if (seekLeadSec > 0.0f)
        {
            std::uint32_t const leadMs = static_cast<std::uint32_t>(seekLeadSec * 1000.0f);
            rules.startLeadMs = std::clamp(leadMs, kSeekWalkoutSiphonLeadMs, 85000u);
        }
        return rules;
    }

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

    // The walk-out focus (DungeonRunSeekWalkoutFocus, walk 55: 15 cycle-1 walk-outs never shot, the puller in range
    // and sight but in "follow", "hunter step out" or another action, and not facing the Son in 10 of 13 no-shot
    // aborts). Pure rules the glue (BossTacticsSeekTankAction's walk-out branch and BossTacticsWalkoutFocusMultiplier)
    // reads.
    //
    // The pull shot: a hunter opens with the instant Arcane Shot when it can cast it now, else Auto Shot (the
    // auto-repeat waits on the ranged swing timer and needs the Son in front). Never a DoT: Serpent Sting stays vetoed.
    // Every other class keeps WalkoutShotSpell's shot (a warrior or paladin still has none and is never picked).
    inline const char* WalkoutPullShotSpell(std::uint8_t classId, bool arcaneReady)
    {
        if (classId == 3)
            return arcaneReady ? "arcane shot" : "auto shot";
        return WalkoutShotSpell(classId);
    }

    // Facing first: with the focus on, a puller in shot position that does not face the Son stops, turns this tick and
    // casts on the next one (a cast in the same tick as SetFacingTo went out before the turn landed).
    inline bool WalkoutFaceFirst(bool focusOn, bool rulesOn, bool inFront)
    {
        return focusOn && rulesOn && !inFront;
    }

    // Face and cast in one tick (DungeonRunSeekWalkoutFaceCast, walk 56: 8 of 11 no-shot aborts stood stopped in range
    // and sight but facing Hakkar). A stopped bot's forced turn is an instant orientation set (ServerFacade::SetFacingTo
    // with force: SetOrientation plus a heartbeat), so the cast can follow in the same tick; the split turn of
    // WalkoutFaceFirst lost the race to UpdateFaceTarget, which turns the bot back to its current target first.
    struct WalkoutFaceStep
    {
        bool stop = false;            // stop the bot before it turns
        bool face = false;            // turn to the Son this tick
        bool force = false;           // the turn is SetFacingTo's instant `force` turn
        bool returnAfterFace = false; // return after the turn, the cast left to the next tick
    };

    // With FaceCast on: always stopped, turned (forced) only when not facing, and the cast follows in the same tick.
    // Off: today's t1106k steps (WalkoutFaceFirst: stop, turn, return; the rules alone: turn and cast).
    inline WalkoutFaceStep WalkoutFaceThenCast(bool faceCastOn, bool focusOn, bool rulesOn, bool inFront)
    {
        WalkoutFaceStep step;
        if (faceCastOn)
        {
            step.stop = true;
            step.face = !inFront;
            step.force = true;
            step.returnAfterFace = false;
            return step;
        }
        bool const faceFirst = WalkoutFaceFirst(focusOn, rulesOn, inFront);
        step.stop = faceFirst;
        step.face = rulesOn && !inFront;
        step.force = false;
        step.returnAfterFace = faceFirst;
        return step;
    }

    // Whether PlayerbotAI::UpdateFaceTarget leaves the bot's facing alone this update (DungeonRunSeekWalkoutFaceCast):
    // the live, unshot walk-out puller faces its Son, not its current target (Hakkar). Every other bot, and the puller
    // once its shot landed, is turned as before.
    inline bool WalkoutFaceTargetYields(bool flagOn, bool livePuller)
    {
        return flagOn && livePuller;
    }

    // Whether the walk-out's own record still names this bot as a live puller: recorded as the task's puller, alive,
    // not charmed, and the task inside its 25 s (the same `<` as the anchor's exemption, WalkoutExempts). A dead or
    // charmed puller (the abort clears the record only on the next reader's update) and a stale record never hold
    // anything.
    inline bool WalkoutFocusPullerLive(bool recordedPuller, bool alive, bool charmed, std::uint32_t sinceStartMs)
    {
        return recordedPuller && alive && !charmed && sinceStartMs < kSeekWalkoutTimeoutMs;
    }

    // The walk-out keeps its puller in combat (DungeonRunSeekWalkoutHoldCombat, walk 56: 5 of the 6 `timeout shot=0`
    // pullers fell to the non-combat engine inside their own task, where the walk-out has no action and `follow`, food
    // or drink walked them off the stand). `combat end` does not fire while this holds: the flag on, the bot the live
    // puller (WalkoutFocusPullerLive: recorded, alive, not charmed, inside the 25 s) and the shot not landed. The walk
    // back is not held, and the hold ends with the task however it ends (shot, timeout, abort, death, charm).
    //
    // `bossFighting` (review L2): the record's boss is alive, in combat and not evading (WalkoutBossFighting), so a wipe,
    // an evade or a kill ends the hold at once instead of at the 25 s.
    inline bool WalkoutHoldsCombat(bool flagOn, bool recordedPuller, bool alive, bool charmed, std::uint32_t sinceStartMs,
                                   bool shot, bool bossFighting = true)
    {
        return flagOn && bossFighting && WalkoutFocusPullerLive(recordedPuller, alive, charmed, sinceStartMs) && !shot;
    }

    // The hold's flag (review L1): HoldCombat needs the walk-out focus, which keeps the held puller's other combat
    // actions (a Hakkar target pick, the rotation) from running; without it nothing is held.
    inline bool WalkoutHoldCombatOn(bool holdCombatFlag, bool focusFlag)
    {
        return holdCombatFlag && focusFlag;
    }

    // The walk-out's boss still fights: found, alive, in combat and not evading (the record's sweep drops a boss that is
    // dead or out of combat; an evading one is added here).
    inline bool WalkoutBossFighting(bool found, bool alive, bool inCombat, bool evading)
    {
        return found && alive && inCombat && !evading;
    }

    // Whether a pick candidate's AI can run the walk-out at all (DungeonRunSeekWalkoutHoldCombat): the walk-out is a
    // combat-engine action, so a bot on the non-combat engine, or one eating or drinking, is not sent. The bot's own
    // engine state is read, never its pet's combat: a hunter whose pet fights while the hunter's AI sits on the
    // non-combat engine is still not ready.
    inline bool WalkoutCombatEngineReady(bool combatEngine, bool eatingOrDrinking)
    {
        return combatEngine && !eatingOrDrinking;
    }

    // Below this health the focus lets go (walk 56 review): a puller 30+ yd out, often past its healers' reach, drinks
    // its potion, uses its healthstone or mends its pet as usual. The walk-out's own aborts (threat, victim, charm, the
    // stall and the timeout) are unchanged.
    constexpr float kWalkoutFocusFloorPct = 40.0f;

    // The actions the focus lets run: the walk-out itself and the pet's dismissal.
    inline bool WalkoutFocusAllows(const std::string& action)
    {
        return action == "boss tactics seek tank" || action == "boss tactics seek pet";
    }

    // Whether the focus holds `action` this tick: the flag on, this bot the live walk-out puller (WalkoutFocusPullerLive)
    // and the shot not landed yet. Every combat-engine action but the walk-out and the pet's dismissal is held:
    // "follow", "hunter step out", "wait for attack", "reach melee"/"reach spell", "feign death". Reaction-engine
    // actions (flee, avoid cast, tank swap) are not multiplied by it and still run: they are the puller's survival
    // moves, and the walk-out already aborts on threat, on the boss's victim and on a charm. Once the shot lands (or the
    // task ends) nothing is held: the walk back is the walk-out action's own.
    inline bool WalkoutFocusBlocks(bool flagOn, bool isPuller, bool shot, const std::string& action)
    {
        return flagOn && isPuller && !shot && !WalkoutFocusAllows(action);
    }

    // The multiplier's whole decision, from plain inputs the glue fills by name (BossTacticsWalkoutFocusMultiplier).
    struct WalkoutFocusInput
    {
        bool flagOn = false;
        bool recordedPuller = false; // the instance's running task names this bot as its puller
        bool alive = false;
        bool charmed = false;
        std::uint32_t sinceStartMs = 0;
        bool shot = false;
        float hpPct = 100.0f;
    };

    // Whether the focus holds `action`: the puller live (WalkoutFocusPullerLive), unshot, at or above the health floor,
    // and the action not the walk-out's own (WalkoutFocusBlocks).
    inline bool WalkoutFocusHolds(const WalkoutFocusInput& in, const std::string& action)
    {
        bool const live = WalkoutFocusPullerLive(in.recordedPuller, in.alive, in.charmed, in.sinceStartMs);
        return in.hpPct >= kWalkoutFocusFloorPct && WalkoutFocusBlocks(in.flagOn, live, in.shot, action);
    }

    // The pull shot in flight (walk 56 review): the instant Arcane Shot leaves no cast behind, and the Son counts as shot
    // only once it is in combat. For this long after the pull cast the puller waits for the Son instead of casting
    // again, and the 6 s stall does not abort the walk-out as "no shot".
    constexpr std::uint32_t kSeekWalkoutShotFlightMs = 2000;

    // Whether the pull cast at `castMs` (0: none this task) may still land at `nowMs`.
    inline bool WalkoutPullShotInFlight(std::uint32_t castMs, std::uint32_t nowMs)
    {
        return castMs != 0 && nowMs - castMs < kSeekWalkoutShotFlightMs;
    }

    // At the stand: whether the puller waits this tick for a pull shot already sent (in flight, the Son not yet in
    // combat) instead of casting again or aborting.
    inline bool WalkoutPullShotWaits(std::uint32_t castMs, std::uint32_t nowMs, bool sonInCombat)
    {
        return !sonInCombat && WalkoutPullShotInFlight(castMs, nowMs);
    }

    // Whether the 6 s stall aborts the walk-out as "no shot" now: stalled, and no pull shot in flight.
    inline bool WalkoutNoShotAbortDue(bool stalled, std::uint32_t castMs, std::uint32_t nowMs)
    {
        return stalled && !WalkoutPullShotInFlight(castMs, nowMs);
    }

    // The `walk-out preempted` row: one per walk-out, keyed by the task's start time. It names the first action the focus
    // held in that walk-out (the engine asks the multiplier only for an action it is about to run, so that action would
    // have run this tick had the walk-out not been chosen or had it returned false).
    inline bool WalkoutPreemptRowDue(std::uint32_t loggedStartMs, std::uint32_t startMs)
    {
        return loggedStartMs != startMs;
    }

    // The walk-out stand rows (DungeonRunSeekWalkoutStandLog, diagnostic, walk 56 fix 4): while the puller walks out
    // unshot, one `seek pull: walk-out stand` row a second names the branch its tick took, and every refused pull cast
    // logs `seek pull: walk-out castrefused` at once, at most kSeekWalkoutCastRefusedRowsMax a walk-out. A 25 s task
    // writes at most 26 stand rows.
    constexpr std::uint32_t kSeekWalkoutStandRowMs = 1000;
    constexpr int kSeekWalkoutCastRefusedRowsMax = 10;

    // The branch the puller's walk-out tick took: `out` walking (not at the stand, not in shot position), `inpos` at
    // the stand but not in shot position (range, floor or sight missing), `wait` a pull shot in flight, `face` the turn
    // with the cast left to the next tick, `castwait` its own cast on the Son still running, `checkfail` the shot's cast
    // check refused (the `abort cast failed` row follows), `cast` the pull cast went out, `castrefused` CastSpell said no.
    enum class WalkoutStandBranch : std::uint8_t
    {
        Out = 0,
        InPos = 1,
        Wait = 2,
        Face = 3,
        CastWait = 4,
        CheckFail = 5,
        Cast = 6,
        CastRefused = 7,
    };

    inline const char* WalkoutStandBranchWord(WalkoutStandBranch b)
    {
        switch (b)
        {
            case WalkoutStandBranch::Out: return "out";
            case WalkoutStandBranch::InPos: return "inpos";
            case WalkoutStandBranch::Wait: return "wait";
            case WalkoutStandBranch::Face: return "face";
            case WalkoutStandBranch::CastWait: return "castwait";
            case WalkoutStandBranch::CheckFail: return "checkfail";
            case WalkoutStandBranch::Cast: return "cast";
            case WalkoutStandBranch::CastRefused: return "castrefused";
        }
        return "out";
    }

    // A second since the walk-out's last stand row (the throttle; the first row of a walk-out is keyed by its start,
    // WalkoutStandRowClaim).
    inline bool WalkoutStandRowDue(std::uint32_t lastRowMs, std::uint32_t nowMs)
    {
        return nowMs - lastRowMs >= kSeekWalkoutStandRowMs;
    }

    // Whether this read writes the stand row: the walk-out's first (its start not logged yet) or a second after the last.
    inline bool WalkoutStandRowClaim(std::uint32_t loggedStartMs, std::uint32_t startMs, std::uint32_t lastRowMs,
                                     std::uint32_t nowMs)
    {
        return loggedStartMs != startMs || WalkoutStandRowDue(lastRowMs, nowMs);
    }

    // Whether a refused pull cast still writes its row: fewer than kSeekWalkoutCastRefusedRowsMax this walk-out (`rows`
    // counts the walk-out started at `loggedStartMs`).
    inline bool WalkoutCastRefusedRowDue(std::uint32_t loggedStartMs, std::uint32_t startMs, int rows)
    {
        return loggedStartMs != startMs || rows < kSeekWalkoutCastRefusedRowsMax;
    }

    // The refused cast's reason word: the core's or the cast check's own result when CastSpell noted one (`castFail`,
    // SpellCastResult; 0 is SPELL_CAST_OK in the core, so none noted), else the CastSpell gate the state before the call
    // points at (PlayerbotAISpells.cpp: flying, the stand-up delay, the moving turn's delay, a move with no master), else
    // `gate` (the self-harm, dispel-hold and pet-redirect gates).
    inline std::string WalkoutCastRefuseWord(std::uint32_t castFail, std::uint32_t castOk, bool flying, bool standing,
                                             bool stopped, bool inFront)
    {
        if (castFail != castOk)
            return "spell_fail_" + std::to_string(castFail);
        if (flying)
            return "flying";
        if (!standing)
            return "stand_delay";
        if (!stopped && !inFront)
            return "facing_delay";
        if (!stopped)
            return "moving";
        return "gate";
    }

    // The stand row's text.
    inline std::string WalkoutStandRowText(WalkoutStandBranch branch, bool combatEngine, bool inRange, bool los,
                                           bool front, bool stopped, std::uint32_t castSpellId, const std::string& refuse,
                                           std::uint32_t tickMs)
    {
        char buf[224];
        snprintf(buf, sizeof(buf),
                 "seek pull: walk-out stand branch=%s state=%c inrange=%d los=%d front=%d stopped=%d cast=%u refuse=%s "
                 "tick_ms=%u",
                 WalkoutStandBranchWord(branch), combatEngine ? 'c' : 'n', inRange ? 1 : 0, los ? 1 : 0, front ? 1 : 0,
                 stopped ? 1 : 0, static_cast<unsigned>(castSpellId), refuse.empty() ? "-" : refuse.c_str(),
                 static_cast<unsigned>(tickMs));
        return buf;
    }

    // The castrefused row's text.
    inline std::string WalkoutCastRefusedRowText(std::uint32_t spellId, const std::string& reason)
    {
        return "seek pull: walk-out castrefused spell=" + std::to_string(spellId) + " reason=" + reason;
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
        bool combatEngine = true; // its AI is on the combat engine and not eating or drinking (HoldCombat only; else true)
        float yd = 0.0f;         // to the Son (or the anchor's centre when no Son is known)
    };

    inline bool WalkoutEligible(const WalkoutCandidate& c)
    {
        return c.alive && c.role != WalkoutRole::None && !c.tank && !c.healer && !c.bossVictim && !c.highThreat &&
            !c.charmed && !c.insane && c.canShoot && c.combatEngine && c.inside && c.pathOk && !c.excluded;
    }

    // Whether a bot stands on the ranged platform: within kSeekWalkoutPlatformYd of the anchor's centre and above
    // Hakkar's evade line. The ranged raid stands 30-50 yd out, outside the tanks' anchor, and still pulls.
    inline bool WalkoutOnPlatform(float distFromCentreYd, float z)
    {
        return distFromCentreYd <= kSeekWalkoutPlatformYd && z >= kSeekWalkoutMinStandZ;
    }

    // Whether a refused shot check still leaves the bot usable for the walk-out: a cast already in progress or a spell
    // on cooldown is over by the time the puller walks to its stand, so a caster mid-cast is not refused.
    inline bool WalkoutShotReadyLater(bool castOk, bool spellInProgress, bool notReady)
    {
        return castOk || spellInProgress || notReady;
    }

    // Whether the fight has run long enough for the walk-out's first start: Cause Insanity lands at +17-18 s and
    // deletes the main tank's threat, which turned the cycle-1 picks into threat aborts.
    inline bool WalkoutFightOldEnough(const WalkoutCycleRules& rules, std::uint32_t fightMs)
    {
        return rules.firstFightStartMs == 0 || fightMs >= rules.firstFightStartMs;
    }

    // Why the pick rejected a member (the no-puller row's per-candidate counts). The words stay short and fixed so a
    // script can count them; the order is WalkoutEligible's, so a dead tank reads `dead`, a tank at threat `tank`.
    enum class WalkoutReject : std::uint8_t
    {
        Away = 0, // off the bot's map or not in the world
        NoAi = 1,
        HunterOnly = 2, // the hunter-reach pass skipped it
        Dead = 3,
        NoRole = 4, // no ranged damage role
        Tank = 5,
        Healer = 6,
        Victim = 7, // the boss's current victim
        Threat = 8,
        Cc = 9, // charmed or under Cause Insanity
        NoShot = 10,
        Outside = 11, // outside the anchor
        Bench = 12,   // left out of the picks (recent failed moves)
        NoPath = 13,  // no navmesh path to the stand
        NoCombat = 14, // its AI is not on the combat engine, or it eats or drinks (DungeonRunSeekWalkoutHoldCombat)
    };

    inline const char* WalkoutRejectWord(WalkoutReject reject)
    {
        switch (reject)
        {
            case WalkoutReject::Away: return "away";
            case WalkoutReject::NoAi: return "no_ai";
            case WalkoutReject::HunterOnly: return "hunter_only";
            case WalkoutReject::Dead: return "dead";
            case WalkoutReject::NoRole: return "no_role";
            case WalkoutReject::Tank: return "tank";
            case WalkoutReject::Healer: return "healer";
            case WalkoutReject::Victim: return "victim";
            case WalkoutReject::Threat: return "threat";
            case WalkoutReject::Cc: return "cc";
            case WalkoutReject::NoShot: return "no_shot";
            case WalkoutReject::Outside: return "outside";
            case WalkoutReject::Bench: return "bench";
            case WalkoutReject::NoPath: return "no_path";
            case WalkoutReject::NoCombat: return "no_combat";
        }
        return "away";
    }

    // The first test WalkoutEligible fails, with `tank` and `healer` ahead of the role: `role` comes from the ranged
    // damage check alone, so a melee tank or healer would otherwise read `no_role`. Only called for an ineligible
    // candidate.
    inline WalkoutReject WalkoutRejectOf(const WalkoutCandidate& c)
    {
        if (!c.alive)
            return WalkoutReject::Dead;
        if (c.tank)
            return WalkoutReject::Tank;
        if (c.healer)
            return WalkoutReject::Healer;
        if (c.role == WalkoutRole::None)
            return WalkoutReject::NoRole;
        if (c.bossVictim)
            return WalkoutReject::Victim;
        if (c.highThreat)
            return WalkoutReject::Threat;
        if (c.charmed || c.insane)
            return WalkoutReject::Cc;
        if (!c.canShoot)
            return WalkoutReject::NoShot;
        if (!c.combatEngine)
            return WalkoutReject::NoCombat;
        if (!c.inside)
            return WalkoutReject::Outside;
        if (!c.pathOk)
            return WalkoutReject::NoPath;
        return WalkoutReject::Bench; // the only test left: excluded
    }

    // The no-puller row's info2: its counts in enum order, zero counts left out; `reason=none,members=<m>` when every
    // member passed (a pick lost to something else, e.g. no group), then the threat reference and the lowest passing
    // candidate's threat, so a threat refusal can be read against both.
    struct WalkoutRejectTally
    {
        std::array<std::uint16_t, 15> n{};
        std::uint16_t members = 0;
        float top = -1.0f; // the live top tank's threat (-1: not measured)
        float low = -1.0f; // the lowest candidate's threat (-1: no candidate reached the threat test)

        void Add(WalkoutReject reject)
        {
            ++n[static_cast<std::size_t>(reject)];
        }
    };

    inline std::string WalkoutRejectText(const WalkoutRejectTally& t)
    {
        std::string out = "reason=";
        bool any = false;
        for (std::size_t i = 0; i < t.n.size(); ++i)
        {
            if (t.n[i] == 0)
                continue;
            if (any)
                out += '|';
            out += WalkoutRejectWord(static_cast<WalkoutReject>(i));
            out += ':';
            out += std::to_string(t.n[i]);
            any = true;
        }
        if (!any)
            out += "none";
        out += ",members=";
        out += std::to_string(t.members);
        char tail[48];
        snprintf(tail, sizeof(tail), " top=%.0f low=%.0f", t.top, t.low);
        out += tail;
        return out;
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

    // The pick's threat rule against the live top tank, the same reference the running abort reads: at
    // kSeekWalkoutThreatShare of it or more. Cause Insanity at ~17 s zeroes the old main tank's threat, so a pick that
    // still compared with roles.tanks[0] passed a bot the abort turned on seconds later.
    inline bool WalkoutPickThreatTooHigh(float botThreat, float topTankThreat)
    {
        return topTankThreat <= 0.0f ? botThreat > 0.0f : botThreat >= kSeekWalkoutThreatShare * topTankThreat;
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

    // The stand search's rays: the same points for every offset from the bearing to the Son (`offsetsDeg`, in order),
    // so a ray blocked by a ledge has neighbours to fall back on. Offset 0 alone is WalkoutStandCandidates point for
    // point. Empty when the Son stands inside the circle.
    inline std::vector<std::pair<float, float>> WalkoutStandCandidatesFan(const BossAnchorCircle& c, float sonX,
                                                                         float sonY,
                                                                         const std::vector<float>& offsetsDeg)
    {
        std::vector<std::pair<float, float>> out;
        float const dx = sonX - c.x;
        float const dy = sonY - c.y;
        float const d = std::sqrt(dx * dx + dy * dy);
        if (d <= c.radius)
            return out;
        float const baseX = dx / d;
        float const baseY = dy / d;
        constexpr float kPi = 3.14159265358979323846f;
        for (float const offset : offsetsDeg)
        {
            float const rad = offset * kPi / 180.0f;
            float const cs = std::cos(rad);
            float const sn = std::sin(rad);
            float const ux = baseX * cs - baseY * sn;
            float const uy = baseX * sn + baseY * cs;
            for (float r = c.radius; r < d; r += kSeekWalkoutStandStepYd)
                out.emplace_back(c.x + ux * r, c.y + uy * r);
        }
        return out;
    }

    // Whether a candidate is the stand: a floor read there at or above kSeekWalkoutMinStandZ (the pullers that walked
    // down the slope to the Son's height died there), within the shot's range of the Son, and in sight of it.
    inline bool WalkoutStandOk(bool floorFound, float floorZ, float toSonYd, float rangeYd, bool sight)
    {
        return floorFound && floorZ >= kSeekWalkoutMinStandZ && toSonYd <= rangeYd && sight;
    }

    // Whether the puller at the stand keeps waiting for a cast already under way: only one aimed at the walk-out's
    // Son. Its Auto Shot or a wand's Shoot still on the boss is not the shot that pulls, so it is interrupted instead.
    inline bool WalkoutShotWaits(bool castOnSon, bool anyCast)
    {
        return anyCast && castOnSon;
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

    // One raid tank as a meet receiver: alive and in the world, not charmed, not the boss's victim, not holding a Son
    // already, and inside the tanks' circle (the walk back ends there).
    struct MeetCandidate
    {
        bool alive = false;
        bool charmed = false;
        bool bossVictim = false;
        bool holdsSon = false;
        bool inside = false;
    };

    // Why this tank cannot wait for the pulled Son: the first failing test in the order below, or null when it can.
    inline const char* MeetRefusalWhy(const MeetCandidate& c)
    {
        if (!c.alive)
            return "dead";
        if (c.charmed)
            return "charmed";
        if (c.bossVictim)
            return "victim";
        if (c.holdsSon)
            return "busy";
        if (!c.inside)
            return "outside";
        return nullptr;
    }

    // The tank that waits for the pulled Son: the raid's third tank (index 2), else the second off-tank (index 1), and
    // never the main tank at index 0, which holds the boss. -1: no tank can wait.
    inline int PickMeetReceiver(const std::vector<MeetCandidate>& tanks)
    {
        if (tanks.size() > 2 && MeetRefusalWhy(tanks[2]) == nullptr)
            return 2;
        if (tanks.size() > 1 && MeetRefusalWhy(tanks[1]) == nullptr)
            return 1;
        return -1;
    }

    // The per-code refusal counts over indices 1..n-1 in MeetRefusalWhy's order, e.g. `dead:1|busy:1`; `none` when no
    // candidate failed.
    inline std::string MeetRefusalCounts(const std::vector<MeetCandidate>& tanks)
    {
        static const char* const kCodes[] = {"dead", "charmed", "victim", "busy", "outside"};
        int counts[5] = {0, 0, 0, 0, 0};
        for (std::size_t i = 1; i < tanks.size(); ++i)
        {
            const char* const why = MeetRefusalWhy(tanks[i]);
            if (!why)
                continue;
            for (int code = 0; code < 5; ++code)
            {
                if (std::strcmp(why, kCodes[code]) == 0)
                {
                    ++counts[code];
                    break;
                }
            }
        }
        std::string out;
        for (int code = 0; code < 5; ++code)
        {
            if (counts[code] == 0)
                continue;
            if (!out.empty())
                out += "|";
            out += kCodes[code];
            out += ":";
            out += std::to_string(counts[code]);
        }
        return out.empty() ? std::string("none") : out;
    }

    // Where the walk back ends: the tanks' circle edge without a meet, the waiting receiver's live position (clamped
    // into that circle) with one.
    inline void WalkoutMeetBackPoint(bool hasMeet, float rx, float ry, const BossAnchorCircle& tank, float botX,
                                     float botY, float& x, float& y)
    {
        if (hasMeet)
            WalkoutBackPoint(tank, rx, ry, x, y);
        else
            WalkoutBackPoint(tank, botX, botY, x, y);
    }

    // Whether the puller has arrived: at the circle's edge without a meet; with one, within kSeekMeetTakeYd of the
    // clamped receiver point (the receiver takes the Son from there) OR at the old circle-edge point, whichever comes
    // first. Walk 54: a 2 yd stop at a receiver that keeps moving timed 17 walk-outs out with the shot landed (4 in
    // walk 53), the puller back inside the circle 7-21 yd from the receiver.
    inline bool WalkoutMeetArrived(bool hasMeet, float px, float py, const BossAnchorCircle& tank, float botX,
                                   float botY)
    {
        if (!hasMeet)
            return WalkoutBackArrived(tank, botX, botY);
        float const dx = botX - px;
        float const dy = botY - py;
        return std::sqrt(dx * dx + dy * dy) <= kSeekMeetTakeYd || WalkoutBackArrived(tank, botX, botY);
    }

    // The take edge (DungeonRunSeekTakeEdge). The bots a Son hits stand clamped on the raid circle's edge, so the Son
    // stands at 31-45 yd and the old take (refuse any Son outside the raid circle) refused 56 % of its rows.
    // The victim may stand this far past the raid circle and still count as inside it.
    constexpr float kSeekTakeEdgeSlackYd = 2.0f;

    // The exemption is bounded (Codex round 1): Attack() walks to melee, so a Son far past the circle, or one that
    // is not really on its inside victim, must not be chased (it leads the tank off the platform, under Hakkar's evade
    // line). The Son must stand within kSeekTakeEdgeMaxPastYd of the raid circle (walk 54: p75 was 45 yd from the
    // spawn with the circle at 30; 38 covers most real cases) AND within kSeekTakeEdgeOnVictimYd of that victim.
    constexpr float kSeekTakeEdgeMaxPastYd = 8.0f;
    constexpr float kSeekTakeEdgeOnVictimYd = 10.0f;

    // What the take edge decides for a Son: `Allowed` (take it, as today for a Son inside the circle), `Refused` (today's
    // `chase_refused`) or `Far` (the victim is inside and the floor is the platform's, but the Son is too far from the
    // circle or from its victim: logged `edge_far`, and refused like Refused).
    enum class SeekTakeEdge
    {
        Allowed,
        Refused,
        Far,
    };

    // The pure verdict. A Son inside the raid circle is never refused. Outside, it is allowed only when ALL hold: the
    // Son's victim stands inside the raid circle (plus kSeekTakeEdgeSlackYd), the Son's floor is the platform's (z at
    // or above kSeekWalkoutMinStandZ, above Hakkar's 45.8 evade line), the Son is within kSeekTakeEdgeMaxPastYd of the
    // circle (`sonNear`) and within kSeekTakeEdgeOnVictimYd of its victim (`sonOnVictim`). The last two failing alone
    // is `Far`.
    inline SeekTakeEdge SeekTakeEdgeVerdict(bool sonInside, bool victimInside, bool sonZOk, bool sonNear,
                                            bool sonOnVictim)
    {
        if (sonInside)
            return SeekTakeEdge::Allowed;
        if (!victimInside || !sonZOk)
            return SeekTakeEdge::Refused;
        if (!sonNear || !sonOnVictim)
            return SeekTakeEdge::Far;
        return SeekTakeEdge::Allowed;
    }

    // The same with the geometry: `raid` is the raid circle (everyone's, not the tanks'), the Son at (sx, sy, sz), its
    // victim at (vx, vy) when `hasVictim`. All distances are 2D from the circle's centre, as BossAnchorRefusesChase
    // measures. The VICTIM's position is measured against the circle plus the slack; the Son's own against the circle
    // itself, and against radius + kSeekTakeEdgeMaxPastYd for the cap.
    inline SeekTakeEdge SeekTakeEdgeCheck(const BossAnchorCircle& raid, float sx, float sy, float sz, bool hasVictim,
                                          float vx, float vy)
    {
        BossAnchorCircle wide = raid;
        wide.radius += kSeekTakeEdgeSlackYd;
        float const sonFromCentre = BossAnchorDistance2d(raid, sx, sy);
        float const dvx = sx - vx;
        float const dvy = sy - vy;
        return SeekTakeEdgeVerdict(InsideAnchor(raid, sx, sy), hasVictim && InsideAnchor(wide, vx, vy),
                                   sz >= kSeekWalkoutMinStandZ,
                                   sonFromCentre <= raid.radius + kSeekTakeEdgeMaxPastYd,
                                   hasVictim && std::sqrt(dvx * dvx + dvy * dvy) <= kSeekTakeEdgeOnVictimYd);
    }

    inline bool SeekTakeEdgeRefuses(const BossAnchorCircle& raid, float sx, float sy, float sz, bool hasVictim,
                                    float vx, float vy)
    {
        return SeekTakeEdgeCheck(raid, sx, sy, sz, hasVictim, vx, vy) != SeekTakeEdge::Allowed;
    }

    // Whether the receiver takes the pulled Son now: it is on the receiver, or within the take reach (Attack closes
    // the last yards to the taunt's melee range).
    inline bool SeekMeetTakeDue(float sonToReceiverYd, bool sonOnReceiver)
    {
        return sonOnReceiver || sonToReceiverYd <= kSeekMeetTakeYd;
    }

    // Whether the receiver keeps waiting for the Son: only while the Son's victim is the walk-out puller, the returner or
    // nobody (0). A Son on anyone else (a DPS or a healer peeled it) is the take branches' business, and the meet closes
    // as `peeled`.
    inline bool MeetReceiverShouldWait(std::uint64_t sonVictim, std::uint64_t pullerGuid, std::uint64_t returnerGuid)
    {
        if (sonVictim == 0)
            return true;
        return (pullerGuid != 0 && sonVictim == pullerGuid) || (returnerGuid != 0 && sonVictim == returnerGuid);
    }

    // Whether a Son's victim is the open meet's walker (the puller that brought it, or the returner): such a Son is on
    // its way to the receiver, not camped on a non-tank, so the non-tank clocks leave it alone.
    inline bool SeekMeetWalkerHasSon(std::uint64_t victim, std::uint64_t pullerGuid, std::uint64_t returnerGuid)
    {
        return victim != 0 && ((pullerGuid != 0 && victim == pullerGuid) || (returnerGuid != 0 && victim == returnerGuid));
    }

    // Why the receiver can no longer wait: charmed (Cause Insanity or any charm), Hakkar's victim, or holding another
    // Son. Null when it still can. The meet closes with this code and the puller falls back to the circle-edge point.
    inline const char* MeetReceiverLostWhy(bool charmed, bool bossVictim, bool holdsOtherSon)
    {
        if (charmed)
            return "receiver_charmed";
        if (bossVictim)
            return "receiver_victim";
        if (holdsOtherSon)
            return "receiver_busy";
        return nullptr;
    }

    // Whether the meet still runs: a receiver is named and the shot landed less than kSeekMeetMaxMs ago.
    inline bool SeekMeetOpen(std::uint64_t receiver, std::uint32_t meetMs, std::uint32_t nowMs)
    {
        return receiver != 0 && nowMs - meetMs < kSeekMeetMaxMs;
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
        // The cycle each walked Son was walked in, in walkedSons' order: a Son walked in an earlier cycle is free
        // again (WalkoutForgetEarlierCycles), so the fight keeps walking a Son out every cycle.
        std::vector<std::pair<std::uint64_t, std::uint32_t>> walkedAt;
        // The stand the task walks to (the platform search), and its refused moves in a row.
        float standX = 0.0f, standY = 0.0f, standZ = 0.0f;
        std::uint32_t standMs = 0;       // the stand's last re-plan
        std::uint32_t noStandSinceMs = 0; // the re-plans have found no stand since then (0: they found one)
        std::uint32_t atStandSinceMs = 0; // the puller stood at its stand since then, unshot (0: not yet)
        std::uint32_t pullCastMs = 0;     // the walk-out focus's pull cast went out then (0: not this task)
        // The in-shot stall clock (DungeonRunSeekWalkoutFaceCast): the time the puller has stood in shot position this
        // task, and the previous read's time when that read was in shot position (0: it was not).
        std::uint32_t inShotMs = 0;
        std::uint32_t lastInShotMs = 0;
        std::uint32_t firstInShotMs = 0; // the task's first in-shot read (the backstop's start; 0: none yet)
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
        // The pulled Son's meet with the tank that waits at the walk's end: the receiver, the Son, when the shot landed
        // and the puller that brought it. WalkoutFinish and WalkoutReturnDone leave them: the meet outlives `back`.
        std::uint64_t meetReceiver = 0;
        std::uint64_t meetSon = 0;
        std::uint32_t meetMs = 0;
        std::uint64_t meetPuller = 0;
    };

    inline void WalkoutMeetClear(SeekWalkoutState& s)
    {
        s.meetReceiver = 0;
        s.meetSon = 0;
        s.meetMs = 0;
        s.meetPuller = 0;
    }

    // The shared meet's compare-and-clear: every reading bot holds a local copy of the meet and may find it over, but
    // only the caller whose copy still matches the shared one (same receiver, same Son, same shot time) clears it, and
    // only that caller logs the terminal row. A stale copy, or a meet already cleared or replaced, returns false.
    inline bool WalkoutMeetCloseIf(SeekWalkoutState& s, std::uint64_t receiver, std::uint64_t son,
                                   std::uint32_t meetMs)
    {
        if (receiver == 0 || s.meetReceiver != receiver || s.meetSon != son || s.meetMs != meetMs)
            return false;
        WalkoutMeetClear(s);
        return true;
    }

    inline bool WalkoutActive(const SeekWalkoutState& s)
    {
        return s.puller != 0;
    }

    // The running task's record for this bot in its own instance: the instance's record, a task running, this bot its
    // puller; nullptr otherwise (another instance's record never matches). `Records` maps an instance id to a record
    // with a `state` (SeekWalkoutState).
    template <typename Records>
    auto WalkoutPullerRecordIn(Records& records, std::uint32_t instanceId, std::uint64_t bot)
        -> decltype(&records.begin()->second)
    {
        auto const it = records.find(instanceId);
        if (it == records.end() || !WalkoutActive(it->second.state) || it->second.state.puller != bot)
            return nullptr;
        return &it->second;
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

    // One record's marks for the two global counts: busy (a task or a walk back runs: the anchor's exemption reads it)
    // and running (a task runs: the walk-out focus reads it, a walk back holds nothing).
    struct WalkoutBusyMarks
    {
        bool busy = false;
        bool running = false;
    };

    inline WalkoutBusyMarks WalkoutMarksOf(const SeekWalkoutState& s)
    {
        WalkoutBusyMarks m;
        m.busy = WalkoutBusy(s);
        m.running = WalkoutActive(s);
        return m;
    }

    // A count's step for one record's change: +1 when the mark appears, -1 when it goes, 0 otherwise.
    inline int WalkoutCountStep(bool before, bool now)
    {
        return before == now ? 0 : (now ? 1 : -1);
    }

    // The seek cycle still takes a walk-out: not spent by one, and fewer than the rules' walk-outs started in it (with
    // the defaults, one per cycle, no second start).
    inline bool WalkoutCycleOpen(const SeekWalkoutState& s, std::uint32_t cycleNo,
                                 const WalkoutCycleRules& rules = WalkoutCycleRules())
    {
        if (s.cycleSpent && s.lastCycle == cycleNo)
            return false;
        return s.countedCycle != cycleNo || s.cycleStarts < rules.maxPerCycle;
    }

    // A new task: none running, the cycle open (WalkoutCycleOpen), and never twice for one living Son (a walked Son
    // that died is forgotten, WalkoutForgetGoneSons, so its respawn may be walked to again).
    inline bool WalkoutMayStart(const SeekWalkoutState& s, std::uint32_t cycleNo, std::uint64_t son,
                                const WalkoutCycleRules& rules = WalkoutCycleRules())
    {
        if (WalkoutBusy(s) || son == 0)
            return false;
        if (!WalkoutCycleOpen(s, cycleNo, rules))
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
        s.atStandSinceMs = 0;
        s.pullCastMs = 0;
        s.inShotMs = 0;
        s.lastInShotMs = 0;
        s.firstInShotMs = 0;
        s.cycleSpent = true;
        s.lastCycle = cycleNo;
        s.walkedSons.push_back(son);
        s.walkedAt.emplace_back(son, cycleNo);
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

    // A retry (a second walk-out in the cycle) starts only while the siphon is still more than the rules' retry floor
    // away, read when it starts against the deadline stored by the cycle's first walk-out: the failed puller's walk
    // back (up to kSeekWalkoutReturnMs) runs between the end that reopened the cycle and this start.
    inline bool WalkoutRetryStartInTime(const SeekWalkoutState& s, std::uint32_t cycleNo, std::uint32_t nowMs,
                                        const WalkoutCycleRules& rules = WalkoutCycleRules())
    {
        if (s.countedCycle != cycleNo || s.cycleStarts < 1)
            return true;
        return static_cast<std::int32_t>(s.siphonDueMs - nowMs) > static_cast<std::int32_t>(rules.retryMinMs);
    }

    // A new task for this puller: WalkoutMayStart, the bot not sitting out its exclusion (a scan cached before the
    // exclusion may still name it), a retry still in time (WalkoutRetryStartInTime), and the next Blood Siphon due in
    // rules.startLeadMs or less (`dueInMs`, 0 once due): the dose window's whole length, so the Son is back
    // and burned before the siphon (the 12 s non-tank burn, SeekAuraPolicy.h kSeekNonTankBurnMs, keeps it off the
    // healers meanwhile). A seek_pull row's `firstStartMinMs` also refuses any start with less time left.
    inline bool WalkoutMayStartFor(const SeekWalkoutState& s, std::uint32_t cycleNo, std::uint64_t son,
                                   std::uint64_t bot, std::uint32_t nowMs, std::uint32_t dueInMs,
                                   const WalkoutCycleRules& rules = WalkoutCycleRules())
    {
        return dueInMs <= rules.startLeadMs &&
            (rules.firstStartMinMs == 0 || dueInMs >= rules.firstStartMinMs) &&
            WalkoutMayStart(s, cycleNo, son, rules) &&
            !WalkoutExcluded(s, bot, nowMs) && WalkoutRetryStartInTime(s, cycleNo, nowMs, rules);
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

    // One re-plan: a found stand replaces the old one and restarts the no-stand clock (a stand that moved more than
    // kSeekWalkoutStandArriveYd also restarts the shot-stall clock, unless `keepClock`: the puller is already in shot
    // position and a patrolling Son keeps moving the stand); none keeps the old stand and runs the clock. True:
    // kSeekWalkoutNoStandMs straight without any stand, the task aborts ("no stand").
    inline bool WalkoutNoteStand(SeekWalkoutState& s, bool found, float x, float y, float z, std::uint32_t nowMs,
                                 bool keepClock = false)
    {
        s.standMs = nowMs;
        if (found)
        {
            float const dx = x - s.standX;
            float const dy = y - s.standY;
            bool const moved = dx * dx + dy * dy >
                kSeekWalkoutStandArriveYd * kSeekWalkoutStandArriveYd;
            s.standX = x;
            s.standY = y;
            s.standZ = z;
            s.noStandSinceMs = 0;
            if (moved && !keepClock)
                s.atStandSinceMs = 0;
            return false;
        }
        if (s.noStandSinceMs == 0)
            s.noStandSinceMs = nowMs;
        return nowMs - s.noStandSinceMs >= kSeekWalkoutNoStandMs;
    }

    inline void WalkoutNoteShot(SeekWalkoutState& s)
    {
        if (WalkoutActive(s))
        {
            s.shot = true;
            s.atStandSinceMs = 0;
            s.inShotMs = 0;
            s.lastInShotMs = 0;
            s.firstInShotMs = 0;
        }
    }

    // The puller reached its stand without a shot: the stall clock starts once and keeps its first tick.
    inline void WalkoutNoteAtStand(SeekWalkoutState& s, std::uint32_t nowMs)
    {
        if (WalkoutActive(s) && s.atStandSinceMs == 0)
            s.atStandSinceMs = nowMs;
    }

    // A shot that will not come: the puller has stood at its stand kSeekWalkoutShotStallMs without landing it (a
    // blocked LoS, a cast the generic AI keeps interrupting). The task aborts unspent ("no shot"), so the Son is free
    // and the next bot retries.
    inline bool WalkoutShotStalled(const SeekWalkoutState& s, std::uint32_t nowMs)
    {
        return WalkoutActive(s) && !s.shot && s.atStandSinceMs != 0 &&
            nowMs - s.atStandSinceMs >= kSeekWalkoutShotStallMs;
    }

    // The in-shot stall clock (DungeonRunSeekWalkoutFaceCast, walk 56: the Sons patrol under the ledge, out of sight or
    // range for seconds, and the wall-time clock aborted pullers that had stood in shot position only part of the 6 s).
    // In shot position: the Son in shot range and sight, the puller on the stand's floor (z >= kSeekWalkoutMinStandZ)
    // and not moving.
    inline bool WalkoutInShotPosition(bool inRange, bool los, bool onStandFloor, bool moving)
    {
        return inRange && los && onStandFloor && !moving;
    }

    // One gap between two in-shot reads counts in full up to this (the GCD's 1.5 s fail-with-delay plus the react
    // delay); the cap only guards against a clock jump (review M1: a 250 ms cap turned slow ticks into 25 s timeouts).
    constexpr std::uint32_t kSeekWalkoutInShotStepMs = 2000;
    // The wall-time backstop: this long after the puller first stood in shot position this task the no-shot abort is
    // due whatever the count (a walk-out tick that stops running counts nothing).
    constexpr std::uint32_t kSeekWalkoutInShotBackstopMs = 12000;

    // One read of the puller's position: in shot position, the gap since the previous read counts when that read was in
    // shot position too (at most kSeekWalkoutInShotStepMs), and the first in-shot read of the task starts the backstop;
    // out of it, nothing counts and the next in-shot read starts a new stretch. Only a running, unshot task counts.
    inline void WalkoutNoteInShot(SeekWalkoutState& s, bool inShot, std::uint32_t nowMs)
    {
        if (!WalkoutActive(s) || s.shot)
            return;
        std::uint32_t const stamp = nowMs == 0 ? 1 : nowMs;
        if (inShot && s.lastInShotMs != 0)
            s.inShotMs += std::min<std::uint32_t>(nowMs - s.lastInShotMs, kSeekWalkoutInShotStepMs);
        if (inShot && s.firstInShotMs == 0)
            s.firstInShotMs = stamp;
        s.lastInShotMs = inShot ? stamp : 0;
    }

    // Whether the 6 s no-shot stall is due: with the in-shot clock, kSeekWalkoutShotStallMs of time in shot position, or
    // kSeekWalkoutInShotBackstopMs since the task's first in-shot read; without it, WalkoutShotStalled's wall time since
    // the arrival at the stand.
    inline bool WalkoutStallDue(const SeekWalkoutState& s, std::uint32_t nowMs, bool inShotClock)
    {
        if (!inShotClock)
            return WalkoutShotStalled(s, nowMs);
        if (!WalkoutActive(s) || s.shot)
            return false;
        return s.inShotMs >= kSeekWalkoutShotStallMs ||
            (s.firstInShotMs != 0 && nowMs - s.firstInShotMs >= kSeekWalkoutInShotBackstopMs);
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

    // A failed walk-out (a timeout, no stand, a refused cast or move) leaves the cycle open only while the siphon is
    // more than kSeekWalkoutRetryMinMs away (a later Son is not back and burned before it) and fewer than
    // kSeekWalkoutMaxPerCycle (the rules') started in the cycle: with one per cycle, never.
    inline bool WalkoutRetryAllowed(const SeekWalkoutState& s, std::uint32_t nowMs,
                                    const WalkoutCycleRules& rules = WalkoutCycleRules())
    {
        return s.cycleStarts < rules.maxPerCycle &&
            static_cast<std::int32_t>(s.siphonDueMs - nowMs) > static_cast<std::int32_t>(rules.retryMinMs);
    }

    // The task ends: a Back end is inside already; a timeout or an abort leaves the puller outside, so its walk back
    // starts (exempt for kSeekWalkoutReturnMs at most). A timeout undoes the task's cycle mark when a retry is allowed
    // (its Son stays walked, so the retry goes to another one); a seek_pull row reopens on an abort too, but never
    // after the shot landed (that walk-out did happen). With the defaults that is never. True: the cycle was reopened.
    inline bool WalkoutFinish(SeekWalkoutState& s, WalkoutEnd end, std::uint32_t nowMs,
                              const WalkoutCycleRules& rules = WalkoutCycleRules())
    {
        bool reopened = false;
        if ((end == WalkoutEnd::Timeout || (rules.reopenOnAbort && end == WalkoutEnd::Abort)) &&
            WalkoutActive(s) && !s.shot && WalkoutRetryAllowed(s, nowMs, rules))
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
        s.atStandSinceMs = 0;
        s.pullCastMs = 0;
        return reopened;
    }

    // Whether an abort's reason frees its Son for the cycle's retry: the puller's own ends (dead, charmed, insanity,
    // victim, threat, gone, a failed cast or move, a stalled shot) leave the Son where it stands, so the retry may take
    // another Son or the same one from another bot. A Son-side end ("no stand", "son gone") and a timeout keep it
    // walked this cycle.
    inline bool WalkoutEndFreesSon(const char* reason)
    {
        if (!reason)
            return false;
        for (const char* free : {"threat", "victim", "dead", "charmed", "insanity", "gone", "cast failed",
                                 "move failed", "no shot"})
            if (std::strcmp(reason, free) == 0)
                return true;
        return false;
    }

    // One walked Son leaves the list, and its cycle record with it.
    inline void WalkoutEraseWalked(SeekWalkoutState& s, std::uint64_t son)
    {
        std::vector<std::uint64_t>::iterator const it = std::find(s.walkedSons.begin(), s.walkedSons.end(), son);
        if (it != s.walkedSons.end())
            s.walkedSons.erase(it);
        for (auto at = s.walkedAt.begin(); at != s.walkedAt.end(); ++at)
        {
            if (at->first == son)
            {
                s.walkedAt.erase(at);
                break;
            }
        }
    }

    // A seek_pull fight walks each cycle its own Sons: every Son walked in a cycle other than `cycleNo` is free again,
    // except the running task's own (it is still being walked to). Without it, two failed walk-outs leave both Sons
    // in the list for the rest of the fight (walk 51: 30 cycles with no walk-out at all).
    inline void WalkoutForgetEarlierCycles(SeekWalkoutState& s, std::uint32_t cycleNo)
    {
        std::vector<std::uint64_t> keptSons;
        std::vector<std::pair<std::uint64_t, std::uint32_t>> keptAt;
        keptSons.reserve(s.walkedSons.size());
        keptAt.reserve(s.walkedAt.size());
        for (std::uint64_t const son : s.walkedSons)
        {
            std::uint32_t cycle = 0;
            bool found = false;
            for (const std::pair<std::uint64_t, std::uint32_t>& at : s.walkedAt)
            {
                if (at.first == son)
                {
                    cycle = at.second;
                    found = true;
                    break;
                }
            }
            if (son == s.son || !found || cycle == cycleNo)
            {
                keptSons.push_back(son);
                if (found)
                    keptAt.emplace_back(son, cycle);
            }
        }
        s.walkedSons.swap(keptSons);
        s.walkedAt.swap(keptAt);
    }

    // No pull happened (the cast or the moves failed, a stalled shot, or no stand): abort with the Son free again
    // (`freeSon`; a seek_pull row's "no stand" keeps it walked this cycle), and the cycle too when a retry is allowed
    // (WalkoutRetryAllowed).
    inline void WalkoutAbortUnspent(SeekWalkoutState& s, std::uint32_t nowMs,
                                    const WalkoutCycleRules& rules = WalkoutCycleRules(), bool freeSon = true)
    {
        if (!WalkoutActive(s))
            return;
        if (freeSon)
            WalkoutEraseWalked(s, s.son);
        if (WalkoutRetryAllowed(s, nowMs, rules))
        {
            s.cycleSpent = s.prevCycleSpent;
            s.lastCycle = s.prevLastCycle;
        }
        WalkoutFinish(s, WalkoutEnd::Abort, nowMs, rules);
    }

    // The shot could not be cast at the stand: abort unspent, and the puller sits out the picks like a move fail, so the
    // retry goes to another bot (the cached scan would hand it the same puller, Son and stand, to fail again).
    inline void WalkoutCastFailed(SeekWalkoutState& s, std::uint64_t bot, std::uint32_t nowMs,
                                  const WalkoutCycleRules& rules = WalkoutCycleRules())
    {
        if (!WalkoutActive(s))
            return;
        WalkoutAbortUnspent(s, nowMs, rules);
        WalkoutExclude(s, bot, nowMs);
    }

    // Walked Sons that died (or left the map) are forgotten, the running task's own Son excepted: the two Sons near the
    // platform respawn after 60 s with the same guid, and the respawn is the next walk-out's Son.
    template <typename IsGone>
    inline void WalkoutForgetGoneSons(SeekWalkoutState& s, IsGone isGone)
    {
        for (std::size_t i = s.walkedSons.size(); i-- > 0;)
        {
            std::uint64_t const son = s.walkedSons[i];
            if (son != s.son && isGone(son))
                WalkoutEraseWalked(s, son);
        }
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
