#pragma once

#include <cstddef>
#include <cstdint>

// Paladin Judgement upkeep (AiPlayerbot.PaladinJudgementUpkeep, default 1).
//
// With the toggle on, paladin bots in a group keep Judgement of Wisdom and
// Judgement of Light up on the boss / the tank's elite target. Vanilla
// mechanics (spell_template, 1.18.1): Judgement (20271) is castable only with a
// Seal up, spends that Seal and applies the Seal's judgement debuff; Seal of
// Wisdom (20166, ranks 20356 / 20357 ...) leaves Judgement of Wisdom, Seal of
// Light (20165, ranks 20347 ...) leaves Judgement of Light, Seal of
// Righteousness / Command leave none that the group benefits from. The debuff
// lasts 10 s (vanilla) and Judgement has a 10 s cooldown, so one paladin per debuff
// re-judges every cycle as the debuff runs out.
//
// Which debuff, and who owns it, is a pure function of the paladins in the
// group, so it is unit-tested on its own (tools/test_judgement_upkeep_policy.cpp)
// and every paladin bot reaches the same answer without talking to the others:
//   - a paladin in the tank role never owns a judgement, except in a 5-man
//     dungeon with AiPlayerbot.ProtPaladinDungeonWisdom enabled where it may
//     backfill Judgement of Wisdom when no non-tank paladin can; if no other
//     paladin can own a debuff, it is simply not upkept;
//   - a non-tank, non-healing paladin (Retribution) owns Judgement of Wisdom,
//     a healing paladin owns Judgement of Light (mod-playerbots' per-role
//     pairing: dps judges Wisdom, the heal strategy judges Light);
//   - a second non-tank paladin of the same role takes the debuff the first
//     one left over, so two paladins never judge the same debuff; a third
//     owns nothing;
//   - the order inside a role is the lowest guid first, the same on every bot.
// Behaviour ported from mod-playerbots @ 79bd428
// (src/Ai/Class/Paladin/Strategy/{Dps,Tank,Heal}PaladinStrategy.cpp default
// actions, GenericPaladinStrategyActionNodeFactory.h judgement_of_wisdom ->
// judgement_of_light fallback); the donor has no per-debuff owner, the owner
// rule and the refresh window are this module's.
// No core includes: callers translate game state into plain inputs.

