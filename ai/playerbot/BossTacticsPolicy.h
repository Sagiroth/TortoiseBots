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
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

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
        return BossTacticKind::None;
    }

    struct BossTacticRow
    {
        std::uint32_t map = 0;
        std::uint32_t bossEntry = 0;    // creature whose presence in the fight activates the row
        BossTacticKind kind = BossTacticKind::None;
        std::uint32_t targetEntry = 0;  // creature the row is about; 0 = the boss itself
        std::uint32_t spellId = 0;      // interrupt, avoid_cast: the spell
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
    };

    // Whether the row carries a gate at all. Only the scan-read kinds may: hold_kill, tank_hold, one_at_a_time and
    // linked_death are read straight from the table, so they could never answer it.
    inline bool RowGateSet(const BossTacticRow& row)
    {
        return row.hpMin != 0 || row.hpMax != 100 || row.botAura != 0;
    }

    // Whether a row is open for this bot right now. `keyOn` is AiPlayerbot.DungeonRunAuraTactics: with it off a
    // gated row is closed, so the table behaves exactly as before the gates existed. The health band is the row's
    // boss entry's health (`bossHpPct`, inclusive), never the add's; `botHasGateAura` is whether this bot carries
    // `botAura`. An ungated row is open whenever the band and the aura allow it.
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

    // The creature a row is about.
    inline std::uint32_t TacticSubjectEntry(const BossTacticRow& row)
    {
        return row.targetEntry ? row.targetEntry : row.bossEntry;
    }

    // One linked_death group: its anchor (the anchor's rows) and its group id (`priority`). One anchor can carry
    // several groups on one map.
    struct LinkedDeathGroupKey
    {
        std::uint32_t anchor = 0;
        std::uint8_t group = 0;
    };

    class BossTacticTable
    {
    public:
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
            // Only a row a fight scan answers can carry a gate. These four are read straight from the table (the dps
            // and tank pickers, the linked_death scan), so a gate on one of them would never be consulted.
            if (RowGateSet(row) && (row.kind == BossTacticKind::HoldKill || row.kind == BossTacticKind::TankHold ||
                    row.kind == BossTacticKind::OneAtATime || row.kind == BossTacticKind::LinkedDeath))
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
            std::vector<BossTacticRow>& rows = rows_[Key(row.map, row.bossEntry)];
            for (const BossTacticRow& have : rows)
            {
                if (have.kind == row.kind && have.targetEntry == row.targetEntry && have.spellId == row.spellId &&
                    have.hpMin == row.hpMin && have.hpMax == row.hpMax)
                    return false;
            }
            rows.push_back(row);
            maps_[row.map] = true;
            if (row.kind == BossTacticKind::AvoidCast)
                avoidCastMaps_[row.map] = true;
            if (row.kind == BossTacticKind::HoldKill || row.kind == BossTacticKind::OneAtATime)
                holdMaps_[row.map] = true;
            if (IsAuraTacticKind(row.kind))
                auraTacticMaps_[row.map] = true;
            if (row.kind == BossTacticKind::DispelPriority)
                dispelMaps_[row.map] = true;
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

        void Seal() { sealed_.store(true, std::memory_order_release); }
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

        // The rows of one boss entry, or null. Null before the seal.
        const std::vector<BossTacticRow>* Rows(std::uint32_t map, std::uint32_t bossEntry) const
        {
            if (!sealed_.load(std::memory_order_acquire))
                return nullptr;
            auto it = rows_.find(Key(map, bossEntry));
            return it == rows_.end() ? nullptr : &it->second;
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
        std::unordered_map<std::uint32_t, bool> linkedDeathMaps_;
        std::unordered_map<std::uint32_t, bool> auraTacticMaps_;
        std::unordered_map<std::uint32_t, bool> dispelMaps_;
        std::unordered_map<std::uint32_t, std::vector<LinkedDeathGroupKey>> linkedDeathGroups_;
        std::size_t size_ = 0;
        std::atomic<bool> sealed_{false};
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
    // like anyone else). A caster on nobody keeps nobody in, and a non-tank never holds.
    inline bool HoldsCaster(bool botIsTank, bool casterIsOnBot, bool casterIsOnNonTank, bool botTargetsCaster,
                            bool casterTankHoldRow)
    {
        return botIsTank && (casterIsOnBot || (casterTankHoldRow && casterIsOnNonTank && botTargetsCaster));
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

    // Whether a free DPS bot leaves `entry` alone right now: a hold_kill row holds it while its blocker lives
    // (KillHeld). The dps target, the dps AoE target, the group tank's victim and the raid mark all drop such a
    // candidate, so with nothing unheld left the bot has no target and waits for the blocker to die instead of
    // feeding the pile. The role gate is the hold-kill action's: a tank or a healer keeps its own pick (the caller
    // passes false for them), and an explicit attack order is taken long before this runs.
    inline bool DpsLeavesAlone(bool freeDpsBot, const ActiveBossRows& rows, std::uint32_t entry,
                               const std::function<std::uint32_t(std::uint32_t)>& countNear)
    {
        return freeDpsBot && KillHeld(rows, entry, countNear);
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
    //   Known: every member guid any observer has seen living, with that guid's last seen health and time.
    //   Armed (LinkedDeathArmed): every known guid not confirmed dead was last seen at or below `param` % within
    //   kLinkedDeathSeenMs. A known guid seen above `param` disarms it; a known guid not seen for longer blocks it (fail
    //   safe: hold).
    //   Latch: a death latches only when the previous read left the release armed AND the read that finds the death
    //   still has it armed. The death is explicit: a known guid the observing bot finds dead or as a corpse
    //   (LinkedDeathUnseen lists the known guids the read did not see living, for the caller to look up). A member merely
    //   not seen (out of range, not cached) is never a death. Each death counts once.
    //   Cleared once every known member is confirmed dead, or the group's window (the `window_s` column, 7 s unless the
    //   encounter's own retry is longer) after the death, so a fight that cannot finish the trio goes back to the
    //   equalise hold instead of burning one member at a time forever.
    //   Encounter end (no latch): the known set, armed and latch reset when no member fights and every non-dead known
    //   guid is accounted for: out of combat at full health this scan (an evade), or not seen for kLinkedDeathEndMs. So an
    //   evade or a wipe does not leave old guids to block the next pull's arming; a respawn under new guids starts clean.
    //   A known guid not seen for kLinkedDeathDropMs while the group is seen (a member despawned) is forgotten too. Within
    //   those windows an unseen member still blocks the arming (fail safe).
    constexpr std::uint32_t kLinkedDeathLatchMs = 15000;
    constexpr std::uint32_t kLinkedDeathSeenMs = 5000;
    constexpr std::uint32_t kLinkedDeathEndMs = 30000;
    constexpr std::uint32_t kLinkedDeathDropMs = 60000;

    // The latch's window in milliseconds from the group's rows: the constant only when a group carries none (the loader
    // refuses that, so it is the plain fallback).
    inline std::uint32_t LinkedDeathWindowMs(float windowSec)
    {
        return windowSec > 0.0f ? static_cast<std::uint32_t>(windowSec * 1000.0f + 0.5f) : kLinkedDeathLatchMs;
    }
    struct LinkedDeathKnown
    {
        std::uint64_t guid = 0;
        int hpPct = 100;          // the last read that saw it living
        std::uint32_t seenMs = 0; // when that read was
    };
    struct LinkedDeathLatch
    {
        bool latched = false;
        std::uint32_t sinceMs = 0;           // when the latch was set (the read that saw the death)
        std::uint64_t deadGuid = 0;          // the member whose death set it
        bool armed = false;                  // the release was armed at the end of the last read
        std::vector<LinkedDeathKnown> known; // every member guid seen living in this fight
        std::vector<std::uint64_t> dead;     // the known guids confirmed dead (each counted once)
        // The current death episode's first and last explicit death, for the one window diagnostic the group's
        // final death writes (first_ms is the episode start, last_ms the elapsed time).
        std::uint32_t firstDeathMs = 0;
        std::uint32_t lastDeathMs = 0;
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

    inline bool LinkedDeathHasGuid(const std::vector<std::uint64_t>& guids, std::uint64_t guid)
    {
        return std::find(guids.begin(), guids.end(), guid) != guids.end();
    }

    // Whether the release is armed: every known guid not yet confirmed dead was seen at or below `param` % within
    // kLinkedDeathSeenMs of `nowMs`. True with no living known member (the latch clears then anyway).
    inline bool LinkedDeathArmed(const LinkedDeathLatch& latch, float param, std::uint32_t nowMs)
    {
        for (const LinkedDeathKnown& known : latch.known)
        {
            if (LinkedDeathHasGuid(latch.dead, known.guid))
                continue;
            if (static_cast<float>(known.hpPct) > param || nowMs - known.seenMs > kLinkedDeathSeenMs)
                return false;
        }
        return true;
    }

    // The known members this read did not see living and that are not yet counted dead: the guids the caller looks up
    // with its own unit lookup. Not seeing one is not a death; only a unit found dead is (LinkedDeathLatchTick's
    // `deadNow`).
    inline std::vector<std::uint64_t> LinkedDeathUnseen(const LinkedDeathLatch& latch,
                                                        const std::vector<LinkedMember>& members)
    {
        std::vector<std::uint64_t> unseen;
        for (const LinkedDeathKnown& known : latch.known)
        {
            if (LinkedDeathHasGuid(latch.dead, known.guid))
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

    // Whether the group's encounter has ended without a latch: no member of it fights (`members` empty), and every
    // non-dead known guid is accounted for: either it is in this scan's `idle` set (seen, out of combat, full health:
    // an evade, the bosses reset), or it was not seen for kLinkedDeathEndMs (the 30 s no-sighting timeout). One
    // idle-full-health guid does not end the encounter while another known guid is unresolved and recent: that guid
    // may still be fighting. An idle unit below full health, or still in combat, is a fight.
    inline bool LinkedDeathEncounterEnded(const LinkedDeathLatch& latch, const std::vector<LinkedMember>& members,
                                          const std::vector<LinkedMember>& idle, std::uint32_t nowMs)
    {
        if (!members.empty())
            return false;
        for (const LinkedMember& member : idle)
            if (member.inCombat || member.hpPct < 100)
                return false;
        for (const LinkedDeathKnown& known : latch.known)
        {
            if (LinkedDeathHasGuid(latch.dead, known.guid))
                continue;
            const bool idleNow = std::any_of(idle.begin(), idle.end(),
                                             [&known](const LinkedMember& member) { return member.guid == known.guid; });
            if (!idleNow && nowMs - known.seenMs < kLinkedDeathEndMs)
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
            if (!LinkedDeathHasGuid(latch.dead, it->guid) && nowMs - it->seenMs >= kLinkedDeathDropMs)
                it = latch.known.erase(it);
            else
                ++it;
        }
    }

    // One read of the group updates the latch and answers whether it is on. `param`: the hold percent; `members`: the
    // living members this read saw, in combat; `deadNow`: the LinkedDeathUnseen guids the caller found dead; `nowMs`:
    // the world clock; `idle`: the known guids this read did not see fighting but found alive (read only when `members`
    // is empty; `inCombat` marks one still fighting; a known guid's sighting, never a new registration); `windowMs`: how
    // long the latch lasts after the death (LinkedDeathWindowMs of the group's window_s). The sightings are applied first
    // (a member above `param` disarms, a low one re-arms once every other known member is fresh), then the encounter end
    // (see LinkedDeathEncounterEnded) and the guarded latches, then the deaths; a death latches only when armed both
    // before this call and after it.
    inline bool LinkedDeathLatchTick(LinkedDeathLatch& latch, float param, const std::vector<LinkedMember>& members,
                                     const std::vector<std::uint64_t>& deadNow, std::uint32_t nowMs,
                                     const std::vector<LinkedMember>& idle = std::vector<LinkedMember>(),
                                     std::uint32_t windowMs = kLinkedDeathLatchMs,
                                     LinkedDeathWindowEvent* windowEvent = nullptr)
    {
        if (windowEvent)
            *windowEvent = LinkedDeathWindowEvent();
        for (const LinkedMember& member : members)
        {
            auto known = std::find_if(latch.known.begin(), latch.known.end(),
                                      [&member](const LinkedDeathKnown& k) { return k.guid == member.guid; });
            if (known == latch.known.end())
            {
                LinkedDeathKnown added;
                added.guid = member.guid;
                added.hpPct = member.hpPct;
                added.seenMs = nowMs;
                latch.known.push_back(added);
            }
            else
            {
                known->hpPct = member.hpPct;
                known->seenMs = nowMs;
            }
            // A living member is no death: a raise undoes an earlier one, so its next death counts again.
            latch.dead.erase(std::remove(latch.dead.begin(), latch.dead.end(), member.guid), latch.dead.end());
            if (latch.dead.empty())
                latch.firstDeathSeen = false;
        }
        // An idle unit is a sighting of a known guid only (its health and time): an idle unit above param disarms it.
        for (const LinkedMember& member : idle)
        {
            auto known = std::find_if(latch.known.begin(), latch.known.end(),
                                      [&member](const LinkedDeathKnown& k) { return k.guid == member.guid; });
            if (known == latch.known.end())
                continue;
            known->hpPct = member.hpPct;
            known->seenMs = nowMs;
        }
        if (latch.latched && nowMs - latch.sinceMs >= windowMs)
            latch.latched = false;
        if (!latch.latched)
        {
            // An ended encounter forgets the whole fight (known, dead, armed): the next pull arms on its own guids.
            if (LinkedDeathEncounterEnded(latch, members, idle, nowMs))
            {
                latch = LinkedDeathLatch();
                return false;
            }
            if (!members.empty() || !idle.empty())
                LinkedDeathDropStale(latch, nowMs);
        }
        const bool armedBefore = latch.armed;
        std::vector<std::uint64_t> newDeaths;
        for (std::uint64_t guid : deadNow)
        {
            bool known = false;
            for (const LinkedDeathKnown& k : latch.known)
                if (k.guid == guid)
                    known = true;
            if (!known || LinkedDeathHasGuid(latch.dead, guid))
                continue;
            if (!latch.firstDeathSeen)
            {
                latch.firstDeathSeen = true;
                latch.firstDeathMs = nowMs;
            }
            latch.lastDeathMs = nowMs;
            latch.dead.push_back(guid);
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
        if (members.empty() && !newDeaths.empty() && latch.dead.size() == latch.known.size())
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
    // 3 * param. `fired` (optional) names the trigger this read started.
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

    // A live creature in the fight, as the kill-first pick sees it.
    struct TacticCandidate
    {
        std::uint32_t entry = 0;
        float distance = 0.0f;
        bool pickable = false;  // alive, attackable, reachable by the caller's rules
    };

    // Nobody is sent after a creature further away than this.
    constexpr float kBossTacticsEngageRange = 40.0f;

    // Index of the candidate to kill first, or -1 when no kill_first row names
    // a pickable candidate: lowest priority wins, the nearer one breaks a tie.
    inline int PickKillFirst(const ActiveBossRows& active, const std::vector<TacticCandidate>& candidates)
    {
        int best = -1;
        int bestPriority = 256;
        float bestDistance = 0.0f;
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
            if (best < 0 || priority < bestPriority || (priority == bestPriority && c.distance < bestDistance))
            {
                best = static_cast<int>(i);
                bestPriority = priority;
                bestDistance = c.distance;
            }
        }
        return best;
    }

    // Index of the nearest candidate a free DPS bot may attack: the same candidate rules the kill_first pick applies
    // (pickable, kBossTacticsEngageRange) minus the entries a hold_kill row holds, so a bot switching off a held entry
    // lands on the blocker or a loose add and never on another held entry. -1 when nothing is left.
    inline int PickNearestUnheld(const ActiveBossRows& rows, const std::vector<TacticCandidate>& candidates,
                                 const std::function<std::uint32_t(std::uint32_t)>& countNear)
    {
        int best = -1;
        float bestDistance = 0.0f;
        for (std::size_t i = 0; i < candidates.size(); ++i)
        {
            const TacticCandidate& c = candidates[i];
            if (!c.pickable || c.distance > kBossTacticsEngageRange)
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
}
