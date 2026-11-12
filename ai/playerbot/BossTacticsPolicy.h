#pragma once

// Per-boss tactics table and its pure decision rules (opt-in, see
// docs/play/boss-tactics.md).
//
// The rows live in the world table `ai_boss_tactic`, keyed by map + boss
// creature entry, and are read ONCE at startup into BossTacticTable (loader:
// ai/playerbot/BossTacticsMgr.cpp). Lookups never touch the database. A row
// is "active" while a live creature with its boss entry attacks the bot's
// side; the strategy "boss tactics" then applies it.
//
// Pattern: mod-playerbots' per-map raid strategies (trigger -> action per
// boss mechanic) reimplemented as data rows; provenance in its PR.
//
// No core includes: callers translate game state into plain inputs so the
// rules stay testable in tools/test_boss_tactics_policy.cpp. The table has
// the same lifecycle as runtime/NamedLocationIndex.h: one loading thread adds
// rows, calls Seal() once, and readers see nothing before the seal.

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "BossAnchorPolicy.h"
#include "GuardGracePolicy.h"
#include "policy/combat/RearFlankPolicy.h"
#include "policy/heal/DispelTargetPolicy.h"

namespace ai
{
    // Row kinds. The text form is what the `kind` column holds.
    enum class BossTacticKind : std::uint8_t
    {
        None = 0,
        Interrupt = 1,  // interrupt `spell_id` while the caster casts it
        KillFirst = 2,  // DPS bots kill `target_entry` before anything else; lowest `priority` first
        Behind = 3,     // non-tank bots stay out of the boss's front within `param` yd (frontal cleave)
        AvoidCast = 4,  // non-tank bots leave `param` yd around the caster while it casts `spell_id`
        TankHold = 5,   // opt-in: a tank may stay in this caster's avoid_cast blast while the caster is on a non-tank
        HoldKill = 6,   // DPS leave `boss_entry` alone while more than `priority` living units of `target_entry` are near
        OneAtATime = 7, // all DPS kill one living unit of `boss_entry` at a time: the lowest health, the lowest guid on a tie
        LinkedDeath = 8, // a group that must die together: equalise its health, hold every member at <= `param` %, then burn
        DpsHoldAfter = 9,    // hold damage on the row's subject for `param` seconds (see TacticMode)
        DispelPriority = 10,  // cleanse the row's aura in `priority` order, under the row's TacticMode
        FleeOnAura = 11,      // a member carrying the row's aura moves out of the group (or to the tank)
        FreezeOnAura = 12,    // a member carrying the row's aura stands still and does nothing
        TauntAfterCast = 13,  // the row's tank index taunts the subject back after the row's trigger
        SwapOnAura = 14,      // the tanks rotate the boss at `param` stacks of the row's aura
        UseObject = 15,       // one bot clicks the ready game object of `target_entry` nearest the boss
        SeekAura = 16,        // the raid gathers in the cloud of a dying `target_entry` to carry `spell_id`
        PullGate = 17,        // the run leader never pulls the boss while it wears `spell_id` above the stack band
        Spread = 18,          // the ranged raid keeps `param` yd apart while the boss fights
        NeedsAura = 19,       // only a bot carrying `spell_id` may target `target_entry`
        BurnMana = 20,        // up to `priority` eligible bots drain the boss's mana while it is at or above `param` %
        RangedOnly = 21,      // melee bots leave the boss alone while it stands `param` yd or more above them
        ThreatSuccessor = 22, // damage dealers are capped at the highest off-tank's threat (ThreatCapPolicy.h)
        Anchor = 23,          // while the boss fights, every boss/raid move stays within `param` yd of the spot
        TankIgnore = 24,      // while the boss fights, tanks leave `target_entry` to the damage dealers (no guard, no taunt)
        SeekPull = 25,        // the seek walk-out's rules for one source: walk-outs a cycle, the kill lead, the last start
        GateHold = 26,        // DPS spare `boss_entry` at or below `param` % while more than `priority` blockers fight near
        TankPost = 27,        // a named tank waits at a spot for a unit that is not in the fight yet, then attacks it
        AddCrew = 28,         // once `target_entry` is dead, the boss's add duty goes to a named crew
        HealReserve = 29,     // healers keep the row's mana % back from top-ups outside the row's release window
    };

    // The per-kind mode word of a row (`tactic_mode`), or Invalid when the word does not belong to the row's kind. An
    // empty word is the kind's own behaviour (Default). The loader skips an Invalid row.
    enum class TacticMode : std::uint8_t
    {
        Default = 0,
        Now = 1,       // dispel_priority: remove the aura as soon as it lands
        Clear = 2,     // dispel_priority: remove it once the carrier is clear of the group
        Never = 3,     // dispel_priority: never remove it
        Away = 4,      // flee_on_aura: run until no other group member is near
        ToTank = 5,    // flee_on_aura: walk to the main tank
        Engage = 6,    // dps_hold_after, taunt_after_cast: the first engagement
        Blink = 7,     // dps_hold_after, taunt_after_cast: the subject teleported
        UntilTank = 8, // dps_hold_after, taunt_after_cast: until the subject is on a tank
        Invalid = 9,
        TankAnchor = 10, // use_object: the tank the boss is on also brings the boss to the object
        Assigned = 11, // interrupt: a directed row the run leader may order (F3), inert until its key reads it
        All = 12,      // avoid_cast: every bot leaves the blast while the spell is cast, tanks too (F4-A)
        Strict = 13,   // threat_successor: the row opts the boss into AiPlayerbot.DungeonRunThreatSuccessorStrict
        HoldSpot = 14, // tank_post: the post starts during another fight, latches its arrival and drags back
    };

    inline bool IsAuraTacticKind(BossTacticKind kind)
    {
        return kind == BossTacticKind::DpsHoldAfter || kind == BossTacticKind::DispelPriority ||
            kind == BossTacticKind::FleeOnAura || kind == BossTacticKind::FreezeOnAura ||
            kind == BossTacticKind::TauntAfterCast || kind == BossTacticKind::SwapOnAura;
    }

    inline TacticMode ParseTacticMode(BossTacticKind kind, const std::string& text)
    {
        if (text.empty())
            return TacticMode::Default;
        switch (kind)
        {
            case BossTacticKind::DispelPriority:
                if (text == "now")
                    return TacticMode::Now;
                if (text == "clear")
                    return TacticMode::Clear;
                if (text == "never")
                    return TacticMode::Never;
                break;
            case BossTacticKind::FleeOnAura:
                if (text == "away")
                    return TacticMode::Away;
                if (text == "to_tank")
                    return TacticMode::ToTank;
                break;
            case BossTacticKind::DpsHoldAfter:
            case BossTacticKind::TauntAfterCast:
                if (text == "engage")
                    return TacticMode::Engage;
                if (text == "blink")
                    return TacticMode::Blink;
                if (text == "until_tank")
                    return TacticMode::UntilTank;
                break;
            case BossTacticKind::UseObject:
                if (text == "tank_anchor")
                    return TacticMode::TankAnchor;
                break;
            case BossTacticKind::AvoidCast:
                if (text == "all")
                    return TacticMode::All;
                break;
            case BossTacticKind::ThreatSuccessor:
                if (text == "strict")
                    return TacticMode::Strict;
                break;
            case BossTacticKind::TankPost:
                if (text == "hold_spot")
                    return TacticMode::HoldSpot;
                break;
            default:
                break;
        }
        return TacticMode::Invalid;
    }

    inline BossTacticKind ParseBossTacticKind(const std::string& text)
    {
        if (text == "interrupt")
            return BossTacticKind::Interrupt;
        if (text == "kill_first")
            return BossTacticKind::KillFirst;
        if (text == "behind")
            return BossTacticKind::Behind;
        if (text == "avoid_cast")
            return BossTacticKind::AvoidCast;
        if (text == "tank_hold")
            return BossTacticKind::TankHold;
        if (text == "hold_kill")
            return BossTacticKind::HoldKill;
        if (text == "one_at_a_time")
            return BossTacticKind::OneAtATime;
        if (text == "linked_death" || text == "together")
            return BossTacticKind::LinkedDeath;
        if (text == "dps_hold_after")
            return BossTacticKind::DpsHoldAfter;
        if (text == "dispel_priority")
            return BossTacticKind::DispelPriority;
        if (text == "flee_on_aura")
            return BossTacticKind::FleeOnAura;
        if (text == "freeze_on_aura")
            return BossTacticKind::FreezeOnAura;
        if (text == "taunt_after_cast")
            return BossTacticKind::TauntAfterCast;
        if (text == "swap_on_aura")
            return BossTacticKind::SwapOnAura;
        if (text == "use_object")
            return BossTacticKind::UseObject;
        if (text == "seek_aura")
            return BossTacticKind::SeekAura;
        if (text == "pull_gate")
            return BossTacticKind::PullGate;
        if (text == "spread")
            return BossTacticKind::Spread;
        if (text == "needs_aura")
            return BossTacticKind::NeedsAura;
        if (text == "burn_mana")
            return BossTacticKind::BurnMana;
        if (text == "ranged_only")
            return BossTacticKind::RangedOnly;
        if (text == "threat_successor")
            return BossTacticKind::ThreatSuccessor;
        if (text == "anchor")
            return BossTacticKind::Anchor;
        if (text == "tank_ignore")
            return BossTacticKind::TankIgnore;
        if (text == "seek_pull")
            return BossTacticKind::SeekPull;
        if (text == "gate_hold")
            return BossTacticKind::GateHold;
        if (text == "tank_post")
            return BossTacticKind::TankPost;
        if (text == "heal_reserve")
            return BossTacticKind::HealReserve;
        if (text == "add_crew")
            return BossTacticKind::AddCrew;
        return BossTacticKind::None;
    }

    struct BossTacticRow
    {
        std::uint32_t map = 0;
        std::uint32_t bossEntry = 0;    // creature whose presence in the fight activates the row
        BossTacticKind kind = BossTacticKind::None;
        std::uint32_t targetEntry = 0;  // creature the row is about; 0 = the boss itself
        std::uint32_t spellId = 0;      // interrupt, avoid_cast: the spell; needs_aura: the aura the bot must carry
        std::uint8_t priority = 0;      // kill_first: lower is killed first
        float param = 0.0f;             // behind, avoid_cast: radius in yards
        // linked_death: the release burn's order, a comma list of the group's entries in `burn_order` (the loader
        // fills it). Empty for every other kind and for a group without one.
        std::vector<std::uint32_t> burnOrder;
        // The row's gates (`hp_min`, `hp_max`, `bot_aura`): the boss's health % band (inclusive) and the aura a bot
        // must carry. Only a row a fight scan reads may carry one (RowGateSet); 0/100/0 is no gate.
        std::uint8_t hpMin = 0;
        std::uint8_t hpMax = 100;
        std::uint32_t botAura = 0;
        // The kind's own mode word (`tactic_mode`), parsed by ParseTacticMode.
        TacticMode mode = TacticMode::Default;
        // linked_death: the release latch's window in seconds (`window_s`).
        float windowSec = 7.0f;
        // seek_aura: the spell whose aura on a group member restarts the cycle (`cycle_spell`), the cycle's length in
        // seconds (`cycle_s`), and the spot the source is held at (`spot_x/y/z`). 0 for every other kind.
        std::uint32_t cycleSpell = 0;
        float cycleSec = 0.0f;
        float spotX = 0.0f, spotY = 0.0f, spotZ = 0.0f;

        bool HasSpot() const { return spotX != 0.0f || spotY != 0.0f || spotZ != 0.0f; }
    };

    // A threat_successor row's Cause Insanity timer (`cycle_spell`): the reset window is open when the time since the
    // last reset lies in [k*minGapMs, k*maxGapMs] for k = 1..3 (the double gaps included), and a damage dealer at or
    // above stopPct of the reset reference holds its damage on the boss. `param` and `window_s` hold the gap bounds.
    struct ThreatResetWindow
    {
        std::uint32_t spell = 0;
        std::uint32_t minGapMs = 0;
        std::uint32_t maxGapMs = 0;
        std::uint8_t stopPct = 0;
    };

    // An anchor row's circle for one bot: the row's spot, and the tanks' radius (`priority`, 0: the shared one) or the
    // shared radius (`param`).
    inline BossAnchorCircle AnchorCircleOf(const BossTacticRow& row, bool tank)
    {
        BossAnchorCircle c;
        c.x = row.spotX;
        c.y = row.spotY;
        c.z = row.spotZ;
        c.radius = BossAnchorRadius(row.param, static_cast<float>(row.priority), tank);
        return c;
    }

    // Whether the row carries a gate at all. Only the scan-read kinds may: hold_kill, tank_hold, one_at_a_time and
    // linked_death are read straight from the table, so they could never answer it.
    inline bool RowGateSet(const BossTacticRow& row)
    {
        return row.hpMin != 0 || row.hpMax != 100 || row.botAura != 0;
    }

    // Whether a row is open for this bot right now. `keyOn` is AiPlayerbot.DungeonRunAuraTactics: with it off a
    // gated row is closed, so the table behaves exactly as before the gates existed. The health band is the row's
    // boss entry's health (`bossHpPct`, inclusive; the unit PickBossGateUnit picks), never the add's; `botHasGateAura`
    // is whether this bot carries `botAura`. An ungated row is open whenever the band and the aura allow it.
    inline bool RowGateOpen(const BossTacticRow& row, bool keyOn, int bossHpPct, bool botHasGateAura)
    {
        if (RowGateSet(row) && !keyOn)
            return false;
        if (bossHpPct < row.hpMin || bossHpPct > row.hpMax)
            return false;
        if (row.botAura != 0 && !botHasGateAura)
            return false;
        return true;
    }

    // Whether a needs_aura row keeps this bot off the unit: the key is on, the unit's entry has a row, and the bot
    // carries none of the row's aura. One rule for the attack pickers, the taunt picks and the run guard.
    inline bool AuraGateRefuses(bool keyOn, std::uint32_t requiredAura, bool botHasAura)
    {
        return keyOn && requiredAura != 0 && !botHasAura;
    }
    // Whether the per-bot combat gate (AttackersValue::InCombat) applies to a target. A carrier of a needs_aura unit's aura
    // skips it: the Shade of Jin'do copies the boss's threat list at 0 threat (Creature::AddThreatsOf), so no carrier is "in
    // combat" with it before its first hit, and the kill_first pick, the run's carrier skip and the held target all refused
    // it. Everything else (friendly, evade, visibility, range, ignore lists) still applies.
    inline bool CombatGateApplies(bool keyOn, std::uint32_t requiredAura, bool botHasAura)
    {
        return !(keyOn && requiredAura != 0 && botHasAura);
    }


    // One living unit of a boss entry the fight sees: its max health, current health % and guid.
    struct BossGateUnit
    {
        std::uint32_t maxHealth = 0;
        int hpPct = 0;
        std::uint64_t guid = 0;
    };

    // Whether `a` outranks `b` as its entry's authoritative unit. Max health first: a boss clone or add of the same
    // entry has less health than the real boss, so a low-health clone never supplies the band. Then the highest
    // current %, then the lowest guid: the pick depends only on the units, not on the order the scan met them.
    inline bool BossGateUnitBefore(const BossGateUnit& a, const BossGateUnit& b)
    {
        if (a.maxHealth != b.maxHealth)
            return a.maxHealth > b.maxHealth;
        if (a.hpPct != b.hpPct)
            return a.hpPct > b.hpPct;
        return a.guid < b.guid;
    }

    // The index of the unit an entry's rows gate on, -1 for an empty list.
    inline int PickBossGateUnit(const std::vector<BossGateUnit>& units)
    {
        int pick = -1;
        for (int i = 0; i < static_cast<int>(units.size()); ++i)
        {
            if (pick < 0 || BossGateUnitBefore(units[i], units[pick]))
                pick = i;
        }
        return pick;
    }

    // A boss that vanishes mid-fight (ScriptedAI::EnterVanish: invisible, not selectable, attack stopped; High Priestess
    // Arlokk) drops out of the bot's attackers list while it stays alive and in combat. The fight scan loads a boss
    // entry's rows only from the attackers, so the rows that handle its adds (Arlokk's prowler kill_first) would end
    // with the boss's visibility. This memory carries the entry across the gap: it is noted whenever the scan sees a boss
    // entry that has rows, and handed back as "absent" while the bot still has attackers and the sighting is young.
    constexpr std::uint32_t kBossVanishGraceMs = 90000;   // Arlokk's Vanish lasts 35-50 s; two vanishes are ~145 s apart
    constexpr float kBossVanishRangeYd = 60.0f;           // the remembered unit must still be this near the bot

    class BossPresenceMemory
    {
    public:
        struct Seen
        {
            std::uint32_t entry = 0;
            std::uint64_t guid = 0;
            int hpPct = 0;
            std::uint32_t seenMs = 0;
        };

        // The scan saw this boss entry's unit (the PickBossGateUnit pick) at nowMs.
        void Note(std::uint32_t entry, std::uint64_t guid, int hpPct, std::uint32_t nowMs)
        {
            for (Seen& seen : seen_)
            {
                if (seen.entry == entry)
                {
                    seen.guid = guid;
                    seen.hpPct = hpPct;
                    seen.seenMs = nowMs;
                    return;
                }
            }
            Seen seen;
            seen.entry = entry;
            seen.guid = guid;
            seen.hpPct = hpPct;
            seen.seenMs = nowMs;
            seen_.push_back(seen);
        }

        // The remembered entries not among `presentEntries` (this tick's attackers) whose sighting is within the grace
        // window. A bot with no attackers is out of the fight, so everything is forgotten; an expired sighting is
        // forgotten too.
        std::vector<Seen> Absent(const std::vector<std::uint32_t>& presentEntries, std::uint32_t nowMs, bool hasAttackers)
        {
            std::vector<Seen> out;
            if (!hasAttackers)
            {
                seen_.clear();
                return out;
            }
            for (std::size_t i = 0; i < seen_.size();)
            {
                if (nowMs - seen_[i].seenMs > kBossVanishGraceMs)
                {
                    seen_.erase(seen_.begin() + static_cast<std::ptrdiff_t>(i));
                    continue;
                }
                if (std::find(presentEntries.begin(), presentEntries.end(), seen_[i].entry) == presentEntries.end())
                    out.push_back(seen_[i]);
                ++i;
            }
            return out;
        }

        bool Empty() const { return seen_.empty(); }

    private:
        std::vector<Seen> seen_;
    };

    // Whether the remembered unit of a vanished boss still belongs to this bot's fight: it is still in the map, alive,
    // not running home, in combat, and near the bot.
    inline bool VanishedBossStillInFight(bool found, bool alive, bool evading, bool inCombat, float distanceYd)
    {
        return found && alive && !evading && inCombat && distanceYd <= kBossVanishRangeYd;
    }

    // The creature a row is about. A use_object row is about its boss: the target is a game object, not a creature.
    inline std::uint32_t TacticSubjectEntry(const BossTacticRow& row)
    {
        if (row.kind == BossTacticKind::UseObject)
            return row.bossEntry;
        return row.targetEntry ? row.targetEntry : row.bossEntry;
    }

    // A table's revision: unique for the process, assigned when the table is constructed. A freed table's address can
    // come back for a changed table, so a cache of the table's content keys on the revision, never on the address.
    inline std::uint64_t NextBossTacticRevision()
    {
        static std::atomic<std::uint64_t> counter{0};
        return counter.fetch_add(1, std::memory_order_relaxed) + 1;
    }
    // One linked_death group: its anchor (the anchor's rows) and its group id (`priority`). One anchor can carry
    // several groups on one map.
    struct LinkedDeathGroupKey
    {
        std::uint32_t anchor = 0;
        std::uint8_t group = 0;
    };
    // The tank slots a unit is assigned to by its own rows, one per kind: an until_tank row (read only with
    // AiPlayerbot.DungeonRunAuraTactics) and a tank_post row (read only with AiPlayerbot.DungeonRunTankPost). Each is
    // the lowest slot of its own kind; 0: no row of that kind assigns the unit. The kinds name different tanks, so the
    // guard takes the slot of the kind whose own key is on (SlotFor).
    struct AssignedTankSlot
    {
        std::uint32_t untilTankSlot = 0;
        std::uint32_t tankPostSlot = 0;

        // The slot the raid guard leaves the unit to: the lowest slot among the kinds whose key is on, 0 when no
        // enabled kind names the unit.
        std::uint32_t SlotFor(bool auraTactics, bool tankPost) const
        {
            std::uint32_t slot = 0;
            if (auraTactics && untilTankSlot > 0)
                slot = untilTankSlot;
            if (tankPost && tankPostSlot > 0 && (slot == 0 || tankPostSlot < slot))
                slot = tankPostSlot;
            return slot;
        }
    };

    // An add_crew row: the switch unit whose death hands the boss's adds over (`switchEntry`), and how many AoE
    // ranged bots of the raid join the crew (`crewSize`, 0: melee and tanks only).
    struct AddCrewRow
    {
        std::uint32_t switchEntry = 0;
        std::uint8_t crewSize = 0;
    };

    class BossTacticTable
    {
    public:
        BossTacticTable() : revision_(NextBossTacticRevision()) {}

        std::uint64_t Revision() const { return revision_; }

