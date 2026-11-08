#pragma once

// The seek_aura boss tactic's pure rules (Zul'Gurub's Hakkar and his Poisonous Blood 24321): when the raid gathers at
// the row's spot B, who walks into the source's cloud, when the source may be damaged, and when the off-tank pulls
// another Son of Hakkar 11357. Tested in tools/test_seek_aura_policy.cpp; callers pass plain numbers so this header
// stays free of core includes, and the row's columns arrive exactly as BossTacticsMgr reads them.

#include <algorithm>
#include <cstddef>
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
    constexpr float kSeekPickupYd = 60.0f;           // the tank's pull walks to a source this near B (the fallback: 35)
    constexpr int kSeekPullMinMissing = 3;           // pull a source only while this many seekers lack the aura
    constexpr float kSeekGatherStartPct = 5.0f;      // seekers start walking at hold % + this
    constexpr float kSeekTankSwingMarginPct = 10.0f; // the off-tank stops swinging this close above the hold

    // Cycle clock: the fight's combat start (the first read that sees the boss in combat with the group) starts it, and
    // every accepted sighting of the row's cycle spell on a group member restarts it: an observed siphon wins over the
    // estimate. The core arms BloodSiphon_Timer at the cycle length and only counts it down in combat, so the first
    // siphon lands one cycle after the fight starts, the same rule as every later one. lastSiphonMs 0 means no
    // sighting yet; combatStartMs 0 means the fight is not known yet (no due time).
    inline std::uint32_t SeekCycleDueMs(std::uint32_t combatStartMs, std::uint32_t lastSiphonMs, std::uint32_t cycleMs)
    {
        if (lastSiphonMs)
            return lastSiphonMs + cycleMs;
        return combatStartMs ? combatStartMs + cycleMs : 0;
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

    // The dose window [due - lead, due - kSeekCloseMs], both ends inclusive. The last three seconds before the due
    // time are too late for the dose to be on at the siphon — but once the due time passes with no sighting (a missed
    // 24324; walk 40 had two 184-185 s gaps) the window stays open for a while, instead of shutting for the rest of the
    // fight with the Son held at the spot and the siphon undosed.
    inline bool SeekWindowOpen(std::uint32_t nowMs, std::uint32_t dueMs, std::uint32_t leadMs)
    {
        if (dueMs == 0 || nowMs + leadMs < dueMs)
            return false;
        return nowMs + kSeekCloseMs <= dueMs || nowMs >= dueMs;
    }

    // How long the window stays overdue after a due time nobody saw the siphon for. Past it the due time is stale and
    // the estimate rolls forward (SeekRolledDueMs), so the window does not stay open for the rest of the fight: it
    // shuts until the rolled cycle's own window, and the forced burn follows the rolled due instead of firing forever.
    // It has to stay below cycle - lead or the roll would land inside a window already open: Hakkar's 90 s cycle with
    // an 80 s lead leaves 10 s, and 8 s shuts the window for the 2 s before the rolled one opens at due + 90 - 80.
    constexpr std::uint32_t kSeekOverdueGraceMs = 8000;

    // The due time a fight reads: unchanged until the overdue grace runs out, then rolled one cycle at a time until it
    // is current again (due += cycle). A clock that is never sighted ends with the due time just ahead of now, so the
    // next window is the estimate's own, not the missed siphon's.
    inline std::uint32_t SeekRolledDueMs(std::uint32_t nowMs, std::uint32_t dueMs, std::uint32_t cycleMs)
    {
        if (dueMs == 0 || cycleMs == 0)
            return dueMs;
        while (nowMs > dueMs + kSeekOverdueGraceMs)
            dueMs += cycleMs;
        return dueMs;
    }

    // The burn is forced once the window is about to close.
    inline bool SeekForceBurn(std::uint32_t nowMs, std::uint32_t dueMs)
    {
        return nowMs + kSeekForceMs >= dueMs;
    }

    // A picked Son that only joins the fight with the siphon close is burnt at once, not at the forced burn's 15 s: walk
    // 41 left three such Sons at 67-90 % a few seconds before the siphon (mh1 -27 s, th1 -23 s, th4 -11 s), so the
    // cloud was never earned. A pick already overdue (dueInMs 0) is late by definition; no due time is not.
    constexpr std::uint32_t kSeekLateEngageMs = 30000;

    // The same rule at another lead: a seek_pull row's `param` is the kill lead (walk 51: back -> death median 27 s,
    // p75 31 s, so a burn from 45 s kills about 18 s before the siphon; no earlier than the row's lead keeps the
    // killed Son's 60 s respawn ahead of the next cycle's walk-out).
    inline bool SeekLateEngagedBurnWithin(bool sourceInCombat, std::uint32_t dueMs, std::uint32_t dueInMs,
                                          std::uint32_t leadMs)
    {
        return sourceInCombat && dueMs != 0 && dueInMs <= leadMs;
    }

    inline bool SeekLateEngagedBurn(bool sourceInCombat, std::uint32_t dueMs, std::uint32_t dueInMs)
    {
        return SeekLateEngagedBurnWithin(sourceInCombat, dueMs, dueInMs, kSeekLateEngageMs);
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

    // The seek's reach of a Son: its distance from B, or with a live boss anchor, for a Son inside the anchor's own
    // radius (`anchorRadiusYd`, the raid's), the nearer of that and its distance from the anchor's centre (the boss's
    // spawn). B sits 15 yd from Hakkar's spawn, so a Son brought to the far edge of the anchor stood 37-38 yd from B,
    // beyond kSeekFallbackYd, and was held through the siphon.
    inline float SeekReachYd(float distFromSpotYd, float distFromAnchorYd, bool anchored, float anchorRadiusYd)
    {
        return anchored && distFromAnchorYd <= anchorRadiusYd ? std::min(distFromSpotYd, distFromAnchorYd)
                                                              : distFromSpotYd;
    }

    // A Son that has fought a non-tank this long goes to the first off-tank (SeekTakeFightingSonOnHealer); this long
    // and no tank took it, the hold lets the damage dealers kill it (SeekHoldDecision). Walk 41: a Son sat on a healer
    // at 100 % for 13 s and the siphon went undosed behind it. Walk 48: the old 8 s take and 12 s release still left
    // the held Sons at a median 26.5 s, and 12 of the 23 losses were Son wipes, so both steps are shorter.
    constexpr std::uint32_t kSeekHealerTakeMs = 3000;
    constexpr std::uint32_t kSeekNonTankBurnMs = 5000;

    // A hold the fight keeps longer than this is released to the damage dealers whatever the window, the gather or the
    // Son's health say: walk 48's held Sons lasted a median 26.5 s (p75 47 s, max 126 s), and the losses with a
    // walk-out lost 8.8 members to Sons a fight against 4.5 without. A lost cloud costs less than a Son wipe. Only
    // fighting time counts (SeekHoldClockTicks): a spare waiting at its spawn is the reservoir, not a stall.
    constexpr std::uint32_t kSeekHoldMaxMs = 20000;

    // Whether this read feeds the hold clock. Only a Son fighting a victim (`engaged`) counts toward the 20 s cap, so
    // a spare standing at its spawn or waiting out a shut window never enters it: the hold rule holds every
    // non-selected Son wherever it stands, and a clock on that would release the whole reservoir for good after 20 s,
    // after which the area veto stops protecting the Sons later siphons need. The cap itself reaches the selected
    // source, and a Son on a tank inside the window (`victimTank`: the Son's victim is a player tank). A Son on a
    // non-tank is the healer clock's (SeekHealerClockTick).
    inline bool SeekHoldClockTicks(bool selected, bool engaged, bool victimTank, bool windowOpen)
    {
        return engaged && (selected || (victimTank && windowOpen));
    }

    // Whether one read feeds the shared hold clock: a Son this read saw alive, that the clock counts
    // (SeekHoldClockTicks) and that the hold rule still holds. A read of a Son seen dead erases its record and a
    // read that does not tick a live Son changes nothing (SeekHoldClockTick), so another bot may keep feeding it.
    // Every other Son is left out: an idle spare never gets a record, so the cap can never release the reservoir the
    // area veto protects; a Son the rule already releases is damageable and needs no clock.
    inline bool SeekSonTicks(bool seenAlive, bool ticks, bool holds)
    {
        return seenAlive && ticks && holds;
    }

    // The meet Son stays held for the damage dealers while the meet is open and no tank has it (DungeonRunSeekSonMeet).
    // Walk 54: 57 of 58 meet Sons were released 2-6 s after the shot at 98 % hp, 33-50 yd out, because the puller
    // counted as a non-tank on the 5 s clock (and the late-engaged burn forced the rest); the damage dealers then
    // peeled the Son off the puller before the receiver could act. Checked first, so no clock, cap or burn releases it.
    // `meetOpen` is false with the flag off.
    inline bool SeekMeetHoldsSon(bool meetOpen, bool sonIsMeetSon, bool sonVictimIsTank)
    {
        return meetOpen && sonIsMeetSon && !sonVictimIsTank;
    }

    // Healers leave the open meet's puller and returner alone (DungeonRunSeekMeetNoHeal). Walk 55: 24 of 27 meets ended
    // with the Son peeled onto a healer, because a heal on the puller adds threat on every mob that has the puller on
    // its list (heal threat x0.5 split over Hakkar and the Son, a 1-1.5k heal about 250-375 on the Son against the one
    // Auto Shot the puller holds). So while the meet is open a heal target that is the puller or returner is skipped,
    // unless it is below kSeekMeetHealFloorPct hp (then the heal wins over the threat). `flagOn` is the key.
    constexpr float kSeekMeetHealFloorPct = 40.0f;
    inline bool SeekMeetHealHeld(bool flagOn, bool meetOpen, bool isPullerOrReturner, float hpPct)
    {
        return flagOn && meetOpen && isPullerOrReturner && hpPct >= kSeekMeetHealFloorPct;
    }

    // Why the hold rule leaves a Son alone or lets it go: the reason the seek logs with each release. `Held` is the
    // hold itself and `Dosed` the hold once every seeker already carries the poison (nothing a kill buys) — both still
    // hold the Son; `Cap20`, `NonTank` and `SelectedBurn` are the release paths.
    enum class SeekHoldWhy
    {
        Held,
        Cap20,
        NonTank,
        SelectedBurn,
        Dosed,
    };

    // Whether a reason still holds the Son. Dosed is a hold variant, not a release: both the decision and the release
    // log read held-ness through this one predicate.
    inline bool SeekHoldWhyHolds(SeekHoldWhy why)
    {
        return why == SeekHoldWhy::Held || why == SeekHoldWhy::Dosed;
    }

    // The hold rule's one body: SeekHoldDecision answers SeekHoldWhyHolds(SeekHoldWhyOf(...)) and the seek logs the
    // reason of each release, so the decision and its log cannot drift.
    inline SeekHoldWhy SeekHoldWhyOf(bool isSelected, bool windowOpen, bool gatherReady, bool forceBurn, float hpPct,
        float holdPct, float distFromSpotYd, int seekers, std::uint32_t nonTankHeldMs = 0,
        std::uint32_t heldMs = 0)
    {
        if (heldMs >= kSeekHoldMaxMs)
            return SeekHoldWhy::Cap20;
        if (nonTankHeldMs >= kSeekNonTankBurnMs)
            return SeekHoldWhy::NonTank;
        if (!isSelected || distFromSpotYd > kSeekFallbackYd || !windowOpen)
            return SeekHoldWhy::Held;
        if (seekers <= 0)
            return SeekHoldWhy::Dosed;
        if (hpPct <= holdPct && !gatherReady && !forceBurn)
            return SeekHoldWhy::Held;
        return SeekHoldWhy::SelectedBurn;
    }

    // Whether the damage on a source is held. Only the one selected source is ever damageable, and only inside the
    // dose window and within the seek's reach of the spot; at or below the hold percent it still waits for the gather
    // (or the forced burn). Every other Son stays held wherever it stands: a second low Son killed beside the pick
    // spends the raid's only source for a later cycle, and a pick killed while the window is shut leaves the next
    // siphon without its cloud (the poison lasts longer than the window). With no seeker left without the poison the
    // raid is dosed: nothing is killed then, however the gather ran, until the siphon strips the aura again.
    //
    // `nonTankHeldMs` is how long the Son has been fighting a non-tank (0: on a tank, or no victim). Past
    // kSeekNonTankBurnMs it is no longer held, whatever else holds it: the off-tank had kSeekHealerTakeMs to take it
    // and a moment more, and a Son left on the raid costs more than the cycle's cloud (walk 45: six deaths behind one
    // hold). `heldMs` is how long the fight has held this Son while it fought (0: not held); past kSeekHoldMaxMs it is
    // released too. The clock behind it counts fighting time only (SeekHoldClockTicks).
    inline bool SeekHoldDecision(bool isSelected, bool windowOpen, bool gatherReady, bool forceBurn, float hpPct,
        float holdPct, float distFromSpotYd, int seekers, std::uint32_t nonTankHeldMs = 0,
        std::uint32_t heldMs = 0)
    {
        return SeekHoldWhyHolds(SeekHoldWhyOf(isSelected, windowOpen, gatherReady, forceBurn, hpPct, holdPct,
                                             distFromSpotYd, seekers, nonTankHeldMs, heldMs));
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

    // The fallback pull branch: exactly two tanks alive. The third off-tank pulls by itself with three, and with fewer
    // than two nobody is left at the spot to take a pulled Son over.
    inline bool SeekFallbackPullBranch(std::size_t tankCount)
    {
        return tankCount == 2;
    }

    // The fallback puller's candidate: alive, not charmed (a creature-charmed player is hostile and its AI is gone),
    // not the boss's current victim (the pull would take the boss's target out of the tank's face, and the victim
    // cannot turn and shoot), and not one of the three tanks the seek leaves out.
    inline bool SeekFallbackCandidate(bool alive, bool charmed, bool bossVictim, bool exemptTank)
    {
        return alive && !charmed && !bossVictim && !exemptTank;
    }

    // The fallback pulls only a Son near the spot: its shot has to reach a Son the off-tank can then take over, and a
    // Son farther out costs the window while the raid walks to it (the third off-tank's own pull walks its 60 yd).
    inline bool SeekFallbackPullDue(bool windowOpen, bool sourceInFight, int missing, float nearestSourceToSpotYd)
    {
        return windowOpen && !sourceInFight && missing >= kSeekPullMinMissing &&
            nearestSourceToSpotYd <= kSeekFallbackYd;
    }

    // A latched pull keeps after its Son while the Son stands inside this reach of B and the latch is younger than the
    // latch window. The Son patrols from 32 to 112 yd out, so the pickup radius alone dropped the tank from a Son it
    // was already walking to the moment the Son stepped past 60 yd (walk 41: the tank targeted the Son in 1-5 frames a
    // window and stood still in all of them).
    constexpr float kSeekPullLatchYd = 90.0f;
    constexpr std::uint32_t kSeekPullLatchMs = 20000;

    inline bool SeekPullLatchKeeps(bool hasLatch, float sonToSpotYd, std::uint32_t elapsedMs)
    {
        return hasLatch && sonToSpotYd <= kSeekPullLatchYd && elapsedMs <= kSeekPullLatchMs;
    }

    // The pull branch's own step. Walk: the Son is out of reach and not fighting, so the tank runs to it and takes the
    // tick itself (the drag back to B moves with the same MoveTo). Hit: at melee reach, the attack lands and the Son
    // joins the fight. Done: it already fights or already follows this tank, so the take/drag branches own it. The
    // branch used to set the target and give the tick back, so the tank never moved and the rotation returned to the
    // boss (walk 40: 75 of 83 siphons had no Son in combat; walk 41: the closest tank-to-Son was 27-54 yd in every
    // undosed window).
    constexpr float kSeekPullHitYd = 5.0f;

    enum class SeekPullStep : std::uint8_t
    {
        Walk = 0,
        Hit = 1,
        Done = 2,
    };

    inline SeekPullStep SeekTankPullStep(bool sonInCombat, bool sonOnTank, float tankToSonYd)
    {
        if (sonInCombat || sonOnTank)
            return SeekPullStep::Done;
        if (tankToSonYd <= kSeekPullHitYd)
            return SeekPullStep::Hit;
        return SeekPullStep::Walk;
    }

    // A Son that has been fighting a non-tank (a healer, a damage dealer) for kSeekHealerTakeMs is the first off-tank's
    // own take, whatever the third off-tank does: the third off-tank had those seconds.
    inline bool SeekTakeFightingSonOnHealer(std::uint32_t heldMs)
    {
        return heldMs >= kSeekHealerTakeMs;
    }

    // Whether an off-tank may take a Son, drag one or walk a pull at all: never while it is the boss's victim (after
    // Cause Insanity the threat successor can hand Hakkar to it, and its walk would bring him onto the gather).
    inline bool SeekTankTakeAllowed(bool isBossVictim)
    {
        return !isBossVictim;
    }

    // Why a take this tick did or did not taunt: `chase_refused` (the Son stands outside the anchor and this class
    // cannot taunt it from here), `no_taunt` (the class has no taunt), `attack` (the taunt is on cooldown or out of
    // range, so Attack only), or `taunt` (the taunt is being cast). The first three are refusal codes. `edge_far` replaces
    // `chase_refused` when DungeonRunSeekTakeEdge refused a Son whose victim is inside but that stands too far out.
    inline const char* SeekTakeReasonWord(bool chaseRefused, bool hasTaunt, bool canTaunt, bool edgeFar = false)
    {
        if (chaseRefused && !canTaunt)
            return edgeFar ? "edge_far" : "chase_refused";
        if (!hasTaunt)
            return "no_taunt";
        if (canTaunt)
            return "taunt";
        return "attack";
    }

    // Who pulls an unengaged Son this read: exactly one actor. The third off-tank while three tanks live and it is not
    // the boss's victim (SeekTankTakeAllowed stands its whole task down then); otherwise the ranged fallback (two tanks,
    // the third off-tank the victim, or the anchored shot that leaves the tanks no Son to walk to).
    struct SeekPullActors
    {
        bool thirdTank = false;
        bool fallback = false;
    };

    inline SeekPullActors SeekPullActorsFor(std::size_t tankCount, bool thirdTankIsBossVictim, bool anchorShot)
    {
        SeekPullActors out;
        if (tankCount >= 3 && !thirdTankIsBossVictim && !anchorShot)
        {
            out.thirdTank = true;
            return out;
        }
        out.fallback = tankCount >= 2;
        return out;
    }

    // Who opens a Son that is not fighting yet: the third off-tank while it lives. With fewer than three tanks alive
    // the off-tank (tanks[1]) takes the Son over instead, and the raid's nearest hunter pulls it with one ranged shot;
    // without a hunter the nearest ranged damage dealer does. Nobody pulls when there is no off-tank to take it over.
    enum class SeekPullRole : std::uint8_t
    {
        None = 0,
        ThirdTank = 1,
        Hunter = 2,
        Ranged = 3,
    };

    inline SeekPullRole SeekPullRoleOf(bool thirdTankAlive, bool offTankAlive, bool hunterAlive, bool rangedAlive)
    {
        if (thirdTankAlive)
            return SeekPullRole::ThirdTank;
        if (!offTankAlive)
            return SeekPullRole::None;
        if (hunterAlive)
            return SeekPullRole::Hunter;
        if (rangedAlive)
            return SeekPullRole::Ranged;
        return SeekPullRole::None;
    }

    // The puller's own hit on the Son its pull latched is the pull, not damage on a held source: the hold answers true
    // for every Son that is not the picked source, so without this the swing (or the fallback's shot) never lands and
    // the Son never aggroes. The latch names that Son — it rides the fight record from the pull's start, through the
    // pickup radius and past the Son joining the fight — so the exemption follows the pull itself rather than one
    // tick's search (walk 41: another Son in the fight left the pull target unset and the hold vetoed the puller's own
    // full-health hits). Any other Son keeps the hold, and every DoT keeps it too (SeekDotVeto): its ticks would run
    // through the hold on the pick.
    inline bool SeekPullHitExemptOnLatch(bool puller, bool latchedTarget)
    {
        return puller && latchedTarget;
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
    // in, else the fallback's 35 yd (the gather then goes to the source wherever it stands). A forced burn, a source
    // already at the hold percent, or a Son already fighting inside that reach reaches the fallback too, whatever the
    // tank count: the off-spot Son has to die for its cloud, and a Son left fighting the healers at 11-25 yd from the
    // spot was never selected, so it kept hitting them until the raid burned it too late.
    inline float SeekSourceSearchYd(bool thirdTank, bool forceBurn, bool belowHold, bool engagedSon)
    {
        if (forceBurn || belowHold || engagedSon)
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
