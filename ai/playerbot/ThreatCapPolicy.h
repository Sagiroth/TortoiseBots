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

    // One tank on the target as the caller resolved it from the raid's tank order (GetRaidTankRoles): index 0 is the
    // main tank, every later one an assigned off-tank.
    struct ThreatCapTank
    {
        float threat = 0.0f;
        bool offTank = false;
        bool onBoss = false; // this tank hits the boss (its victim is the boss); NOT "the boss hits this tank"
        bool holdsBoss = false; // the boss hits this tank (the boss's victim is this tank): it holds the boss right now
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
    // tank that does NOT hold the boss. The reference is the highest threat among the tanks (main or off) whose
    // `holdsBoss` is false, floored at max(1 % of the highest tank, 100) so it is never 0. When every tank holds the
    // boss (no tank on the list that does not, or none alive, in the world and uncharmed: the caller leaves those out)
    // the pick above stands. With no holder at all (the boss has no victim, or a damage dealer holds it) every tank is a
    // non-holder, so the reference is the highest tank. Off, the code below runs exactly as before.
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
                if (tank.holdsBoss)
                    continue;
                if (!best || tank.threat > best->threat)
                    best = &tank;
            }
            if (best)
            {
                const float floor = std::max(highest * kThreatCapOffTankMinShare, kThreatCapOffTankMinThreat);
                pick.threat = std::max(best->threat, floor);
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

    inline float ThreatCapReference(const std::vector<ThreatCapTank>& tanks, bool successorCap, bool notHolder = false)
    {
        return ThreatCapPickReference(tanks, successorCap, notHolder).threat;
    }
}
