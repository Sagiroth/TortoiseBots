#pragma once

// The seek_aura boss tactic's pure rules (Zul'Gurub's Hakkar and his Poisonous Blood 24321): when the raid gathers at
// the row's spot B, who walks into the source's cloud, when the source may be damaged, and when the off-tank pulls
// another Son of Hakkar 11357. Tested in tools/test_seek_aura_policy.cpp; callers pass plain numbers so this header
// stays free of core includes, and the row's columns arrive exactly as BossTacticsMgr reads them.

#include <cstdint>
#include <string>
#include <vector>

namespace ai
{
    constexpr float kSeekRunYdPerS = 7.0f;           // run speed
    constexpr std::uint32_t kSeekLatencyMs = 500;    // decision and start of the walk
    constexpr float kSeekCloudInsetYd = 1.5f;        // how far inside the cloud edge a bot aims
    constexpr std::uint32_t kSeekCloseMs = 3000;     // the window closes this long before due
    constexpr std::uint32_t kSeekForceMs = 15000;    // the burn is forced this long before due
    constexpr std::uint32_t kSeekGatherMaxMs = 8000; // a gather never waits longer than this
    constexpr float kSeekSpotYd = 8.0f;              // the source is "at B" within this
    constexpr float kSeekFallbackYd = 35.0f;         // the seek's outer reach: a source beyond it is out of the fight
    constexpr float kSeekPickupYd = 60.0f;           // a source this near B may be pulled
    constexpr int kSeekPullMinMissing = 3;           // pull a source only while this many seekers lack the aura
    constexpr float kSeekGatherStartPct = 5.0f;      // seekers start walking at hold % + this
    constexpr float kSeekTankSwingMarginPct = 10.0f; // the off-tank stops swinging this close above the hold

    // Cycle clock: the first sighting of the boss in combat with a victim starts it, and every accepted sighting of the
    // row's cycle spell on a group member restarts it. lastCycleMs 0 means no sighting yet.
    inline std::uint32_t SeekDueMs(std::uint32_t engageMs, std::uint32_t lastCycleMs, std::uint32_t cycleMs)
    {
        return (lastCycleMs ? lastCycleMs : engageMs) + cycleMs;
    }

    // A sighting starts a new cycle only when no cycle is known or it is at least half a cycle after the last one: all
    // the raid's bots see the same siphon within a few hundred milliseconds, so the second sighting must not move the
    // due time again.
    inline bool SeekCycleAccepts(std::uint32_t lastCycleMs, std::uint32_t seenMs, std::uint32_t cycleMs)
    {
        if (lastCycleMs == 0)
            return true;
        return seenMs >= lastCycleMs + cycleMs / 2;
    }

    // The dose window [due - lead, due - kSeekCloseMs], both ends inclusive.
    inline bool SeekWindowOpen(std::uint32_t nowMs, std::uint32_t dueMs, std::uint32_t leadMs)
    {
        return nowMs + leadMs >= dueMs && nowMs + kSeekCloseMs <= dueMs;
    }

    // The burn is forced once the window is about to close.
    inline bool SeekForceBurn(std::uint32_t nowMs, std::uint32_t dueMs)
    {
        return nowMs + kSeekForceMs >= dueMs;
    }

    // Who seeks: every living bot that lacks the aura, except the boss's victim (the main tank) and the two tanks kept
    // off the poison (the cause-insanity backup), and never a charmed bot.
    inline bool SeekWanted(bool keyOn, bool windowOpen, bool hasAura, bool isBossVictim, bool isExemptTank, bool charmed,
        bool alive)
    {
        return keyOn && windowOpen && alive && !hasAura && !isBossVictim && !isExemptTank && !charmed;
    }

    // A pet inside the siphon's radius is one full heal or damage tick of its own: the core reads the poison on every
    // living unit, pets included. Hunters and warlocks dismiss theirs from this long before the due time until this
    // long after it, and plainly while the siphon's dummy aura is on the bot. dueInMs is signed: negative once due.
    constexpr int kSeekPetLeadMs = 5000;
    constexpr int kSeekPetTailMs = 2000;

    inline bool SeekPetMustLeave(int dueInMs, bool siphonActive)
    {
        return siphonActive || (dueInMs <= kSeekPetLeadMs && dueInMs >= -kSeekPetTailMs);
    }

    // A placement spell that leaves a totem behind: one more undosed unit to the siphon's hit, and its pulse outlives
    // the cast. Every shaman totem spell is named "<element> totem" (the damage ones too), so the spell name decides.
    inline bool SeekTotemSpell(const std::string& spell)
    {
        return spell.size() > 6 && spell.compare(spell.size() - 6, 6, " totem") == 0;
    }

    // When a class's pet has to be out of the fight: a hunter's pet only around the siphon (it waits in the current
    // slot and is called back), a warlock's demon from the engage on — a new one costs a soul shard and a 10 s cast,
    // so it is not resummoned once per siphon.
    inline bool SeekClassPetMustLeave(bool warlock, int dueInMs, bool siphonActive)
    {
        return warlock || SeekPetMustLeave(dueInMs, siphonActive);
    }