        // Loading only. False after Seal(), for an unusable row, or when the
        // (map, boss, kind, target, spell, hp_min, hp_max) key is already there: the
        // first row wins, as it would under the table's primary key.
        bool Add(const BossTacticRow& row)
        {
            if (sealed_.load(std::memory_order_acquire))
                return false;
            if (row.map == 0 || row.bossEntry == 0 || row.kind == BossTacticKind::None)
                return false;
            // A mode word the row's kind does not take (ParseTacticMode) makes the whole row unusable.
            if (row.mode == TacticMode::Invalid)
                return false;
            // The health band is a percentage range, low to high.
            if (row.hpMin > row.hpMax || row.hpMax > 100)
                return false;
            // Only a row a fight scan answers can carry a gate. These kinds are read straight from the table (the dps
            // and tank pickers, the linked_death scan, the threat reference), so a gate on one of them would never be
            // consulted.
            if (RowGateSet(row) && (row.kind == BossTacticKind::HoldKill || row.kind == BossTacticKind::TankHold ||
                    row.kind == BossTacticKind::OneAtATime || row.kind == BossTacticKind::LinkedDeath ||
                    row.kind == BossTacticKind::ThreatSuccessor || row.kind == BossTacticKind::TankIgnore ||
                    row.kind == BossTacticKind::AddCrew))
                return false;
            if (row.kind == BossTacticKind::Interrupt && row.spellId == 0)
                return false;
            if (row.kind == BossTacticKind::Behind && !(row.param > 0.0f))
                return false;
            if (row.kind == BossTacticKind::AvoidCast && (row.spellId == 0 || !(row.param > 0.0f)))
                return false;
            // A tank_hold row is keyed by the caster's boss_entry (target_entry 0): HasTankHold reads the subject while
            // IsTankHoldPickup looks the caster up as a boss_entry, so a target_entry would key the two differently.
            if (row.kind == BossTacticKind::TankHold && row.targetEntry != 0)
                return false;
            // The same for a hold_kill row: its boss_entry is the entry the DPS leave alone and its target_entry the
            // blocker that holds them off, so a row without a blocker could never hold anything.
            if (row.kind == BossTacticKind::HoldKill && row.targetEntry == 0)
                return false;
            // A one_at_a_time row is keyed by the entry it paces, its own boss_entry (like a tank_hold row): a
            // target_entry would leave the row's entry and the paced entry disagreeing.
            if (row.kind == BossTacticKind::OneAtATime && row.targetEntry != 0)
                return false;
            // A linked_death row is one partner of a group, its boss_entry the anchor: a row without a partner lists
            // nobody, and the hold percent has to be a percentage.
            if (row.kind == BossTacticKind::LinkedDeath &&
                (row.targetEntry == 0 || !(row.param > 0.0f) || row.param > 100.0f || !(row.windowSec > 0.0f)))
                return false;
            // dps_hold_after: the hold in seconds, more than zero and at most 30.
            if (row.kind == BossTacticKind::DpsHoldAfter && (!(row.param > 0.0f) || row.param > 30.0f))
                return false;
            // A dispel_priority row ranks one aura of the row's boss.
            if (row.kind == BossTacticKind::DispelPriority && row.spellId == 0)
                return false;
            // flee_on_aura: the aura that sends the carrier away, and (mode away) the distance to keep, never negative.
            if (row.kind == BossTacticKind::FleeOnAura && (row.spellId == 0 || row.param < 0.0f))
                return false;
            if (row.kind == BossTacticKind::FreezeOnAura && row.spellId == 0)
                return false;
            // taunt_after_cast: the deadline in seconds and a trigger, a spell or one of the two spell-less modes.
            if (row.kind == BossTacticKind::TauntAfterCast && (!(row.param > 0.0f) ||
                    (row.spellId == 0 && row.mode != TacticMode::Blink && row.mode != TacticMode::UntilTank)))
                return false;
            // swap_on_aura: the aura and a rotation of at least two tanks (priority is the rotation's size).
            if (row.kind == BossTacticKind::SwapOnAura && (row.spellId == 0 || row.priority < 2))
                return false;
            // use_object: the game object to click; a clicker role of free DPS, healer or non-tank; the boss must
            // stand within `param` yd and still. The timer restarts every click (or runs from first sight before any),
            // so a row with no trigger at all could never fire, and a short non-zero window would refire faster than
            // the object can arm: only 0 or at least 10 s.
            if (row.kind == BossTacticKind::UseObject &&
                (row.targetEntry == 0 || row.priority > 2 || !(row.param > 0.0f) || row.param > 60.0f ||
                    (row.windowSec != 0.0f && row.windowSec < 10.0f) || (row.spellId == 0 && row.windowSec == 0.0f)))
                return false;
            // seek_aura: the source whose death leaves the cloud, the aura the bots must carry, the cycle spell and
            // length, and the spot the source is held at. `param` is the gather radius around the source, `priority`
            // the health % the DPS stop at, and the dose window has to fit inside the cycle.
            if (row.kind == BossTacticKind::SeekAura &&
                (row.targetEntry == 0 || row.spellId == 0 || row.cycleSpell == 0 || !row.HasSpot() ||
                    !(row.param > 0.0f) || row.param > 10.0f || row.priority > 99 ||
                    !(row.cycleSec > 0.0f) || row.cycleSec > 600.0f ||
                    !(row.windowSec > 0.0f) || row.windowSec >= row.cycleSec))
                return false;
            // pull_gate: the boss itself (target_entry 0), the stake spell it wears and the seconds the run holds
            // before it ends. priority is the most stacks allowed.
            if (row.kind == BossTacticKind::PullGate &&
                (row.targetEntry != 0 || row.spellId == 0 || !(row.param > 0.0f) || row.param > 300.0f))
                return false;
            // spread: the spacing the answer needs, in yards; the spell only documents the splash.
            if (row.kind == BossTacticKind::Spread &&
                (row.targetEntry != 0 || row.param < 5.0f || row.param > 30.0f))
                return false;
            // needs_aura: the entry and the aura a bot must carry, and nothing else. The aura is the row's own gate, so
            // a health band or a bot aura could never be answered, and the kind takes no mode word.
            if (row.kind == BossTacticKind::NeedsAura &&
                (row.targetEntry == 0 || row.spellId == 0 || RowGateSet(row) || row.mode != TacticMode::Default))
                return false;
            // burn_mana: the boss itself (target_entry 0), the start percent it burns from (exclusive 0 and 100), and
            // the most burners (0 = every eligible bot, so 0-10). spell_id is unused and the kind takes no mode word.
            if (row.kind == BossTacticKind::BurnMana &&
                (row.targetEntry != 0 || !(row.param > 0.0f) || row.param >= 100.0f || row.priority > 10 ||
                    row.mode != TacticMode::Default))
                return false;
            // ranged_only: the boss itself or an entry, and the vertical gap the melee bots are held under. The kind
            // takes no mode word (`param` is the whole rule).
            if (row.kind == BossTacticKind::RangedOnly &&
                (row.param < 3.0f || row.param > 40.0f || row.mode != TacticMode::Default))
                return false;
            // threat_successor: the boss itself and nothing else — the row marks the creature whose damage dealers are
            // capped at the off-tank. It carries no gate (see above). Its one mode word is `strict`: the boss opts into
            // AiPlayerbot.DungeonRunThreatSuccessorStrict (HasStrictThreatSuccessor); empty keeps the plain cap.
            if (row.kind == BossTacticKind::ThreatSuccessor &&
                (row.targetEntry != 0 || (row.mode != TacticMode::Default && row.mode != TacticMode::Strict)))
                return false;
            // A threat_successor row with a cycle_spell carries the boss's reset window too: `param` and `window_s` the
            // earliest and latest gaps in seconds (5 <= param < window_s <= 60), `priority` the damage dealers' stop
            // percent (50-100). A row without one is today's plain successor cap; cycle_s is never used.
            if (row.kind == BossTacticKind::ThreatSuccessor &&
                (row.cycleSec != 0.0f ||
                 (row.cycleSpell != 0 && (!(row.param >= 5.0f) || !(row.param < row.windowSec) ||
                                          !(row.windowSec <= 60.0f) || row.priority < 50 || row.priority > 100))))
                return false;
            // tank_ignore: the entry the tanks leave alone while the row's boss fights, and nothing else. It is read
            // straight from the table (the tank pickers, the run guard), so it carries no gate (see above) and no mode.
            if (row.kind == BossTacticKind::TankIgnore &&
                (row.targetEntry == 0 || row.spellId != 0 || row.mode != TacticMode::Default))
                return false;
            // seek_pull: the seek source (`target_entry`), the most walk-outs in one cycle (`priority`, 1-5), the kill
            // lead in seconds (`param`, 10-85) and the last start watchdog (`window_s`, 10-85). Read straight from the
            // table (SeekPullRow), so it carries no gate, and it names no spell, spot or cycle columns.
            if (row.kind == BossTacticKind::SeekPull &&
                (row.targetEntry == 0 || row.spellId != 0 || row.priority < 1 || row.priority > 5 ||
                    row.param < 10.0f || row.param > 85.0f || row.windowSec < 10.0f || row.windowSec > 85.0f ||
                    row.mode != TacticMode::Default))
                return false;
            // gate_hold: the spared entry (boss_entry), its blocker (target_entry), the health gate (`param`, 1-99 %)
            // and the blockers that may still fight (`priority`). `window_s` is the blocker search radius (0: 200 yd,
            // else 50-250). The blocker must be a different unit: the spared boss would count as its own blocker and
            // hold itself forever. The row's own health gate is `param`, so the band columns, a spell, a spot and a
            // mode word are refused.
            if (row.kind == BossTacticKind::GateHold &&
                (row.targetEntry == 0 || row.targetEntry == row.bossEntry || row.spellId != 0 ||
                    !(row.param >= 1.0f) || row.param > 99.0f ||
                    !(row.windowSec == 0.0f || (row.windowSec >= 50.0f && row.windowSec <= 250.0f)) ||
                    RowGateSet(row) || row.mode != TacticMode::Default))
                return false;
            // anchor: the boss itself (target_entry 0, no spell); the centre is the spot (spot_x/y/z, the boss's home),
            // `param` the radius for everyone and `priority` the tanks' own radius (0: the shared one, never wider).
            // It takes no mode word.
            if (row.kind == BossTacticKind::Anchor &&
                (row.targetEntry != 0 || row.spellId != 0 || !row.HasSpot() || !(row.param >= 10.0f) ||
                    row.param > 60.0f || static_cast<float>(row.priority) > row.param ||
                    row.mode != TacticMode::Default))
                return false;
            // tank_post: the awaited unit (`target_entry`), the tank slot (priority 1-7), the wait spot, the arrival
            // radius, 5-100 yd: a subject that lands and runs after a healer across the room, and the longest hold. A
            // fight-scan kind, so its health band arms the post. No spell, no cycle columns; the only mode word is
            // `hold_spot`.
            if (row.kind == BossTacticKind::TankPost &&
                (row.targetEntry == 0 || row.priority < 1 || row.priority > 7 || !row.HasSpot() ||
                    row.param < 5.0f || row.param > 100.0f || row.windowSec < 10.0f || row.windowSec > 120.0f ||
                    row.spellId != 0 || (row.mode != TacticMode::Default && row.mode != TacticMode::HoldSpot)))
                return false;
            // heal_reserve: the boss itself (target_entry 0, no spell, priority 0), the reserve percent (`param`,
            // 10-100) and the release lead in seconds (`window_s`, 0-120). A fight-scan kind, so its health band
            // arms it. No mode word, no spot and no cycle columns.
            if (row.kind == BossTacticKind::HealReserve &&
                (row.targetEntry != 0 || row.spellId != 0 || row.priority != 0 || row.param < 10.0f ||
                    row.param > 100.0f || row.windowSec < 0.0f || row.windowSec > 120.0f ||
                    row.mode != TacticMode::Default))
                return false;
            // add_crew: the switch unit whose death hands the boss's adds over (`target_entry`, never the boss itself)
            // and the crew size (`priority`, 0-5; the first `priority` AoE ranged bots of the crew class order join the
            // melee and the tanks). No spell, no radius, no gate (see above) and no mode word.
            if (row.kind == BossTacticKind::AddCrew &&
                (row.targetEntry == 0 || row.targetEntry == row.bossEntry || row.spellId != 0 ||
                    row.priority > 5 || row.param != 0.0f || RowGateSet(row) || row.mode != TacticMode::Default))
                return false;
            // The cycle columns are the seek_aura kind's and the reset window's (threat_successor with a cycle_spell),
            // the spot columns the seek_aura, anchor and tank_post kinds'.
            if (row.kind != BossTacticKind::SeekAura && row.kind != BossTacticKind::ThreatSuccessor &&
                (row.cycleSpell != 0 || row.cycleSec != 0.0f))
                return false;
            if (row.kind != BossTacticKind::SeekAura && row.kind != BossTacticKind::Anchor &&
                row.kind != BossTacticKind::TankPost && row.HasSpot())
                return false;
            // A gate is a fight-scan question; these five answer it themselves or not at all (a pull gate already
            // decides the run).
            if (RowGateSet(row) && (row.kind == BossTacticKind::SeekAura || row.kind == BossTacticKind::PullGate ||
                    row.kind == BossTacticKind::Spread || row.kind == BossTacticKind::BurnMana ||
                    row.kind == BossTacticKind::RangedOnly || row.kind == BossTacticKind::Anchor ||
                    row.kind == BossTacticKind::SeekPull))
                return false;
            std::vector<BossTacticRow>& rows = rows_[Key(row.map, row.bossEntry)];
            for (const BossTacticRow& have : rows)
            {
                if (have.kind == row.kind && have.targetEntry == row.targetEntry && have.spellId == row.spellId &&
                    have.hpMin == row.hpMin && have.hpMax == row.hpMax)
                    return false;
            }
            rows.push_back(row);
            maps_[row.map] = true;
            // The automatic crowd control's exclusions (Commander's run facts): a kill_first unit is killed, a
            // linked_death group's partner must die with it, and a seek_aura source is dragged to the spot and burned
            // there (a polymorphed or trapped Son cannot be dragged), so none is ever held.
            if (row.kind == BossTacticKind::KillFirst || row.kind == BossTacticKind::LinkedDeath ||
                row.kind == BossTacticKind::SeekAura)
                noCcEntries_[row.map].insert(row.targetEntry != 0 ? row.targetEntry : row.bossEntry);
            if (row.kind == BossTacticKind::FreezeOnAura)
                freezeAuras_[row.map].push_back(row.spellId);
            if (row.kind == BossTacticKind::AvoidCast)
                avoidCastMaps_[row.map] = true;
            if (row.kind == BossTacticKind::HoldKill || row.kind == BossTacticKind::OneAtATime)
                holdMaps_[row.map] = true;
            if (row.kind == BossTacticKind::GateHold)
                gateHoldMaps_[row.map] = true;
            if (IsAuraTacticKind(row.kind))
                auraTacticMaps_[row.map] = true;
            if (row.kind == BossTacticKind::DispelPriority)
                dispelMaps_[row.map] = true;
            if (row.kind == BossTacticKind::UseObject)
                useObjectMaps_[row.map] = true;
            if (row.kind == BossTacticKind::SeekAura)
            {
                seekAuraMaps_[row.map] = true;
                seekTargets_[row.map].insert(row.targetEntry);
            }
            if (row.kind == BossTacticKind::PullGate)
                pullGateMaps_[row.map] = true;
            if (row.kind == BossTacticKind::Spread)
                spreadMaps_[row.map] = true;
            if (row.kind == BossTacticKind::HealReserve)
                healReserveMaps_[row.map] = true;
            if (row.kind == BossTacticKind::Anchor)
                anchorMaps_[row.map] = true;
            if (row.kind == BossTacticKind::TankPost)
                tankPostMaps_[row.map] = true;
            // The value's lookup is one row per boss: a second add_crew row of the same map and boss replaces the
            // first here (the table holds one such row per boss in the data).
            if (row.kind == BossTacticKind::AddCrew)
                addCrew_[Key(row.map, row.bossEntry)] = AddCrewRow{row.targetEntry, row.priority};
            // A row that assigns the unit to a tank slot: the raid guard leaves the unit to that tank, so the table
            // keeps each kind's lowest slot for the unit (the guard uses the kind whose key is on).
            if (row.priority > 0 && row.kind == BossTacticKind::TankPost)
            {
                std::uint32_t& have = assignedTankSlots_[row.map][TacticSubjectEntry(row)].tankPostSlot;
                if (have == 0 || row.priority < have)
                    have = row.priority;
            }
            else if (row.priority > 0 && row.kind == BossTacticKind::TauntAfterCast &&
                row.mode == TacticMode::UntilTank)
            {
                std::uint32_t& have = assignedTankSlots_[row.map][TacticSubjectEntry(row)].untilTankSlot;
                if (have == 0 || row.priority < have)
                    have = row.priority;
            }
            if (row.kind == BossTacticKind::NeedsAura)
                requiredAura_[row.map][row.targetEntry] = row.spellId;
            // The held entry is the row's target, or its boss when the row names the boss itself (target_entry 0).
            if (row.kind == BossTacticKind::RangedOnly)
                rangedOnlyGaps_[row.map].emplace(row.targetEntry != 0 ? row.targetEntry : row.bossEntry, row.param);
            if (row.kind == BossTacticKind::ThreatSuccessor)
            {
                threatSuccessors_[row.map].insert(TacticSubjectEntry(row));
                if (row.mode == TacticMode::Strict)
                    strictThreatSuccessors_[row.map].insert(TacticSubjectEntry(row));
                if (row.cycleSpell != 0)
                    resetWindows_[row.map][TacticSubjectEntry(row)] = ThreatResetWindow{row.cycleSpell,
                        static_cast<std::uint32_t>(row.param * 1000.0f),
                        static_cast<std::uint32_t>(row.windowSec * 1000.0f), row.priority};
            }
            if (row.kind == BossTacticKind::TankIgnore)
                tankIgnoreBosses_[row.map][row.targetEntry].push_back(row.bossEntry);
            if (row.kind == BossTacticKind::LinkedDeath)
            {
                linkedDeathMaps_[row.map] = true;
                // The group rows are read from the anchor, and the anchor alone: a partner has no row of its own.
                // One anchor can carry several groups (its rows' priorities): each is its own key.
                std::vector<LinkedDeathGroupKey>& groups = linkedDeathGroups_[row.map];
                LinkedDeathGroupKey const key{row.bossEntry, row.priority};
                if (std::find_if(groups.begin(), groups.end(), [&key](const LinkedDeathGroupKey& have)
                        { return have.anchor == key.anchor && have.group == key.group; }) == groups.end())
                    groups.push_back(key);
            }
            ++size_;
            return true;
        }

        // The seal opens the table to readers. The revision moves here too, so a content read taken before the seal
        // (empty rows) is never served for the sealed table.
        void Seal()
        {
            revision_ = NextBossTacticRevision();
            sealed_.store(true, std::memory_order_release);
        }
        bool Sealed() const { return sealed_.load(std::memory_order_acquire); }
        std::size_t Size() const { return size_; }

        // Whether the map has any row. False before the seal.
        bool HasMap(std::uint32_t map) const
        {
            return sealed_.load(std::memory_order_acquire) && maps_.find(map) != maps_.end();
        }

        // Whether the map has an avoid_cast row (the reaction engine's pass and the cast hold run only there). False
        // before the seal.
        bool HasAvoidCastMap(std::uint32_t map) const
        {
            return sealed_.load(std::memory_order_acquire) && avoidCastMaps_.find(map) != avoidCastMaps_.end();
        }

        // Whether the map has a hold_kill or one_at_a_time row (the hold multiplier's scan for heals, buffs and moves
        // runs only there: a map with only kill_first or interrupt rows has no hold to answer). A linked_death map has
        // its own flag (HasLinkedDeathMap). False before the seal.
        bool HasHoldMap(std::uint32_t map) const
        {
            return sealed_.load(std::memory_order_acquire) && holdMaps_.find(map) != holdMaps_.end();
        }

        // Whether the map has a gate_hold row (the hold multiplier's area and DoT veto runs there too). Its own flag
        // keeps a map with only hold_kill rows from paying for the gate read. False before the seal.
        bool HasGateHoldMap(std::uint32_t map) const
        {
            return sealed_.load(std::memory_order_acquire) && gateHoldMaps_.find(map) != gateHoldMaps_.end();
        }

        // Whether the map has a linked_death row (the hold multiplier's area and DoT veto runs there: a map without a
        // die-together group has no held member to keep at its percent). False before the seal.
        bool HasLinkedDeathMap(std::uint32_t map) const
        {
            return sealed_.load(std::memory_order_acquire) && linkedDeathMaps_.find(map) != linkedDeathMaps_.end();
        }

        // Whether the map has a row of one of the six aura kinds (dps_hold_after, dispel_priority, flee_on_aura,
        // freeze_on_aura, taunt_after_cast, swap_on_aura): only there does the new work run. False before the seal.
        bool HasAuraTacticMap(std::uint32_t map) const
        {
            return sealed_.load(std::memory_order_acquire) && auraTacticMaps_.find(map) != auraTacticMaps_.end();
        }

        // Whether the map has a dispel_priority row (the dispel predicate and the dispel hold look there). False before
        // the seal.
        bool HasDispelMap(std::uint32_t map) const
        {
            return sealed_.load(std::memory_order_acquire) && dispelMaps_.find(map) != dispelMaps_.end();
        }

        // Whether the map has a use_object row (the clicker and the tank anchor run only there). False before the seal.
        bool HasUseObjectMap(std::uint32_t map) const
        {
            return sealed_.load(std::memory_order_acquire) && useObjectMaps_.find(map) != useObjectMaps_.end();
        }

        // Whether the map has a seek_aura row (the cloud exemption and the seek walk run only there). False before the
        // seal.
        bool HasSeekAuraMap(std::uint32_t map) const
        {
            return sealed_.load(std::memory_order_acquire) && seekAuraMaps_.find(map) != seekAuraMaps_.end();
        }

        // The map's seek_aura source entries (the creatures whose death leaves the cloud): the third off-tank's pickup
        // rule reads them without a fight scan. Empty before the seal.
        const std::unordered_set<std::uint32_t>& SeekTargets(std::uint32_t map) const
        {
            static const std::unordered_set<std::uint32_t> none;
            if (!sealed_.load(std::memory_order_acquire))
                return none;
            auto it = seekTargets_.find(map);
            return it == seekTargets_.end() ? none : it->second;
        }

        // Whether the map has a pull_gate row (the run leader's gate lookup runs only there). False before the seal.
        bool HasPullGateMap(std::uint32_t map) const
        {
            return sealed_.load(std::memory_order_acquire) && pullGateMaps_.find(map) != pullGateMaps_.end();
        }

        // Whether the map has a spread row (the ranged spacing widens only there). False before the seal.
        bool HasSpreadMap(std::uint32_t map) const
        {
            return sealed_.load(std::memory_order_acquire) && spreadMaps_.find(map) != spreadMaps_.end();
        }

        // Whether the map has a heal_reserve row (the healer top-up gates read it only there). False before the seal.
        bool HasHealReserveMap(std::uint32_t map) const
        {
            return sealed_.load(std::memory_order_acquire) && healReserveMaps_.find(map) != healReserveMaps_.end();
        }

        // Whether the map has an anchor row (the movers' clamp looks for a live one only there). False before the seal.
        bool HasAnchorMap(std::uint32_t map) const
        {
            return sealed_.load(std::memory_order_acquire) && anchorMaps_.find(map) != anchorMaps_.end();
        }

        // Whether the map has a tank_post row (the trigger runs only there). False before the seal.
        bool HasTankPostMap(std::uint32_t map) const
        {
            return sealed_.load(std::memory_order_acquire) && tankPostMaps_.find(map) != tankPostMaps_.end();
        }

        // The map's add_crew row for a boss, keyed by (map, boss entry): the switch unit whose death hands the boss's
        // adds over and the crew size (0-5). False before the seal, or when the boss has no such row: the "add crew"
        // value then reads no crew and every bot keeps today's add duty. The outputs are untouched on false.
        bool AddCrewOf(std::uint32_t map, std::uint32_t bossEntry, std::uint32_t& switchEntry,
                       std::uint8_t& crewSize) const
        {
            if (!sealed_.load(std::memory_order_acquire) || addCrew_.empty())
                return false;
            auto const it = addCrew_.find(Key(map, bossEntry));
            if (it == addCrew_.end())
                return false;
            switchEntry = it->second.switchEntry;
            crewSize = it->second.crewSize;
            return true;
        }

        // The tank slots (1-7) a unit of `entry` is assigned to by rows of its own, one per kind: a taunt_after_cast
        // row in mode until_tank and a tank_post row. Each is the lowest slot of its own kind. Both 0 before the seal
        // or when no row of that kind names the unit: the raid guard reads SlotFor to leave such a unit to its tank.
        AssignedTankSlot AssignedTankSlotOf(std::uint32_t map, std::uint32_t entry) const
        {
            static const AssignedTankSlot none;
            if (!sealed_.load(std::memory_order_acquire))
                return none;
            auto const byMap = assignedTankSlots_.find(map);
            if (byMap == assignedTankSlots_.end())
                return none;
            auto const it = byMap->second.find(entry);
            return it == byMap->second.end() ? none : it->second;
        }

        // The aura a map's needs_aura row requires on a bot before it may target the entry (0: none). The gate is the
        // unit's, wherever it stands, so the lookup never names the row's boss. 0 before the seal.
        std::uint32_t RequiredAura(std::uint32_t map, std::uint32_t entry) const
        {
            if (!sealed_.load(std::memory_order_acquire) || requiredAura_.empty())
                return 0;
            auto const byMap = requiredAura_.find(map);
            if (byMap == requiredAura_.end())
                return 0;
            auto const it = byMap->second.find(entry);
            return it == byMap->second.end() ? 0 : it->second;
        }

        // The vertical gap a map's ranged_only row holds the entry under (0: no row), keyed by the held entry: the
        // row's target, or its boss for target_entry 0. 0 before the seal.
        float RangedOnlyGap(std::uint32_t map, std::uint32_t entry) const
        {
            if (!sealed_.load(std::memory_order_acquire) || rangedOnlyGaps_.empty())
                return 0.0f;
            auto const byMap = rangedOnlyGaps_.find(map);
            if (byMap == rangedOnlyGaps_.end())
                return 0.0f;
            auto const it = byMap->second.find(entry);
            return it == byMap->second.end() ? 0.0f : it->second;
        }

        // Whether the creature entry carries a `threat_successor` row on this map: the damage dealers' threat is then
        // capped at the assigned off-tanks' highest (ThreatCapPolicy.h), so the second tank stays second for a threat
        // reset. Read from the table by ThreatValue::GetTankThreat, so no gate or mode can qualify it. False before the
        // seal.
        bool HasThreatSuccessor(std::uint32_t map, std::uint32_t entry) const
        {
            if (!sealed_.load(std::memory_order_acquire) || threatSuccessors_.empty())
                return false;
            auto const byMap = threatSuccessors_.find(map);
            return byMap != threatSuccessors_.end() && byMap->second.find(entry) != byMap->second.end();
        }

        // Whether that `threat_successor` row carries the mode word `strict`: the boss asks for the strict successor
        // line (AiPlayerbot.DungeonRunThreatSuccessorStrict: no projection escape, the white-damage stop). A row without
        // it keeps the plain cap with the key on or off. False before the seal.
        bool HasStrictThreatSuccessor(std::uint32_t map, std::uint32_t entry) const
        {
            if (!sealed_.load(std::memory_order_acquire) || strictThreatSuccessors_.empty())
                return false;
            auto const byMap = strictThreatSuccessors_.find(map);
            return byMap != strictThreatSuccessors_.end() && byMap->second.find(entry) != byMap->second.end();
        }

        // The threat_successor row's Cause Insanity timer (`cycle_spell` != 0): null when the row carries none (today's
        // Hakkar row), which is also what the key-off path reads as no window. False before the seal.
        const ThreatResetWindow* ThreatResetWindowOf(std::uint32_t map, std::uint32_t entry) const
        {
            if (!sealed_.load(std::memory_order_acquire) || resetWindows_.empty())
                return nullptr;
            auto const byMap = resetWindows_.find(map);
            if (byMap == resetWindows_.end())
                return nullptr;
            auto const it = byMap->second.find(entry);
            return it == byMap->second.end() ? nullptr : &it->second;
        }

        // The bosses whose tank_ignore row names the entry on this map (empty: none): while one of them fights, the
        // tanks leave the entry to the damage dealers (TankIgnoresUnit). Empty before the seal.
        const std::vector<std::uint32_t>& TankIgnoreBosses(std::uint32_t map, std::uint32_t entry) const
        {
            static const std::vector<std::uint32_t> none;
            if (!sealed_.load(std::memory_order_acquire) || tankIgnoreBosses_.empty())
                return none;
            auto const byMap = tankIgnoreBosses_.find(map);
            if (byMap == tankIgnoreBosses_.end())
                return none;
            auto const it = byMap->second.find(entry);
            return it == byMap->second.end() ? none : it->second;
        }

        // Whether the map has a needs_aura row (the healers' Shade-victim weighting looks only there). False before
        // the seal.
        bool HasRequiredAuraMap(std::uint32_t map) const
        {
            return sealed_.load(std::memory_order_acquire) && requiredAura_.find(map) != requiredAura_.end();
        }

        // Whether the boss carries a row of the kind (dev's Ossirian runner yields to the use_object row). False
        // before the seal.
        bool HasKindFor(std::uint32_t map, std::uint32_t bossEntry, BossTacticKind kind) const
        {
            const std::vector<BossTacticRow>* rows = Rows(map, bossEntry);
            if (!rows)
                return false;
            for (const BossTacticRow& row : *rows)
                if (row.kind == kind)
                    return true;
            return false;
        }

        // The seek_pull row of one boss and source (null: none): the seek walk-out's per-cycle rules. Read straight
        // from the table by the walk-out's own read, never from the fight scan. Null before the seal.
        const BossTacticRow* SeekPullRow(std::uint32_t map, std::uint32_t bossEntry, std::uint32_t targetEntry) const
        {
            const std::vector<BossTacticRow>* rows = Rows(map, bossEntry);
            if (!rows)
                return nullptr;
            for (const BossTacticRow& row : *rows)
                if (row.kind == BossTacticKind::SeekPull && row.targetEntry == targetEntry)
                    return &row;
            return nullptr;
        }