namespace TortoiseBots
{

enum class JudgementKind : uint8_t
{
    None = 0,
    Wisdom,
    Light,
};

// A paladin bot of the group, as the owner rule needs to see it.
struct JudgementPaladin
{
    uint64_t id = 0;       // stable per character (the guid)
    bool healer = false;   // heal role (PlayerbotAI::IsHeal)
    bool tank = false;     // tank role (PlayerbotAI::IsTank): never owns a judgement
    bool canWisdom = false; // knows Seal of Wisdom
    bool canLight = false;  // knows Seal of Light
};

// Re-judge when the debuff has this long left or less (ms). The debuff lasts
// 10 s and Judgement's cooldown is 10 s, so the window is never what holds a
// refresh back: the cooldown is. It lets the Seal swap start a global early.
inline constexpr int kJudgementRefreshWindowMs = 4000;

// Target must have at least this much health left to be worth a Seal + Judgement.
inline constexpr uint8_t kJudgementMinTargetHealthPct = 25;

// A healer judges only with this much mana (at least the engine's medium-mana
// line): heals come first and a Seal + Judgement is a tenth of a low-level bar.
inline constexpr uint8_t kJudgementHealerMinManaPct = 50;

inline constexpr std::size_t kMaxJudgementPaladins = 40;

// The debuff `self` owns, None when it owns none. `paladins` is every paladin
// bot of the group that is alive and in reach, `self` included. When
// `tankMayOwnWisdom` is true (5-man dungeon with ProtPaladinDungeonWisdom),
// a tank paladin may backfill Wisdom if no non-tank paladin can own it.
inline JudgementKind JudgementKindFor(uint64_t self, const JudgementPaladin* paladins, std::size_t count, bool tankMayOwnWisdom = false)
{
    if (!paladins || count == 0)
        return JudgementKind::None;
    if (count > kMaxJudgementPaladins)
        count = kMaxJudgementPaladins;

    // role: 0 = non-healers, 1 = healers, -1 = any. Lowest guid wins; `skip`
    // (when `skipValid`) is a paladin that already owns the other debuff.
    auto pick = [&](int role, bool needWisdom, bool needLight, uint64_t skip, bool skipValid, bool allowTank) -> const JudgementPaladin*
    {
        const JudgementPaladin* best = nullptr;
        for (std::size_t i = 0; i < count; ++i)
        {
            const JudgementPaladin& p = paladins[i];
            if (p.tank != allowTank)
                continue;
            if (skipValid && p.id == skip)
                continue;
            if (role >= 0 && p.healer != (role == 1))
                continue;
            if (needWisdom && !p.canWisdom)
                continue;
            if (needLight && !p.canLight)
                continue;
            if (!best || p.id < best->id)
                best = &p;
        }
        return best;
    };

    // Preferred owners by role.
    const JudgementPaladin* wisdom = pick(0, true, false, 0, false, false);
    const JudgementPaladin* light = pick(1, false, true, 0, false, false);

    // Backfill a debuff nobody of its role can own with a non-tank paladin that
    // owns nothing yet (a second Retribution paladin takes Light, a second Holy
    // paladin takes Wisdom). A lone paladin never owns both.
    if (!wisdom)
        wisdom = pick(-1, true, false, light ? light->id : 0, light != nullptr, false);
    if (!light)
        light = pick(-1, false, true, wisdom ? wisdom->id : 0, wisdom != nullptr, false);

    // In 5-man dungeons, if no non-tank paladin can own Wisdom, the tank may.
    if (!wisdom && tankMayOwnWisdom)
        wisdom = pick(-1, true, false, 0, false, true);

    if (wisdom && wisdom->id == self)
        return JudgementKind::Wisdom;
    if (light && light->id == self)
        return JudgementKind::Light;
    return JudgementKind::None;
}

// Only worth the mana and the Seal swap on a fight that matters: an elite /
// boss, or (inside a dungeon or raid) the mob the group's tank is on. Never
// solo (no one benefits), never a nearly dead target.
inline bool IsJudgementUpkeepTarget(bool inGroup, bool eliteOrBoss, bool tankTargetInInstance, uint8_t targetHealthPct)
{
    if (!inGroup)
        return false;
    if (targetHealthPct < kJudgementMinTargetHealthPct)
        return false;
    return eliteOrBoss || tankTargetInInstance;
}

// Mana / heal gate. A healer yields to healing: not while a party member is
// below the medium-health line and not below kJudgementHealerMinManaPct.
// Anyone else needs the engine's medium-mana line (a Seal + Judgement is
// pointless on an empty bar).
inline bool JudgementUpkeepAffordable(bool healer, uint8_t manaPct, uint8_t mediumManaPct, bool partyNeedsHealing)
{
    if (manaPct < mediumManaPct)
        return false;
    if (!healer)
        return true;
    return manaPct >= kJudgementHealerMinManaPct && !partyNeedsHealing;
}

// The debuff has to be (re)applied: absent, or about to run out.
inline bool JudgementNeedsRefresh(bool debuffPresent, int32_t remainingMs)
{
    return !debuffPresent || remainingMs <= kJudgementRefreshWindowMs;
}

// Cast the owner's Seal now? An empty Seal slot is filled with the owner's
// Seal when `refillEmptySlot` is true (so no Seal is wasted on a swap later);
// a tank that uses Seal of Righteousness on bosses sets `refillEmptySlot` to
// false so it only swaps to Wisdom when the debuff needs a refresh and
// Judgement is ready.
inline bool ShouldCastUpkeepSeal(JudgementKind kind, bool anySealUp, bool wantedSealUp, bool needsRefresh, bool judgementReady, bool refillEmptySlot = true)
{
    if (kind == JudgementKind::None || wantedSealUp)
        return false;
    if (!anySealUp && refillEmptySlot)
        return true;
    return needsRefresh && judgementReady;
}

// Judge now? The owner's Seal is up and the debuff needs a refresh.
inline bool ShouldCastUpkeepJudgement(JudgementKind kind, bool wantedSealUp, bool needsRefresh)
{
    return kind != JudgementKind::None && wantedSealUp && needsRefresh;
}

enum class ProtPaladinSeal : uint8_t
{
    Righteousness = 0,
    Wisdom,
};

// Single decision for which Seal a Protection paladin wants in combat, so
// Seal of Righteousness and Seal of Wisdom triggers never flip-flop.
inline ProtPaladinSeal ProtPaladinWantedSeal(
    bool featureEnabled,
    bool inFiveManDungeon,
    bool targetIsBoss,
    bool lowMana,
    bool knowsWisdom)
{
    if (!knowsWisdom)
        return ProtPaladinSeal::Righteousness;
    if (lowMana)
        return ProtPaladinSeal::Wisdom;
    if (featureEnabled && inFiveManDungeon && !targetIsBoss)
        return ProtPaladinSeal::Wisdom;
    return ProtPaladinSeal::Righteousness;
}

} // namespace TortoiseBots
