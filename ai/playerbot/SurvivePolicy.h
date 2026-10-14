#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

// Pure decision rules for death fix B (rest stop + pool-only escape +
// death telemetry). No core includes: callers in strategy/ and PlayerbotAI
// translate game state into these plain inputs, so the rules stay testable
// in tools/test_survive_policy.cpp without the server.

namespace ai
{
    // Rest stop: a bot with free rations (the item cheat) keeps eating until
    // almostFullHealth instead of stopping at mediumHealth. Start threshold
    // untouched: the trigger band still opens at critical/low/medium.
    inline std::uint32_t RestStopHealthPct(bool hasItemCheat, std::uint32_t mediumHealth,
        std::uint32_t lowHealth, std::uint32_t almostFullHealth)
    {
        // mediumHealth is the unchanged START threshold (the trigger band
        // still opens there); only the stop moves. Named so callers pass
        // all three bands and the invariant stays visible.
        (void)mediumHealth;
        if (hasItemCheat)
            return almostFullHealth;
        return lowHealth;
    }

    // Drink stop, the mana analogue of the rest stop above. The drink trigger
    // still opens at its old line; only the stop rises to the almost-full
    // level so a cheat-bot caster does not re-pull half-oom. Bots without
    // the cheat keep the old 85 stop.
    inline std::uint32_t DrinkStopManaPct(bool hasItemCheat, std::uint32_t almostFullHealth,
        std::uint32_t normalStop = 85)
    {
        return hasItemCheat ? almostFullHealth : normalStop;
    }

    inline bool ShouldDrinkAtManaPct(bool usesMana, std::uint32_t manaPct,
        std::uint32_t stopManaPct)
    {
        return usesMana && manaPct < stopManaPct;
    }

    // Why a bot did not start resting on `.bot action rest` (issue #916). The
    // order already ran the drink/food actions; this only names the first
    // gate that held, so the refusal can say why instead of the generic
    // fallback. Empty means no known gate held.
    struct RestRefusalInput
    {
        bool alive = true;
        bool inCombat = false;        // the bot itself is in combat
        bool groupFighting = false;   // has attackers or a party member near in combat
        bool usesMana = false;
        std::uint32_t manaPct = 100;
        std::uint32_t drinkStopPct = 85;
        std::uint32_t healthPct = 100;
        std::uint32_t eatStopPct = 50;
        bool hasDrink = true;         // a drink in the bags, or the item cheat
        bool hasFood = true;          // food in the bags, or the item cheat
        bool leaderMoving = false;    // its master is moving (follow breaks rest)
    };

    inline std::string RestRefusalReason(RestRefusalInput const& in)
    {
        if (!in.alive)
            return "dead";
        if (in.inCombat)
            return "in combat";
        if (in.groupFighting)
            return "party still fighting";

        bool const needsDrink = in.usesMana && in.manaPct < in.drinkStopPct;
        bool const needsFood = in.healthPct < in.eatStopPct;
        if (!needsDrink && !needsFood)
        {
            // Above both stop lines but not full: the drink and food actions
            // have nothing to do, so the bot does not sit down.
            std::string text = "nothing to rest (";
            if (in.usesMana)
                text += "mana " + std::to_string(in.manaPct) + "% >= " +
                    std::to_string(in.drinkStopPct) + "%, ";
            text += "health " + std::to_string(in.healthPct) + "% >= " +
                std::to_string(in.eatStopPct) + "%)";
            return text;
        }
        bool const drinkBlocked = !needsDrink || !in.hasDrink;
        bool const foodBlocked = !needsFood || !in.hasFood;
        if (drinkBlocked && foodBlocked)
            return needsDrink && needsFood ? "no food or drink" : needsDrink ? "no drink" : "no food";
        if (in.leaderMoving)
            return "leader is moving";
        return "";
    }

    // "No bot rested: Anna in combat; Bert no drink." Bots without a known
    // reason are left out. Empty when no bot has one (the caller keeps its
    // generic text).
    inline std::string FormatRestRefusal(
        std::vector<std::pair<std::string, std::string>> const& reasons)
    {
        std::string text;
        for (auto const& entry : reasons)
        {
            if (entry.second.empty())
                continue;
            text += (text.empty() ? "" : "; ") + entry.first + " " + entry.second;
        }
        if (text.empty())
            return text;
        return "No bot rested: " + text + ".";
    }

    // Drink is only for mana users: warriors (rage) and rogues (energy) get
    // no water, druids/shamans/paladins/hunters (mana) do. Callers pass
    // bot->GetPowerType() == POWER_MANA.
    inline bool ShouldSeedDrink(bool usesMana)
    {
        return usesMana;
    }