        // The map's linked_death groups, in load order: each (anchor, priority) key's rows name one group. The link
        // scan reads these instead of a fight's rows, which under "focus rti targets" are the raid mark's entry only.
        // Empty before the seal or for a map without rows of the kind.
        const std::vector<LinkedDeathGroupKey>& LinkedDeathGroups(std::uint32_t map) const
        {
            static const std::vector<LinkedDeathGroupKey> none;
            if (!sealed_.load(std::memory_order_acquire))
                return none;
            auto it = linkedDeathGroups_.find(map);
            return it == linkedDeathGroups_.end() ? none : it->second;
        }

        // The map's freeze_on_aura auras: the one thing a bot can carry that holds every action it has, which the
        // run's automatic crowd control reads to skip a frozen bot. Empty before the seal.
        const std::vector<std::uint32_t>& FreezeAuras(std::uint32_t map) const
        {
            static const std::vector<std::uint32_t> none;
            if (!sealed_.load(std::memory_order_acquire))
                return none;
            auto it = freezeAuras_.find(map);
            return it == freezeAuras_.end() ? none : it->second;
        }

        // The map's kill_first and linked_death units (a row's target_entry of 0 is its boss): the automatic crowd
        // control never picks one. Empty before the seal.
        const std::unordered_set<std::uint32_t>& NoCcEntries(std::uint32_t map) const
        {
            static const std::unordered_set<std::uint32_t> none;
            if (!sealed_.load(std::memory_order_acquire))
                return none;
            auto it = noCcEntries_.find(map);
            return it == noCcEntries_.end() ? none : it->second;
        }

        // The rows of one boss entry, or null. Null before the seal.
        const std::vector<BossTacticRow>* Rows(std::uint32_t map, std::uint32_t bossEntry) const
        {
            if (!sealed_.load(std::memory_order_acquire))
                return nullptr;
            auto it = rows_.find(Key(map, bossEntry));
            return it == rows_.end() ? nullptr : &it->second;
        }

        // Every row of one map, sorted by (bossEntry, kind, targetEntry, spellId):
        // the decision capture stores the table once per capture segment and the
        // replay rebuilds it from this list, so the order must not depend on the
        // map's iteration. Empty before the seal.
        std::vector<BossTacticRow> RowsOnMap(std::uint32_t map) const
        {
            std::vector<BossTacticRow> out;
            if (!sealed_.load(std::memory_order_acquire))
                return out;
            for (const auto& entry : rows_)
                if ((entry.first >> 32) == map)
                    out.insert(out.end(), entry.second.begin(), entry.second.end());
            std::sort(out.begin(), out.end(), [](const BossTacticRow& a, const BossTacticRow& b)
            {
                if (a.bossEntry != b.bossEntry)
                    return a.bossEntry < b.bossEntry;
                if (a.kind != b.kind)
                    return a.kind < b.kind;
                if (a.targetEntry != b.targetEntry)
                    return a.targetEntry < b.targetEntry;
                return a.spellId < b.spellId;
            });
            return out;
        }

    private:
        static std::uint64_t Key(std::uint32_t map, std::uint32_t bossEntry)
        {
            return (static_cast<std::uint64_t>(map) << 32) | bossEntry;
        }

        std::unordered_map<std::uint64_t, std::vector<BossTacticRow>> rows_;
        std::unordered_map<std::uint32_t, bool> maps_;
        std::unordered_map<std::uint32_t, bool> avoidCastMaps_;
        std::unordered_map<std::uint32_t, bool> holdMaps_;
        std::unordered_map<std::uint32_t, bool> gateHoldMaps_;
        std::unordered_map<std::uint32_t, bool> linkedDeathMaps_;
        std::unordered_map<std::uint32_t, bool> auraTacticMaps_;
        std::unordered_map<std::uint32_t, bool> dispelMaps_;
        std::unordered_map<std::uint32_t, bool> useObjectMaps_;
        std::unordered_map<std::uint32_t, bool> seekAuraMaps_;
        std::unordered_map<std::uint32_t, std::unordered_set<std::uint32_t>> seekTargets_;
        std::unordered_map<std::uint32_t, bool> pullGateMaps_;
        std::unordered_map<std::uint32_t, bool> spreadMaps_;
        std::unordered_map<std::uint32_t, bool> healReserveMaps_;
        std::unordered_map<std::uint32_t, bool> anchorMaps_;
        std::unordered_map<std::uint32_t, bool> tankPostMaps_;
        // add_crew: (map << 32 | boss entry) -> the switch unit and the crew size.
        std::unordered_map<std::uint64_t, AddCrewRow> addCrew_;
        // map -> the unit -> the lowest raid tank slot a tank row (until_tank, tank_post) assigns it to, and which
        // kinds of row name it.
        std::unordered_map<std::uint32_t, std::unordered_map<std::uint32_t, AssignedTankSlot>> assignedTankSlots_;
        std::unordered_map<std::uint32_t, std::unordered_map<std::uint32_t, std::uint32_t>> requiredAura_;
        std::unordered_map<std::uint32_t, std::unordered_map<std::uint32_t, float>> rangedOnlyGaps_;
        std::unordered_map<std::uint32_t, std::unordered_set<std::uint32_t>> threatSuccessors_;
        std::unordered_map<std::uint32_t, std::unordered_set<std::uint32_t>> strictThreatSuccessors_;
        // threat_successor with a cycle_spell: map -> the subject entry -> its reset window.
        std::unordered_map<std::uint32_t, std::unordered_map<std::uint32_t, ThreatResetWindow>> resetWindows_;
        // tank_ignore: map -> the ignored entry -> the bosses whose row names it.
        std::unordered_map<std::uint32_t, std::unordered_map<std::uint32_t, std::vector<std::uint32_t>>> tankIgnoreBosses_;
        std::unordered_map<std::uint32_t, std::vector<LinkedDeathGroupKey>> linkedDeathGroups_;
        std::unordered_map<std::uint32_t, std::unordered_set<std::uint32_t>> noCcEntries_;
        std::unordered_map<std::uint32_t, std::vector<std::uint32_t>> freezeAuras_;
        std::size_t size_ = 0;
        std::atomic<bool> sealed_{false};
        std::uint64_t revision_;
    };
    // Activation gate. The tactics change how bots play, so they are not on for
    // everybody: either the server owner opted in (AiPlayerbot.DungeonTactics = 1)
    // or this bot sits in a group a remote commander has bound (the commander
    // runs). Always off on a map without rows, so nothing else pays for it.
    inline bool BossTacticsWanted(bool optIn, bool inCommandedGroup, bool mapHasRows)
    {
        return mapHasRows && (optIn || inCommandedGroup);
    }

    // Rows of the bosses present in a fight.
    using ActiveBossRows = std::vector<const BossTacticRow*>;

    // --- dispel_priority --------------------------------------------------------------------------------------------
    // The rank of an aura no dispel_priority row lists: after every listed one.
    constexpr int kUnlistedDispelRank = 255;

    // One aura a member carries, as the core sees it: its id and its SpellEntry::Dispel, which is the cure that removes
    // it (the row never names the type; the aura's own data does).
    struct AuraOnMember
    {
        std::uint32_t spellId = 0;
        std::uint32_t dispelType = 0;
    };

    // What a cure of one dispel type may do for one member.
    struct DispelVerdict
    {
        bool excluded = false;           // no cure of this type may touch the member (a never aura, or an un-clear clear one)
        bool wait = false;               // excluded only until the carrier is clear of the group (mode clear)
        int rank = kUnlistedDispelRank;  // the member's aura rank: lower is cured first
        std::uint32_t auraId = 0;        // the aura that decided (0: the member carries none of the type)
        TacticMode mode = TacticMode::Default;
    };

    inline const BossTacticRow* DispelRowFor(const ActiveBossRows& active, std::uint32_t spellId)
    {
        for (const BossTacticRow* row : active)
        {
            if (row->kind == BossTacticKind::DispelPriority && row->spellId == spellId)
                return row;
        }
        return nullptr;
    }

    // Whether a verdict needs the carrier's distance from the group at all: only a `clear` row asks for it.
    inline bool DispelNeedsAllyDistance(const ActiveBossRows& active, const std::vector<AuraOnMember>& auras)
    {
        for (const AuraOnMember& aura : auras)
        {
            const BossTacticRow* row = DispelRowFor(active, aura.spellId);
            if (row && row->mode == TacticMode::Clear)
                return true;
        }
        return false;
    }

    // The cure verdict for one member and one dispel type. The whole type is excluded while any of its auras is a
    // `never` row, or a `clear` row whose carrier is still inside the row's yards (or the config default when 0): the
    // core picks a random aura of the type, so a cure could remove the one that must stay. Otherwise the member ranks
    // by the lowest `priority` among its listed auras, and an unlisted aura ranks after every listed one. A hard
    // never-dispel aura (IsNeverDispelSpellId) stays excluded unless a `clear` row lists it and the carrier is clear.
    inline DispelVerdict DispelMemberVerdict(const ActiveBossRows& active, const std::vector<AuraOnMember>& auras,
                                            std::uint32_t dispelType, float nearestAllyYd, float defaultClearYd)
    {
        DispelVerdict verdict;
        for (const AuraOnMember& aura : auras)
        {
            if (aura.dispelType != dispelType)
                continue;
            const BossTacticRow* row = DispelRowFor(active, aura.spellId);
            bool const hardNever = IsNeverDispelSpellId(aura.spellId);
            float const clearYd = row && row->param > 0.0f ? row->param : defaultClearYd;
            bool const clearReady = row && row->mode == TacticMode::Clear && nearestAllyYd >= clearYd;
            if (row && row->mode == TacticMode::Never)
            {
                verdict.excluded = true;
                verdict.wait = false;
                verdict.auraId = aura.spellId;
                verdict.mode = TacticMode::Never;
                return verdict;
            }
            if (row && row->mode == TacticMode::Clear && !clearReady)
            {
                verdict.excluded = true;
                verdict.wait = true;
                verdict.auraId = aura.spellId;
                verdict.mode = TacticMode::Clear;
                return verdict;
            }
            if (hardNever && !clearReady)
            {
                verdict.excluded = true;
                verdict.wait = false;
                verdict.auraId = aura.spellId;
                verdict.mode = row ? row->mode : TacticMode::Never;
                return verdict;
            }
            int const rank = row ? static_cast<int>(row->priority) : kUnlistedDispelRank;
            if (verdict.auraId == 0 || rank < verdict.rank)
            {
                verdict.rank = rank;
                verdict.auraId = aura.spellId;
                verdict.mode = row ? row->mode : TacticMode::Default;
            }
        }
        return verdict;
    }

    // The first excluded verdict among the types a cure removes, or a default (not excluded) one. The caller passes
    // every type the cast spell removes, not only the one that triggered it: a poison-triggered Cleanse also strips
    // magic and disease, and the core picks one of the spell's types at random.
    inline DispelVerdict DispelCureVerdict(const ActiveBossRows& active, const std::vector<AuraOnMember>& auras,
                                          const std::vector<std::uint32_t>& types, float nearestAllyYd,
                                          float defaultClearYd)
    {
        for (std::uint32_t type : types)
        {
            DispelVerdict const verdict = DispelMemberVerdict(active, auras, type, nearestAllyYd, defaultClearYd);
            if (verdict.excluded)
                return verdict;
        }
        return DispelVerdict();
    }

    // Order of two cure candidates: rank first, the target tier only on a tie. A rank-0 aura is cured before a rank-2
    // one even when the latter stands in cast range and the former needs a short walk.
    inline bool DispelCandidateBetter(int rankA, int tierA, int rankB, int tierB)
    {
        if (rankA != rankB)
            return rankA < rankB;
        return tierA < tierB;
    }

    // Who a cure cast is aimed at, as the last gate can place it: the bot itself, a group member, a pet whose owner
    // is a group member (a hunter pet has no Player, so PartyMemberToDispel offers it by its owner's group), or a unit
    // the gate cannot place. A target that is neither is refused: an unknown unit must never receive a cure whose
    // protected auras the rows did not judge.
    enum class DispelTargetKind : std::uint8_t
    {
        Self,
        GroupMember,
        GroupPet,
        Unknown
    };

    inline bool DispelTargetEligible(DispelTargetKind target)
    {
        return target != DispelTargetKind::Unknown;
    }

    // The cast-time verdict for a cure: an ineligible target is refused outright (fail closed), otherwise the same
    // per-type verdict as the cure action over every dispel type the cast spell removes.
    inline DispelVerdict DispelCastVerdict(DispelTargetKind target, const ActiveBossRows& active,
                                           const std::vector<AuraOnMember>& auras,
                                           const std::vector<std::uint32_t>& types, float nearestAllyYd,
                                           float defaultClearYd)
    {
        if (!DispelTargetEligible(target))
        {
            DispelVerdict verdict;
            verdict.excluded = true;
            return verdict;
        }
        return DispelCureVerdict(active, auras, types, nearestAllyYd, defaultClearYd);
    }

    // True when a creature of `casterEntry` casting `spellId` must be interrupted.
    inline bool IsInterruptListed(const ActiveBossRows& active, std::uint32_t casterEntry, std::uint32_t spellId)
    {
        for (const BossTacticRow* row : active)
        {
            if (row->kind == BossTacticKind::Interrupt && row->spellId == spellId &&
                TacticSubjectEntry(*row) == casterEntry)
                return true;
        }
        return false;
    }

    // The radius to keep from a creature of `casterEntry` while it casts `spellId`: the widest
    // avoid_cast row that lists the pair, or 0 when none does.
    inline float AvoidCastRadius(const ActiveBossRows& active, std::uint32_t casterEntry, std::uint32_t spellId)
    {
        float radius = 0.0f;
        for (const BossTacticRow* row : active)
        {
            if (row->kind == BossTacticKind::AvoidCast && row->spellId == spellId &&
                TacticSubjectEntry(*row) == casterEntry && row->param > radius)
                radius = row->param;
        }
        return radius;
    }

    // The radius to keep while a creature of `casterEntry` casts out of either of its two cast slots: the wider of the
    // rows that list the generic cast and the rows that list the channel. A caster can run both at once (a channel and
    // a bolt), and the channel is the one a row for a channelled blast names (CURRENT_CHANNELED_SPELL, never the
    // generic slot). An empty slot (id 0) lists nothing, so it gives 0.
    inline float ListedCastRadius(const ActiveBossRows& active, std::uint32_t casterEntry, std::uint32_t genericSpellId,
                                  std::uint32_t channeledSpellId)
    {
        return std::max(AvoidCastRadius(active, casterEntry, genericSpellId),
                        AvoidCastRadius(active, casterEntry, channeledSpellId));
    }

    // Whether a tank_hold row lists a creature of `casterEntry`: this caster's avoid_cast blast may be held by a tank
    // while the caster is on a non-tank (the row means "a tank may stay in this caster's blast", see HoldsCaster). The
    // subject is the row's target_entry, or its boss_entry when that is 0, so the row is keyed by the caster's entry.
    inline bool HasTankHold(const ActiveBossRows& active, std::uint32_t casterEntry)
    {
        for (const BossTacticRow* row : active)
        {
            if (row->kind == BossTacticKind::TankHold && TacticSubjectEntry(*row) == casterEntry)
                return true;
        }
        return false;
    }

    // Whether a bot holds a caster, so stays in its blast (no walk-out, no cast hold). A tank holds when the caster
    // is on it. It also holds — only for a caster whose entry has a tank_hold row — when the caster is on a non-tank
    // AND this tank's own current target is the caster, so that one tank can take it. Walk 20's Voidwalkers on
    // healers shocked the raid when the tanks walked out too, but holding every tank in every blast walked tanks into
    // blasts that kill them (the Gurubashi Bat Rider's 10 yd explosion, where nothing is gained by staying); so the
    // second case is opt-in per caster and, within that, only the tank already on the caster (every other tank leaves
    // like anyone else). A caster on nobody keeps nobody in, and a non-tank never holds. While a row in mode `all`
    // lists the cast in progress no bot holds the caster, so every tank walks out of that blast too (F4-A).
    inline bool HoldsCaster(bool botIsTank, bool casterIsOnBot, bool casterIsOnNonTank, bool botTargetsCaster,
                            bool casterTankHoldRow, bool allRowCasting = false)
    {
        if (allRowCasting)
            return false;
        return botIsTank && (casterIsOnBot || (casterTankHoldRow && casterIsOnNonTank && botTargetsCaster));
    }

    // Whether an avoid_cast row in mode `all` lists the caster's cast in progress: while that spell is cast no bot
    // holds the caster, tanks included (the Rider instakills itself, so there is no threat to keep). The call sites
    // pass the caster's own current generic and channelled spell ids; an empty slot (id 0) lists nothing.
    inline bool AvoidCastAllRow(const ActiveBossRows& active, std::uint32_t casterEntry, std::uint32_t genericId,
                                std::uint32_t channeledId)
    {
        for (const BossTacticRow* row : active)
        {
            if (row->kind == BossTacticKind::AvoidCast && row->mode == TacticMode::All &&
                TacticSubjectEntry(*row) == casterEntry &&
                (row->spellId == genericId || row->spellId == channeledId))
                return true;
        }
        return false;
    }

    // Whether a tank takes the raid mark (rti). When the mark's own entry is listed by a tank_hold row it is the loose
    // caster the row covers, and it yields to a tank_hold pickup that stands nearer to this bot: the tournament's
    // loose bucket takes that add first, so the mark does not pull the tank off it. Without the row, or with no nearer
    // pickup, the mark is taken as it always was.
    inline bool TankTakesMark(bool markHasTankHoldRow, bool hasPickup, float nearestPickupDistance, float markDistance)
    {
        return !markHasTankHoldRow || !hasPickup || nearestPickupDistance >= markDistance;
    }

    // Whether a bot `distance` yd from such a caster has to leave (see HoldsCaster).
    inline bool MustLeaveCast(bool holdsCaster, float radius, float distance)
    {
        return !holdsCaster && radius > 0.0f && distance < radius;
    }

    // Whether an action may run while a freeze_on_aura row holds the bot: nothing but the freeze action itself. A
    // carrier's heal still builds threat on every enemy in combat with the healer (boss_mandokir.cpp's Gaze allows no
    // threat and 2 yd of movement), so the heals are frozen too. Not frozen: every action runs.
    inline bool FreezeHoldsAction(bool carriesAura, bool actionIsFreeze)
    {
        return !carriesAura || actionIsFreeze;
    }

    // What the freeze action does to its carrier this tick (BossTacticsFreezeAction::Execute). A lost-control state
    // (fear, confuse, knockback) owns the bot's movement and ends on its own: nothing is touched, and the rising edge
    // waits until it is over. Otherwise the rising edge stops the attack, the cast and the walk, a movement generator
    // on top is dropped (Clear then MoveIdle, as the commander's KeepStillDuringAct), and an unfinished spline is
    // stopped every tick: a chase's or a follow's own Finalize leaves it walking past the gaze's 2 yd.
    struct FreezeStill
    {
        bool attackStop = false;    // the rising edge: stop the attack and the cast
        bool dropGenerator = false; // drop the movement generator on top and idle
        bool stopSpline = false;    // the spline is not finished: stop it
    };

    inline FreezeStill FreezeStillTick(bool risingEdge, bool lostControl, bool generatorIdle, bool splineFinalized)
    {
        FreezeStill still;
        if (lostControl)
            return still;
        still.attackStop = risingEdge;
        still.dropGenerator = risingEdge || !generatorIdle;
        still.stopSpline = risingEdge || !splineFinalized;
        return still;
    }

    // Whether the one-slot fight-scan cache may answer this call (BossTacticsActions.cpp): only inside an AI update,
    // whose end closes and clears the slot (BossTacticsScanScope), for the same bot, at the same update tick, on the
    // same tactics table. An explicit command runs between updates on the same world thread where the tick does not
    // move, and the maps may have despawned an add since: it always re-scans.
    inline bool FightScanCacheHit(bool insideUpdate, bool sameAi, bool sameBot, std::uint32_t cachedTick,
                                  std::uint32_t tick, bool sameTable)
    {
        return insideUpdate && sameAi && sameBot && cachedTick == tick && sameTable;
    }

    // The escape points lie this far past the radius, so a caster shifting a little between
    // ticks does not put the bot back on the edge.
    constexpr float kAvoidCastStepOut = 2.0f;
    constexpr int kAvoidCastPoints = 7;

    struct AvoidCastPoint
    {
        float x = 0.0f;
        float y = 0.0f;
    };

    // Escape points on a circle of radius + kAvoidCastStepOut around the caster: straight out
    // along the caster->bot bearing first (the shortest walk), then 45, 90 and 135 degrees to
    // either side, alternating. A bot standing on the caster has no bearing and uses
    // `fallbackAngle`. Returns the number of points written (kAvoidCastPoints).
    inline int AvoidCastDestinations(float casterX, float casterY, float botX, float botY, float radius,
                                     float fallbackAngle, AvoidCastPoint* out)
    {
        float const dx = botX - casterX;
        float const dy = botY - casterY;
        float const bearing = (dx * dx + dy * dy) < 0.01f ? fallbackAngle : std::atan2(dy, dx);
        float const ring = radius + kAvoidCastStepOut;
        float const step = 3.14159265f / 4.0f;
        float const offsets[kAvoidCastPoints] = { 0.0f, step, -step, 2 * step, -2 * step, 3 * step, -3 * step };
        for (int i = 0; i < kAvoidCastPoints; ++i)
        {
            out[i].x = casterX + ring * std::cos(bearing + offsets[i]);
            out[i].y = casterY + ring * std::sin(bearing + offsets[i]);
        }
        return kAvoidCastPoints;
    }

