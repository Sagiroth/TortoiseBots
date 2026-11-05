#pragma once

#include <algorithm>
#include <cstdint>

// Rage/cooldown gates for the warrior trigger rows, so an ability the bot
// cannot pay for is never armed and the rotation never stalls on it.

namespace ai
{
    // Vanilla 1.12 rage costs / thresholds.
    constexpr uint32_t WARRIOR_DISARM_RAGE_COST = 20;
    constexpr uint32_t WARRIOR_BATTLE_SHOUT_RAGE_COST = 10;
    constexpr uint32_t WARRIOR_MOCKING_BLOW_RAGE_COST = 10;
    // Bloodrage only tops up below this rage.
    constexpr uint32_t WARRIOR_BLOODRAGE_RAGE_FLOOR = 20;

    // Tactical Mastery (Arms talent 128, 5 ranks: 12295 / 12676 / 12677 /
    // 12678 / 12679) keeps 5 rage per rank across a stance swap: 5 / 10 /
    // 15 / 20 / 25. The core truncates to 50 / 100 / 150 / 200 / 250 rage
    // units on entering Battle, Defensive or Berserker Stance (SpellAuras
    // aura scripts 831-835; rage is stored in tenths), and the premade
    // builds take it 5/5, so a dancing warrior keeps up to 25. Without it
    // nothing survives the swap.
    constexpr uint32_t WARRIOR_TACTICAL_MASTERY_RANK_1 = 12295;
    constexpr uint32_t WARRIOR_TACTICAL_MASTERY_RANK_2 = 12676;
    constexpr uint32_t WARRIOR_TACTICAL_MASTERY_RANK_3 = 12677;
    constexpr uint32_t WARRIOR_TACTICAL_MASTERY_RANK_4 = 12678;
    constexpr uint32_t WARRIOR_TACTICAL_MASTERY_RANK_5 = 12679;
    constexpr uint32_t WARRIOR_TACTICAL_MASTERY_KEPT_PER_RANK = 5;

    // Highest learned Tactical Mastery rank; highest-wins covers both
    // spellbook shapes (lower ranks kept or replaced on train).
    inline unsigned TacticalMasteryRank(bool hasRank1, bool hasRank2, bool hasRank3,
                                        bool hasRank4 = false, bool hasRank5 = false)
    {
        return hasRank5 ? 5 : hasRank4 ? 4 : hasRank3 ? 3 : hasRank2 ? 2 : hasRank1 ? 1 : 0;
    }

    // Rage the bot holds after the swap: already in the destination stance
    // it keeps everything, otherwise only what Tactical Mastery lets
    // through. Holds for Battle, Defensive and Berserker alike: the core
    // truncates on entering any of them.
    inline uint32_t RageAfterStanceSwap(bool inTargetStance, uint32_t rage, unsigned tmRank)
    {
        return inTargetStance ? rage : std::min(rage, WARRIOR_TACTICAL_MASTERY_KEPT_PER_RANK * tmRank);
    }

    // Disarm is only worth casting on a target that carries a weapon;
    // without one the disarm aura does nothing and the rage is wasted.
    inline bool ShouldDisarm(bool targetHasWeapon, uint32_t rage)
    {
        return targetHasWeapon && rage >= WARRIOR_DISARM_RAGE_COST;
    }

    // Bloodrage costs health, so only spend it when hurt is acceptable.
    inline bool ShouldBloodrage(bool spellReady, bool healthAtLeastMedium, uint32_t rage)
    {
        return spellReady && healthAtLeastMedium && rage < WARRIOR_BLOODRAGE_RAGE_FLOOR;
    }

    // Battle Shout is only affordable at 10+ rage; the buff-missing and
    // Blessing-of-Might comparisons live in BattleShoutPolicy.h.
    inline bool ShouldBattleShout(uint32_t rage)
    {
        return rage >= WARRIOR_BATTLE_SHOUT_RAGE_COST;
    }

    constexpr uint32_t WARRIOR_REND_RAGE_COST = 10;
    constexpr uint32_t WARRIOR_MAIN_STRIKE_RAGE_COST = 30; // Mortal Strike / Bloodthirst

    // Rend (10 rage) re-applied on every new target kept the bar below the
    // main strike's 30 for the whole fight (walk 15: Mortal Strike 0-3
    // casts a run while Rend fired 41 times). A dps warrior that knows the
    // main strike only re-applies Rend when the strike stays affordable
    // after it. No cooldown-remaining read exists here, so the pool is the
    // only gate.
    inline bool ShouldRendForDps(bool knowsMainStrike, uint32_t rage)
    {
        return !knowsMainStrike || rage >= WARRIOR_MAIN_STRIKE_RAGE_COST + WARRIOR_REND_RAGE_COST;
    }
    // Taunt is on cooldown (or resisted) and the current target left the tank:
    // Mocking Blow (Battle Stance, 2 min) is the second taunt. `rage` is the
    // rage paid in Battle Stance (RageAfterStanceSwap): the swap keeps only
    // what Tactical Mastery allows, so the pre-swap bar would arm a cast the
    // bot arrives too poor to fire.
    inline bool ShouldMockingBlow(bool known, bool lostAggro, bool tauntReady, bool mockingReady,
                                  bool inMelee, uint32_t rage)
    {
        return known && lostAggro && !tauntReady && mockingReady && inMelee &&
            rage >= WARRIOR_MOCKING_BLOW_RAGE_COST;
    }
    // Thunder Clap is a rage trap in the Arms swing cycle (Mortal
    // Strike/Overpower/Sweeping Strikes out-damage it), so grouped PvE Arms
    // DPS leaves it to the tank. Solo Arms has no tank covering it, PvP/BG
    // Arms wants the snare, and tanks and non-Arms keep the row. Grouped is
    // the bot's real group, not an online master: an ungrouped bot with a
    // master online still has no tank beside it.
    inline bool ShouldSkipThunderClapForArms(bool isTank, bool isArms, bool grouped, bool pvpOrBg)
    {
        return !isTank && isArms && grouped && !pvpOrBg;
    }
} // namespace ai