    // Pool-only low-HP escape: the restored donor "critical health -> flee"
    // node must not fire for owned/hired bots answering to a real player
    // master (dungeon/raid groups). Pool bots have no real master.
    inline bool ShouldFleeAtCriticalHealth(bool hasRealPlayerMaster)
    {
        return !hasRealPlayerMaster;
    }

    // Pool melee hold ground (AiPlayerbot.PoolMeleeHoldGround, off by
    // default): with the key on, warriors and rogues are not granted the
    // generic "flee" combat strategy. Its panic node has no mana gate for
    // rage/energy users and none of its nodes can outrun a mob, so the flee
    // only stopped the swings. mod-playerbots has no such strategy; its
    // melee fight on with their defensive cooldowns. Key off: unchanged.
    inline bool ShouldGrantPoolFlee(bool meleeHoldGround, bool isRageOrEnergyClass)
    {
        return !(meleeHoldGround && isRageOrEnergyClass);
    }

    // Revenge (self-defence) answers before every other gate in
    // AttackAnythingAction::isUseful: a mob that already holds the bot as
    // its victim is fought while travelling, while wounded, and by healers
    // that otherwise start no fights. Answering pulls nothing new.
    // Callers pass target->GetVictim() == bot (non-player hostile target).
    inline bool ShouldAnswerAttacker(bool victimIsBot)
    {
        return victimIsBot;
    }

    // Hard-mob ignore (AttackersValue::IgnoreTarget): a +6 mob on a long
    // trip is walked past - unless it is already fighting the bot (victim
    // or threat), in which case it stays a valid attacker so combat state
    // and revenge engage. Without the carve-out the bot could die to it
    // without a swing.
    inline bool ShouldIgnoreHardHostile(int targetLevel, int botLevel, bool alreadyFightingBot)
    {
        return !alreadyFightingBot && targetLevel > botLevel + 5;
    }

    // Outnumbered hold ground for the hybrid self-healers (OutNumberedTrigger):
    // a solo shaman, paladin or druid heals through an outnumbered fight like a
    // solo priest instead of breaking off, but only while it still has the
    // mana to heal (at or above LowMana, the same line PanicTrigger treats as
    // "mana to fight on"). Out of mana, or in a group, it keeps the flee.
    inline bool ShouldSelfHealerHoldGround(bool isHybridSelfHealer, bool inGroup,
        bool hasMana, std::uint32_t manaPct, std::uint32_t lowManaPct)
    {
        return isHybridSelfHealer && !inGroup && hasMana && manaPct >= lowManaPct;
    }

    // Death telemetry: the deaths.csv 'adds' loop used to count victim-
    // filtered "all targets" entries, but SetDeathState drains the attacker
    // set and clears victim pointers before OnDeath runs, so it was
    // structurally always 0. Instead PlayerbotAI samples the live attacker
    // set while fighting (NoteFightAttackers) and the death writer reads the
    // snapshot below. Entries cap the per-tick copy; the window bounds how
    // old a sample may be and still count.
    struct DeathAttackerEntry
    {
        std::string name;
        std::uint32_t level = 0;
    };

    std::uint32_t const kDeathAttackerSnapshotWindowMs = 30000;
    std::size_t const kDeathAttackerListCap = 8;

    // Wrap-safe freshness like DeathClusterPolicy: unsigned subtraction stays
    // correct across the ~49 d WorldTimer wrap; 0 means no sample yet.
    inline bool IsDeathAttackerSnapshotFresh(std::uint32_t nowMs, std::uint32_t snapshotMs,
        std::uint32_t windowMs = kDeathAttackerSnapshotWindowMs)
    {
        if (snapshotMs == 0)
            return false;
        return (nowMs - snapshotMs) <= windowMs;
    }

    // Formats the snapshot the way the old loop did — Name(level) entries
    // concatenated with no separator, killer excluded by name (same-kind
    // adds sharing the killer's name stay excluded, as before), count of the
    // kept entries. Keeps the deaths.csv schema and dashboard stable.
    inline std::pair<std::string, std::uint32_t> FormatDeathAttackers(
        std::vector<DeathAttackerEntry> const& entries, std::string const& killerName)
    {
        std::string text;
        std::uint32_t count = 0;
        for (DeathAttackerEntry const& entry : entries)
        {
            if (!killerName.empty() && entry.name == killerName)
                continue;
            if (count >= kDeathAttackerListCap)
                break;
            text += entry.name + "(" + std::to_string(entry.level) + ")";
            ++count;
        }
        return { text, count };
    }
}