    // A listed cast in progress, as plain numbers: the caster's position and the radius to keep from it (3D, centre to
    // centre, as the core measures the blast).
    struct CastDangerZone
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float radius = 0.0f;
    };

    inline bool InsideCastZone(const CastDangerZone& zone, float x, float y, float z)
    {
        float const dx = zone.x - x;
        float const dy = zone.y - y;
        float const dz = zone.z - z;
        return zone.radius > 0.0f && std::sqrt(dx * dx + dy * dy + dz * dz) < zone.radius;
    }

    inline bool InsideAnyCastZone(const std::vector<CastDangerZone>& zones, float x, float y, float z)
    {
        for (const CastDangerZone& zone : zones)
            if (InsideCastZone(zone, x, y, z))
                return true;
        return false;
    }

    // The 2D distance from (px, py) to the segment (ax, ay)-(bx, by).
    inline float SegmentPointDistance2D(float ax, float ay, float bx, float by, float px, float py)
    {
        float const dx = bx - ax;
        float const dy = by - ay;
        float const len2 = dx * dx + dy * dy;
        float u = len2 > 0.0f ? ((px - ax) * dx + (py - ay) * dy) / len2 : 0.0f;
        u = u < 0.0f ? 0.0f : u > 1.0f ? 1.0f : u;
        float const cx = ax + u * dx - px;
        float const cy = ay + u * dy - py;
        return std::sqrt(cx * cx + cy * cy);
    }

    // A walk-out's straight line may not take the bot deeper into a blast it stands in (closer to that caster than it
    // starts, kAvoidCastChordSlack aside) nor through a blast it is outside of. The bot starts inside its own blast, so
    // that one only has to grow away from the caster; a point 135 degrees off the bearing would cross the caster.
    constexpr float kAvoidCastChordSlack = 0.5f;

    inline bool AvoidCastChordClear(float botX, float botY, float destX, float destY,
                                    const std::vector<CastDangerZone>& zones)
    {
        for (const CastDangerZone& zone : zones)
        {
            float const sx = botX - zone.x;
            float const sy = botY - zone.y;
            float const start = std::sqrt(sx * sx + sy * sy);
            float const closest = SegmentPointDistance2D(botX, botY, destX, destY, zone.x, zone.y);
            if (start < zone.radius)
            {
                if (closest < start - kAvoidCastChordSlack)
                    return false;
            }
            else if (closest < zone.radius)
                return false;
        }
        return true;
    }

    // The escape points worth trying, shortest walk first: the ring points (AvoidCastDestinations) whose straight line
    // from the bot is clear (AvoidCastChordClear). Returns how many were written to `out` (at most kAvoidCastPoints).
    inline int AvoidCastEscapes(float casterX, float casterY, float botX, float botY, float radius, float fallbackAngle,
                                const std::vector<CastDangerZone>& zones, AvoidCastPoint* out)
    {
        AvoidCastPoint ring[kAvoidCastPoints];
        int const all = AvoidCastDestinations(casterX, casterY, botX, botY, radius, fallbackAngle, ring);
        int n = 0;
        for (int i = 0; i < all; ++i)
            if (AvoidCastChordClear(botX, botY, ring[i].x, ring[i].y, zones))
                out[n++] = ring[i];
        // Insertion sort by walk length; ties keep the ring's order (straight out, then the nearer turns).
        for (int i = 1; i < n; ++i)
        {
            AvoidCastPoint const p = out[i];
            float const d = (p.x - botX) * (p.x - botX) + (p.y - botY) * (p.y - botY);
            int j = i - 1;
            while (j >= 0 && (out[j].x - botX) * (out[j].x - botX) + (out[j].y - botY) * (out[j].y - botY) > d)
            {
                out[j + 1] = out[j];
                --j;
            }
            out[j + 1] = p;
        }
        return n;
    }

    // The cast hold: while listed casts run, a movement whose destination (its target's position, or a stay spot) lies
    // in one of them is held, so a bot does not walk back into a blast it left or into one beside it. Not the walk-out
    // itself, not a move whose target is the bot itself, and not the tank's move to the caster it holds (`zones` leaves
    // out the blasts of the casters the bot holds).
    inline bool CastHoldsMove(bool isWalkOut, bool targetIsSelf, bool hasDestination, float x, float y, float z,
                              const std::vector<CastDangerZone>& zones)
    {
        if (isWalkOut || targetIsSelf || !hasDestination)
            return false;
        return InsideAnyCastZone(zones, x, y, z);
    }

    // How a walk-out ended, for its bot_events.csv row: a walk started; MoveTo took a point but no walk started (a
    // collapsed path, a transport or a teleport wait); or no usable point.
    enum class AvoidCastWalk
    {
        None = 0,
        Started = 1,
        Deferred = 2,
    };

    inline const char* AvoidCastWalkName(AvoidCastWalk walk)
    {
        switch (walk)
        {
            case AvoidCastWalk::Started:
                return "started";
            case AvoidCastWalk::Deferred:
                return "deferred";
            case AvoidCastWalk::None:
                break;
        }
        return "none";
    }

    // The avoid_cast walk-outs a bot met lately, kept to log a cast once per bot. A cast is the same while the bot keeps
    // meeting it: same caster, same spell, same cast instance (the address of the caster's current Spell object, new
    // for every cast) and seen again within kAvoidCastLogGapMs; the gap covers an allocator handing a finished cast's
    // address to the next one. The bot keeps several, since the nearest of two casters at once can alternate.
    constexpr std::uint32_t kAvoidCastLogGapMs = 2000;
    constexpr int kAvoidCastLogCasts = 4;

    struct AvoidCastLogEntry
    {
        bool known = false;
        std::uint64_t casterGuid = 0;
        std::uint32_t spellId = 0;
        std::uint64_t instance = 0;
        std::uint32_t lastSeenMs = 0;
    };

    struct AvoidCastLogState
    {
        AvoidCastLogEntry casts[kAvoidCastLogCasts];
    };

    // True when this walk-out is the first of its cast for the bot; always records that it was seen now.
    inline bool AvoidCastIsNewCast(AvoidCastLogState& state, std::uint64_t casterGuid, std::uint32_t spellId,
                                   std::uint64_t instance, std::uint32_t nowMs)
    {
        AvoidCastLogEntry* slot = nullptr;
        for (AvoidCastLogEntry& e : state.casts)
        {
            if (!e.known || static_cast<std::uint32_t>(nowMs - e.lastSeenMs) >= kAvoidCastLogGapMs)
            {
                // Free, or a cast not seen for the gap: the slot can be reused (matching it counts as new).
                if (!slot || (slot->known && !e.known))
                    slot = &e;
                continue;
            }
            if (e.casterGuid == casterGuid && e.spellId == spellId && e.instance == instance)
            {
                e.lastSeenMs = nowMs;
                return false;
            }
        }
        if (!slot)
        {
            // All four are live: drop the one seen longest ago.
            slot = &state.casts[0];
            for (AvoidCastLogEntry& e : state.casts)
                if (static_cast<std::uint32_t>(nowMs - e.lastSeenMs) > static_cast<std::uint32_t>(nowMs - slot->lastSeenMs))
                    slot = &e;
        }
        slot->known = true;
        slot->casterGuid = casterGuid;
        slot->spellId = spellId;
        slot->instance = instance;
        slot->lastSeenMs = nowMs;
        return true;
    }

    // A hold_kill row's blocker is counted in this radius around the bot: wide enough for the pile a Mad Servant dies
    // in (its Portal of Madness lands at the corpse), narrow enough not to count a second pack.
    constexpr float kHoldKillRangeYd = 50.0f;

    // A DPS AoE row is held while a held boss entry's creature stands this close to the bot's current target: the AoE
    // would take it as well (walk 21: one AoE killed four Mad Servants together, and each of them leaves two Mad
    // Voidwalkers).
    constexpr float kHoldKillAoeNearYd = 10.0f;

    // Whether DPS leave `entry` alone right now. A hold_kill row blocks while more than `priority` living units of its
    // target_entry (the blocker: the Mad Voidwalker a dead Mad Servant's portal summons, or the portal itself) are
    // near, so `priority` 0 means "not one more". The count is the caller's, a grid search around the bot: a Portal of
    // Madness is unselectable (unit_flags 33555200) and never attacks, so the bots' attacker list would never see it.
    // The subject is the row's boss_entry; a hold_kill row's target_entry is the blocker, the thing to finish first,
    // never the boss the row is about.
    inline bool KillHeld(const ActiveBossRows& rows, std::uint32_t entry,
                         const std::function<std::uint32_t(std::uint32_t)>& countNear)
    {
        for (const BossTacticRow* row : rows)
        {
            if (row->kind != BossTacticKind::HoldKill || row->bossEntry != entry)
                continue;
            if (countNear(row->targetEntry) > row->priority)
                return true;
        }
        return false;
    }

    // Whether a unit of a hold_kill row's blocker entry counts towards its budget: a creature fighting, or one that
    // can never fight at all (UNIT_FLAG_NOT_SELECTABLE — the Portal of Madness 15141 only ticks its summon and is
    // never an attacker). A loose Voidwalker that has not woken yet is neither, so it does not hold the Servant.
    inline bool CountsAsBlocker(bool inCombat, bool notSelectable)
    {
        return inCombat || notSelectable;
    }

    // gate_hold's blocker search radius when `window_s` is 0: wide enough to see a second boss fighting anywhere in
    // the room, narrow enough to stay one encounter.
    constexpr float kGateHoldRadiusYd = 200.0f;

    inline float GateHoldRadius(float windowSec)
    {
        return windowSec > 0.0f ? windowSec : kGateHoldRadiusYd;
    }

    // Whether a unit of a gate_hold row's blocker entry counts while it fights: alive (the caller checks), in combat
    // and not evading. Unlike a hold_kill blocker (CountsAsBlocker) an unselectable unit that is not fighting never
    // counts: an evaded second boss back in the air is out of the encounter and must not stall the spared one's kill.
    // Selectability is handed in so the rule can state that it never decides on its own.
    inline bool CountsAsFightingBlocker(bool inCombat, bool evading, bool notSelectable)
    {
        (void)notSelectable;
        return inCombat && !evading;
    }

    // Whether DPS leave `entry` alone under a gate_hold row: the entry is at or below the row's gate percent (`param`)
    // and more than `priority` of the row's blockers still fight. `countFighting` counts the row's living, in-combat,
    // non-evading blockers within GateHoldRadius of the bot (the "fighting blockers" value). The spared unit is the
    // row's boss_entry; the blocker its target_entry. An evaded or out-of-combat blocker releases the hold, so a
    // living boss with an unkillable blocker is never left to stand.
    inline bool GateHeld(const ActiveBossRows& rows, std::uint32_t entry, int hpPct,
                         const std::function<std::uint32_t(const BossTacticRow&)>& countFighting)
    {
        for (const BossTacticRow* row : rows)
        {
            if (row->kind != BossTacticKind::GateHold || row->bossEntry != entry)
                continue;
            if (hpPct > static_cast<int>(row->param))
                continue;
            if (countFighting(*row) > row->priority)
                return true;
        }
        return false;
    }

    // Whether a free DPS bot leaves `entry` alone right now: a hold_kill row holds it while its blocker lives
    // (KillHeld), and a gate_hold row spares it at its health gate while a blocker fights (GateHeld). The dps target,
    // the dps AoE target, the group tank's victim and the raid mark all drop such a candidate, so with nothing held
    // left the bot has no target and waits for the blocker to die instead of feeding the pile. `hpPct` is the
    // candidate's health; `countFighting` counts a gate row's fighting blockers. The role gate is the hold-kill
    // action's: a tank or a healer keeps its own pick (the caller passes false for them), and an explicit attack order
    // is taken long before this runs.
    inline bool DpsLeavesAlone(bool freeDpsBot, const ActiveBossRows& rows, std::uint32_t entry, int hpPct,
                               const std::function<std::uint32_t(std::uint32_t)>& countNear,
                               const std::function<std::uint32_t(const BossTacticRow&)>& countFighting)
    {
        if (!freeDpsBot)
            return false;
        if (KillHeld(rows, entry, countNear))
            return true;
        return GateHeld(rows, entry, hpPct, countFighting);
    }

    // A living unit of the paced entry that fights, as the one_at_a_time pick sees it: health first, guid as the
    // tie-break. `eligible` is whether it may be THE pick (not crowd-controlled, not running home); only PickSoleKill
    // reads it. An ineligible unit is still a unit of the entry that stands in the pile, so the area veto still counts
    // it. Every bot builds the same list from the fight-wide grid search, so the pick needs no vote and no new state.
    struct SoleCandidate
    {
        std::uint64_t guid = 0;
        int hpPct = 100;
        bool inCombat = false;
        bool eligible = true;
    };

    // Index of the one unit the whole fight kills next, or -1 when none of the entry fights yet: the lowest health
    // among the eligible candidates that are in combat, the lowest guid on a tie.
    inline int PickSoleKill(const std::vector<SoleCandidate>& c)
    {
        int best = -1;
        for (std::size_t i = 0; i < c.size(); ++i)
        {
            if (!c[i].inCombat || !c[i].eligible)
                continue;
            if (best < 0 || c[i].hpPct < c[best].hpPct || (c[i].hpPct == c[best].hpPct && c[i].guid < c[best].guid))
                best = static_cast<int>(i);
        }
        return best;
    }

    // Whether a unit of a one_at_a_time entry is one every bot leaves alone right now: the fight has a sole pick and it
    // is another unit. No pick (nobody of the entry fights yet) means nothing is paced: the first unit engaged becomes
    // the pick and every bot converges on it, fighting or not.
    inline bool OneAtATimeHolds(std::uint64_t solePick, std::uint64_t guid)
    {
        return solePick != 0 && solePick != guid;
    }

    // Index of the held unit of a paced entry the area veto answers to, among the fight-wide candidates (the value's
    // living, fighting units of the entry, crowd-controlled ones included; NOT the bot's collapsible attackers list):
    // the first candidate within `maxDistance` (`distance[i]`, infinity for one that could not be resolved) that is held,
    // or -1. `budgetHolds` is a hold_kill row whose blocker budget is over: every candidate of the entry is held. Else
    // (a one_at_a_time row) every candidate except the sole pick is held, an ineligible one included, so a crowd-
    // controlled Servant next to the cast still vetoes it. -1 when the entry has no pick yet (nothing is paced).
    inline int HeldSubjectIndex(const std::vector<SoleCandidate>& c, const std::vector<float>& distance,
                                float maxDistance, bool budgetHolds)
    {
        const int pick = PickSoleKill(c);
        for (std::size_t i = 0; i < c.size() && i < distance.size(); ++i)
        {
            if (distance[i] > maxDistance)
                continue;
            if (budgetHolds || (pick >= 0 && OneAtATimeHolds(c[pick].guid, c[i].guid)))
                return static_cast<int>(i);
        }
        return -1;
    }

    // A linked_death group: its anchor (the rows' boss_entry), the group id (the rows' priority, so one map can carry
    // several groups) and every partner its rows list. Any member resolves the same group from the anchor's rows.
    struct LinkedDeathGroup
    {
        std::uint32_t bossEntry = 0;
        std::uint8_t groupId = 0;
        float param = 0.0f;                 // the hold percent
        // The release latch's window in seconds (the max over the group's rows): 7 s unless the encounter's own
        // resurrection retry is longer (Thekal's trio: 15 s).
        float windowSec = 7.0f;
        std::vector<std::uint32_t> entries; // the anchor first, then each partner
        // The release burn's order (`burn_order` of the group's rows, in the encounter's own death order): the first
        // living member of the list is burned, then the next. Empty: the ordinary released pick (the lowest), for a
        // group whose order does not matter.
        std::vector<std::uint32_t> burnOrder;
    };

    // The group a creature of `entry` belongs to, or false. A partner resolves the same group, but the anchor's rows
    // have to be among `active` (a partner has no rows of its own): they are when the anchor joined the fight.
    inline bool FindLinkedDeathGroup(const ActiveBossRows& active, std::uint32_t entry, LinkedDeathGroup& out)
    {
        for (const BossTacticRow* row : active)
        {
            if (row->kind != BossTacticKind::LinkedDeath || (row->bossEntry != entry && row->targetEntry != entry))
                continue;
            out = LinkedDeathGroup();
            out.bossEntry = row->bossEntry;
            out.groupId = row->priority;
            out.param = row->param;
            out.windowSec = row->windowSec;
            out.entries.push_back(row->bossEntry);
            for (const BossTacticRow* other : active)
            {
                if (other->kind != BossTacticKind::LinkedDeath || other->bossEntry != row->bossEntry ||
                    other->priority != row->priority)
                    continue;
                out.param = std::max(out.param, other->param);
                out.windowSec = std::max(out.windowSec, other->windowSec);
                if (std::find(out.entries.begin(), out.entries.end(), other->targetEntry) == out.entries.end())
                    out.entries.push_back(other->targetEntry);
                if (out.burnOrder.empty() && !other->burnOrder.empty())
                    out.burnOrder = other->burnOrder;
            }
            if (out.burnOrder.empty() && !row->burnOrder.empty())
                out.burnOrder = row->burnOrder;
            return true;
        }
        return false;
    }

    // A living member of a linked_death group, as the pick sees it. `attackable` is the caller's candidate rule
    // (alive, attackable, within the engage range).
    struct LinkedMember
    {
        std::uint64_t guid = 0;
        std::uint32_t entry = 0; // the creature entry: the burn order names entries
        int hpPct = 100;
        bool attackable = true;
        bool inCombat = false; // only read for the idle units of an encounter-end check (LinkedDeathEncounterEnded)
    };

    // Whether the group is released: fewer than two living members, or every one of them at or below `param` %. Then
    // the ordinary pick stands and all DPS burn the lowest, so the members die together.
    inline bool LinkedDeathReleased(float param, const std::vector<LinkedMember>& members)
    {
        if (members.size() < 2)
            return true;
        for (const LinkedMember& member : members)
            if (static_cast<float>(member.hpPct) > param)
                return false;
        return true;
    }

    // The member a free DPS bot attacks while the group is held: the attackable one with the highest health fraction,
    // the lowest guid on a tie. A member at or below `param` % is never it while another living member stands above
    // that (the hold), so the raid equalises the group and brings every member down to param, not past it. -1 when the
    // group is released (LinkedDeathReleased) or no member above param % can be attacked: the ordinary pick then.
    inline int PickLinkedDeath(float param, const std::vector<LinkedMember>& members)
    {
        if (LinkedDeathReleased(param, members))
            return -1;
        int best = -1;
        for (std::size_t i = 0; i < members.size(); ++i)
        {
            if (!members[i].attackable || static_cast<float>(members[i].hpPct) <= param)
                continue;
            if (best < 0 || members[i].hpPct > members[best].hpPct ||
                (members[i].hpPct == members[best].hpPct && members[i].guid < members[best].guid))
                best = static_cast<int>(i);
        }
        return best;
    }

    // Index of the first living member of the group in the released burn's order, or -1. An entry out of the bot's
    // reach (not attackable) is passed over for the next, so a member the bot cannot attack does not stall the burn;
    // a living member the order does not name is never picked. The order is the encounter's own: the Thekal trio's
    // raiser is the first living of Lor'Khan -> Zath -> Thekal, so killing in that order makes each death kill the
    // corpse's chosen raiser before its 7 s check and extends the window to the 8 s fallback.
    inline int PickLinkedDeathBurn(const std::vector<std::uint32_t>& order,
                                   const std::vector<LinkedMember>& members)
    {
        for (std::uint32_t entry : order)
            for (std::size_t i = 0; i < members.size(); ++i)
                if (members[i].entry == entry && members[i].attackable)
                    return static_cast<int>(i);
        return -1;
    }

    // The release latch, one state per group and instance. Once a known member dies while the release is armed, the
    // burn runs on for the rest of the group even if a heal brings one back above `param` or a raise restores a member
    // to full health (both would otherwise re-hold the group mid-burn and split the raid's damage again).
    // Every bot's read feeds this one state, so the armed release is the union of what all observers have seen, never
    // one bot's range-limited view: a bot that sees two low members but not a healthy third must not arm it.
    //   Known: every member guid any observer has seen living, with that guid's latest observation from any bot:
    //   alive/dead, in combat, health and time. Only living units of the group's configured entries are registered;
    //   the table is capped at kLinkedDeathKnownMax, a respawn under new guids must not grow it without bound.
    //   Armed (LinkedDeathArmed): every known living guid's latest observation is at or below `param` % and no older
    //   than kLinkedDeathSeenMs. A latest observation above `param` disarms it; a guid not seen for longer blocks it
    //   (fail safe: hold).
    //   Latch: a death latches only when the previous read left the release armed AND the read that finds the death
    //   still has it armed. The death is explicit: a known guid the observing bot finds dead or as a corpse
    //   (LinkedDeathUnseen lists the known guids the read did not see living, for the caller to look up). A member merely
    //   not seen (out of range, not cached) is never a death. Each death counts once.
    //   Cleared once every known member is confirmed dead, or the group's window (the `window_s` column, 7 s unless the
    //   encounter's own retry is longer) after the death, so a fight that cannot finish the trio goes back to the
    //   equalise hold instead of burning one member at a time forever.
    //   Encounter end (no latch): the known set, armed and latch reset when no known living guid is fighting and every
    //   one of them whose latest observation is younger than kLinkedDeathEndMs is out of combat at full health and no
    //   older than kLinkedDeathSeenMs (an evade). A guid last seen fighting or below full health keeps the fight on,
    //   even if an older observation from another bot said idle; one not seen for kLinkedDeathEndMs no longer counts.
    //   So an evade or a wipe does not leave old guids to block the next pull's arming; two observers that each see only
    //   some idle members still reset it. A known guid not seen for kLinkedDeathDropMs while the group is seen (a member
    //   despawned) is forgotten too. Within those windows an unseen member still blocks the arming (fail safe).
    constexpr std::uint32_t kLinkedDeathLatchMs = 15000;
    constexpr std::uint32_t kLinkedDeathSeenMs = 5000;
    constexpr std::uint32_t kLinkedDeathEndMs = 30000;
    constexpr std::uint32_t kLinkedDeathDropMs = 60000;
    constexpr std::size_t kLinkedDeathKnownMax = 32;

    // The latch's window in milliseconds from the group's rows: the constant only when a group carries none (the loader
    // refuses that, so it is the plain fallback).
    inline std::uint32_t LinkedDeathWindowMs(float windowSec)
    {
        return windowSec > 0.0f ? static_cast<std::uint32_t>(windowSec * 1000.0f + 0.5f) : kLinkedDeathLatchMs;
    }

    struct LinkedDeathKnown
    {
        std::uint64_t guid = 0;
        int hpPct = 100;          // the latest observation's health
        bool alive = true;        // the latest observation found it alive
        bool inCombat = false;    // the latest observation found it in combat
        std::uint32_t seenMs = 0; // when that observation was
    };
    struct LinkedDeathLatch
    {
        bool latched = false;
        std::uint32_t sinceMs = 0;           // when the latch was set (the read that saw the death)
        std::uint64_t deadGuid = 0;          // the member whose death set it
        bool armed = false;                  // the release was armed at the end of the last read
        std::vector<LinkedDeathKnown> known; // every member guid observed in this fight, latest observation first-hand
        // The current death episode's first explicit death, for the one window diagnostic the group's final death
        // writes (first_ms is the episode start, last_ms the elapsed time).
        std::uint32_t firstDeathMs = 0;
        bool firstDeathSeen = false;
    };

    // One final-death diagnostic per group episode: whether the whole group died inside its window.
    struct LinkedDeathWindowEvent
    {
        bool fired = false;
        std::uint32_t firstMs = 0;
        std::uint32_t lastMs = 0;
        std::uint32_t windowMs = 0;
        bool inside = false;
    };

    // Whether the release is armed: every known living guid's latest observation is at or below `param` % and no
    // older than kLinkedDeathSeenMs of `nowMs`. True with no living known member (the latch clears then anyway).
    inline bool LinkedDeathArmed(const LinkedDeathLatch& latch, float param, std::uint32_t nowMs)
    {
        for (const LinkedDeathKnown& known : latch.known)
        {
            if (!known.alive)
                continue;
            if (static_cast<float>(known.hpPct) > param || nowMs - known.seenMs > kLinkedDeathSeenMs)
                return false;
        }
        return true;
    }

    // The known members this read did not see living and whose latest observation is still alive: the guids the caller
    // looks up with its own unit lookup. Not seeing one is not a death; only a unit found dead is (LinkedDeathLatchTick's
    // `deadNow`).
    inline std::vector<std::uint64_t> LinkedDeathUnseen(const LinkedDeathLatch& latch,
                                                        const std::vector<LinkedMember>& members)
    {
        std::vector<std::uint64_t> unseen;
        for (const LinkedDeathKnown& known : latch.known)
        {
            if (!known.alive)
                continue;
            bool living = false;
            for (const LinkedMember& member : members)
                if (member.guid == known.guid)
                    living = true;
            if (!living)
                unseen.push_back(known.guid);
        }
        return unseen;
    }

    // Whether the group's encounter has ended without a latch: every known living guid whose latest observation is
    // younger than kLinkedDeathEndMs was last seen out of combat at full health, and not more than kLinkedDeathSeenMs
    // ago (an evade, the bosses reset). The observations come from every bot, so two observers that each see only some
    // of the idle members still agree on the whole group. A guid last seen fighting or below full health keeps the
    // fight on, however much older another bot's idle sighting of it is; a guid not seen for kLinkedDeathEndMs is no
    // longer part of the fight (the 30 s no-sighting timeout). Dead guids never block it.
    inline bool LinkedDeathEncounterEnded(const LinkedDeathLatch& latch, std::uint32_t nowMs)
    {
        for (const LinkedDeathKnown& known : latch.known)
        {
            if (!known.alive)
                continue;
            const std::uint32_t age = nowMs - known.seenMs;
            if (age >= kLinkedDeathEndMs)
                continue;
            if (known.inCombat || known.hpPct < 100 || age > kLinkedDeathSeenMs)
                return false;
        }
        return true;
    }

    // Forgets each known living guid not seen for kLinkedDeathDropMs: a member that despawned, not one that died (a
    // dead guid is counted once and never blocks the arming). The caller runs it only while the group is seen.
    inline void LinkedDeathDropStale(LinkedDeathLatch& latch, std::uint32_t nowMs)
    {
        for (auto it = latch.known.begin(); it != latch.known.end();)
        {
            if (it->alive && nowMs - it->seenMs >= kLinkedDeathDropMs)
                it = latch.known.erase(it);
            else
                ++it;
        }
    }

    // One read of the group updates the latch and answers whether it is on. `param`: the hold percent; `members`: the
    // living members this read saw, in combat; `deadNow`: the LinkedDeathUnseen guids the caller found dead; `nowMs`:
    // the world clock at the write; `scanMs`: when the bot's scan of this read was taken, before it reached the lock.
    // `idle`: the known guids this read did not see fighting but found alive (read only when `members` is empty;
    // `inCombat` marks one still fighting; a known guid's sighting, never a new registration); `windowMs`: how long the
    // latch lasts after the death (LinkedDeathWindowMs of the group's window_s).
    // Each sighting is stamped `scanMs` and replaces its guid's latest observation only when it is newer than the stored
    // one: the lock's order is not the observation order, so a bot that scanned earlier but wrote later cannot undo a
    // newer sighting (a fighting member written at 2 stays fighting against an idle sighting scanned at 1 and written
    // at 3). A tie keeps the stored observation. The sightings are applied first, then the encounter end
    // (over the whole table, see LinkedDeathEncounterEnded) and the stale guids, then the deaths; a death latches only
    // when armed both before this read and after it.
    inline bool LinkedDeathLatchTick(LinkedDeathLatch& latch, float param, const std::vector<LinkedMember>& members,
                                     const std::vector<std::uint64_t>& deadNow, std::uint32_t nowMs, std::uint32_t scanMs,
                                     const std::vector<LinkedMember>& idle = std::vector<LinkedMember>(),
                                     std::uint32_t windowMs = kLinkedDeathLatchMs,
                                     LinkedDeathWindowEvent* windowEvent = nullptr)
    {
        if (windowEvent)
            *windowEvent = LinkedDeathWindowEvent();
        const auto findKnown = [&latch](std::uint64_t guid) {
            return std::find_if(latch.known.begin(), latch.known.end(),
                                [guid](const LinkedDeathKnown& k) { return k.guid == guid; });
        };
        for (const LinkedMember& member : members)
        {
            auto known = findKnown(member.guid);
            if (known == latch.known.end())
            {
                if (latch.known.size() >= kLinkedDeathKnownMax)
                    continue;
                LinkedDeathKnown added;
                added.guid = member.guid;
                added.hpPct = member.hpPct;
                added.inCombat = true;
                added.seenMs = scanMs;
                latch.known.push_back(added);
            }
            else if (scanMs > known->seenMs)
            {
                known->hpPct = member.hpPct;
                known->alive = true;
                known->inCombat = true;
                known->seenMs = scanMs;
            }
        }
        // An idle unit is a sighting of a known guid only (its health, combat state and time).
        for (const LinkedMember& member : idle)
        {
            auto known = findKnown(member.guid);
            if (known == latch.known.end() || scanMs <= known->seenMs)
                continue;
            known->hpPct = member.hpPct;
            known->alive = true;
            known->inCombat = member.inCombat;
            known->seenMs = scanMs;
        }
        // A living member is no death: a raise makes its guid living again, so its next death counts once more, and with
        // no dead member left the episode is over (the next death starts a new one).
        if (std::none_of(latch.known.begin(), latch.known.end(),
                         [](const LinkedDeathKnown& k) { return !k.alive; }))
            latch.firstDeathSeen = false;
        if (latch.latched && nowMs - latch.sinceMs >= windowMs)
            latch.latched = false;
        if (!latch.latched)
        {
            // An ended encounter forgets the whole fight (known, armed): the next pull arms on its own guids.
            if (LinkedDeathEncounterEnded(latch, nowMs))
            {
                latch = LinkedDeathLatch();
                return false;
            }
            if (!members.empty() || !idle.empty())
                LinkedDeathDropStale(latch, nowMs);
        }
        const bool armedBefore = latch.armed;
        // A known guid found dead is a later observation than any earlier living one; a raise makes it living again,
        // so its next death counts once more.
        std::vector<std::uint64_t> newDeaths;
        for (std::uint64_t guid : deadNow)
        {
            auto known = findKnown(guid);
            if (known == latch.known.end() || !known->alive || scanMs <= known->seenMs)
                continue;
            known->alive = false;
            known->inCombat = false;
            known->seenMs = scanMs;
            if (!latch.firstDeathSeen)
            {
                latch.firstDeathSeen = true;
                latch.firstDeathMs = nowMs;
            }
            newDeaths.push_back(guid);
        }
        const bool armedNow = LinkedDeathArmed(latch, param, nowMs);
        if (!latch.latched && armedBefore && armedNow && !newDeaths.empty())
        {
            latch.latched = true;
            latch.sinceMs = nowMs;
            latch.deadGuid = newDeaths.front();
        }
        latch.armed = armedNow;
        const bool allDead = std::none_of(latch.known.begin(), latch.known.end(),
                                          [](const LinkedDeathKnown& k) { return k.alive; });
        if (members.empty() && allDead)
        {
            if (windowEvent && latch.firstDeathSeen)
            {
                windowEvent->fired = true;
                windowEvent->firstMs = 0;
                windowEvent->lastMs = nowMs - latch.firstDeathMs;
                windowEvent->windowMs = windowMs;
                windowEvent->inside = windowEvent->lastMs <= windowMs;
            }
            latch = LinkedDeathLatch();
            return false;
        }
        return latch.latched;
    }

    // A read whose scan and write are the same moment (scanMs = nowMs).
    inline bool LinkedDeathLatchTick(LinkedDeathLatch& latch, float param, const std::vector<LinkedMember>& members,
                                     const std::vector<std::uint64_t>& deadNow, std::uint32_t nowMs,
                                     const std::vector<LinkedMember>& idle = std::vector<LinkedMember>(),
                                     std::uint32_t windowMs = kLinkedDeathLatchMs,
                                     LinkedDeathWindowEvent* windowEvent = nullptr)
    {
        return LinkedDeathLatchTick(latch, param, members, deadNow, nowMs, nowMs, idle, windowMs, windowEvent);
    }

    // Whether a member at `hpPct` % is held right now: at or below `param` while another living member stands above it.
    inline bool LinkedDeathHolds(float param, int hpPct, const std::vector<LinkedMember>& members)
    {
        if (static_cast<float>(hpPct) > param)
            return false;
        for (const LinkedMember& member : members)
            if (static_cast<float>(member.hpPct) > param)
                return true;
        return false;
    }

    // Whether `guid` names a member the tactic holds right now (LinkedDeathHolds). Target selection falls through such
    // a raid mark to the group's pick: the mark would otherwise park the bot on a member that has to live to the burn.
    // A guid outside the group is never held here.
    inline bool LinkedDeathHoldsMember(float param, const std::vector<LinkedMember>& members, std::uint64_t guid)
    {
        for (const LinkedMember& member : members)
            if (member.guid == guid)
                return LinkedDeathHolds(param, member.hpPct, members);
        return false;
    }

    // Index of the held member of a linked_death group within `maxDistance` of the action (`distance[i]`, infinity for
    // one that could not be resolved), or -1. While the group is held, an area cast on another member that stands next
    // to a held one takes the held member too, and a DoT on it ticks on after the hold: both start the raise the hold
    // exists to avoid. Once every living member is at or below `param` % the group is released and the burn may AoE.
    inline int LinkedDeathHeldIndex(float param, const std::vector<LinkedMember>& members,
                                    const std::vector<float>& distance, float maxDistance)
    {
        for (std::size_t i = 0; i < members.size() && i < distance.size(); ++i)
        {
            if (distance[i] > maxDistance)
                continue;
            if (LinkedDeathHolds(param, members[i].hpPct, members))
                return static_cast<int>(i);
        }
        return -1;
    }

    // The gated linked_death pick: -1 when the key (AiPlayerbot.DungeonRunLinkedDeath) or the boss tactics strategy is
    // off, `entry` is in no group, or the group is released; else the index into `members` (the group's living units,
    // the same list LinkedDeathHolds reads).
    inline int PickLinkedDeathTarget(bool keyOn, bool tacticsOn, const ActiveBossRows& active, std::uint32_t entry,
                                     const std::vector<LinkedMember>& members)
    {
        if (!keyOn || !tacticsOn)
            return -1;
        LinkedDeathGroup group;
        if (!FindLinkedDeathGroup(active, entry, group))
            return -1;
        return PickLinkedDeath(group.param, members);
    }

    // What (re)started a dps_hold_after hold, for the log row.
    enum class DpsHoldTrigger : std::uint8_t
    {
        None = 0,
        Engage,  // the first sighting of the subject among a bot's attackers
        Band,    // the row's health band went from closed to open
        Spell,   // the subject started casting (or gained) the row's spell
        Blink,   // the subject jumped more than 10 yd within 1.5 s
    };

    inline const char* DpsHoldTriggerName(DpsHoldTrigger trigger)
    {
        switch (trigger)
        {
            case DpsHoldTrigger::Engage:
                return "engage";
            case DpsHoldTrigger::Band:
                return "band";
            case DpsHoldTrigger::Spell:
                return "spell";
            case DpsHoldTrigger::Blink:
                return "blink";
            case DpsHoldTrigger::None:
                break;
        }
        return "none";
    }

    // A blink: the subject moved more than 10 yd between two reads at most 1500 ms apart. A slower read is not
    // comparable (a walk can cover 12 yd in 2 s), and the hold's other triggers answer a real cast or aura.
    constexpr float kDpsHoldBlinkYd = 10.0f;
    constexpr std::uint32_t kDpsHoldBlinkMs = 1500;
    // A hold state nobody read for 10 minutes is stale (the subject left the fight); the caller drops it.
    constexpr std::uint32_t kDpsHoldStaleMs = 600000;
    // A read gap longer than this ends the fight the state held. The reads come from the bots' live attacker scan, so
    // nothing reads the state while the boss is out of combat after an evade or wipe; a quick repull is therefore only
    // seen as the first in-combat read after a silence (the seek clock's kSeekRepullGapMs uses the same 10 s).
    constexpr std::uint32_t kDpsHoldRepullGapMs = 10000;
    // The cast/aura edge is kept per observing bot. An observer nobody heard from for 10 s is gone (it no longer
    // vouches that a signal was already seen), and one record holds at most 64 observers.
    constexpr std::uint32_t kDpsHoldObserverStaleMs = 10000;
    constexpr std::size_t kDpsHoldMaxObservers = 64;

    inline bool DpsHoldBlinked(float lastX, float lastY, float x, float y, std::uint32_t dtMs)
    {
        if (dtMs > kDpsHoldBlinkMs)
            return false;
        float const dx = x - lastX;
        float const dy = y - lastY;
        return std::sqrt(dx * dx + dy * dy) > kDpsHoldBlinkYd;
    }

    // One eligible bot's view of the cast/aura signal: whether it has already seen the signal true (its edge bit).
    struct DpsHoldObserver
    {
        std::uint64_t guid = 0;
        std::uint32_t lastReadMs = 0;
        bool castSeen = false;
    };

    // The hold of one subject and row, shared by every bot of the instance (the caller owns the map and its mutex).
    // `startMs` is the latest trigger, `untilMs` its `param` seconds, `capMs` the until_tank cap (3 * param): the hold
    // can never outlive it. The band, engage and blink edges are shared; the cast/aura edge is kept per observing bot
    // (`observers`), so one bot's view of a cast never hides the rising edge from another, and a signal that stays true
    // does not restart the hold. The last position and read time are the blink trigger's other half.
    struct DpsHoldState
    {
        std::uint32_t startMs = 0;
        std::uint32_t untilMs = 0;
        std::uint32_t capMs = 0;
        std::uint32_t lastReadMs = 0;
        float lastX = 0.0f;
        float lastY = 0.0f;
        bool gateWasOpen = false;
        bool engageSeen = false;
        bool hasPos = false;
        // The trigger that last restarted the hold, and when: one log row per trigger and bot reads them.
        DpsHoldTrigger lastTrigger = DpsHoldTrigger::None;
        std::uint32_t lastTriggerMs = 0;
        std::vector<DpsHoldObserver> observers;
    };

    // A dps_hold_after row's fight key: map, instance, subject guid, boss entry, spell and health band (the row's own key
    // columns, BossTacticsActions.cpp DpsHoldKeyOf). The fight state and its telemetry rows share it.
    using DpsHoldKey = std::tuple<std::uint32_t, std::uint32_t, std::uint64_t, std::uint32_t, std::uint32_t,
                                  std::uint8_t, std::uint8_t>;

    // Prunes the telemetry rows (BossTacticsActions.cpp g_dpsHoldLog: one per observing bot and hold key). A row stays
    // only while its hold is still in `holds` and its map instance (key parts 0 and 1) still exists; every other row is
    // erased. Returns how many were erased.
    template <typename LogMap, typename HoldMap, typename InstanceAlive>
    std::size_t PruneDpsHoldLog(LogMap& log, const HoldMap& holds, InstanceAlive instanceAlive)
    {
        std::size_t erased = 0;
        for (auto it = log.begin(); it != log.end();)
        {
            DpsHoldKey const& key = it->first.second;
            bool const keep = holds.count(key) != 0 && instanceAlive(std::get<0>(key), std::get<1>(key));
            if (keep)
                ++it;
            else
            {
                it = log.erase(it);
                ++erased;
            }
        }
        return erased;
    }

    // One read's facts. `engageEnabled`/`gateEnabled`/`castEnabled`/`blinkEnabled` say which triggers the row uses
    // (the SQL shape decides, so the rule stays data): engage for a spell-less row that is not a blink, the band for
    // a row with one, the spell for a row that has one, and blink for mode `blink`.
    struct DpsHoldInputs
    {
        bool engaged = false;
        bool engageEnabled = false;
        // The row's whole gate (RowGateOpen): a closed band, the key off or a bot without `bot_aura` means the row
        // does not apply, and a hold it started does not outlive it.
        bool gateOpen = true;
        bool gateEnabled = false;
        // Whether this bot is part of the row at all (no `bot_aura`, or it carries it). A read by a bot outside the row
        // returns without touching the shared hold or any other bot's edge, and drops this bot's own cast/aura edge:
        // when it is eligible again it starts from "not seen", so the next cast's rising edge is its to promote.
        bool botEligible = true;
        // Who reads (the bot's guid): the key of the per-observer cast/aura edge.
        std::uint64_t observer = 0;
        bool castOrAuraSeen = false;
        bool castEnabled = false;
        bool blinkEnabled = false;
        bool untilTank = false;
        bool victimIsTank = false;
        float holdSec = 0.0f;
        float x = 0.0f;
        float y = 0.0f;
    };

    // Applies one read to the hold and answers whether it is on. A trigger restarts it; a hold lasts `param` seconds
    // from the latest trigger. With `untilTank`, a subject whose victim is not a tank keeps it on past that, up to
    // 3 * param. The caller resets the state when the boss leaves combat (evade or wipe, the fight it held is over),
    // so the engage edge fires again on the re-pull; see DpsHoldResetOnBossReset. `fired` (optional) names the
    // trigger this read started.
    inline bool DpsHoldTick(DpsHoldState& state, const DpsHoldInputs& in, std::uint32_t nowMs,
                            DpsHoldTrigger* fired = nullptr)
    {
        if (fired)
            *fired = DpsHoldTrigger::None;
        if (!in.botEligible)
        {
            for (std::size_t i = 0; i < state.observers.size(); ++i)
            {
                if (state.observers[i].guid == in.observer)
                {
                    state.observers.erase(state.observers.begin() + static_cast<std::ptrdiff_t>(i));
                    break;
                }
            }
            return false;
        }
        // This observer's cast/aura edge. Observers not heard from for a while are dropped; a new one starts as the
        // others see the signal (a bot joining mid-cast does not restart a hold the cast already started), or as
        // "not seen" when nobody else is watching it.
        for (std::size_t i = state.observers.size(); i-- > 0;)
        {
            if (state.observers[i].guid != in.observer &&
                nowMs - state.observers[i].lastReadMs > kDpsHoldObserverStaleMs)
                state.observers.erase(state.observers.begin() + static_cast<std::ptrdiff_t>(i));
        }
        DpsHoldObserver* me = nullptr;
        for (DpsHoldObserver& o : state.observers)
        {
            if (o.guid == in.observer)
            {
                me = &o;
                break;
            }
        }
        if (!me)
        {
            bool othersSee = false;
            for (const DpsHoldObserver& o : state.observers)
                othersSee = othersSee || o.castSeen;
            if (state.observers.size() >= kDpsHoldMaxObservers)
            {
                std::size_t oldest = 0;
                for (std::size_t i = 1; i < state.observers.size(); ++i)
                {
                    if (nowMs - state.observers[i].lastReadMs > nowMs - state.observers[oldest].lastReadMs)
                        oldest = i;
                }
                state.observers.erase(state.observers.begin() + static_cast<std::ptrdiff_t>(oldest));
            }
            DpsHoldObserver fresh;
            fresh.guid = in.observer;
            fresh.castSeen = othersSee;
            state.observers.push_back(fresh);
            me = &state.observers.back();
        }
        me->lastReadMs = nowMs;
        std::uint32_t const holdMs = in.holdSec > 0.0f
            ? static_cast<std::uint32_t>(in.holdSec * 1000.0f + 0.5f)
            : 0;
        DpsHoldTrigger trigger = DpsHoldTrigger::None;
        if (in.gateOpen && in.engageEnabled && in.engaged && !state.engageSeen)
        {
            state.engageSeen = true;
            trigger = DpsHoldTrigger::Engage;
        }
        if (in.gateOpen && in.gateEnabled && !state.gateWasOpen && trigger == DpsHoldTrigger::None)
            trigger = DpsHoldTrigger::Band;
        if (in.gateOpen && in.castEnabled && in.castOrAuraSeen && !me->castSeen &&
            trigger == DpsHoldTrigger::None)
            trigger = DpsHoldTrigger::Spell;
        if (in.gateOpen && in.blinkEnabled && state.hasPos &&
            DpsHoldBlinked(state.lastX, state.lastY, in.x, in.y, nowMs - state.lastReadMs) &&
            trigger == DpsHoldTrigger::None)
            trigger = DpsHoldTrigger::Blink;
        state.gateWasOpen = in.gateOpen;
        me->castSeen = in.castOrAuraSeen;
        state.lastX = in.x;
        state.lastY = in.y;
        state.lastReadMs = nowMs;
        state.hasPos = true;
        if (trigger != DpsHoldTrigger::None)
        {
            state.startMs = nowMs;
            state.untilMs = nowMs + holdMs;
            state.capMs = nowMs + 3 * holdMs;
            state.lastTrigger = trigger;
            state.lastTriggerMs = nowMs;
            if (fired)
                *fired = trigger;
        }
        if (!in.gateOpen || holdMs == 0)
            return false;
        if (nowMs < state.untilMs)
            return true;
        return in.untilTank && !in.victimIsTank && nowMs < state.capMs;
    }

    // The fight the state held is over: the boss is out of combat (an evade or a wipe), or nobody read the state for
    // longer than kDpsHoldRepullGapMs (the boss left the live attacker scan, so no read saw him out of combat and the
    // next one finds the re-pull already running). The engage edge then fires again on the re-pull. Health plays no part:
    // an in-combat boss that is back at 100 % (Blood Siphon heals him) is still the same fight and keeps its spent edge
    // and running hold. Call before DpsHoldTick, which stamps the read time.
    inline void DpsHoldResetOnBossReset(DpsHoldState& state, bool bossInCombat, std::uint32_t nowMs)
    {
        const bool silent = state.hasPos && nowMs - state.lastReadMs > kDpsHoldRepullGapMs;
        if (!bossInCombat || silent)
            state = DpsHoldState();
    }

    // The held-subject veto over every live unit of one subject entry: `held[i]` is whether unit i is held after its
    // read, `distance[i]` its distance from the area's centre. Returns the first held unit within `maxDistance`, or -1.
    // The caller reads every unit, so any held unit in range vetoes, not only the last one read.
    inline int DpsHoldVetoIndex(const std::vector<bool>& held, const std::vector<float>& distance, float maxDistance)
    {
        for (std::size_t i = 0; i < held.size() && i < distance.size(); ++i)
        {
            if (held[i] && distance[i] <= maxDistance)
                return static_cast<int>(i);
        }
        return -1;
    }

    // taunt_after_cast (mode until_tank), the `priority` tank of the raid order: the first ALIVE tank at or after it,
    // wrapping to the front, so a dead first off-tank hands the taunt to the next one instead of leaving the subject
    // loose. -1 when no tank is alive or the row names no index. `tankAlive` is the raid order (GetRaidTankRoles), so
    // its own list already drops a tank that is dead or out of the group.
    inline int TauntAfterTankIndex(const std::vector<bool>& tankAlive, int wanted)
    {
        int const count = static_cast<int>(tankAlive.size());
        if (count == 0 || wanted < 0)
            return -1;
        for (int step = 0; step < count; ++step)
        {
            int const index = (wanted + step) % count;
            if (tankAlive[index])
                return index;
        }
        return -1;
    }

    // Whether a tank can still take the taunt back at all, ignoring its cooldown: in control (a stun, a fear, a
    // confuse, a hex or a charm means no cast), not silenced or pacified, its taunt known. This is what a window's
    // holder is kept by: the taunt it just cast being on cooldown must not hand the window to a second tank, which
    // would burn its own taunt in the same window.
    inline bool TauntAfterTankAvailable(bool inControl, bool silencedOrPacified, bool hasTaunt)
    {
        return inControl && !silencedOrPacified && hasTaunt;
    }

    // Whether a tank can cast its taunt right now: available (TauntAfterTankAvailable) and the taunt off cooldown.
    // Readiness is only read when PICKING a window's tank, never to keep the one that has it. The target and range
    // checks are separate: a window's first choice must also be on the subject and in melee
    // (TauntAfterTankCanTaunt), while the pick's fallback only has to be able to cast it.
    // `isVictim`: the tank is the subject's current victim (Jin'do's Banish sent it to the pit, the boss follows it): it
    // never takes the window's fallback while another tank can, since the taunt would go to the tank the boss is
    // already on. The pick's last tier still returns it when it is the only usable tank.
    inline bool TauntAfterTankCanHold(bool inControl, bool silencedOrPacified, bool hasTaunt, bool tauntReady,
        bool isVictim = false)
    {
        return TauntAfterTankAvailable(inControl, silencedOrPacified, hasTaunt) && tauntReady && !isVictim;
    }

    // Whether a tank can take a taunt back right now: it can cast it (TauntAfterTankCanHold), it is in melee of the
    // subject, with it as its target, and not its victim.
    inline bool TauntAfterTankCanTaunt(bool inControl, bool silencedOrPacified, bool hasTaunt, bool tauntReady,
        bool targetingSubject, bool isVictim, bool inMelee)
    {
        return TauntAfterTankCanHold(inControl, silencedOrPacified, hasTaunt, tauntReady) &&
            targetingSubject && !isVictim && inMelee;
    }

    // The tank of one until_tank window: the first usable tank at or after `priority` that can taunt the subject right
    // now (TauntAfterTankCanTaunt) counts before one that cannot; then a tank that can cast its taunt at all
    // (canHold), which is walking in or on another target; only then the first usable tank, whatever its state. A
    // hexed or silenced tank is never chosen while a healthy one stands. It is read once per window, when the window
    // opens or its tank is no longer available, so one tank holds the window while its deadline runs; `priority` is
    // one past the previous holder for a window whose taunt was cast and failed, so a resisted taunt hands the next
    // one to another tank. -1 when no tank is usable.
    inline int TauntAfterTankPick(const std::vector<bool>& usable, const std::vector<bool>& canTaunt,
        const std::vector<bool>& canHold, int wanted)
    {
        int const count = static_cast<int>(usable.size());
        if (count == 0 || wanted < 0)
            return -1;
        for (int step = 0; step < count; ++step)
        {
            int const index = (wanted + step) % count;
            if (usable[index] && index < static_cast<int>(canTaunt.size()) && canTaunt[index])
                return index;
        }
        for (int step = 0; step < count; ++step)
        {
            int const index = (wanted + step) % count;
            if (usable[index] && index < static_cast<int>(canHold.size()) && canHold[index])
                return index;
        }
        return TauntAfterTankIndex(usable, wanted);
    }

    // The tank a window's read assigns: its holder while the window runs and that tank is still available (in
    // control, not silenced, its taunt known: TauntAfterTankAvailable, never its cooldown), else the pick at
    // `wanted`. The glue's advance and the policy tests read this one expression, so a holder that loses control
    // hands the window over at once while one whose taunt is cooling keeps it.
    inline int TauntWindowAssign(bool opening, bool holderAvailable, int holderSlot, int wanted,
        const std::vector<bool>& usable, const std::vector<bool>& canTaunt, const std::vector<bool>& canHold)
    {
        if (!opening && holderAvailable)
            return holderSlot;
        return TauntAfterTankPick(usable, canTaunt, canHold, wanted);
    }

    // The slot of `guid` in the raid order, -1 when it is no tank of it any more: a window's holder is kept by guid,
    // since a slot stored from one bot's order can name a different tank after the order changed.
    inline int TankSlotOf(const std::vector<std::uint64_t>& guids, std::uint64_t guid)
    {
        if (guid == 0)
            return -1;
        for (std::size_t i = 0; i < guids.size(); ++i)
        {
            if (guids[i] == guid)
                return static_cast<int>(i);
        }
        return -1;
    }

    // The one tank that advances a window's shared state: its holder while that tank is a usable tank of the order
    // AND available (TauntAfterTankAvailable: in control, not silenced), else the first tank at or after `wanted` that
    // is usable and available, else the first usable one (TauntAfterTankIndex). A stunned, hexed or silenced holder is
    // therefore not the writer any more, so another tank hands the window over in the same tick instead of waiting for
    // the silent-writer timeout. -1 when no tank is usable.
    inline int TauntWindowOwner(const std::vector<bool>& usable, const std::vector<bool>& available, int holder,
        int wanted)
    {
        auto const ready = [&](int index) {
            return index >= 0 && index < static_cast<int>(usable.size()) && usable[index] &&
                index < static_cast<int>(available.size()) && available[index];
        };
        if (ready(holder))
            return holder;
        int const count = static_cast<int>(usable.size());
        if (count > 0 && wanted >= 0)
        {
            for (int step = 0; step < count; ++step)
            {
                int const index = (wanted + step) % count;
                if (ready(index))
                    return index;
            }
        }
        return TauntAfterTankIndex(usable, wanted);
    }

    // Whether one tank's read is the window's single writer: the owner's own read, or any read once the owner has not
    // advanced the state for `silentMs` (it stands out of the fight's scan, so its window would never end).
    inline bool TauntWindowAdvance(int owner, int reader, std::uint32_t nowMs, std::uint32_t advancedMs,
        std::uint32_t silentMs)
    {
        if (owner >= 0 && owner == reader)
            return true;
        return nowMs - advancedMs >= silentMs;
    }

    // The one boundary of a taunt_after_cast window: `sinceMs` how long ago it opened and `deadlineMs` the row's
    // param. The deadline instant itself is still the window's (it can taunt), so the window is handed over only
    // from the next millisecond.
    inline bool TauntDeadlinePassed(std::uint32_t sinceMs, std::uint32_t deadlineMs)
    {
        return sinceMs > deadlineMs;
    }

    // The taunt_after_cast deadline: `triggered` is a window that is open, `sinceMs` how long ago it opened and
    // `deadlineMs` the row's param. Nothing is taken back while the subject's victim is the assigned tank.
    inline bool TauntAfterDue(bool triggered, std::uint32_t sinceMs, std::uint32_t deadlineMs, bool victimIsAssigned)
    {
        if (!triggered || victimIsAssigned)
            return false;
        return !TauntDeadlinePassed(sinceMs, deadlineMs);
    }

    // Whether a tank in the raid order can still take the taunt: a creature-charmed tank yields its AI completely
    // (AiPlayerbot.DungeonRunCharmYield), so it is as gone as a dead one and the order wraps past it.
    inline bool TauntAfterTankUsable(bool hasCharmer, bool charmerIsPlayer)
    {
        return !(hasCharmer && !charmerIsPlayer);
    }

    enum class TauntEpisodeStep
    {
        None = 0,
        Due = 1,      // the open window allows the taunt and the victim is not its tank
        Regained = 2, // a tank has the subject back: the episode ends
    };

    struct TauntEpisodeRead
    {
        bool starts = false; // a window opens now: the caller resets its deadline clock
        bool open = false;
        bool missed = false; // the window that just ran out on a non-tank is logged once
        TauntEpisodeStep step = TauntEpisodeStep::None;
    };

    // taunt_after_cast, a row with a spell id (Jin'do's Banish 24466): the spell is an instant teleport with no aura
    // and no cast slot to read, and it leaves the tank as the subject's victim. The row's trigger is the tank's own
    // teleport, seen as an edge and latched (TauntTeleportLatch), never a steady-state distance: a tank that is merely
    // far (kiting, a knockback, pathing) opens nothing. The edge is the tank appearing within kTauntDestinationYd of
    // the spell's destination (Spell target position, atSpellDestination) or any jump of its own position of more than
    // kTauntTeleportYd between two reads (jumpYd, -1 when the glue has no recent read of the same victim). Banish drops
    // a player in the ZG pit 47 yd west of Jin'do's stand and he follows it to the rim, about 21 yd from the landing:
    // the latch keeps the window open against that, until another tank has the subject, the banished tank is back
    // within kTauntBackYd of it, or kTauntLatchMaxMs pass. A bot walks about 2 yd/s in a fight, so a holding tank
    // never jumps.
    constexpr float kTauntTeleportYd = 20.0f;
    constexpr float kTauntDestinationYd = 12.0f;
    constexpr float kTauntBackYd = 10.0f;
    constexpr std::uint32_t kTauntLatchMaxMs = 12000;

    struct TauntTeleportLatch
    {
        bool open = false;
        std::uint64_t tankGuid = 0;      // the tank the teleport moved
        std::uint32_t startMs = 0;
        bool wasAtDestination = false;   // the previous read had a tank at the spell's destination (the edge's memory)
    };

    // One read of the taunt_after_cast victim, as the episode inputs want it: `onNonTank` opens (and holds) the
    // episode, `victimIsTank` closes it when a tank has the subject back. A player that is no tank (a charm, or Hex
    // wiping the tank's threat and leaving him on a non-tank) is the spell-less rows' trigger; a spell-keyed row adds
    // the tank its spell teleported (the latch above). `victimIsTankRole` is the raid order's answer for the player.
    struct TauntAfterVictimRead
    {
        bool onNonTank = false;
        bool victimIsTank = false;
    };

    struct TauntAfterVictimIn
    {
        bool victimIsPlayer = false;
        bool victimIsTankRole = false;
        bool spellKeyed = false;
        std::uint64_t victimGuid = 0;
        float ydFromSubject = 0.0f;
        float jumpYd = -1.0f;              // the victim's own displacement since the last read (-1: none)
        bool atSpellDestination = false;   // the victim stands within kTauntDestinationYd of the spell's destination
        std::uint32_t nowMs = 0;
    };

    inline TauntAfterVictimRead TauntAfterVictim(const TauntAfterVictimIn& in, TauntTeleportLatch& latch)
    {
        TauntAfterVictimRead out;
        out.victimIsTank = in.victimIsPlayer && in.victimIsTankRole;
        out.onNonTank = in.victimIsPlayer && !in.victimIsTankRole;
        if (!in.spellKeyed)
        {
            latch = TauntTeleportLatch();
            return out;
        }
        if (!out.victimIsTank)
        {
            // A non-tank player or a pet: the plain read, the latch has nothing to hold.
            latch.open = false;
            return out;
        }
        bool const destinationEdge = in.atSpellDestination && !latch.wasAtDestination;
        latch.wasAtDestination = in.atSpellDestination;
        if (latch.open)
        {
            bool const sameTank = in.victimGuid == latch.tankGuid;
            bool const aged = in.nowMs - latch.startMs > kTauntLatchMaxMs;
            if (sameTank && !aged && in.ydFromSubject > kTauntBackYd)
            {
                out.victimIsTank = false;
                out.onNonTank = true;
                return out;
            }
            latch.open = false; // another tank has the subject, or the tank is back, or the window ran out
            return out;
        }
        if ((destinationEdge || in.jumpYd > kTauntTeleportYd) && in.ydFromSubject > kTauntBackYd)
        {
            latch.open = true;
            latch.tankGuid = in.victimGuid;
            latch.startMs = in.nowMs;
            out.victimIsTank = false;
            out.onNonTank = true;
        }
        return out;
    }

    // One read of a taunt_after_cast window (mode until_tank): `onNonTank` the subject's victim is a player that is
    // not a tank, `victimIsTank` any tank has it. While the subject stays on a non-tank a new window opens the moment
    // the last one ran out (`param` seconds), so a taunt that missed or could not be cast is retried at that cadence
    // instead of the row going dead until a tank happens to take the subject back. A tank having it back ends the
    // episode, and a subject between victims (a pet, a transition) keeps the open window's clock running.
    // `tauntable` is false for a subject the core makes immune to taunts (Creature::HasImmunityFlag(
    // CREATURE_IMMUNITY_TAUNT)): no taunt of the row's tank can land on it, so the row opens no episode and spends no
    // attack or taunt there.
    inline TauntEpisodeRead TauntEpisodeTick(bool tauntable, bool onNonTank, bool victimIsTank, bool open,
        std::uint32_t sinceMs, std::uint32_t deadlineMs, bool victimIsAssigned)
    {
        TauntEpisodeRead out;
        if (!tauntable)
            return out;
        out.open = open;
        if (onNonTank)
        {
            if (TauntAfterDue(true, open ? sinceMs : 0, deadlineMs, victimIsAssigned))
            {
                out.starts = !open;
                out.open = true;
                out.step = TauntEpisodeStep::Due;
                return out;
            }
            // The window ran out, or its own assigned tank is the victim (it cannot taunt itself, yet counts as a
            // non-tank: banished, teleported or charmed out of the role, the pick falling back to it as the only tank
            // left). The run-out is logged once and the next window opens at once; a window whose assigned tank is
            // the victim opens once and then runs its deadline like any other, never restarting on every read.
            bool const runOut = open && TauntDeadlinePassed(sinceMs, deadlineMs);
            if (victimIsAssigned && open && !runOut)
            {
                out.step = TauntEpisodeStep::None;
                return out;
            }
            out.missed = runOut;
            out.starts = true;
            out.open = true;
            out.step = victimIsAssigned ? TauntEpisodeStep::None : TauntEpisodeStep::Due;
            return out;
        }
        if (victimIsTank)
        {
            if (open)
                out.step = TauntEpisodeStep::Regained;
            out.open = false;
        }
        return out;
    }

    // The widest live spread row's spacing, never narrower than the fallback: the row widens a formation, it does not
    // crowd the raid back into the splash.
    inline float WidestSpreadYd(const std::vector<float>& params, float fallback)
    {
        float best = fallback;
        for (float param : params)
            if (param > best)
                best = param;
        return best;
    }

    // One living creature of a kill_first entry within kBossTacticsEngageRange of the bot, as the grid search saw it
    // (the "kill first grid units" value). The fight decides who is pickable; this is only the search.
    struct KillFirstGridUnit
    {
        std::uint64_t guid = 0;
        std::uint32_t entry = 0;
        float distance = 0.0f;   // from the bot
        bool alive = true;
        bool evading = false;
        bool victimOurs = false; // its victim is a member of the bot's group (or one of their pets)
        bool victimHealer = false; // ... and that member is a healer (the Shades of Jin'do eat the healers first)
        bool weHoldIt = false;   // a member or their pet is on its threat list, or attacks it
        std::uint64_t owner = 0; // owner, else creator, else the temporary summon's summoner (0: none)
    };

    // Whether a grid hit of a kill_first entry may join the pick: alive, not running home, and part of our fight --
    // it is engaged with the group (its victim is ours, or one of ours holds it), or it is a ward or totem of a unit
    // of this fight. Being in combat is not enough: a creature of the entry brawling with an NPC or a player outside
    // the group within range is not this fight's, and keeps an idle add of the entry elsewhere on the map out too.
    inline bool KillFirstGridEligible(bool alive, bool evading, bool victimOurs, bool weHoldIt,
        bool ownerOrSummonerInFight)
    {
        return alive && !evading && (victimOurs || weHoldIt || ownerOrSummonerInFight);
    }

    // A live creature in the fight, as the kill-first pick sees it.
    struct TacticCandidate
    {
        std::uint32_t entry = 0;
        float distance = 0.0f;
        bool pickable = false;  // alive, attackable, reachable by the caller's rules
        bool victimHealer = false; // its victim is a healer of the bot's group (a Shade of Jin'do on a healer)
        float dropYd = 0.0f;       // signed height of the bot against the candidate (bot z - candidate z)
    };

    // A candidate the attackers path and the grid read both found keeps one entry; a victim that is a healer on either
    // read counts (the grid read sees the victim from the unit, the attackers path from the same unit: they agree
    // unless one read missed it).
    inline void MergeVictimHealer(TacticCandidate& existing, bool gridVictimHealer)
    {
        existing.victimHealer = existing.victimHealer || gridVictimHealer;
    }

    // Nobody is sent after a creature further away than this.
    constexpr float kBossTacticsEngageRange = 40.0f;

    // Index of the candidate to kill first, or -1 when no kill_first row names
    // a pickable candidate: lowest priority wins, then one whose victim is a
    // healer of ours (the Shades of Jin'do copy the boss's threat list, on which
    // the healers' heal threat is top, and eat them), the nearer one breaks the
    // rest of the tie.
    inline int PickKillFirst(const ActiveBossRows& active, const std::vector<TacticCandidate>& candidates)
    {
        int best = -1;
        int bestPriority = 256;
        float bestDistance = 0.0f;
        bool bestVictimHealer = false;
        for (std::size_t i = 0; i < candidates.size(); ++i)
        {
            const TacticCandidate& c = candidates[i];
            if (!c.pickable || c.distance > kBossTacticsEngageRange)
                continue;
            int priority = 256;
            for (const BossTacticRow* row : active)
            {
                if (row->kind == BossTacticKind::KillFirst && TacticSubjectEntry(*row) == c.entry &&
                    static_cast<int>(row->priority) < priority)
                    priority = row->priority;
            }
            if (priority == 256)
                continue;
            if (best < 0 || priority < bestPriority ||
                (priority == bestPriority &&
                 (c.victimHealer > bestVictimHealer ||
                  (c.victimHealer == bestVictimHealer && c.distance < bestDistance))))
            {
                best = static_cast<int>(i);
                bestPriority = priority;
                bestDistance = c.distance;
                bestVictimHealer = c.victimHealer;
            }
        }
        return best;
    }

    // A kill_first row a bot opens only while it carries the row's `bot_aura`: the Shade of Jin'do 14986, which only a
    // Delusions 24306 carrier can see and hurt (boss_jindo.cpp:326-330). The fight scan drops a gated row for a bot without
    // the aura, so an open one in the bot's rows means the bot carries it now.
    inline bool CarrierKillFirstRow(const BossTacticRow& row)
    {
        return row.kind == BossTacticKind::KillFirst && row.botAura != 0;
    }

    inline bool HasCarrierKillFirstRow(const ActiveBossRows& active)
    {
        for (const BossTacticRow* row : active)
            if (CarrierKillFirstRow(*row))
                return true;
        return false;
    }

    // Whether the fight's open rows ask for a threat successor: the boss is taunt-immune, so a threat wipe (Hex) hands it
    // to whoever stands highest, and only the assigned off-tank that held it takes it back. An off-tank that leaves for a
    // Shade stops holding, the damage dealers' cap falls to half the main tank's threat (ThreatCapPolicy.h), and the
    // wipe hands the boss to a damage dealer.
    inline bool HasThreatSuccessorRow(const ActiveBossRows& active)
    {
        for (const BossTacticRow* row : active)
            if (row->kind == BossTacticKind::ThreatSuccessor)
                return true;
        return false;
    }

    // Which kill_first pick a bot makes. A free DPS (no explicit attack order) takes every open row (PickKillFirst). A DPS
    // under the run's own order (`orderAutomated`: the run's focus, the 3 s ward refocus) takes only the carrier
    // override (PickCarrierOverride): its own subject's damage counts and nobody else's does. A player's order is never
    // overridden: the player's target or focus wins. Tanks and healers keep their jobs.
    enum class KillFirstPickMode : std::uint8_t { None, Free, Carrier };

    inline KillFirstPickMode KillFirstPickModeFor(bool battleDps, bool explicitOrder, bool orderAutomated,
                                                  bool carrierRowOpen)
    {
        if (!battleDps)
            return KillFirstPickMode::None;
        if (!explicitOrder)
            return KillFirstPickMode::Free;
        return orderAutomated && carrierRowOpen ? KillFirstPickMode::Carrier : KillFirstPickMode::None;
    }

    // --- carrier strike ---------------------------------------------------------------------------------------------
    // A cursed healer or off-tank turns on its own Shade (a kill_first row gated by the curse) while the raid is safe.
    // The kill_first node itself is DPS-only and sits far below every heal row, so this carrier needs its own node and
    // an explicit guard: a healer that leaves the raid to hit a Shade must not be the difference between a member
    // living and dying.
    enum class CarrierStrikeRole : std::uint8_t { Dps, Healer, OffTank, MainTank };
    enum class CarrierStrikeVerdict : std::uint8_t
    {
        Strike,
        NotCarrier,
        NoShade,
        MainTank,
        ThreatSuccessor,
        HoldsBoss,
        GuardLowMember,
        GuardTank,
        GuardHealers,
        GuardMana,
        GuardDispel,
        Dps,
    };

    // One other healer as the count reads it, every field the caller's read of the member.
    struct CarrierHealer
    {
        bool alive = false;
        bool cursed = false;     // carries the fight's carrier aura
        bool charmed = false;    // a charmer owns its AI (the Brain Wash Totem), so it does not heal the raid
        float distanceYd = 0.0f; // from the carrier (the raid)
        float dropYd = 0.0f;     // signed height against the carrier: how far below it (+), how far above it (-)
    };

    // A healer whose height against the carrier exceeds this is on another floor (Jin'do's troll pit floor sits ~12 yd
    // under the boss's), where its heals cannot reach the raid. Read both ways: the carrier itself may be the one in the
    // pit, and a healer on the room floor is then just as far from it.
    constexpr float kCarrierPitDropYd = 6.0f;

    // The other healers that can really heal the raid: alive, not carriers, not charmed, within the carrier's own heal
    // reach (twice the heal range) and on the same floor as the carrier.
    inline bool CarrierHealerCounts(const CarrierHealer& healer, float reachYd)
    {
        return healer.alive && !healer.cursed && !healer.charmed && healer.distanceYd <= reachYd &&
            std::fabs(healer.dropYd) <= kCarrierPitDropYd;
    }

    inline std::uint8_t CarrierHealerCount(const std::vector<CarrierHealer>& healers, float reachYd)
    {
        std::uint8_t count = 0;
        for (const CarrierHealer& healer : healers)
            if (CarrierHealerCounts(healer, reachYd))
                ++count;
        return count;
    }

    struct CarrierStrikeInput
    {
        CarrierStrikeRole role;
        bool cursed;            // this bot carries the carrier row's aura
        bool shadeInReach;      // a pickable unit of the carrier row within kCarrierHoldReachYd
        bool holdsBoss;         // a boss of the fight is on this bot
        bool threatSuccessor;   // the fight's boss has a `threat_successor` row: a tank leaving it hands it to a DPS
        std::uint8_t lowestMemberPct; // the lowest living group member within 2x heal range
        std::uint8_t tankPct;         // the main tank (0: no main tank alive)
        std::uint8_t otherHealers;    // CarrierHealerCount: healers other than this bot that can heal the raid
        std::uint8_t manaPct;
        bool dispelNowPending;  // a `now` dispel_priority aura on a member in reach this class can cure
        // AiPlayerbot.DungeonRunShadeHealerCover is on and the bot's pick is a Shade on a healer of ours within
        // kShadeCoverReachYd of the bot and on its floor, or one it pulled off a healer (ShadeCoverLatch): a free
        // off-tank takes it even on a threat_successor fight (false = today's rules).
        bool shadeCover = false;
        // Under the cover: the lowest living member within 2x heal range other than the Shade's victim, the bot's own
        // health always counted. 0 (the default) refuses, so a caller that forgets it keeps the off-tank home.
        std::uint8_t lowestCoverPct = 0;
    };

    // The carrier's own guards that no config key carries; the lowest-member line is the live AiPlayerbot.LowHealth the
    // caller passes to CarrierStrikeAllowed (a healer that leaves the raid must not drop it below the low-heal line).
    constexpr std::uint8_t kCarrierStrikeTankPct = 60;
    constexpr std::uint8_t kCarrierStrikeHealers = 2;    // other healers that can heal the raid still standing
    constexpr std::uint8_t kCarrierStrikeManaPct = 30;

    // The first check that fails gives the verdict; earlier checks are the roles this rule never covers.
    inline CarrierStrikeVerdict CarrierStrikeAllowed(const CarrierStrikeInput& in, std::uint8_t lowHealthPct)
    {
        if (in.role == CarrierStrikeRole::Dps)
            return CarrierStrikeVerdict::Dps;
        if (in.role == CarrierStrikeRole::MainTank)
            return CarrierStrikeVerdict::MainTank;
        // On a successor fight no off-tank may leave the boss, held or not: while no off-tank holds, the damage dealers'
        // cap is half the main tank's threat, and the next threat wipe hands the boss to a damage dealer.
        // The healer cover is the other side of that rule: a cursed off-tank the boss is not hitting takes a Shade that is
        // eating a healer (an off-tank holding the boss stays: HoldsBoss below).
        bool const offTankCover = in.role == CarrierStrikeRole::OffTank && in.shadeCover && in.cursed && !in.holdsBoss;
        if (in.role == CarrierStrikeRole::OffTank && in.threatSuccessor && !offTankCover)
            return CarrierStrikeVerdict::ThreatSuccessor;
        if (!in.cursed)
            return CarrierStrikeVerdict::NotCarrier;
        if (!in.shadeInReach)
            return CarrierStrikeVerdict::NoShade;
        if (in.role == CarrierStrikeRole::OffTank && in.holdsBoss)
            return CarrierStrikeVerdict::HoldsBoss;
        if (in.role == CarrierStrikeRole::Healer)
        {
            if (in.dispelNowPending)
                return CarrierStrikeVerdict::GuardDispel;
            if (in.lowestMemberPct < lowHealthPct)
                return CarrierStrikeVerdict::GuardLowMember;
            if (in.tankPct < kCarrierStrikeTankPct)
                return CarrierStrikeVerdict::GuardTank;
            if (in.otherHealers < kCarrierStrikeHealers)
                return CarrierStrikeVerdict::GuardHealers;
            if (in.manaPct < kCarrierStrikeManaPct)
                return CarrierStrikeVerdict::GuardMana;
        }
        if (in.role == CarrierStrikeRole::OffTank)
        {
            // Under the cover the low member is usually the healer the Shade is eating: killing the Shade is the rescue,
            // so only that victim is left out of the line; the off-tank's own health and everyone else's still count.
            if ((offTankCover ? in.lowestCoverPct : in.lowestMemberPct) < lowHealthPct)
                return CarrierStrikeVerdict::GuardLowMember;
            if (in.tankPct < kCarrierStrikeTankPct)
                return CarrierStrikeVerdict::GuardTank;
        }
        return CarrierStrikeVerdict::Strike;
    }

    // The damaging spells this class strikes a Shade with, shortest cast first; empty means melee or wand (Attack).
    // CLASS_* ids are kept literal so this header stays free of the core includes.
    inline std::vector<const char*> CarrierStrikeSpells(std::uint8_t cls)
    {
        switch (cls)
        {
            case 5:  // CLASS_PRIEST
                return {"mind blast", "smite"};
            case 11: // CLASS_DRUID
                return {"moonfire", "wrath"};
            case 7:  // CLASS_SHAMAN
                return {"earth shock", "lightning bolt"};
            case 2:  // CLASS_PALADIN
                return {"holy shock"};
            default:
                return {};
        }
    }

    // Whether the kill_first trigger has work: the current target is not the pick, or the bookkeeping already names the
    // pick while the live victim is another unit (or none). The target value can lead the swing by a tick; testing only
    // the target left the trigger off for the rest of the fight, and the action's kept-target branch never attacked.
    inline bool KillFirstNeedsAttack(bool hasPick, bool currentIsPick, bool victimIsPick)
    {
        return hasPick && (!currentIsPick || !victimIsPick);
    }

    // Whether the bot's explicit attack target is the run's automated order: the run marks the target its focus or
    // attack order set ("automated attack target"), and every command commit (a player's `attack`, a mark order) clears
    // that mark, so only an order the run itself issued still matches it.
    inline bool OrderIsAutomated(std::uint64_t explicitTarget, std::uint64_t automatedTarget)
    {
        return explicitTarget != 0 && explicitTarget == automatedTarget;
    }

    // The bot's "automated attack target" mark after an order: `runIssued` is whether the run issued it (for a held
    // attack, recorded when it was held, since it fires outside the run's issuing), `accepted` whether the bot took it,
    // `explicitTarget` the explicit target the order committed (the commit already cleared the mark: `currentMark`).
    // Only a taken run order marks its target; anything else leaves the mark as it is.
    inline std::uint64_t RunOrderMark(bool runIssued, bool accepted, std::uint64_t explicitTarget,
                                      std::uint64_t currentMark)
    {
        return runIssued && accepted ? explicitTarget : currentMark;
    }

    // How far the carrier turns on its subject, under an explicit attack order or free (t1106b): the hold reach for
    // every bot, melee or ranged. Any Delusions carrier can see and damage any Shade (the curse is the only gate, not
    // which carrier the Shade was summoned on), and the Shades are summoned on one carrier and walk off to a healer: a
    // 15/30 yd first override left half of the cursed DPS on Jin'do with a Shade beside them, and only one carrier on a
    // Shade takes ~17 s while a new one comes every 7-8 s.
    // Distances are GetDistance's, bounding radii already off.
    constexpr float kCarrierHoldReachYd = kBossTacticsEngageRange;

    // Why a carrier did not turn on its Shade (BossTacticsKillFirstAction's row). `carrierRowOpen`: the fight read
    // shows an open aura-gated kill_first row, so this bot carries the curse and a Shade is a subject of it.
    // `shadeInFight`: a living unit of the row's entry is among the fight's units; `pickable`/`distanceYd`: what the
    // pick made of the nearest of them. None: no carrier row, or a Shade in reach that was pickable (the action takes
    // it, no row).
    enum class CarrierNoSwitch : std::uint8_t { None, NoShade, NotAttackable, BeyondReach };

    inline CarrierNoSwitch CarrierNoSwitchReason(bool carrierRowOpen, bool shadeInFight, bool pickable,
        float distanceYd, float reachYd)
    {
        if (!carrierRowOpen)
            return CarrierNoSwitch::None;
        if (!shadeInFight)
            return CarrierNoSwitch::NoShade;
        if (!pickable)
            return CarrierNoSwitch::NotAttackable;
        if (distanceYd > reachYd)
            return CarrierNoSwitch::BeyondReach;
        return CarrierNoSwitch::None;
    }

    // The kill-first switch row (BossTacticsKillFirst reason=switch): how long this bot's pick had known the Shade it
    // just turned on (`firstSeenMs` is the pick's first sight of it, 0 when unknown). The unsigned subtraction survives
    // the 32-bit clock wrap. 0 for an unknown first sight, so the row never claims a delay it did not measure.
    inline std::uint32_t CarrierSwitchSinceMs(std::uint32_t firstSeenMs, std::uint32_t nowMs)
    {
        return firstSeenMs == 0 ? 0u : nowMs - firstSeenMs;
    }

    // How long a bot's first-sight record of a Shade survives a pass that did not list it (the attackers list flickers,
    // and a Shade out of reach is not listed for a moment): the record is dropped only once the bot's pick has not seen the
    // Shade for this long, so `since_ms` stays the time since the FIRST sight.
    constexpr std::uint32_t kShadeSeenGraceMs = 3000;

    inline bool ShadeSeenExpired(std::uint32_t lastSeenMs, std::uint32_t nowMs)
    {
        return nowMs - lastSeenMs > kShadeSeenGraceMs;
    }

    // A needs_aura entry (the Shade of Jin'do 14986) takes damage only from a carrier's direct hit: the core zeroes
    // every hit whose source lacks the aura, a DoT tick lands only while the caster still carries it, and a pet's hit
    // is credited to the pet and never counts. So a DoT cast at one and the pet sent at one only spend the caster's
    // time, which on a 7-8 s summon cadence the Shade does not have.
    inline bool AuraGatedDirectOnlyDamage(bool gateKeyOn, std::uint32_t requiredAura)
    {
        return gateKeyOn && requiredAura != 0;
    }

    // Healer cover (AiPlayerbot.DungeonRunShadeHealerCover): a Shade on a healer of ours ranks first within this reach,
    // and the BossTacticsShadeCover row calls a carrier further from the Shade out_of_range.
    constexpr float kShadeCoverReachYd = 30.0f;

    // Whether a carrier candidate ranks in the healer-cover tier: its victim is a healer of ours, within the cover reach.
    // Whether a Shade is on the bot's floor: the troll pit sits ~12 yd under the boss's floor (kCarrierPitDropYd, the
    // line the carrier strike's healer count uses), and a melee carrier sent down there cannot get back for a long time.
    inline bool ShadeCoverSameFloor(float dropYd)
    {
        return std::fabs(dropYd) <= kCarrierPitDropYd;
    }

    inline bool ShadeCoverTier(const TacticCandidate& c, bool preferHealerVictim)
    {
        return preferHealerVictim && c.victimHealer && c.distance <= kShadeCoverReachYd &&
            ShadeCoverSameFloor(c.dropYd);
    }

    // The off-tank's cover latch (M1): a cursed off-tank that takes a Shade off a healer soon pulls it onto itself, and
    // the Shade's victim then is no healer any more. The latch keeps the cover for that off-tank while the Shade's
    // victim is the off-tank itself, until the Shade dies (the pick no longer names it), the bot loses the curse, or
    // kShadeCoverLatchMs pass since the Shade was last on a healer.
    constexpr std::uint32_t kShadeCoverLatchMs = 15000;

    struct ShadeCoverLatch
    {
        std::uint64_t shade = 0; // 0: no latch
        std::uint32_t sinceMs = 0; // the last time the latched Shade was seen on a healer
    };

    // `shade`: the bot's pick (0 when none); `onHealer`: its victim is a healer of ours; `onSelf`: its victim is the bot.
    // Distance and floor are the caller's (the cover needs both whatever the latch says). True when the cover holds.
    inline bool ShadeCoverLatchHolds(ShadeCoverLatch& latch, std::uint64_t shade, bool cursed, bool onHealer,
        bool onSelf, std::uint32_t nowMs)
    {
        if (!cursed || shade == 0)
        {
            latch = ShadeCoverLatch();
            return false;
        }
        if (onHealer)
        {
            latch.shade = shade;
            latch.sinceMs = nowMs;
            return true;
        }
        if (latch.shade != shade || nowMs - latch.sinceMs >= kShadeCoverLatchMs)
        {
            latch = ShadeCoverLatch();
            return false;
        }
        return onSelf;
    }

    // The latched Shade as the bot reads it before any general pick (Codex round 2): once the Shade is on the off-tank it
    // loses the healer tier, so a nearer Shade would win the general pick, the latch would see another guid and end, and
    // the off-tank would go back to the boss. The latched Shade therefore keeps the pick while every fact below holds.
    struct ShadeCoverLatchedShade
    {
        bool found = false;        // the unit resolves and is in the world
        bool carrierEntry = false; // an open carrier row's entry (the bot still carries the curse for it)
        bool alive = false;
        bool attackable = false;   // the pick's own attack check (visible, valid target)
        float distanceYd = 0.0f;
        float dropYd = 0.0f;       // bot z - Shade z
        bool onSelf = false;       // the Shade's victim is the bot
    };

    inline bool ShadeCoverKeepsLatched(const ShadeCoverLatch& latch, bool cursed, const ShadeCoverLatchedShade& s,
        std::uint32_t nowMs)
    {
        return latch.shade != 0 && cursed && s.found && s.carrierEntry && s.alive && s.attackable && s.onSelf &&
            s.distanceYd <= kShadeCoverReachYd && ShadeCoverSameFloor(s.dropYd) &&
            nowMs - latch.sinceMs < kShadeCoverLatchMs;
    }

    // The strike's pick: the latched Shade while it keeps (ShadeCoverKeepsLatched), else the general carrier pick.
    inline std::uint64_t ShadeCoverResolvePick(const ShadeCoverLatch& latch, bool cursed,
        const ShadeCoverLatchedShade& latched, std::uint64_t generalPick, std::uint32_t nowMs)
    {
        return ShadeCoverKeepsLatched(latch, cursed, latched, nowMs) ? latch.shade : generalPick;
    }

    // The off-tank half of the cover only while leaving cannot change the threat successor (Opus round 2, R2-1): the
    // off-tank's own threat on the threat_successor boss is above 0 and at least `share` of the highest tank's, the line
    // AiPlayerbot.DungeonRunThreatSuccessorKeepsAway keeps a cursed tank on the successor list by
    // (kThreatCapOffTankAwayShare, ThreatCapPolicy.h). Below it (a Hex wiped the off-tank) the off-tank stays on the
    // boss as the successor reference. The caller reads it only on a threat_successor fight, and refuses the cover when
    // the successor boss is not in its fight list.
    inline bool ShadeCoverThreatAllows(float ownThreat, float highestTankThreat, float share)
    {
        return ownThreat > 0.0f && ownThreat >= highestTankThreat * share;
    }

    // Index of the nearest pickable candidate of an open carrier row (CarrierKillFirstRow) within `reachYd`, or -1.
    // With `preferHealerVictim` (the healer cover key) a candidate in the cover tier (ShadeCoverTier) wins over every
    // other one, the nearer of them first; without it the pick is the nearest, exactly as before.
    inline int PickCarrierOverride(const ActiveBossRows& active, const std::vector<TacticCandidate>& candidates,
                                   float reachYd, bool preferHealerVictim = false)
    {
        int best = -1;
        for (std::size_t i = 0; i < candidates.size(); ++i)
        {
            const TacticCandidate& c = candidates[i];
            if (!c.pickable || c.distance > reachYd)
                continue;
            bool carried = false;
            for (const BossTacticRow* row : active)
                if (CarrierKillFirstRow(*row) && TacticSubjectEntry(*row) == c.entry)
                    carried = true;
            if (!carried)
                continue;
            if (best < 0)
            {
                best = static_cast<int>(i);
                continue;
            }
            bool const tier = ShadeCoverTier(c, preferHealerVictim);
            bool const bestTier = ShadeCoverTier(candidates[best], preferHealerVictim);
            if (tier > bestTier || (tier == bestTier && c.distance < candidates[best].distance))
                best = static_cast<int>(i);
        }
        return best;
    }

    // --- Shade cover (walk 56 F3) -----------------------------------------------------------------------------------
    // A Shade of Jin'do copies the boss's threat list, where heal threat is top, so it walks to a healer: 50 of 59
    // Shade-killed healers in walk 56 were not cursed and cannot hurt it. Only a Delusions carrier can. The cover row
    // (BossTacticsShadeCover) names each carrier that is asked to cover such a healer and is not on the Shade, and why.

    // Who the row asks about: a cursed damage dealer, or a cursed off-tank that does not hold the boss. A healer keeps its
    // own carrier-strike guards, the main tank never leaves the boss, and an uncursed bot cannot hit a Shade at all.
    inline bool ShadeCoverAsks(CarrierStrikeRole role, bool cursed, bool holdsBoss)
    {
        if (!cursed)
            return false;
        if (role == CarrierStrikeRole::Dps)
            return true;
        return role == CarrierStrikeRole::OffTank && !holdsBoss;
    }

    enum class ShadeCoverMiss : std::uint8_t
    {
        None,        // the carrier is on this Shade
        Dead,
        Charmed,     // a charmer owns its AI (the Brain Wash Totem)
        OtherShade,  // already on another Shade
        OutOfRange,  // further than kShadeCoverReachYd from this Shade
        ThreatGate,  // an off-tank on a threat_successor fight: the carrier strike keeps it on the boss
        Guard,       // an off-tank the carrier strike's guards hold back (the main tank under 60 %, its own low health)
        OnBoss,      // on a boss of the fight
        OtherTarget, // on anything else (a ward, a totem, a troll)
        NoTarget,
    };

    struct ShadeCoverFacts
    {
        bool alive = true;
        bool charmed = false;
        float distanceYd = 0.0f; // carrier to the Shade
        float dropYd = 0.0f;     // carrier z - Shade z
        bool threatGate = false; // the off-tank's successor rule holds it (ShadeCoverThreatGate)
        bool guard = false;      // an off-tank a carrier-strike guard holds back (ShadeCoverOffTankGuard)
        bool onThisShade = false;
        bool onOtherShade = false;
        bool onBoss = false;
        bool hasTarget = false;
    };

    // The first word that fits: a dead or charmed carrier cannot act at all; one already busy on another Shade is not
    // idle, whatever its range; then range, the successor rule, and what it is hitting instead.
    inline ShadeCoverMiss ShadeCoverMissReason(const ShadeCoverFacts& f, float reachYd)
    {
        if (!f.alive)
            return ShadeCoverMiss::Dead;
        if (f.onThisShade)
            return ShadeCoverMiss::None;
        if (f.charmed)
            return ShadeCoverMiss::Charmed;
        if (f.onOtherShade)
            return ShadeCoverMiss::OtherShade;
        if (f.distanceYd > reachYd || !ShadeCoverSameFloor(f.dropYd))
            return ShadeCoverMiss::OutOfRange;
        if (f.threatGate)
            return ShadeCoverMiss::ThreatGate;
        if (f.guard)
            return ShadeCoverMiss::Guard;
        if (f.onBoss)
            return ShadeCoverMiss::OnBoss;
        if (f.hasTarget)
            return ShadeCoverMiss::OtherTarget;
        return ShadeCoverMiss::NoTarget;
    }

    inline const char* ShadeCoverMissWord(ShadeCoverMiss miss)
    {
        switch (miss)
        {
            case ShadeCoverMiss::None:        return "none";
            case ShadeCoverMiss::Dead:        return "dead";
            case ShadeCoverMiss::Charmed:     return "charmed";
            case ShadeCoverMiss::OtherShade:  return "other_shade";
            case ShadeCoverMiss::OutOfRange:  return "out_of_range";
            case ShadeCoverMiss::ThreatGate:  return "threat_gate";
            case ShadeCoverMiss::Guard:       return "guard";
            case ShadeCoverMiss::OnBoss:      return "on_boss";
            case ShadeCoverMiss::OtherTarget: return "other_target";
            case ShadeCoverMiss::NoTarget:    return "no_target";
        }
        return "none";
    }

    // The off-tank's successor rule as the cover row reads it: on a threat_successor fight the carrier strike keeps a
    // free off-tank on the boss (CarrierStrikeAllowed's ThreatSuccessor verdict), unless the carrier strike is on and the
    // healer cover key lets it take a Shade on a healer.
    inline bool ShadeCoverThreatGate(CarrierStrikeRole role, bool threatSuccessor, bool coverOn = false)
    {
        return role == CarrierStrikeRole::OffTank && threatSuccessor && !coverOn;
    }

    // The guards the row can read for another member: with the cover on, the carrier strike still holds an off-tank back
    // while the main tank is under kCarrierStrikeTankPct or its own health is under the low-health line.
    inline bool ShadeCoverOffTankGuard(CarrierStrikeRole role, bool coverOn, std::uint8_t mainTankPct,
        std::uint8_t ownPct, std::uint8_t lowHealthPct)
    {
        return role == CarrierStrikeRole::OffTank && coverOn &&
            (mainTankPct < kCarrierStrikeTankPct || ownPct < lowHealthPct);
    }

    // One record per (carrier, Shade). A miss is logged only once it has lasted kShadeCoverGraceMs (a carrier turning
    // onto the Shade this second is no miss), then at most once per kShadeCoverLogGapMs. A record not noted for
    // kShadeSeenGraceMs starts a new miss.
    constexpr std::uint32_t kShadeCoverGraceMs = 2000;
    constexpr std::uint32_t kShadeCoverLogGapMs = 5000;
    constexpr std::size_t kShadeCoverMaxEntries = 512;

    struct ShadeCoverLogRecord
    {
        std::uint32_t firstMissMs = 0;
        std::uint32_t lastSeenMs = 0;
        std::uint32_t lastLogMs = 0;
        bool open = false;   // a miss is running
        bool logged = false; // lastLogMs is set
        bool deadLogged = false; // the member's `dead` row is written: one per (member, Shade)
    };

    // Note a miss at `nowMs`; true when the row is due. The unsigned subtractions survive the 32-bit clock wrap.
    inline bool ShadeCoverLogDue(ShadeCoverLogRecord& record, std::uint32_t nowMs)
    {
        if (!record.open || ShadeSeenExpired(record.lastSeenMs, nowMs))
        {
            record.open = true;
            record.firstMissMs = nowMs;
        }
        record.lastSeenMs = nowMs;
        if (nowMs - record.firstMissMs < kShadeCoverGraceMs)
            return false;
        if (record.logged && nowMs - record.lastLogMs < kShadeCoverLogGapMs)
            return false;
        record.logged = true;
        record.lastLogMs = nowMs;
        return true;
    }

    // Every (member, Shade) record of the cover row, bounded at kShadeCoverMaxEntries. Not thread-safe: the caller locks.
    class ShadeCoverLogBook
    {
    public:
        using Key = std::pair<std::uint64_t, std::uint64_t>;

        // Note the pair at `nowMs`: `miss` (the member is not on the Shade) and `dead` (the member is dead). A member on
        // the Shade drops its record. True when a row is due; a dead member's row is due once per pair.
        bool Note(std::uint64_t member, std::uint64_t shade, bool miss, bool dead, std::uint32_t nowMs)
        {
            Key const key(member, shade);
            if (!miss)
            {
                records_.erase(key);
                return false;
            }
            if (records_.find(key) == records_.end() && records_.size() >= kShadeCoverMaxEntries)
                Prune(nowMs);
            ShadeCoverLogRecord& record = records_[key];
            if (dead && record.deadLogged)
            {
                record.lastSeenMs = nowMs;
                return false;
            }
            if (!ShadeCoverLogDue(record, nowMs))
                return false;
            if (dead)
                record.deadLogged = true;
            return true;
        }

        // A miss is running for the pair: a member that died while missing the Shade is still asked (its `dead` row).
        bool MissOpen(std::uint64_t member, std::uint64_t shade, std::uint32_t nowMs) const
        {
            auto const it = records_.find(Key(member, shade));
            return it != records_.end() && it->second.open && !ShadeSeenExpired(it->second.lastSeenMs, nowMs);
        }

        std::size_t Size() const { return records_.size(); }
        bool Has(std::uint64_t member, std::uint64_t shade) const { return records_.count(Key(member, shade)) != 0; }

    private:
        // At the cap: drop every record not noted for kShadeSeenGraceMs; when that frees nothing, drop the oldest
        // quarter by last note, so the live misses keep their grace and gap.
        void Prune(std::uint32_t nowMs)
        {
            for (auto it = records_.begin(); it != records_.end();)
            {
                if (ShadeSeenExpired(it->second.lastSeenMs, nowMs))
                    it = records_.erase(it);
                else
                    ++it;
            }
            if (records_.size() < kShadeCoverMaxEntries)
                return;
            std::vector<std::pair<std::uint32_t, Key>> byAge;
            byAge.reserve(records_.size());
            for (auto const& entry : records_)
                byAge.push_back(std::make_pair(nowMs - entry.second.lastSeenMs, entry.first));
            std::size_t const drop = std::max<std::size_t>(1, records_.size() / 4);
            std::nth_element(byAge.begin(), byAge.begin() + (drop - 1), byAge.end(),
                [](auto const& a, auto const& b) { return a.first > b.first; });
            for (std::size_t i = 0; i < drop; ++i)
                records_.erase(byAge[i].second);
        }

        std::map<Key, ShadeCoverLogRecord> records_;
    };

    // --- carrier scan (walk 53) ------------------------------------------------------------------------------------
    // A Delusions carrier the attackers list never showed a Shade: the Shade is summoned on a carrier and only its
    // carrier can see it, so both the carrier override and the kill_first grid pass found nothing while one stood
    // beside the bot (2,127 of 3,276 Shade-frames, 65 %, had no cursed bot on the Shade). The scan reads the carrier
    // row's entry off the grid itself; this says which check kept every hit out of the pick.
    enum class CarrierScanRefusal : std::uint8_t
    {
        None,
        NoUnit,
        Ineligible,
        BeyondReach,
        NoLos,
        InvalidTarget,
    };

    struct CarrierScanFacts
    {
        bool unitFound = false;    // a living unit of the carrier row's entry from the grid read
        bool gridEligible = false; // KillFirstGridEligible(alive, evading, victimOurs, weHoldIt, ownerInFight)
        float distanceYd = -1.0f;  // bot to unit
        bool los = false;          // bot->IsWithinLOSInMap(unit)
        bool validAttack = false;  // bot->IsValidAttackTarget(unit)
    };

    // The first check that fails gives the verdict; its order is the order a carrier would walk them in: is there a
    // unit of the entry at all, is it this fight's, is it in reach, can the bot see it, may it attack it.
    inline CarrierScanRefusal CarrierScanRefuses(const CarrierScanFacts& f, float reachYd)
    {
        if (!f.unitFound)
            return CarrierScanRefusal::NoUnit;
        if (!f.gridEligible)
            return CarrierScanRefusal::Ineligible;
        if (f.distanceYd < 0.0f || f.distanceYd > reachYd)
            return CarrierScanRefusal::BeyondReach;
        if (!f.los)
            return CarrierScanRefusal::NoLos;
        if (!f.validAttack)
            return CarrierScanRefusal::InvalidTarget;
        return CarrierScanRefusal::None;
    }

    // Index of the nearest hit that passes every check, else -1.
    inline int CarrierScanPick(const std::vector<CarrierScanFacts>& hits, float reachYd)
    {
        int best = -1;
        for (std::size_t i = 0; i < hits.size(); ++i)
        {
            if (CarrierScanRefuses(hits[i], reachYd) != CarrierScanRefusal::None)
                continue;
            if (best < 0 || hits[i].distanceYd < hits[best].distanceYd)
                best = static_cast<int>(i);
        }
        return best;
    }

    inline const char* CarrierScanReasonWord(CarrierScanRefusal refusal)
    {
        switch (refusal)
        {
            case CarrierScanRefusal::None:          return "none";
            case CarrierScanRefusal::NoUnit:        return "no_unit";
            case CarrierScanRefusal::Ineligible:    return "ineligible";
            case CarrierScanRefusal::BeyondReach:   return "beyond_reach";
            case CarrierScanRefusal::NoLos:         return "no_los";
            case CarrierScanRefusal::InvalidTarget: return "invalid_target";
        }
        return "none";
    }

    // What the no-switch row says about the scan: how many grid hits the carrier entry had, which check refused each,
    // whether a player's order closed the pick, and whether any candidate of the entry was pickable at all.
    struct CarrierScanTally
    {
        int hits = 0;
        int byCode[6] = {};
        bool playerOrder = false;
        bool freeMode = false; // the pick mode was Free (no order at all) rather than Carrier (the run's own order)
        bool pickableSeen = false;
    };

    // Whether the scan's Shade replaces the ordinary kill_first pick (Codex, round 3). A free DPS takes every open row
    // (PickKillFirst), so a Powerful Healing Ward or a Brain Wash Totem in view wins the ordinary pick while a Shade
    // lives, and a carrier is the only one who can kill that Shade (the uncursed raid leaves the ward and totem to
    // nobody else). With the key on, a carrier (an open carrier row: the fight scan drops it for a bot without the
    // aura) in any mode but None (a player's order is never overridden) takes the scan's Shade over the ordinary pick;
    // no Shade found keeps the ordinary pick. An ordinary pick that already is a carrier-row entry (the carrier
    // override's own Shade) stays: it is a Shade, and its ranking (a healer's Shade first) is the override's.
    inline bool CarrierScanTakesPrecedence(bool isCarrier, bool keyOn, KillFirstPickMode mode, bool scanFound,
                                           bool ordinaryIsCarrierEntry)
    {
        return keyOn && isCarrier && mode != KillFirstPickMode::None && scanFound && !ordinaryIsCarrierEntry;
    }

    // The row's word from the scan: a player's order first (it is never overridden), then the most common refusal among
    // the grid hits (the enum order breaks a tie; the None code counts hits that passed every check and is never the
    // word), then why there was no hit at all: no hit and no unit in the fight is no_unit, no hit and an unpickable
    // unit is not_pickable. A hit the scan could take that the pick did not is the pick's own rule saying no
    // (not_pickable). A pickable unit in the fight with no grid hit can only be out of the reach.
    inline const char* CarrierScanWord(const CarrierScanTally& tally, bool shadeInFight, bool pickable)
    {
        if (tally.playerOrder)
            return "player_order";
        int best = -1;
        for (int code = static_cast<int>(CarrierScanRefusal::NoUnit);
             code <= static_cast<int>(CarrierScanRefusal::InvalidTarget); ++code)
            if (tally.byCode[code] > 0 && (best < 0 || tally.byCode[code] > tally.byCode[best]))
                best = code;
        if (best >= 0)
            return CarrierScanReasonWord(static_cast<CarrierScanRefusal>(best));
        if (tally.hits > 0)
            return "not_pickable";
        if (!shadeInFight)
            return "no_unit";
        if (!pickable)
            return "not_pickable";
        return CarrierScanReasonWord(CarrierScanRefusal::BeyondReach);
    }

    // --- taunt gate ------------------------------------------------------------------------------------------------
    // A tank never taunts a subject it must not hold: a needs_aura entry (the Shade of Jin'do, which only a carrier can
    // damage and which copies the boss's threat list onto a healer) or a creature the core marks immune to taunts
    // (Jin'do). A needs_aura subject is refused for a bot that LACKS the aura, and also for a carrier that is the
    // BOSS's current victim (`holdsBoss`, the main tank holding Jin'do: a Shade taunt would move the boss's threat
    // pull off it, walk 54: 19 of 114 Shade taunts, Jin'do on a damage dealer 5 s later in 3 of them). A cursed
    // off-tank that does not hold the boss may still taunt the Shade (the same rule as AttackerOfNonTankSkipsUnit,
    // which keeps the Shade in the scan for a carrier). Key off, every taunt passes as before.
    enum class TauntGate : std::uint8_t { Allow, NeedsAura, TauntImmune, HoldingBoss, PostSubject };

    inline TauntGate TauntGateFor(bool keyOn, std::uint32_t requiredAura, bool botHasAura, bool tauntImmune,
                                  bool holdsBoss)
    {
        if (!keyOn)
            return TauntGate::Allow;
        if (requiredAura != 0 && !botHasAura)
            return TauntGate::NeedsAura;
        if (requiredAura != 0 && holdsBoss)
            return TauntGate::HoldingBoss;
        if (tauntImmune)
            return TauntGate::TauntImmune;
        return TauntGate::Allow;
    }

    inline const char* TauntGateWord(TauntGate gate)
    {
        switch (gate)
        {
            case TauntGate::Allow:       return "allow";
            case TauntGate::NeedsAura:   return "needs_aura";
            case TauntGate::TauntImmune: return "taunt_immune";
            case TauntGate::HoldingBoss: return "holding_boss";
            case TauntGate::PostSubject: return "post_subject";
        }
        return "allow";
    }

    // AiPlayerbot.DungeonRunTankPostOwnsSubject: while another tank's post is live (its record read within
    // kTankPostLiveMs) on this subject entry, in this instance, by a tank that can act, this tank does not taunt the
    // subject: the post's own slot takes it (walk 2c a-1: slots 3, 0 and 2 taunted Cindarion before slot 1 arrived).
    constexpr std::uint32_t kTankPostLiveMs = 3000;
    inline bool TankPostSubjectReserved(bool keyOn, bool otherPostLive, bool otherTankCanAct, bool botOwnsPost)
    {
        return keyOn && otherPostLive && otherTankCanAct && !botOwnsPost;
    }

    // AiPlayerbot.DungeonRunTankPostOwnsUnit: a live post owns the one unit it latched at its arrival, so another
    // tank's record reserves no unit of the row's entry but that one. A record that latched nothing yet (`latchedGuid`
    // 0) covers every unit of the entry as before, and with the key off every record does.
    inline bool TankPostRecordCovers(bool keyOn, std::uint64_t latchedGuid, std::uint64_t targetGuid)
    {
        return !keyOn || latchedGuid == 0 || latchedGuid == targetGuid;
    }

    // The post's reservation also needs its tank able to take the unit back: the tank within kTankPostReserveRangeYd
    // of the subject (3D), and the subject still on it or loose (not its victim) for less than
    // kTankPostReserveGraceMs. A tank stranded across the room, or one that lost the unit past the grace, reserves
    // nothing, so the other tanks may taunt it.
    //
    // Walk 2c: the awaited unit landed 48-55 yd away from its post tank and the tank reached melee at about +13 s, so
    // the reservation keeps the unit at 60 yd and through a 15 s loose spell. Both limits are the post's own; the
    // guard's assignment clock keeps its kGuardAssignedGraceMs.
    constexpr float kTankPostReserveRangeYd = 60.0f;
    constexpr std::uint32_t kTankPostReserveGraceMs = 15000;
    static_assert(kTankPostReserveGraceMs == kGuardPostGraceMs, "the guard's tank_post grace is the post's own");
    inline bool TankPostReserves(bool keyOn, bool otherPostLive, bool otherTankCanAct, bool botOwnsPost,
                                 bool ownerWithinRange, bool subjectOnOwner, std::uint32_t looseMs)
    {
        return TankPostSubjectReserved(keyOn, otherPostLive, otherTankCanAct, botOwnsPost) && ownerWithinRange &&
            (subjectOnOwner || looseMs < kTankPostReserveGraceMs);
    }

    // Which limit lapsed a post's reservation this read: the range first, then the loose grace (the order the
    // reservation reads them). `none` while the reservation holds, and with the key off (nothing is written then).
    enum class TankPostLapse : std::uint8_t
    {
        None = 0,
        Range = 1,
        Grace = 2,
    };
    inline TankPostLapse TankPostLapseOf(bool keyOn, bool ownerWithinRange, bool subjectOnOwner,
                                         std::uint32_t looseMs)
    {
        if (!keyOn)
            return TankPostLapse::None;
        if (!ownerWithinRange)
            return TankPostLapse::Range;
        if (!subjectOnOwner && looseMs >= kTankPostReserveGraceMs)
            return TankPostLapse::Grace;
        return TankPostLapse::None;
    }

    // One lapse row per lapse event: `lapseLogged` is the record's flag. A read that still sees the lapse writes
    // nothing; a read where the reservation holds again re-arms it for the next lapse.
    inline bool TankPostLapseDue(TankPostLapse lapse, bool& lapseLogged)
    {
        if (lapse == TankPostLapse::None)
        {
            lapseLogged = false;
            return false;
        }
        if (lapseLogged)
            return false;
        lapseLogged = true;
        return true;
    }

    // The post owner's own record update re-arms the same flag: its tank back within kTankPostReserveRangeYd of the
    // subject with the subject on it, or loose off it for less than the grace, is the reservation holding again, so a
    // later lapse writes its own row even when no other tank reads the reservation in between. While a lapse is still
    // active the flag stays, and a reader's later read of that lapse stays quiet.
    inline bool TankPostLapseOwnerUpdate(bool keyOn, bool ownerWithinRange, bool subjectOnOwner,
                                         std::uint32_t looseMs, bool& lapseLogged)
    {
        lapseLogged = lapseLogged && TankPostLapseOf(keyOn, ownerWithinRange, subjectOnOwner, looseMs) !=
            TankPostLapse::None;
        return lapseLogged;
    }

    // The loose clock of a post record (AiPlayerbot.DungeonRunTankPostOwnsSubject). It is 0 while the subject is on its
    // post tank, and while the subject cannot be hit before the clock has started (unselectable or not attackable: a
    // boss still in the air, out of reach of every tank), so the grace is not spent before it lands. It starts on the
    // first hittable tick with the subject off the tank. A running clock keeps its start through unhittable spells
    // shorter than the grace (a shrug, a blink, a brief immunity). A spell of the grace length or more is a real
    // lift-off: the clock clears, so the next hittable tick starts a new grace. `prior` is the record's clock (0: not
    // started); `unhittableSince` is the start of the current unhittable spell (0: none).
    struct TankPostLooseClockState
    {
        std::uint32_t looseSinceMs = 0;
        std::uint32_t unhittableSinceMs = 0;
    };
    inline TankPostLooseClockState TankPostLooseClock(std::uint32_t prior, std::uint32_t unhittableSince,
                                                      bool subjectOnOwner, bool subjectSelectable,
                                                      bool subjectAttackable, std::uint32_t nowMs)
    {
        TankPostLooseClockState out;
        if (subjectOnOwner)
            return out;
        if (subjectSelectable && subjectAttackable)
        {
            out.looseSinceMs = prior != 0 ? prior : nowMs;
            return out;
        }
        out.unhittableSinceMs = unhittableSince != 0 ? unhittableSince : nowMs;
        out.looseSinceMs = nowMs - out.unhittableSinceMs >= kTankPostReserveGraceMs ? 0 : prior;
        return out;
    }

    // The area taunts (challenging shout, challenging roar) taunt every hostile within their radius of the caster, not
    // only the selected target: one unit of the scan that another live post reserves refuses the cast.
    constexpr float kAreaTauntRadiusYd = 10.0f;
    struct AreaTauntUnit
    {
        float yd = 0.0f;
        bool reservedByOther = false;
    };
    inline bool AreaTauntHitsReservedSubject(const std::vector<AreaTauntUnit>& units, float radius)
    {
        for (const AreaTauntUnit& unit : units)
            if (unit.reservedByOther && unit.yd <= radius)
                return true;
        return false;
    }

    // The gate's refusal row is throttled per (bot, gate word), not per bot: the 495 taunt_immune rows at Jin'do (walk
    // 54) could hide a needs_aura row that fell in the same 5 s. `lastMs` is one bot's last-row time per word (indexed
    // by the TauntGate value; 0 = never). Returns whether a row for `gate` is due, and stamps `nowMs` when it is.
    constexpr std::size_t kTauntGateWords = 5;
    using TauntGateLogTimes = std::array<std::uint32_t, kTauntGateWords>;

    inline bool TauntGateLogDueFor(TauntGateLogTimes& lastMs, TauntGate gate, std::uint32_t nowMs, std::uint32_t gapMs)
    {
        std::uint32_t& last = lastMs[static_cast<std::size_t>(gate)];
        if (last != 0 && nowMs - last < gapMs)
            return false;
        last = nowMs;
        return true;
    }

    // Carrier hold (AiPlayerbot.DungeonRunCarrierHold): a Delusions carrier standing on its Shade keeps it. "not dps
    // target active" fires whenever the current target differs from "dps target" (the skull, Jin'do), and its action
    // "dps assist" (relevance 60) outranks kill-first (21), so the carrier was pulled off a live Shade for Jin'do
    // (76 times in walk 54, 65 in walk 53). With the key on the trigger stays quiet when the current target is a
    // needs_aura subject the bot carries the aura for. A non-carrier on the Shade (it cannot damage it) and every
    // other target keep today's rule; key off is today.
    inline bool CarrierHoldsCurrent(bool keyOn, bool currentIsNeedsAura, bool botHasRequiredAura)
    {
        return keyOn && currentIsNeedsAura && botHasRequiredAura;
    }

    // Whether "not dps target active" fires, for a live non-player current target. The hostile player target is
    // decided first and is never suppressed by the hold (a carrier on a Shade still switches to an enemy player);
    // the hold (`holdsCurrent`) only suppresses the dps-target comparison. No enemy and no dps target: no fire.
    inline bool NotDpsTargetFires(bool enemyKnown, bool targetIsEnemy, bool holdsCurrent, bool dpsKnown,
                                  bool targetIsDps)
    {
        if (enemyKnown)
            return !targetIsEnemy;
        if (holdsCurrent)
            return false;
        if (dpsKnown)
            return !targetIsDps;
        return false;
    }

    // The kill-first trigger's check interval in seconds: every second under TotemFocus (a totem lives 1-2 s) or
    // CarrierHold (the re-pick after a Shade dies took a median 2.0 s), every two seconds otherwise.
    inline std::uint32_t KillFirstCheckSeconds(bool totemFocus, bool carrierHold)
    {
        return (totemFocus || carrierHold) ? 1 : 2;
    }

    // Whether the kill-first trigger is due at `nowSec`, with both keys read by the caller at every check (so a
    // `.reload config` takes effect on a trigger that is already built). `lastSec` is the last due time (0 = never);
    // it is stamped whenever the check is due. Keys off this is the base Trigger::needCheck rule for interval 2:
    // due when never checked or two seconds have passed.
    inline bool KillFirstCheckDue(bool totemFocus, bool carrierHold, std::int64_t nowSec, std::int64_t& lastSec)
    {
        std::int64_t const gap = KillFirstCheckSeconds(totemFocus, carrierHold);
        if (gap < 2 || lastSec == 0 || nowSec - lastSec >= gap)
        {
            lastSec = nowSec;
            return true;
        }
        return false;
    }

    // tank_ignore (t1105h J2): a tank leaves a unit alone (no guard pick, no engage, no taunt, no tank target) while one
    // of the bosses whose row names its entry (`bosses`, BossTacticTable::TankIgnoreBosses) fights. The damage dealers
    // keep it: it is only the tanks' pick that is closed.
    inline bool TankIgnoresUnit(bool tank, const std::vector<std::uint32_t>& bosses,
                                const std::function<bool(std::uint32_t)>& bossEngaged)
    {
        if (!tank)
            return false;
        for (std::uint32_t const boss : bosses)
            if (bossEngaged(boss))
                return true;
        return false;
    }

    // Index of the nearest candidate a free DPS bot may attack: the same candidate rules the kill_first pick applies
    // (pickable, kBossTacticsEngageRange) minus the entries a hold_kill row holds, so a bot switching off a held entry
    // lands on the blocker or a loose add and never on another held entry. `seekHeldEntry`: a seek_aura source the row
    // holds right now (its entry is not a hold_kill one), skipped too, so the drop never re-picks another Son. -1 when
    // nothing is left.
    inline int PickNearestUnheld(const ActiveBossRows& rows, const std::vector<TacticCandidate>& candidates,
                                 const std::function<std::uint32_t(std::uint32_t)>& countNear,
                                 std::uint32_t seekHeldEntry = 0)
    {
        int best = -1;
        float bestDistance = 0.0f;
        for (std::size_t i = 0; i < candidates.size(); ++i)
        {
            const TacticCandidate& c = candidates[i];
            if (!c.pickable || c.distance > kBossTacticsEngageRange)
                continue;
            if (seekHeldEntry != 0 && c.entry == seekHeldEntry)
                continue;
            if (KillHeld(rows, c.entry, countNear))
                continue;
            if (best < 0 || c.distance < bestDistance)
            {
                best = static_cast<int>(i);
                bestDistance = c.distance;
            }
        }
        return best;
    }

    // Whether the bot stands in the boss's frontal half-plane within `radius`
    // yards (2D). Same front test as the generic rear flank (RearFlankPolicy.h).
    inline bool InsideBossFront(float bossX, float bossY, float bossFacing, float botX, float botY, float radius)
    {
        const float dx = botX - bossX;
        const float dy = botY - bossY;
        if (Dist2Sq(0.0f, 0.0f, dx, dy) > radius * radius)
            return false;
        return NeedsRearFlank(FlankHeadingDistance(bossFacing, std::atan2(dy, dx)), 0.0f);
    }

    // Flank destination for a bot in the boss's front: the nearer of the two
    // sides, 120 degrees off the facing (outside the front half-plane) at the
    // bot's own distance, never closer than `minDist`.
    inline void BossFlankDestination(float bossX, float bossY, float bossFacing, float botX, float botY,
                                     float minDist, float& outX, float& outY)
    {
        float dist = std::sqrt(Dist2Sq(bossX, bossY, botX, botY));
        if (dist < minDist)
            dist = minDist;
        float lx, ly, rx, ry;
        FlankOffset(bossX, bossY, bossFacing, kFlankMaxAngle, dist, lx, ly);
        FlankOffset(bossX, bossY, bossFacing, -kFlankMaxAngle, dist, rx, ry);
        if (Dist2Sq(lx, ly, botX, botY) <= Dist2Sq(rx, ry, botX, botY))
        {
            outX = lx;
            outY = ly;
        }
        else
        {
            outX = rx;
            outY = ry;
        }
    }

    // --- tank_post --------------------------------------------------------------------------------------------------
    // A named tank waits at a spot for a unit that is not in the fight yet (an unselectable second boss), then attacks
    // it on arrival. The row's band arms the post, so the whole read is a fight scan. Every number comes from the row.
    // With the row's `hold_spot` mode and AiPlayerbot.DungeonRunTankPostHoldSpot, the post also starts while the tank
    // holds another hostile, latches its arrival, drags the subject back to the spot, outlives its window once arrived
    // and yields to a boss stand seat. AiPlayerbot.DungeonRunTankPostLeash adds the pre-arrival walk back.
    constexpr float kTankPostSpotYd = 3.0f;    // the tank holds within this of the spot
    constexpr float kTankPostSearchYd = 200.0f; // the awaited unit's grid search radius
    constexpr float kTankPostDragYd = 8.0f;     // hold_spot: past this from the spot with the subject on the tank, walk back

    enum class TankPostDecision : std::uint8_t
    {
        None = 0,   // no post: the row is closed, or there is nobody to wait for yet
        Walk = 1,   // the tank walks to the spot
        Hold = 2,   // the tank is at the spot and waits
        Engage = 3, // the unit arrived: attack it
        End = 4,    // the post ends now: window, death, evade, band or tank
        Drag = 5,   // hold_spot: the arrived subject is on the tank, walk it back to the spot keeping the target
        Yield = 6,  // hold_spot: the tank holds a boss stand seat, the post waits (its record is kept)
        Leash = 7,  // hold_spot + leash: before the arrival the tank is past 8 yd from the spot: walk back keeping
                    // its target
    };

    struct TankPostIn
    {
        bool live = false;          // the row is open for this bot: the boss stands in its health band
        bool started = false;       // a post is already held
        bool tankCanAct = true;     // alive and not creature-charmed
        bool subjectFound = false;  // a unit of the awaited entry stands within kTankPostSearchYd
        bool subjectAlive = false;
        bool subjectSelectable = false; // no UNIT_FLAG_NOT_SELECTABLE
        bool subjectEvading = false;
        float subjectToSpotYd = 0.0f;
        float arrivalRadiusYd = 0.0f; // the row's param
        float botToSpotYd = 0.0f;
        bool holdingOtherHostile = false; // the tank holds another hostile that attacks a group member
        bool windowExpired = false;       // the row's window_s has passed since the post started
        bool holdSpot = false;            // the row's `hold_spot` mode and its key are on
        bool arrivedBefore = false;       // this post's latched arrival counts for the unit this read saw
        bool subjectOnTank = false;       // the subject's victim is this tank
        bool seated = false;              // the tank holds a boss stand seat (read only with holdSpot)
        bool leash = false;               // hold_spot and its leash key: walk back before the arrival
        bool slotMoved = false;           // the post's row is still in the fight, for another slot index (read by the end word only)
        std::uint64_t subjectGuid = 0;    // the awaited unit this read saw (0: none)
    };

    // The awaited unit has arrived when it stands within the row's radius of the spot, alive and selectable. Combat
    // is not asked: a unit that lands and becomes targetable before it enters combat is attacked by the posted tank.
    // An unselectable unit (a second boss still in the air) is not arrived, however close it circles.
    inline bool TankPostArrived(bool found, bool alive, bool selectable, float subjectToSpotYd,
                                float arrivalRadiusYd)
    {
        return found && alive && selectable && subjectToSpotYd <= arrivalRadiusYd;
    }

    // Whether the post's latched arrival counts for the unit this read saw: only the same unit stays arrived, so a
    // second unit of the same target_entry starts unarrived.
    inline bool TankPostLatchCounts(bool arrivedLogged, std::uint64_t arrivedSubjectGuid,
                                    std::uint64_t subjectGuid)
    {
        return arrivedLogged && arrivedSubjectGuid == subjectGuid;
    }

    // Whether this tick latches (and logs) the post's arrival. A plain post keeps its old rule and latches only when
    // it engages. With the row's `hold_spot` mode, the unit physically stands arrived, whatever the step chose: a
    // first arrival can come out as Drag (the tank already more than 8 yd from the spot with the unit on it) or Yield
    // (a stand seat is due); it must still latch, or the post would later treat it as never arrived and end on its
    // window.
    inline bool TankPostLatchesArrival(const TankPostIn& in, TankPostDecision step)
    {
        if (!in.holdSpot)
            return step == TankPostDecision::Engage;
        if (in.arrivedBefore || step == TankPostDecision::None || step == TankPostDecision::End)
            return false;
        return TankPostArrived(in.subjectFound, in.subjectAlive, in.subjectSelectable, in.subjectToSpotYd,
            in.arrivalRadiusYd);
    }

    // One creature of the awaited entry as the grid search saw it ("tank post units" value).
    struct TankPostUnit
    {
        std::uint64_t guid = 0;
        float x = 0.0f, y = 0.0f, z = 0.0f;
        bool alive = false;
        bool selectable = false;
        bool evading = false;
    };

    // Whether the boss stand seat the tank holds yields the post. A stay (S2: the bot already stood clear) is not a
    // seat the plan gave it, so the post keeps going and the leash keeps slot 1 at the spot while the tank is clear.
    inline bool TankPostSeated(bool standValid, bool stay) { return standValid && !stay; }

    // The unit the post waits for: the nearest living one to the spot (the nearest dead one when none lives, so its
    // death still ends the post). -1 for an empty list. With `ownsUnit` (AiPlayerbot.DungeonRunTankPostOwnsUnit) the
    // unit this post latched wins while it lives, and a unit another live post latched is no candidate, so two posts
    // on one entry (two Hatchers, say) each keep their own unit instead of trading them read by read.
    inline int PickTankPostUnit(const std::vector<TankPostUnit>& units, float spotX, float spotY, float spotZ,
                                bool ownsUnit, std::uint64_t latchedGuid,
                                const std::unordered_set<std::uint64_t>& otherLatched)
    {
        if (ownsUnit && latchedGuid != 0)
        {
            for (std::size_t i = 0; i < units.size(); ++i)
            {
                if (units[i].guid == latchedGuid && units[i].alive)
                    return static_cast<int>(i);
            }
        }
        int best = -1;
        float bestYd = 0.0f;
        bool bestAlive = false;
        for (std::size_t i = 0; i < units.size(); ++i)
        {
            const TankPostUnit& unit = units[i];
            if (ownsUnit && unit.guid != latchedGuid && otherLatched.count(unit.guid) != 0)
                continue;
            const float dx = unit.x - spotX;
            const float dy = unit.y - spotY;
            const float dz = unit.z - spotZ;
            const float yd = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (best < 0 || (unit.alive && !bestAlive) || (unit.alive == bestAlive && yd < bestYd))
            {
                best = static_cast<int>(i);
                bestYd = yd;
                bestAlive = unit.alive;
            }
        }
        return best;
    }

    // What the post's tank does this tick. A post that was held and can no longer be held ends; a row that is not
    // open (or has nothing to wait for) and holds no post does nothing. Another hostile on a group member keeps the
    // tank on it and the post waits, except in hold_spot once the subject arrives, where the subject outranks it.
    // In hold_spot the window closes only before the arrival, the arrival stays latched while the subject is alive and
    // selectable, and a stand seat yields the post (the record is kept).
    inline TankPostDecision TankPostStep(const TankPostIn& in)
    {
        if (!in.live || !in.tankCanAct)
            return in.started ? TankPostDecision::End : TankPostDecision::None;
        if (in.windowExpired && !(in.holdSpot && in.arrivedBefore))
            return in.started ? TankPostDecision::End : TankPostDecision::None;
        if (!in.subjectFound || !in.subjectAlive || in.subjectEvading)
            return in.started ? TankPostDecision::End : TankPostDecision::None;
        if (in.holdSpot && in.seated)
            return TankPostDecision::Yield;
        bool const arrived = TankPostArrived(in.subjectFound, in.subjectAlive, in.subjectSelectable,
                                 in.subjectToSpotYd, in.arrivalRadiusYd) ||
            (in.holdSpot && in.arrivedBefore && in.subjectSelectable);
        if (arrived)
        {
            if (!in.holdSpot)
                return in.holdingOtherHostile ? TankPostDecision::Hold : TankPostDecision::Engage;
            if (in.subjectOnTank && in.botToSpotYd > kTankPostDragYd)
                return TankPostDecision::Drag;
            return TankPostDecision::Engage;
        }
        if (in.holdSpot && in.leash && in.botToSpotYd > kTankPostDragYd)
            return TankPostDecision::Leash;
        if (in.holdingOtherHostile)
            return TankPostDecision::Hold;
        return in.botToSpotYd > kTankPostSpotYd ? TankPostDecision::Walk : TankPostDecision::Hold;
    }

    // The Engage walk's destination: a point at melee distance on the line from the tank to the subject. The action
    // walks there itself because MoveTo(Unit*) aims a hostile unit at the stance's formation spot (or refuses the move
    // when there is none), not at the unit. `reachYd` is the combined melee reach; the point stands 80 % of it from the
    // subject, so the next tick's reach test passes without hugging the body. False when the two stand on one spot.
    inline bool TankPostMeleePoint(float botX, float botY, float subjectX, float subjectY, float reachYd, float& x,
                                   float& y)
    {
        float const dx = botX - subjectX;
        float const dy = botY - subjectY;
        float const len = std::sqrt(dx * dx + dy * dy);
        if (len < 0.1f)
            return false;
        float const stop = std::min(len, std::max(0.5f, reachYd * 0.8f));
        x = subjectX + dx / len * stop;
        y = subjectY + dy / len * stop;
        return true;
    }

    // AiPlayerbot.DungeonRunTankPostWalks: the Engage step walks to the subject while it is out of melee reach and no
    // taunt went out (a taunt that cast ends the tick; a subject in reach is attacked where it stands). A boss stand
    // seat, a stay included, owns the tank: the step does not walk out of a breath lane. Walk 2f a-2: slot 1 stood on
    // the post at (8, -200) with Cindarion 40 yd away, the attack returned true every tick and the step never walked.
    inline bool TankPostEngageWalks(bool keyOn, bool tauntWentOut, bool inMeleeReach, bool standActive)
    {
        return keyOn && !tauntWentOut && !inMeleeReach && !standActive;
    }

    // The end word of a post (the step's own order). `slotMoved`: the post's row is still in the fight, but for another
    // slot index than this bot now holds (a tank died and GetRaidTankRoles renumbered): "slot", not "band".
    inline const char* TankPostEndWord(bool live, bool slotMoved, bool tankCanAct, bool windowExpired,
                                       bool subjectFound, bool subjectAlive, bool subjectEvading)
    {
        if (!live)
            return slotMoved ? "slot" : "band";
        if (!tankCanAct)
            return "tank";
        if (windowExpired)
            return "window";
        if (!subjectFound)
            return "gone";
        if (!subjectAlive)
            return "dead";
        if (subjectEvading)
            return "evade";
        return "band";
    }

    // The `slot` end word of a post whose row left this bot's slot: the row is still held (another row of the same
    // slot) or the renumbering moved it while the owns-subject key is on. With the key off a renumbered end says
    // `band`, the pre-feature word.
    inline bool TankPostEndsAsSlot(bool rowStillHeld, bool slotMoved, bool ownsSubjectKeyOn)
    {
        return rowStillHeld || (slotMoved && ownsSubjectKeyOn);
    }

    // A step row is written when the post enters Drag, Yield or Leash (not while it stays in it).
    inline bool TankPostStepEntered(std::uint8_t lastStep, TankPostDecision step)
    {
        return (step == TankPostDecision::Drag || step == TankPostDecision::Yield ||
                   step == TankPostDecision::Leash) &&
               lastStep != static_cast<std::uint8_t>(step);
    }

    // AiPlayerbot.DungeonRunTankPostTaunts: the Engage step takes the subject back whenever it is not on the post's tank,
    // off a healer or a damage dealer, and off another tank only with AiPlayerbot.DungeonRunTankPostOwnsSubject (without
    // the reservation that tank's own class taunt is not refused, so the subject flips between the two tanks; walk 2d: 3
    // of 3 reservations lapsed on grace while slot 1 hit him without a taunt; slot 1's first taunt +108..+121 s). The
    // gate still decides (needs_aura, taunt_immune); a warrior out of Defensive Stance takes it first; the area taunt
    // only when the single taunt cannot go out, the subject is within kAreaTauntRadiusYd and on a non-tank, and no other
    // post's subject is in its radius.
    enum class TankPostTaunt : std::uint8_t { None, Taunt, Stance, AreaTaunt, Gate, NotReady };
    struct TankPostTauntIn
    {
        bool keyOn = false; bool engage = false; bool subjectOnTank = false; bool subjectSelectable = false;
        bool gateRefuses = false; bool tauntCastable = false; bool needsStance = false; bool stanceCastable = false;
        bool areaCastable = false; bool areaInRange = false; bool areaGateRefuses = false; bool victimIsTank = false;
        bool ownsSubjectOn = false;
    };
    // Whether any taunt path is open at all: the key on, the subject off this tank and selectable, and off another tank
    // only with the reservation (its taunt passes, but the other tank's own class taunt would not be refused, so the
    // subject would flip between the two). Read before the gate inputs, which write rows and advance the post clocks.
    inline bool TankPostTauntAttemptable(const TankPostTauntIn& in)
    {
        if (!in.keyOn || !in.engage || in.subjectOnTank || !in.subjectSelectable) return false;
        return !in.victimIsTank || in.ownsSubjectOn;
    }
    // Whether the area taunt's own gate has to be read at all: the step picks the area taunt only with a known spell,
    // castable now, the subject in range and off a non-tank. In every other state the gate cannot change the verdict,
    // and reading it scans the radius and advances the other posts' clocks.
    inline bool TankPostAreaTauntGateNeeded(const TankPostTauntIn& in)
    {
        return in.areaCastable && in.areaInRange && !in.victimIsTank;
    }
    inline TankPostTaunt TankPostTauntStep(const TankPostTauntIn& in)
    {
        if (!TankPostTauntAttemptable(in)) return TankPostTaunt::None;
        if (in.gateRefuses) return TankPostTaunt::Gate;
        if (in.tauntCastable) return TankPostTaunt::Taunt;
        if (in.needsStance && in.stanceCastable) return TankPostTaunt::Stance;
        if (in.areaCastable && in.areaInRange && !in.areaGateRefuses && !in.victimIsTank) return TankPostTaunt::AreaTaunt;
        return TankPostTaunt::NotReady;
    }
    inline const char* TankPostTauntWord(TankPostTaunt t)  // none, taunt, stance, area, gate, not_ready
    {
        switch (t)
        {
            case TankPostTaunt::Taunt:
                return "taunt";
            case TankPostTaunt::Stance:
                return "stance";
            case TankPostTaunt::AreaTaunt:
                return "area";
            case TankPostTaunt::Gate:
                return "gate";
            case TankPostTaunt::NotReady:
                return "not_ready";
            case TankPostTaunt::None:
            default:
                return "none";
        }
    }
    // --- add_crew --------------------------------------------------------------------------------------------------
    // AiPlayerbot.DungeonRunAddCrew: once the row's switch unit (`target_entry`) is dead, the boss's add duty goes to
    // the melee, the tanks and the first `priority` AoE ranged bots of the raid; the other ranged damage dealers stay
    // on the boss. Before the switch, and with the key or the row off, today's duty stands (the Onyxia whelp hunt for
    // its own classes). The kind is generic: the caller supplies the roles and the raid order.
    enum class AddCrewState : std::uint8_t { Waiting = 0, Crew = 1, NotCrew = 2 };

    // AoE ranged classes in crew order (1.12 Classes): mage 0, warlock 1, hunter 2, shaman 3, druid 4; -1 none.
    inline int AddCrewClassOrder(std::uint8_t classId)
    {
        switch (classId)
        {
            case 8: return 0;
            case 9: return 1;
            case 3: return 2;
            case 7: return 3;
            case 11: return 4;
            default: return -1;
        }
    }

    struct AddCrewCandidate
    {
        std::uint64_t guid = 0;
        int order = -1;
    };

    // This bot's place among the raid's living AoE ranged damage dealers, sorted by (order, guid); -1 when not listed.
    inline int AddCrewRank(std::vector<AddCrewCandidate> raid, std::uint64_t me)
    {
        std::sort(raid.begin(), raid.end(), [](const AddCrewCandidate& a, const AddCrewCandidate& b)
        { return a.order != b.order ? a.order < b.order : a.guid < b.guid; });
        for (std::size_t i = 0; i < raid.size(); ++i)
            if (raid[i].guid == me)
                return static_cast<int>(i);
        return -1;
    }

    inline bool AddCrewMember(bool tank, bool healer, bool ranged, int aoeRank, std::uint8_t crewSize)
    {
        if (healer)
            return false;
        if (tank || !ranged)
            return true;
        return aoeRank >= 0 && aoeRank < crewSize;
    }

    inline AddCrewState AddCrewStateFor(bool keyOn, bool rowFound, bool switchAlive, bool member)
    {
        if (!keyOn || !rowFound || switchAlive)
            return AddCrewState::Waiting;
        return member ? AddCrewState::Crew : AddCrewState::NotCrew;
    }
}
