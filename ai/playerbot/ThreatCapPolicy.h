#pragma once

// The threat reference a damage dealer's threat percentage is measured against (ThreatValue::GetTankThreat): the
// highest tank's threat. On a boss whose tactic data asks for a successor (ai_boss_tactic kind `threat_successor`,
// Hakkar) the second tank has to stay second so the boss turns to it on a threat reset: there the reference is the
// highest threat among the assigned off-tanks on the target, as soon as that is above a tiny absolute floor while an
// off-tank holds the boss (while none does, the old half share of the main tank's threat). The
// trigger is data, never the creature's taunt immunity: most raid bosses in this world data are taunt-immune and must
// keep the highest tank, or a zero reference would read as "the tank has no threat" and veto every damage action.
// Tested in tools/test_threat_cap_policy.cpp; the caller resolves the raid's tank order into plain numbers, so this
// header stays free of core includes.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace ai
{
    // The high-threat line, as a percentage of the reference tank's threat (ThreatValue::Calculate): the damage
    // back-off vetoes single-target attacks at or above it. Every target uses 80 %, successor bosses included.
    constexpr std::uint8_t kThreatHighLinePct = 80;

    // Whether an action skips the damage threat back-off (ThreatMultiplier) whatever the bot's threat on its target.
    // Heals always do: a heal pulls no mob off the tank. A party cure or dispel (CurePartyMemberAction: dispel magic,
    // cure poison/disease, remove curse, cleanse, purify on a party member) is a SINGLE-threat CastSpellAction and was
    // vetoed at the high line like a nuke, so a healer high on Jin'do's threat list left his Hex on the tank for its
    // full 5 s (walk 56: 50 of 91 tank Hexes never cured, `dispel magic on party` result I in 10 of 12 frames). With
    // AiPlayerbot.DungeonRunCureIgnoresThreat on, a party cure is exempt like a heal; off, as before.
    inline bool ThreatCapExempt(bool heal, bool partyCure, bool cureIgnoresThreat)
    {
        return heal || (partyCure && cureIgnoresThreat);
    }

    // The high-threat trigger's projection escape (HighThreatTrigger::IsActive): at or over the high line a damage dealer
    // is let through while its threat, rising at `delta` per second, projects under the reference tank's threat in
    // kHighThreatProjectionSeconds. On a `threat_successor` boss that let the damage dealers ride just under a slowly
    // climbing non-holding tank (walk 56: every tank-inherited Hex had the top damage dealer at 0.89-0.99 of the
    // successor, every DPS-inherited one at 1.00 or more). With AiPlayerbot.DungeonRunThreatSuccessorStrict on there is
    // no escape on a boss whose `threat_successor` row carries `strict` (StrictSuccessorTarget); everywhere else, a plain
    // successor row included, and with the key off, today's escape.
    constexpr float kHighThreatProjectionSeconds = 5.0f;

    // Whether the strict successor line applies to a target: the key is on AND the target's `threat_successor` row carries
    // the mode word `strict` (BossTacticTable::HasStrictThreatSuccessor). Strict was built for Jin'do's Hex; on Hakkar it
    // held the damage dealers' white damage while he healed back (walk 57: 0/58 kills, against 4/41 in walk 56; Strict cost
    // about 13 of the 18 points the damage on him fell), so the boss data opts in
    // per row, never a boss constant. A successor row without the word keeps the plain cap with the key on or off: the
    // projection escape, no white-damage latch, the old multiplier exemption. The one gate of the whole Strict path (the
    // escape, the white-stop trigger, the multiplier hold).
    inline bool StrictSuccessorTarget(bool strictKey, bool rowOptIn)
    {
        return strictKey && rowOptIn;
    }

    inline bool HighThreatProjectionEscapes(bool successorTarget, bool strictKey, float current, float delta, float tank)
    {
        if (successorTarget && strictKey)
            return false;
        return delta > 0.0f && current + delta * kHighThreatProjectionSeconds < tank;
    }

    // The white-damage stop on a boss whose `threat_successor` row carries `strict` (AiPlayerbot.DungeonRunThreatSuccessorStrict,
    // StrictSuccessorTarget). The melee
    // auto-attack and the wand's Shoot carry no threat type, and every running swing or auto-repeat (Auto Shot included)
    // keeps going without passing the multiplier again, so the 80 % veto never bound them and a damage dealer kept
    // climbing past the line on white damage alone. They stop at kSuccessorWhiteStopPct of the reference and may start
    // again only under kSuccessorWhiteResumePct, so a read that wobbles around the stop line does not start and stop the
    // auto-repeat every tick. Auto Shot is a SINGLE-threat action, so its restart also waits for the 80 % veto: a hunter
    // resumes under 80 %, not 85 %. Specials keep the 80 % line. The lines are high on purpose: 405801e86 removed a 65 %
    // stop that cost a fifth of the raid's damage; the aim is damage dealers at 80-92 % of the successor instead of
    // 89-100 %+. DoT ticks already on the boss cannot be stopped.
    constexpr std::uint8_t kSuccessorWhiteStopPct = 92;
    constexpr std::uint8_t kSuccessorWhiteResumePct = 85;

    // One bot's white-damage stop, kept across ticks. Every read advances it: a tank, or a target without a
    // `threat_successor` row carrying `strict` (a plain successor row included), clears it, so the next boss starts from the stop line again. `refZero`: the successor
    // reference reads exactly 0 (a tank Hex just wiped it; ThreatPctOf then reads 100 % for everyone). White damage is
    // not stopped then and the latch clears: the 0-reference hold already vetoes the specials, and stopping the white
    // damage too would cost the whole raid's damage for every Hex window.
    struct SuccessorWhiteLatch
    {
        bool stopped = false;

        bool Update(bool successorTarget, bool isTank, std::uint8_t pct, bool refZero = false)
        {
            stopped = successorTarget && !isTank && !refZero &&
                pct >= (stopped ? kSuccessorWhiteResumePct : kSuccessorWhiteStopPct);
            return stopped;
        }
    };

    // Whether the bot's white damage runs on the successor target itself: a melee swing whose victim is that target, or an
    // auto-repeat (wand Shoot, Auto Shot) aimed at it. A swing an action moved onto a Shade, an add or a totem is left
    // alone: the latch reads the threat on the successor, not on that unit.
    inline bool SuccessorWhiteOnTarget(bool meleeAttacking, bool victimIsTarget, bool autoRepeat, bool autoRepeatOnTarget)
    {
        return (meleeAttacking && victimIsTarget) || (autoRepeat && autoRepeatOnTarget);
    }

    // What the white stop cancels, per attack mode: the melee swing only when its victim is the successor, the auto-repeat
    // (wand Shoot, Auto Shot) only when its target is the successor. A bot meleeing the boss while its auto-repeat is on an
    // add keeps the shot, and the reverse keeps the swing: only the white damage that climbs the successor's threat stops.
    struct SuccessorWhiteStops
    {
        bool melee = false;
        bool autoRepeat = false;
    };

    inline SuccessorWhiteStops SuccessorWhiteStopsOf(bool stopNow, bool meleeOnSuccessor, bool autoRepeatOnSuccessor)
    {
        SuccessorWhiteStops stops;
        stops.melee = stopNow && meleeOnSuccessor;
        stops.autoRepeat = stopNow && autoRepeatOnSuccessor;
        return stops;
    }

    // The trigger's step (SuccessorWhiteCapTrigger::IsActive), run in the engine's trigger pass at the start of every
    // decision cycle: advance the latch and say whether the bot's running white damage must be stopped right now. The
    // trigger stops it on the spot instead of queueing an action, so no higher-relevance action picked in the same cycle
    // (a heal, a move, an emergency) lets the swing or the auto-repeat run on past the line.
    inline bool SuccessorWhiteStopNow(SuccessorWhiteLatch& latch, bool successorTarget, bool isTank, std::uint8_t pct,
                                      bool refZero, bool whiteRunning)
    {
        return latch.Update(successorTarget, isTank, pct, refZero) && whiteRunning;
    }

    // The percentage a damage dealer's threat reads against the reference tank's (ThreatValue::Calculate), saturated to
    // the uint8 the threat values carry. botThreat * 100 / maxThreat above 255 used to wrap (300 % read as 44 %), and
    // the cap then let the attack through exactly where the margin matters most. `maxThreat` <= 0 is the caller's "no
    // reference" answer, kept at 0. The float-to-uint8 cast truncates exactly as the callers' implicit one did.
    inline std::uint8_t ThreatPctSaturated(float botThreat, float maxThreat)
    {
        if (maxThreat <= 0.0f)
            return 0;
        const float pct = botThreat * 100.0f / maxThreat;
        if (pct >= 255.0f)
            return 255;
        if (pct <= 0.0f)
            return 0;
        return static_cast<std::uint8_t>(pct);
    }

    // The relative threat a damage dealer reads against `maxThreat` (ThreatValue::Calculate after its friendly/player/group
    // checks). < 0: no reference, no cap (0). A reference of exactly 0 on a target in combat that is not fleeing reads 100:
    // the damage dealers are held, never uncapped. That is what a successor whose threat a Hex just wiped gives under
    // AiPlayerbot.DungeonRunThreatSuccessorNotHolder; with the key off nothing new reaches it.
    inline std::uint8_t ThreatPctOf(float botThreat, float maxThreat, bool targetInCombat, bool fleeing)
    {
        if (maxThreat < 0)
            return 0;
        if (targetInCombat && maxThreat == 0 && !fleeing)
            return 100;
        if (maxThreat > 0 && fleeing)
            return 0;
        return ThreatPctSaturated(botThreat, maxThreat);
    }

    // One tank on the target as the caller resolved it from the raid's tank order (GetRaidTankRoles): index 0 is the
    // main tank, every later one an assigned off-tank.
    struct ThreatCapTank
    {
        float threat = 0.0f;
        bool offTank = false;
        bool onBoss = false; // this tank hits the boss (its victim is the boss); NOT "the boss hits this tank"
        bool holdsBoss = false; // the boss hits this tank (the boss's victim is this tank): it holds the boss right now
        bool cursed = false; // wears Delusions of Jin'do, marked only under AiPlayerbot.DungeonRunThreatSuccessorKeepsAway
    };

    // The off-tank floor, as an absolute amount rather than a share of the highest tank's threat: the reference is
    // whichever of a 1 % share or this fixed amount is larger. A share-only floor (the 50 % this policy once used) sat
    // the cap at the main tank whenever the off-tank dealt a fraction of the main tank's damage, and Hakkar's Cause
    // Insanity then deleted exactly that tank's threat and handed him to the top damage dealer (walk 41: 43 % of the
    // boss's post-charm victims were not tanks). The floor only keeps a barely-engaged or reset off-tank (dead, or its
    // threat just deleted) from capping the damage dealers at almost nothing.
    constexpr float kThreatCapOffTankMinShare = 0.01f;
    constexpr float kThreatCapOffTankMinThreat = 100.0f;

    // The floor while no assigned off-tank has the boss as its victim (Hakkar's off-tanks are away on Sons): their
    // threat is only what an earlier hold left, and a floor that low would freeze the damage dealers while the main
    // tank holds the boss, so the old share of the highest tank's threat applies.
    constexpr float kThreatCapOffTankAwayShare = 0.5f;

    // Whether a tank counts as on the boss for the successor pick: it hits the boss, or it wears Jin'do's Hex. A Hexed tank
    // is polymorphed for about 5 s and cannot hit, yet it is the wiped successor: if it dropped out, the pick would fall
    // back to the new holder's own threat for those seconds and the damage dealers could climb to 80 % of it.
    inline bool ThreatCapOnBoss(bool hitsBoss, bool wearsHex)
    {
        return hitsBoss || wearsHex;
    }

    // AiPlayerbot.DungeonRunThreatSuccessorKeepsAway: a tank wearing Delusions of Jin'do (24306) stands on its Shade by
    // design while its Jin'do threat stays (walk 56: a cursed main tank on a Shade dropped out of the pick and the
    // reference fell back to the holder). The caller marks such a tank `cursed`; it then counts as on the boss, but only
    // while its threat is above 0 and at least kThreatCapOffTankAwayShare of the highest tank's: Delusions is never cured
    // and the cursed tank's threat does not climb on its Shade, so a cursed tank with little or no Jin'do threat would
    // otherwise sit as a 0 reference and hold every damage dealer for the whole curse. Off (no tank marked), nothing changes.
    // A charmed tank is never kept: the caller leaves it off the list as before.
    constexpr std::uint32_t kJindoDelusionsSpellId = 24306;

    inline bool ThreatCapCursedCountsOnBoss(float threat, float highest)
    {
        return threat > 0.0f && threat >= highest * kThreatCapOffTankAwayShare;
    }

    inline void ThreatCapCursedOnBoss(std::vector<ThreatCapTank>& tanks)
    {
        float highest = 0.0f;
        for (const ThreatCapTank& tank : tanks)
            highest = std::max(highest, tank.threat);
        for (ThreatCapTank& tank : tanks)
            if (tank.cursed && !tank.onBoss && ThreatCapCursedCountsOnBoss(tank.threat, highest))
                tank.onBoss = true;
    }

    // The reference and which kind of tank it is: `offTank` is the `threat_successor` cap standing on an assigned
    // off-tank, false when the highest tank is the reference.
    struct ThreatCapPick
    {
        float threat = -1.0f;
        bool offTank = false;
        // Who holds the boss among the tanks on the list: the log row's `holder=` word (a damage dealer or nobody
        // holding reads `none`). Set whatever the key says.
        enum class Holder { None, Main, Off };
        Holder holder = Holder::None;
    };

    inline const char* ThreatCapHolderWord(ThreatCapPick::Holder holder)
    {
        return holder == ThreatCapPick::Holder::Main ? "main" : holder == ThreatCapPick::Holder::Off ? "off" : "none";
    }

    // -1 when no tank is on the target (the caller's own "no reference" answer, kept from the previous max scan).
    // Without the successor cap: the highest tank, as always. With it: the highest threat among the off-tanks that are
    // on the target (threat > 0) and stand above the absolute floor (kThreatCapOffTankMinThreat, or 1 % of the highest
    // tank when that is larger), and the highest tank when no off-tank does: a zero reference is never returned while
    // a tank stands, since the caller reads that as "the tank has no threat" and stops all damage.
    //
    // `notHolder` (AiPlayerbot.DungeonRunThreatSuccessorNotHolder, only with the successor cap): the successor is the
    // tank that does NOT hold the boss. The reference is the highest threat among the tanks (main or off) that do not
    // hold the boss (`holdsBoss` false) AND are hitting it (`onBoss`): a tank on a Son (Hakkar's off-tanks), held on a
    // Shade, or away from the boss is no successor. The reference is that tank's real threat, 0 included: the caller
    // (ThreatPctOf) reads a 0 reference on an engaged target as "hold the damage dealers", never as "no cap". When no tank
    // qualifies (every tank holds the boss, is away, dead, out of the world or charmed: the caller leaves the dead and
    // charmed off the list) the pick below stands exactly as with the key off, away share included. With no holder at all
    // (the boss has no victim, or a damage dealer holds it) every tank that hits the boss is a non-holder, so the
    // reference is the highest of them. Off, the code below runs exactly as before.
    inline ThreatCapPick ThreatCapPickReference(const std::vector<ThreatCapTank>& tanks, bool successorCap,
                                                bool notHolder = false)
    {
        ThreatCapPick pick;
        if (tanks.empty())
            return pick;
        float highest = 0.0f;
        for (const ThreatCapTank& tank : tanks)
        {
            highest = std::max(highest, tank.threat);
            if (tank.holdsBoss && pick.holder == ThreatCapPick::Holder::None)
                pick.holder = tank.offTank ? ThreatCapPick::Holder::Off : ThreatCapPick::Holder::Main;
        }
        pick.threat = highest;
        if (!successorCap)
            return pick;
        if (notHolder)
        {
            const ThreatCapTank* best = nullptr;
            for (const ThreatCapTank& tank : tanks)
            {
                if (tank.holdsBoss || !tank.onBoss)
                    continue;
                if (!best || tank.threat > best->threat)
                    best = &tank;
            }
            if (best)
            {
                pick.threat = best->threat;
                pick.offTank = best->offTank;
                return pick;
            }
        }
        bool haveOffTank = false;
        bool offTankOnBoss = false;
        float offTank = 0.0f;
        for (const ThreatCapTank& tank : tanks)
        {
            if (tank.offTank && tank.onBoss)
                offTankOnBoss = true;
            if (!tank.offTank || tank.threat <= 0.0f)
                continue;
            offTank = haveOffTank ? std::max(offTank, tank.threat) : tank.threat;
            haveOffTank = true;
        }
        // An off-tank on the boss is the successor and the absolute floor applies; with every off-tank away the main
        // tank's reference stands unless one still stands above the old half share.
        const float floor = offTankOnBoss ? std::max(highest * kThreatCapOffTankMinShare, kThreatCapOffTankMinThreat)
                                          : highest * kThreatCapOffTankAwayShare;
        if (!haveOffTank || offTank <= floor)
            return pick;
        pick.threat = offTank;
        pick.offTank = true;
        return pick;
    }

    // The Hex snapshot row (BossTacticsHex, ThreatValues.cpp LogHexSnapshot): Jin'do's Hex (17172) lands on the boss's
    // current victim and wipes its threat. The row names who wore it (the main tank, an off-tank or a damage dealer) and
    // the threat of both tanks and the top damage dealer right after, so a walk can see who the boss turned to and why.
    // One row per Hex: the same member does not log again inside kHexSnapshotGapMs (the Hex lasts 5 s, Jin'do recasts
    // no sooner than 20 s).
    constexpr std::uint32_t kJindoHexSpellId = 17172;
    constexpr std::uint32_t kHexSnapshotGapMs = 8000;

    enum class HexHolder { None, Main, Off, Dps };

    // `tankIndex`: the member's place in the raid's tank order (0 main tank, 1.. off-tanks), -1 for anyone else.
    // `hexed`: the member wears the Hex. None: nobody does.
    inline HexHolder HexHolderOf(bool hexed, int tankIndex)
    {
        if (!hexed)
            return HexHolder::None;
        return tankIndex == 0 ? HexHolder::Main : tankIndex > 0 ? HexHolder::Off : HexHolder::Dps;
    }

    inline const char* HexHolderWord(HexHolder holder)
    {
        return holder == HexHolder::Main ? "main" : holder == HexHolder::Off ? "off"
             : holder == HexHolder::Dps ? "dps" : "none";
    }

    // `lastMs` 0: this member never logged a Hex. The unsigned subtraction survives the 32-bit clock wrap.
    inline bool HexSnapshotDue(std::uint32_t lastMs, std::uint32_t nowMs)
    {
        return lastMs == 0 || nowMs - lastMs >= kHexSnapshotGapMs;
    }

    // The member scan behind the Hex row: Hex wipes its wearer's threat and the boss turns to someone else within a
    // tick, so the wearer is found by its aura among the group's members, never by being the boss's current victim.
    // The scan runs at most once per kHexScanGapMs per boss. `tankIndex` as in HexHolderOf (-1: not in the tank order),
    // `lastLoggedMs` is when this member's last Hex row was written (0: never).
    constexpr std::uint32_t kHexScanGapMs = 1000;

    inline bool HexScanDue(std::uint32_t lastScanMs, std::uint32_t nowMs)
    {
        return lastScanMs == 0 || nowMs - lastScanMs >= kHexScanGapMs;
    }

    struct HexMember
    {
        std::uint64_t key = 0;
        int tankIndex = -1;
        std::uint32_t lastLoggedMs = 0;
    };

    // Index of the first wearer in `wearers` (the members that carry the Hex now) whose Hex has no row yet, -1 for none.
    inline int HexWearerToLog(const std::vector<HexMember>& wearers, std::uint32_t nowMs)
    {
        for (std::size_t i = 0; i < wearers.size(); ++i)
            if (HexSnapshotDue(wearers[i].lastLoggedMs, nowMs))
                return static_cast<int>(i);
        return -1;
    }

    // The tank order's index whose threat the row's `ot_threat` shows: the wearer when an off-tank wore the Hex, else the
    // first off-tank (1). The main tank is always `mt_threat`.
    inline int HexOffTankIndex(int wearerTankIndex)
    {
        return wearerTankIndex >= 1 ? wearerTankIndex : 1;
    }

    inline float ThreatCapReference(const std::vector<ThreatCapTank>& tanks, bool successorCap, bool notHolder = false)
    {
        return ThreatCapPickReference(tanks, successorCap, notHolder).threat;
    }

    // The share of today's reference below which a leave counts as collapsing the cap (ThreatCapLeaveCollapses).
    constexpr float kThreatCapCollapseShare = 0.5f;

    // Whether the tank at `selfPos` in `tanks` (the cap's own tank list, ThreatCapTanksOf) leaving the boss would collapse
    // the damage dealers' threat-cap reference (walk 57 F4 review H1/M2): the successor pick (ThreatCapPickReference,
    // successorCap on, the cap's own `notHolder`) is re-run with that tank marked off the boss (its threat stays: it
    // does not vanish when the tank walks off). Today's pick naming no off-tank reference has none to lose: false.
    // Otherwise it collapses when the new pick names no off-tank reference, or one under kThreatCapCollapseShare of
    // today's. The holder (main, an off-tank, nobody) is whatever the list says. A bad `selfPos` never collapses.
    inline bool ThreatCapLeaveCollapses(std::vector<ThreatCapTank> tanks, int selfPos, bool notHolder = false)
    {
        if (selfPos < 0 || static_cast<std::size_t>(selfPos) >= tanks.size())
            return false;
        const ThreatCapPick now = ThreatCapPickReference(tanks, true, notHolder);
        if (!now.offTank)
            return false;
        tanks[static_cast<std::size_t>(selfPos)].onBoss = false;
        const ThreatCapPick after = ThreatCapPickReference(tanks, true, notHolder);
        return !after.offTank || after.threat < now.threat * kThreatCapCollapseShare;
    }
}
