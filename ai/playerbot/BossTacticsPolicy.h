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
    };

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
    };

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
        // (map, boss, kind, target, spell) key is already there: the first row
        // wins, as it would under the table's primary key.
        bool Add(const BossTacticRow& row)
        {
            if (sealed_.load(std::memory_order_acquire))
                return false;
            if (row.map == 0 || row.bossEntry == 0 || row.kind == BossTacticKind::None)
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
                (row.targetEntry == 0 || !(row.param > 0.0f) || row.param > 100.0f))
                return false;
            std::vector<BossTacticRow>& rows = rows_[Key(row.map, row.bossEntry)];
            for (const BossTacticRow& have : rows)
            {
                if (have.kind == row.kind && have.targetEntry == row.targetEntry && have.spellId == row.spellId)
                    return false;
            }
            rows.push_back(row);
            maps_[row.map] = true;
            if (row.kind == BossTacticKind::AvoidCast)
                avoidCastMaps_[row.map] = true;
            if (row.kind == BossTacticKind::HoldKill || row.kind == BossTacticKind::OneAtATime)
                holdMaps_[row.map] = true;
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
            out.entries.push_back(row->bossEntry);
            for (const BossTacticRow* other : active)
            {
                if (other->kind != BossTacticKind::LinkedDeath || other->bossEntry != row->bossEntry ||
                    other->priority != row->priority)
                    continue;
                out.param = std::max(out.param, other->param);
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
    //   Cleared once every known member is confirmed dead, or `kLinkedDeathLatchMs` after the death, so a fight that
    //   cannot finish the trio goes back to the equalise hold instead of burning one member at a time forever.
    //   Encounter end (no latch): the known set, armed and latch reset when no member fights and every non-dead known
    //   guid is accounted for: out of combat at full health this scan (an evade), or not seen for kLinkedDeathEndMs. So an
    //   evade or a wipe does not leave old guids to block the next pull's arming; a respawn under new guids starts clean.
    //   A known guid not seen for kLinkedDeathDropMs while the group is seen (a member despawned) is forgotten too. Within
    //   those windows an unseen member still blocks the arming (fail safe).
    constexpr std::uint32_t kLinkedDeathLatchMs = 15000;
    constexpr std::uint32_t kLinkedDeathSeenMs = 5000;
    constexpr std::uint32_t kLinkedDeathEndMs = 30000;
    constexpr std::uint32_t kLinkedDeathDropMs = 60000;
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
    // is empty; `inCombat` marks one still fighting; a known guid's sighting, never a new registration). The sightings
    // are applied first (a member above `param` disarms, a low one re-arms once every other known member is fresh), then
    // the encounter end (resets the latch when it has ended, see LinkedDeathEncounterEnded) and the stale guids, then the
    // deaths; a death latches only when armed both before this read and after it.
    inline bool LinkedDeathLatchTick(LinkedDeathLatch& latch, float param, const std::vector<LinkedMember>& members,
                                     const std::vector<std::uint64_t>& deadNow, std::uint32_t nowMs,
                                     const std::vector<LinkedMember>& idle = std::vector<LinkedMember>())
    {
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
        if (latch.latched && nowMs - latch.sinceMs >= kLinkedDeathLatchMs)
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
        if (members.empty() && latch.dead.size() == latch.known.size())
        {
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