    // A guardian a warlock's spell leaves behind (the Infernal of Inferno, a Doomguard): it fights for the whole
    // window and is one more unit for the siphon's hit, so it is never summoned in the fight at all.
    inline bool SeekGuardianSummonAction(const std::string& name)
    {
        return name == "summon inferno" || name == "inferno" || name == "summon doomguard";
    }

    // The class rotations' pet calls and demon summons: a pet is one more undosed unit to the same hit, and a
    // warlock's new demon also costs a soul shard and a 10 s cast that would spend a whole siphon.
    inline bool SeekPetCallAction(const std::string& name)
    {
        return name == "call pet" || name == "revive pet" || name == "initialize pet" || name == "summon imp" ||
            name == "summon voidwalker" || name == "summon succubus" || name == "summon felhunter" ||
            SeekGuardianSummonAction(name);
    }

    // A cloud is the row's when its spell is the aura itself or the trigger spell of its effect.
    inline bool IsSoughtCloud(std::uint32_t dynSpellId, std::uint32_t effectTriggerSpell, std::uint32_t soughtAura)
    {
        return dynSpellId == soughtAura || effectTriggerSpell == soughtAura;
    }

    // The bot gets into the cloud before it expires: it has to run the distance from its position to the aim point
    // (radius - inset) inside the cloud, within the cloud's remaining life minus the decision latency.
    inline bool SeekCloudReachable(float distToCentre, float radius, std::uint32_t remainingMs)
    {
        float const run = distToCentre - (radius - kSeekCloudInsetYd);
        if (run <= 0.0f)
            return true;
        if (remainingMs <= kSeekLatencyMs)
            return false;
        float const budget = kSeekRunYdPerS * (static_cast<float>(remainingMs - kSeekLatencyMs) / 1000.0f);
        return run <= budget;
    }

    // The gather is ready when nobody is missing, when it has waited kSeekGatherMaxMs, or when the burn is forced (the
    // window is about to close, so the source dies whichever way).
    inline bool SeekGatherReady(int missing, std::uint32_t gatherStartMs, std::uint32_t nowMs, bool forceBurn)
    {
        return missing <= 0 || forceBurn || nowMs - gatherStartMs >= kSeekGatherMaxMs;
    }

    // A source is a pick only while some seeker still misses the dose: once every group member carries the aura a
    // kill buys nothing and spends a Son the next cycle needs. A Son that is not fighting is not a pick either: the
    // gathered raid cannot make it come to the spot.
    inline bool SeekSourceSelectable(bool inCombat, int seekers)
    {
        return inCombat && seekers > 0;
    }

    // A source already at or below the hold percent and fighting the raid is the finishable pick: held until the next
    // window, it reset and healed back to full.
    inline bool SeekSourceFinishable(bool inCombat, float hpPct, float holdPct, int seekers)
    {
        return SeekSourceSelectable(inCombat, seekers) && hpPct <= holdPct;
    }

    // The same source inside the seek's reach of the row's spot: a Son farther out is never a kill pick (the gather
    // cannot stand around it without leaving B, and a forced march would spend the window).
    inline bool SeekFinishableInReach(bool inCombat, float hpPct, float holdPct, float distFromSpotYd, int seekers)
    {
        return SeekSourceFinishable(inCombat, hpPct, holdPct, seekers) && distFromSpotYd <= kSeekFallbackYd;
    }

    // Whether the damage on a source is held. Only the one selected source is ever damageable, and only inside the
    // dose window and within the seek's reach of the spot; at or below the hold percent it still waits for the gather
    // (or the forced burn). Every other Son stays held wherever it stands: a second low Son killed beside the pick
    // spends the raid's only source for a later cycle, and a pick killed while the window is shut leaves the next
    // siphon without its cloud (the poison lasts longer than the window). With no seeker left without the poison the
    // raid is dosed: nothing is killed then, however the gather ran, until the siphon strips the aura again.
    inline bool SeekHoldDecision(bool isSelected, bool windowOpen, bool gatherReady, bool forceBurn, float hpPct,
        float holdPct, float distFromSpotYd, int seekers)
    {
        if (!isSelected || distFromSpotYd > kSeekFallbackYd || !windowOpen)
            return true;
        if (seekers <= 0)
            return true;
        return hpPct <= holdPct && !gatherReady && !forceBurn;
    }

    // One living Son at an area action's point: how far it stands from the point (the action's target, or the bot when
    // the action has none) and whether the hold rule holds it.
    struct SeekAreaSon
    {
        float distToPointYd = 0.0f;
        bool held = false;
    };

    // Whether an area action is held because any source inside its area is: an area cast aimed at the picked source
    // also lands on a second, held Son standing beside it, and killing that one spends the raid's other source.
    inline bool SeekAreaVeto(const std::vector<SeekAreaSon>& sons, float radiusYd)
    {
        for (const SeekAreaSon& son : sons)
        {
            if (son.distToPointYd <= radiusYd && son.held)
                return true;
        }
        return false;
    }

    // The second off-tank pulls a new source while the window is open, no source is in the fight yet, enough seekers
    // still miss the aura, and a source stands near the spot.
    inline bool SeekTankPullDue(bool windowOpen, bool sourceInFight, int missing, float nearestSourceToSpotYd)
    {
        return windowOpen && !sourceInFight && missing >= kSeekPullMinMissing &&
            nearestSourceToSpotYd <= kSeekPickupYd;
    }

    // A damage-over-time on a Son of the seek fight: a pure DoT (Corruption, Serpent Sting) and a channelled periodic
    // cast (Mind Flay, Drain Life) tick through the hold percent and the shut window with no direct damage the
    // rotation could stop, so they are held at any time; a spell that also lands direct damage (Fireball, Immolate,
    // Moonfire) is a burst whose tail is acceptable on the Son the hold lets burn, and is held only while that Son is
    // held (SeekHoldDecision). The caller supplies the spell's shape and the hold read.
    inline bool SeekDotVeto(bool hasDirect, bool hasPeriodic, bool channelled, bool held)
    {
        if (!hasPeriodic)
            return false;
        return !hasDirect || channelled || held;
    }

    // The off-tank holding a source stops auto-attacking it once the window is shut and the Son stands at or below the
    // hold percent plus a margin: its own swings would take the Son past the percent before the next window can spend
    // it, and the Son is no longer being brought down anyway. The threat (and the taunt) stays; only the swings stop.
    inline bool SeekTankSwingHeld(bool windowOpen, float hpPct, float holdPct)
    {
        return !windowOpen && hpPct <= holdPct + kSeekTankSwingMarginPct;
    }

    // The hold takes the tick only to stop a swing that is running on the held Son (bot->GetVictim() == son with
    // UNIT_STAT_MELEE_ATTACKING, as the other melee holds read it): the engine runs one successful action a tick, so a
    // hold that answered true on every tick left the tank's Shield Block, its shouts and its taunt no turn.
    inline bool SeekTankHoldConsumesTick(bool swingingAtSon)
    {
        return swingingAtSon;
    }

    // The seek action takes this tick while it walks, and once more to stop a walk already under way. Standing at the
    // gather consumes nothing (the stand action's rule): the engine runs one successful action a tick, so a seek that
    // kept returning true at the spot would silence the rotation, the heals and the burn for the rest of the window.
    inline bool SeekActionConsumesTick(bool atSpot, bool moving)
    {
        return !atSpot || moving;
    }

    // A source at the spot, as the gather's pick sees it: its guid and the health % that breaks the tie.
    struct SeekSourceChoice
    {
        std::uint64_t guid = 0;
        float hpPct = 100.0f;
    };

    // The gather's one source: the current one while it still stands at the spot (two Sons there must not make each
    // bot pick its own, which flipped the shared latch), else the lowest health % and then the lowest guid, so every
    // bot of the fight reads the same source. 0: no source at the spot.
    inline std::uint64_t SeekKeepOrPickGatherSource(std::uint64_t currentGuid,
        const std::vector<SeekSourceChoice>& atSpot)
    {
        std::uint64_t best = 0;
        float bestHp = 0.0f;
        for (const SeekSourceChoice& c : atSpot)
        {
            if (currentGuid != 0 && c.guid == currentGuid)
                return currentGuid;
            if (best == 0 || c.hpPct < bestHp || (c.hpPct == bestHp && c.guid < best))
            {
                best = c.guid;
                bestHp = c.hpPct;
            }
        }
        return best;
    }

    // The gather's latched source: the record's current one wins while its Son is alive inside the seek's reach (the
    // caller checks that), whatever the current search radius or the damage between the two Sons; only a dead or
    // out-of-reach Son repicks, and then the lowest health % (then lowest guid) of the search's sources.
    inline std::uint64_t SeekLatchedGatherSource(bool latchInReach, std::uint64_t currentGuid,
        const std::vector<SeekSourceChoice>& atSpot)
    {
        if (latchInReach && currentGuid != 0)
            return currentGuid;
        return SeekKeepOrPickGatherSource(0, atSpot);
    }

    // The source search's reach around the spot: the spot's own radius while the third tank is there to drag a source
    // in, else the fallback's 35 yd (the gather then goes to the source wherever it stands). A forced burn or a source
    // already at the hold percent reaches the fallback too, whatever the tank count: the off-spot Son has to die for
    // its cloud, and waiting for a drag at the spot left it alive.
    inline float SeekSourceSearchYd(bool thirdTank, bool forceBurn, bool belowHold)
    {
        if (forceBurn || belowHold)
            return kSeekFallbackYd;
        return thirdTank ? kSeekSpotYd : kSeekFallbackYd;
    }

    // The record is only touched while the bots see the boss fighting, so a longer gap is a re-pull: the cycle clock
    // starts over rather than counting the old engage (due in the past, the window shut until the first siphon).
    constexpr std::uint32_t kSeekRepullGapMs = 10000;

    inline bool SeekClockStartOver(bool bossInCombat, std::uint32_t lastSeenMs, std::uint32_t nowMs)
    {
        return !bossInCombat || (lastSeenMs != 0 && nowMs - lastSeenMs > kSeekRepullGapMs);
    }
}
