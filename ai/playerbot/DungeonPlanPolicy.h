#pragma once

// Dungeon plans for the opt-in run leader (docs/play/dungeon-run-leader.md).
//
// The rows live in the world tables `ai_dungeon_plan`, `ai_dungeon_leg` and
// `ai_dungeon_step` and are read at startup into DungeonPlanTable (loader:
// ai/playerbot/DungeonPlanMgr.cpp), and again into a NEW table by a GM's
// `.bot run reloadplans` (DungeonPlanReloadPolicy.h decides whether it replaces
// the old one; a sealed table is never changed). Lookups never touch the database. The data
// comes from turtle-agent's plan v1 files through its exporter: one source for
// the agent's own driver and for the run leader.
//
// No core includes, so the row rules are tested on their own
// (tools/test_dungeon_plan_policy.cpp). Same lifecycle as BossTacticTable: one
// loading thread adds, calls Seal() once, readers see nothing before the seal.

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "BossTacticsPolicy.h"

namespace ai
{
    enum class PlanStepKind : std::uint8_t
    {
        None = 0,
        Move,        // walk (navmesh) to x,y,z; `entry` (0 = none): the area trigger it walks into, armed from here until fired (DungeonRun::ArmReachedTriggers)
        MoveDirect,  // straight walk to x,y,z (a gap the navmesh does not bridge); `entry` as for Move
        Pull,        // pull and kill the pack of `entries` within `radius` of x,y,z
        Boss,        // kill `entries` in order, then `entry`, near x,y,z
        Loot,        // loot chest or corpse `entry` near x,y,z; `item` must end in the bags
        Open,        // use game object `entry` near x,y,z; `item` is what its lock needs
        UseItem,     // use item `entry` from the bags; `item` is what it must make
        WaitObject,  // wait until object `entry` near x,y,z is in state `param`
        WaitClear,   // wait until no living hostile stands within `radius` of x,y,z
        Rest,        // a rest gate even when nobody is low
        Ritual,      // a summoning ritual object several bots use together (`param` bots; 0 = 3): an event's start
        Hold,        // a scripted event: stand at x,y,z, fight what comes, until its done condition (flags)
        Gossip,      // talk to creature `entry` (searched within kGossipSearchYd of x,y,z) and pick option `param` (1-15)
        UseOn,       // use item `item` ON game object `entry` at x,y,z (a key item whose spell opens the object's lock)
    };

    inline PlanStepKind ParsePlanStepKind(const std::string& text)
    {
        static const struct { const char* name; PlanStepKind kind; } kinds[] = {
            {"move", PlanStepKind::Move}, {"move_direct", PlanStepKind::MoveDirect},
            {"pull", PlanStepKind::Pull}, {"boss", PlanStepKind::Boss},
            {"loot", PlanStepKind::Loot}, {"open", PlanStepKind::Open},
            {"use_item", PlanStepKind::UseItem}, {"wait_object", PlanStepKind::WaitObject},
            {"wait_clear", PlanStepKind::WaitClear}, {"rest", PlanStepKind::Rest},
            {"ritual", PlanStepKind::Ritual}, {"hold", PlanStepKind::Hold},
            {"gossip", PlanStepKind::Gossip}, {"use_on", PlanStepKind::UseOn},
        };
        for (const auto& k : kinds)
            if (text == k.name)
                return k.kind;
        return PlanStepKind::None;
    }

    // The kind text of a direct step launched from an exact waypoint (the move
    // with kPlanFlagExact right before it). It is a move_direct in every way
    // (PlanStepKind::MoveDirect, PlanStep::exactLaunch set), under a kind name
    // of its own: a module without exact waypoints does not know the kind and
    // skips the row (UnknownKind), so it sends no direct order for the hop. The
    // rows and this module are deployed and rolled back together; what such an
    // old module does with the row after the hop is not guarded here
    // (docs/play/dungeon-run-leader.md "Exact waypoints").
    constexpr const char* kPlanKindMoveHopExact = "move_hop_exact";
    // The kind text of an open done by a PROOF: the object `entry` is used as by `open`, and the step is done once a
    // spawned game object (flag kPlanFlagProofCreature: a living creature) of the one entry in `entries` stands within
    // `radius` of x,y,z after the use's task answered (runtime/DungeonRunPolicy.h OpenProofStep). It is PlanStepKind::Open
    // with PlanStep::openProof set. A module without it does not know the kind and refuses the row (UnknownKind).
    // Sunken Temple's Atal'ai statues 148830-148835 change no state when used; the right one respawns the light 148883.
    constexpr const char* kPlanKindOpenProof = "open_proof";

    // A row's kind text into the step: its kind, exactLaunch for
    // "move_hop_exact", and openProof for "open_proof". An unknown text leaves the kind None.
    inline void ParsePlanStepRowKind(const std::string& text, PlanStepKind& kind, bool& exactLaunch, bool& openProof)
    {
        exactLaunch = text == kPlanKindMoveHopExact;
        openProof = text == kPlanKindOpenProof;
        kind = exactLaunch ? PlanStepKind::MoveDirect : openProof ? PlanStepKind::Open : ParsePlanStepKind(text);
    }

    inline const char* PlanStepKindName(PlanStepKind kind)
    {
        switch (kind)
        {
            case PlanStepKind::Move: return "move";
            case PlanStepKind::MoveDirect: return "move_direct";
            case PlanStepKind::Pull: return "pull";
            case PlanStepKind::Boss: return "boss";
            case PlanStepKind::Loot: return "loot";
            case PlanStepKind::Open: return "open";
            case PlanStepKind::UseItem: return "use_item";
            case PlanStepKind::WaitObject: return "wait_object";
            case PlanStepKind::WaitClear: return "wait_clear";
            case PlanStepKind::Rest: return "rest";
            case PlanStepKind::Ritual: return "ritual";
            case PlanStepKind::Hold: return "hold";
            case PlanStepKind::Gossip: return "gossip";
            case PlanStepKind::UseOn: return "use_on";
            default: return "none";
        }
    }

    // Step flags (column `flags`).
    constexpr std::uint32_t kPlanFlagAmbush = 1;    // pull: the pack is stealthed; walk through, fight what shows
    constexpr std::uint32_t kPlanFlagAlliance = 2;  // pull/boss: only for an Alliance party
    constexpr std::uint32_t kPlanFlagHorde = 4;     // pull/boss: only for a Horde party
    // hold: what ends the event (at most one of the two; neither: every unit of
    // `entries` within `radius` is dead, after one was seen awake).
    constexpr std::uint32_t kPlanFlagDoneData = 8;     // the instance's data `entry` reads `param`
    constexpr std::uint32_t kPlanFlagDoneObject = 16;  // object `entry` near x,y,z is in state `param` (0-2)
    // hold with kPlanFlagDoneData: that data is one of the core's encounter
    // states (EncounterState: 0 not started, 1 in progress, 2 fail, 3 done,
    // 4 special). The event has failed when it reads 2, or 0 again after it was
    // seen started, and when it does not start soon after the hold begins.
    constexpr std::uint32_t kPlanFlagEncounter = 32;
    // move only: an EXACT waypoint (plan v1's Waypoint.exact, the point a direct
    // step after it is launched from). The run leader counts the step done only
    // once the lead bot stands within kExactArriveYd of the point and has stood
    // still there for 2 s (runtime/DungeonRunPolicy.h ExactWalkTo); never by the
    // plain move's 3 yd arrival or its stalled 6 yd arrival, and no stand-in
    // point (detour) is taken for it. Any other kind with this flag is refused
    // (BadParam).
    constexpr std::uint32_t kPlanFlagExact = 64;
    // loot or open only: the object being GONE (nothing of `entry` spawned near the point, or it despawned on the
    // way: an opened chest that stays gone, a grave opened before a wipe) counts as done, with the note "object gone:
    // counted as done", instead of pausing the run on a refused order. Read only on a row with no item (item 0).
    // wait_object too: the step is done when the object is in the wanted state, or when it is not found after the
    // run saw it at least once and the not-found is authoritative for 3 s (the lead bot alive on the map within reach of the point, its grid loaded; it despawned: Shadowfang Keep's Courtyard Door 18895 is set active by Deathstalker
    // Adamant's escort and removed a few waypoints later). Never seen, the step still waits out its timeout and pauses.
    // Any other kind with this flag is refused (BadParam). 128 is reserved.
    constexpr std::uint32_t kPlanFlagGoneIsDone = 256;
    // gossip only: the NPC being DEAD counts as done. The order refused with the dead-NPC wording (the module's no-npc text
    // with "lies dead", GossipNpcDeadNote) or the task failed with it ends the step with the note "<name> lies dead, nothing
    // to talk to: the step is counted done" instead of pausing the run. Zul'Farrak's Sergeant Bly: his crew dies in the pyramid
    // event's troll waves in some runs. A gossip row without it pauses as before (Weegli: the End Door stays shut without him).
    // Any other kind with this flag is refused (BadParam), and a gossip row with any other flag.
    constexpr std::uint32_t kPlanFlagDeadNpcIsDone = 512;
    // pull only: never deferred. A pack the run leader finds not reachable from the floor the party stands on is
    // otherwise deferred when nothing later in the plan needs it gone (runtime/DungeonRunPolicy.h PullMayBeDeferred,
    // DeferUnreachablePack); with this flag it pauses the run as before. Any other kind with this flag is refused (BadParam).
    constexpr std::uint32_t kPlanFlagNoDefer = 1024;
    // open_proof only: the proof entry is a creature (alive). The next free bit: 16384 (8192 is kPlanFlagStandPull, a boss only).
    constexpr std::uint32_t kPlanFlagProofCreature = 2048;
    // boss only: the boss may stand out of the party's reach by design (Stormwrought Ruins' Eldermaw 62661 swims a
    // pool whose bed is the only navmesh, cut off from the shore; specs/sw-eldermaw.md). Where the step would pause on
    // him ALIVE (the instance search finds him away from his spot, or he stands by his spot and the pulls drew no
    // fight), it ends "not cleared" with a note instead. Any other kind with this flag is refused (BadParam).
    constexpr std::uint32_t kPlanFlagSkipUnreachable = 4096;
    // boss only: pulled from the stand, the move row right before it, once in the tank's shot; no firing stand search; specs/sw-eldermaw.md Spec B.
    constexpr std::uint32_t kPlanFlagStandPull = 8192;
    constexpr std::uint32_t kPlanEncounterNotStarted = 0;
    constexpr std::uint32_t kPlanEncounterFail = 2;
    constexpr std::uint32_t kPlanEncounterMax = 4;

    // Defaults and bounds the loader applies.
    constexpr float kPlanDefaultPackRadius = 15.0f;
    constexpr float kPlanDefaultBossRadius = 30.0f;
    constexpr float kPlanDefaultHoldRadius = 30.0f;
    constexpr std::uint32_t kPlanDefaultRitualBots = 3;
    constexpr float kPlanMaxRadius = 100.0f;
    constexpr float kPlanMaxProofRadius = 20.0f;
    constexpr std::uint32_t kPlanDefaultTimeoutS = 60;
    constexpr std::uint32_t kPlanMaxTimeoutS = 600;
    constexpr std::size_t kPlanMaxEntries = 16;
    // A step row's comment as the dashboard shows it: the row's `comment`, clipped at this many bytes.
    constexpr std::size_t kPlanMaxLabel = 80;
    // gossip: `param` is the option the way `.bot cmd gossip` takes it, counted from 1 at the top of the NPC's
    // menu (runtime/GossipOrderPolicy.h kMaxIndex: a 1.12 menu holds 15 options). The NPC is searched within
    // kGossipSearchYd of x,y,z (GossipOrder::kSearchYards).
    constexpr std::uint32_t kPlanMaxGossipOption = 15;
    constexpr float kGossipSearchYd = 40.0f;

    // A row's `comment` as a step label: a UTF-8-safe clip at kPlanMaxLabel (never cut inside a multi-byte sequence).
    inline std::string PlanStepLabel(const std::string& comment)
    {
        if (comment.size() <= kPlanMaxLabel)
            return comment;
        std::size_t cut = kPlanMaxLabel;
        while (cut > 0 && (static_cast<unsigned char>(comment[cut]) & 0xC0) == 0x80)
            --cut;
        return comment.substr(0, cut);
    }

    struct PlanStep
    {
        std::uint8_t leg = 0;
        std::uint16_t seq = 0;
        PlanStepKind kind = PlanStepKind::None;
        std::uint32_t entry = 0;
        float x = 0.0f, y = 0.0f, z = 0.0f;
        float radius = 0.0f;
        std::uint32_t item = 0;
        std::uint32_t param = 0;
        std::uint32_t flags = 0;
        std::uint32_t timeoutS = 0;
        std::vector<std::uint32_t> entries;
        // The row's kind was "move_hop_exact" (kPlanKindMoveHopExact):
        // a MoveDirect whose launch point is the exact move right before it.
        bool exactLaunch = false;
        // The row's kind was "open_proof" (kPlanKindOpenProof): an Open done by its proof (radius, the one `entries` entry).
        bool openProof = false;
        // The row's `comment`, clipped to kPlanMaxLabel bytes: the dashboard's short name of the step.
        std::string label;
        // boss only, filled at load (DungeonPlanTable::JoinBossTactics): the creatures the `ai_boss_tactic` rows of the
        // plan's map keyed on the step's boss (or one of its entries) are about (TacticSubjectEntry: kill_first targets,
        // interrupt casters, the boss itself), so the run leader never leaves such a creature alive in an earlier pack
        // (runtime/DungeonRunPolicy.h PullMayBeDeferred).
        std::vector<std::uint32_t> tacticEntries;
    };

    // The kind text of a step's row: "move_hop_exact" for an exact launch, "open_proof" for an open by proof,
    // else PlanStepKindName.
    inline const char* PlanStepRowKindName(const PlanStep& step)
    {
        if (step.exactLaunch && step.kind == PlanStepKind::MoveDirect)
            return kPlanKindMoveHopExact;
        if (step.openProof && step.kind == PlanStepKind::Open)
            return kPlanKindOpenProof;
        return PlanStepKindName(step.kind);
    }

    // A leg need row (ai_dungeon_leg_need): what a run needs for one leg of a plan.
    // mode: 1 optional (the leg is skipped when nobody carries the item),
    // 2 door (a key door into the leg the bots hold at with a player present),
    // 3 blocking (an item the run must use). pick: the Lockpicking skill that
    // opens the door instead (door only, 0 none, at most 300). object: the key
    // door's gameobject entry (door only). name/how: the item's name, and where
    // a player gets a door key (short, for the notes).
    enum class LegNeedMode : std::uint8_t
    {
        None = 0,
        Optional = 1,
        Door = 2,
        Blocking = 3,
    };

    struct PlanLegNeed
    {
        std::uint8_t leg = 0;
        std::uint32_t item = 0;
        LegNeedMode mode = LegNeedMode::None;
        std::uint16_t pick = 0;
        std::uint32_t object = 0;
        std::string name;
        std::string how;
    };

    // Why a leg need row was refused (the loader counts them).
    enum class PlanNeedRefusal : std::uint8_t
    {
        None = 0,
        Sealed,
        UnknownPlan,
        NoSuchLeg,
        MissingItem,
        BadMode,
        BadPick,
        BadObject,
        Duplicate,
    };

    inline const char* PlanNeedRefusalName(PlanNeedRefusal refusal)
    {
        switch (refusal)
        {
            case PlanNeedRefusal::None: return "none";
            case PlanNeedRefusal::Sealed: return "Sealed";
            case PlanNeedRefusal::UnknownPlan: return "UnknownPlan";
            case PlanNeedRefusal::NoSuchLeg: return "NoSuchLeg";
            case PlanNeedRefusal::MissingItem: return "MissingItem";
            case PlanNeedRefusal::BadMode: return "BadMode";
            case PlanNeedRefusal::BadPick: return "BadPick";
            case PlanNeedRefusal::BadObject: return "BadObject";
            case PlanNeedRefusal::Duplicate: return "Duplicate";
        }
        return "unknown";
    }

    struct PlanLeg
    {
        std::uint8_t leg = 0;
        std::string name;
    };

    struct DungeonPlan
    {
        std::string name;
        std::uint32_t map = 0;
        std::vector<PlanLeg> legs;
        std::vector<PlanLegNeed> needs;  // (leg, item) order after the seal
        std::vector<PlanStep> steps;  // (leg, seq) order after the seal
        // Plan-wide unit lists (ai_dungeon_plan columns, plan v1's fields of the
        // same names; creature entries, best first):
        std::vector<std::uint32_t> ccPriority; // what the run leader's crowd control takes in a pack
        std::vector<std::uint32_t> ccAlways;   // ... taken first whenever one is in the pack (ahead of ccPriority)
        std::vector<std::uint32_t> killFirst;  // killed first in a fight (the run leader focuses them)

        // An entry's crowd-control rank (0 best), or -1 when it is not to be
        // crowd-controlled: cc_always entries first, in their order (one of
        // them is crowd-controlled whenever it is in the pack, whatever
        // cc_priority unit stands beside it), then the cc_priority entries
        // cc_always does not list. The run leader holds one Polymorph at a
        // time (one mage), so the best-ranked unit present gets it.
        int CcRank(std::uint32_t entry) const
        {
            for (std::size_t i = 0; i < ccAlways.size(); ++i)
                if (ccAlways[i] == entry)
                    return static_cast<int>(i);
            int rank = static_cast<int>(ccAlways.size());
            for (std::uint32_t e : ccPriority)
            {
                if (std::find(ccAlways.begin(), ccAlways.end(), e) != ccAlways.end())
                    continue;
                if (e == entry)
                    return rank;
                ++rank;
            }
            return -1;
        }

        // An entry's kill-first rank (0 first), or -1.
        int KillFirstRank(std::uint32_t entry) const
        {
            for (std::size_t i = 0; i < killFirst.size(); ++i)
                if (killFirst[i] == entry)
                    return static_cast<int>(i);
            return -1;
        }
        // The leg's name, or "" when it has no ai_dungeon_leg row.
        const std::string& LegName(std::uint8_t leg) const
        {
            static const std::string none;
            for (const PlanLeg& l : legs)
                if (l.leg == leg)
                    return l.name;
            return none;
        }

        // The leg's needs, in (leg, item) order. Empty before the seal.
        std::vector<const PlanLegNeed*> NeedsOfLeg(std::uint8_t leg) const
        {
            std::vector<const PlanLegNeed*> out;
            for (const PlanLegNeed& need : needs)
                if (need.leg == leg)
                    out.push_back(&need);
            return out;
        }
    };

    // "4851,4852 ,7175" -> {4851, 4852, 7175}. False for an empty token, a
    // non-number, a zero, or more than kPlanMaxEntries entries.
    inline bool ParsePlanEntries(const std::string& text, std::vector<std::uint32_t>& out)
    {
        out.clear();
        std::size_t pos = 0;
        while (pos <= text.size())
        {
            std::size_t comma = text.find(',', pos);
            std::string token = text.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
            std::size_t first = token.find_first_not_of(" \t");
            std::size_t last = token.find_last_not_of(" \t");
            if (first == std::string::npos)
                return text.find_first_not_of(" \t") == std::string::npos && out.empty();
            token = token.substr(first, last - first + 1);
            if (token.size() > 9 || token.find_first_not_of("0123456789") != std::string::npos)
                return false;
            std::uint32_t value = static_cast<std::uint32_t>(std::stoul(token));
            if (value == 0 || out.size() >= kPlanMaxEntries)
                return false;
            out.push_back(value);
            if (comma == std::string::npos)
                break;
            pos = comma + 1;
        }
        return true;
    }

    // Why a step row was refused (the loader counts them).
    enum class PlanStepRefusal : std::uint8_t
    {
        None = 0,
        Sealed,
        UnknownPlan,
        UnknownKind,
        Duplicate,
        MissingEntry,     // pull without entries; boss/loot/open/use_item/wait_object/ritual/gossip/use_on without entry;
                          // use_on without item; open_proof without proof entries;
                          // hold done by object without entry, by its units without entries
        BadRadius,        // negative, over kPlanMaxRadius (over kPlanMaxProofRadius on an open_proof), or a wait_clear without one
        BadTimeout,       // over kPlanMaxTimeoutS; any timeout_s on an open_proof (the proof wait is kOpenProofWaitMs)
        BadParam,         // wait_object state other than 0-2; hold with both done flags, an encounter
                          // flag without the data flag or a state above 4, an object state above 2;
                          // the exact flag on a step that is not a move; gossip with an option
                          // outside 1-15 or with any flag; use_on with any flag or param;
                          // an open_proof whose proof equals its entry (no 2048), with param, a flag but 2048,
                          // or 2048 on any row that is not open_proof
        BadPoint,         // not a finite point; a use_on or an open_proof without a point (0,0,0)
        BadEntries,       // `entries` is not a comma list of non-zero creature entries; an open_proof names exactly one
    };

    inline const char* PlanStepRefusalName(PlanStepRefusal refusal)
    {
        switch (refusal)
        {
            case PlanStepRefusal::None: return "none";
            case PlanStepRefusal::Sealed: return "Sealed";
            case PlanStepRefusal::UnknownPlan: return "UnknownPlan";
            case PlanStepRefusal::UnknownKind: return "UnknownKind";
            case PlanStepRefusal::Duplicate: return "Duplicate";
            case PlanStepRefusal::MissingEntry: return "MissingEntry";
            case PlanStepRefusal::BadRadius: return "BadRadius";
            case PlanStepRefusal::BadTimeout: return "BadTimeout";
            case PlanStepRefusal::BadParam: return "BadParam";
            case PlanStepRefusal::BadPoint: return "BadPoint";
            case PlanStepRefusal::BadEntries: return "BadEntries";
        }
        return "unknown";
    }

    // Fills the defaults of a step and checks it. The step is changed only when
    // it is accepted.
    inline PlanStepRefusal NormalizePlanStep(PlanStep& step)
    {
        if (step.kind == PlanStepKind::None)
            return PlanStepRefusal::UnknownKind;
        if (!std::isfinite(step.x) || !std::isfinite(step.y) || !std::isfinite(step.z) || !std::isfinite(step.radius))
            return PlanStepRefusal::BadPoint;
        if (step.radius < 0.0f || step.radius > kPlanMaxRadius)
            return PlanStepRefusal::BadRadius;
        if (step.timeoutS > kPlanMaxTimeoutS)
            return PlanStepRefusal::BadTimeout;
        if ((step.flags & kPlanFlagExact) && step.kind != PlanStepKind::Move)
            return PlanStepRefusal::BadParam;
        if ((step.flags & kPlanFlagGoneIsDone) && step.kind != PlanStepKind::Loot && step.kind != PlanStepKind::Open &&
            step.kind != PlanStepKind::WaitObject)
            return PlanStepRefusal::BadParam;
        if ((step.flags & kPlanFlagDeadNpcIsDone) && step.kind != PlanStepKind::Gossip)
            return PlanStepRefusal::BadParam;
        if ((step.flags & kPlanFlagNoDefer) && step.kind != PlanStepKind::Pull)
            return PlanStepRefusal::BadParam;
        if ((step.flags & kPlanFlagSkipUnreachable) && step.kind != PlanStepKind::Boss)
            return PlanStepRefusal::BadParam;
        if ((step.flags & kPlanFlagStandPull) && step.kind != PlanStepKind::Boss)
            return PlanStepRefusal::BadParam;
        if (step.exactLaunch && step.kind != PlanStepKind::MoveDirect)
            return PlanStepRefusal::BadParam;
        if (step.openProof && step.kind != PlanStepKind::Open)
            return PlanStepRefusal::BadParam;
        if ((step.flags & kPlanFlagProofCreature) && !step.openProof)
            return PlanStepRefusal::BadParam;
        switch (step.kind)
        {
            case PlanStepKind::Pull:
                if (step.entries.empty())
                    return PlanStepRefusal::MissingEntry;
                break;
            case PlanStepKind::Boss:
            case PlanStepKind::Loot:
            case PlanStepKind::UseItem:
            case PlanStepKind::Ritual:
                if (step.entry == 0)
                    return PlanStepRefusal::MissingEntry;
                break;
            case PlanStepKind::Open:
                if (step.entry == 0)
                    return PlanStepRefusal::MissingEntry;
                if (step.openProof)
                {
                    if (step.entries.empty())
                        return PlanStepRefusal::MissingEntry;
                    if (step.entries.size() != 1)
                        return PlanStepRefusal::BadEntries;
                    if (!(step.radius > 0.0f) || step.radius > kPlanMaxProofRadius)
                        return PlanStepRefusal::BadRadius;
                    if (step.x == 0.0f && step.y == 0.0f && step.z == 0.0f)
                        return PlanStepRefusal::BadPoint;
                    if (step.param != 0 || (step.flags & ~kPlanFlagProofCreature) != 0)
                        return PlanStepRefusal::BadParam;
                    if (!(step.flags & kPlanFlagProofCreature) && step.entries.front() == step.entry)
                        return PlanStepRefusal::BadParam;
                    if (step.timeoutS != 0)
                        return PlanStepRefusal::BadTimeout;
                }
                break;
            case PlanStepKind::Gossip:
                if (step.entry == 0)
                    return PlanStepRefusal::MissingEntry;
                // The one flag read on a gossip is kPlanFlagDeadNpcIsDone (team flags come with a later change): any other set
                // one is refused, not ignored.
                if (step.param < 1 || step.param > kPlanMaxGossipOption || (step.flags & ~kPlanFlagDeadNpcIsDone) != 0)
                    return PlanStepRefusal::BadParam;
                break;
            case PlanStepKind::UseOn:
                // The object and the item used on it; the point names the object (the commander's entry:<id>@x,y,z).
                // No flag or param is read: a set one is refused, not ignored.
                if (step.entry == 0 || step.item == 0)
                    return PlanStepRefusal::MissingEntry;
                if (step.x == 0.0f && step.y == 0.0f && step.z == 0.0f)
                    return PlanStepRefusal::BadPoint;
                if (step.flags != 0 || step.param != 0)
                    return PlanStepRefusal::BadParam;
                break;
            case PlanStepKind::WaitObject:
                if (step.entry == 0)
                    return PlanStepRefusal::MissingEntry;
                if (step.param > 2)
                    return PlanStepRefusal::BadParam;
                break;
            case PlanStepKind::WaitClear:
                if (!(step.radius > 0.0f))
                    return PlanStepRefusal::BadRadius;
                break;
            case PlanStepKind::Hold:
            {
                bool const byData = (step.flags & kPlanFlagDoneData) != 0;
                bool const byObject = (step.flags & kPlanFlagDoneObject) != 0;
                bool const encounter = (step.flags & kPlanFlagEncounter) != 0;
                if ((byData && byObject) || (encounter && !byData))
                    return PlanStepRefusal::BadParam;
                if (encounter && step.param > kPlanEncounterMax)
                    return PlanStepRefusal::BadParam;
                if (byObject && step.entry == 0)
                    return PlanStepRefusal::MissingEntry;
                if (byObject && step.param > 2)
                    return PlanStepRefusal::BadParam;
                if (!byData && !byObject && step.entries.empty())
                    return PlanStepRefusal::MissingEntry;
                break;
            }
            default:
                break;
        }
        if (step.kind == PlanStepKind::Pull && step.radius == 0.0f)
            step.radius = kPlanDefaultPackRadius;
        if (step.kind == PlanStepKind::Boss && step.radius == 0.0f)
            step.radius = kPlanDefaultBossRadius;
        if (step.kind == PlanStepKind::Hold && step.radius == 0.0f)
            step.radius = kPlanDefaultHoldRadius;
        if (step.kind == PlanStepKind::Ritual && step.param == 0)
            step.param = kPlanDefaultRitualBots;
        if (step.timeoutS == 0)
            step.timeoutS = kPlanDefaultTimeoutS;
        step.label = PlanStepLabel(step.label);
        return PlanStepRefusal::None;
    }

    class DungeonPlanTable
    {
    public:
        // Loading only. False after the seal or for a plan name already there
        // (the first row wins, as under the primary key).
        bool AddPlan(const std::string& name, std::uint32_t map)
        {
            if (Sealed() || name.empty() || map == 0)
                return false;
            if (plans_.count(name))
                return false;
            DungeonPlan plan;
            plan.name = name;
            plan.map = map;
            plans_.emplace(name, std::move(plan));
            return true;
        }

        // Loading only: the plan's unit lists (comma lists as in `entries`;
        // empty is none). False after the seal, for an unknown plan, or when a
        // list is not a valid entry list (the plan then keeps none of them).
        bool SetUnits(const std::string& planName, const std::string& ccPriority, const std::string& ccAlways,
            const std::string& killFirst)
        {
            if (Sealed())
                return false;
            auto it = plans_.find(planName);
            if (it == plans_.end())
                return false;
            std::vector<std::uint32_t> prio, always, kill;
            if (!ParsePlanEntries(ccPriority, prio) || !ParsePlanEntries(ccAlways, always) ||
                !ParsePlanEntries(killFirst, kill))
                return false;
            it->second.ccPriority = std::move(prio);
            it->second.ccAlways = std::move(always);
            it->second.killFirst = std::move(kill);
            return true;
        }

        bool AddLeg(const std::string& planName, std::uint8_t leg, const std::string& name)
        {
            if (Sealed())
                return false;
            auto it = plans_.find(planName);
            if (it == plans_.end())
                return false;
            for (const PlanLeg& have : it->second.legs)
                if (have.leg == leg)
                    return false;
            it->second.legs.push_back(PlanLeg{leg, name});
            return true;
        }

        PlanStepRefusal AddStep(const std::string& planName, PlanStep step)
        {
            if (Sealed())
                return PlanStepRefusal::Sealed;
            auto it = plans_.find(planName);
            if (it == plans_.end())
                return PlanStepRefusal::UnknownPlan;
            PlanStepRefusal refusal = NormalizePlanStep(step);
            if (refusal != PlanStepRefusal::None)
                return refusal;
            // (plan, leg, seq) is the table's primary key; a set keeps the check O(1).
            if (!stepKeys_[planName].insert((static_cast<std::uint32_t>(step.leg) << 16) | step.seq).second)
                return PlanStepRefusal::Duplicate;
            it->second.steps.push_back(std::move(step));
            ++steps_;
            return PlanStepRefusal::None;
        }

         // Loading only (the loader reads needs after steps): one ai_dungeon_leg_need row.
        // At most one row per (plan, leg, item); Door rows carry the key door
        // (pick at most 300, object required), other modes carry neither.
        PlanNeedRefusal AddLegNeed(const std::string& planName, PlanLegNeed need)
        {
            if (Sealed())
                return PlanNeedRefusal::Sealed;
            auto it = plans_.find(planName);
            if (it == plans_.end())
                return PlanNeedRefusal::UnknownPlan;
            if (need.item == 0)
                return PlanNeedRefusal::MissingItem;
            if (need.mode != LegNeedMode::Optional && need.mode != LegNeedMode::Door &&
                need.mode != LegNeedMode::Blocking)
                return PlanNeedRefusal::BadMode;
            if ((need.pick > 0 && need.mode != LegNeedMode::Door) || need.pick > 300)
                return PlanNeedRefusal::BadPick;
            if ((need.object > 0 && need.mode != LegNeedMode::Door) ||
                (need.mode == LegNeedMode::Door && need.object == 0))
                return PlanNeedRefusal::BadObject;
            bool legSeen = false;
            for (const PlanStep& step : it->second.steps)
                if (step.leg == need.leg)
                {
                    legSeen = true;
                    break;
                }
            if (!legSeen)
                return PlanNeedRefusal::NoSuchLeg;
            for (const PlanLegNeed& have : it->second.needs)
                if (have.leg == need.leg && have.item == need.item)
                    return PlanNeedRefusal::Duplicate;
            it->second.needs.push_back(std::move(need));
            return PlanNeedRefusal::None;
        }

        // Loading only (before the seal; `tactics` sealed): each boss step gets the subjects of the tactic rows keyed on
        // (the plan's map, the step's boss entry or one of its entries), once each (PlanStep::tacticEntries). Returns the
        // entries joined; nothing after the seal or with an unsealed tactic table.
        std::size_t JoinBossTactics(const BossTacticTable& tactics)
        {
            std::size_t joined = 0;
            if (Sealed() || !tactics.Sealed())
                return joined;
            for (auto& entry : plans_)
                for (PlanStep& step : entry.second.steps)
                {
                    if (step.kind != PlanStepKind::Boss)
                        continue;
                    std::vector<std::uint32_t> keys = step.entries;
                    keys.push_back(step.entry);
                    for (std::uint32_t boss : keys)
                    {
                        const std::vector<BossTacticRow>* rows = boss ? tactics.Rows(entry.second.map, boss) : nullptr;
                        if (!rows)
                            continue;
                        for (const BossTacticRow& row : *rows)
                        {
                            std::uint32_t const subject = TacticSubjectEntry(row);
                            if (subject && std::find(step.tacticEntries.begin(), step.tacticEntries.end(), subject) ==
                                    step.tacticEntries.end())
                            {
                                step.tacticEntries.push_back(subject);
                                ++joined;
                            }
                        }
                    }
                    // In entry order: a reload that reads the same rows in another order is no change (SamePlanStep).
                    std::sort(step.tacticEntries.begin(), step.tacticEntries.end());
                }
            return joined;
        }

        // Sorts every plan's steps into run order and opens the table to readers.
        void Seal()
        {
            if (Sealed())
                return;
            for (auto& entry : plans_)
            {
                std::vector<PlanStep>& steps = entry.second.steps;
                std::sort(steps.begin(), steps.end(), [](const PlanStep& a, const PlanStep& b)
                {
                    return a.leg != b.leg ? a.leg < b.leg : a.seq < b.seq;
                });
                std::sort(entry.second.legs.begin(), entry.second.legs.end(),
                    [](const PlanLeg& a, const PlanLeg& b) { return a.leg < b.leg; });
                std::sort(entry.second.needs.begin(), entry.second.needs.end(),
                    [](const PlanLegNeed& a, const PlanLegNeed& b)
                    {
                        return a.leg != b.leg ? a.leg < b.leg : a.item < b.item;
                    });
                for (const PlanStep& step : steps)
                    if (step.kind == PlanStepKind::Boss && step.entry)
                        bossEntries_.insert(BossKey(entry.second.map, step.entry));
            }
            sealed_.store(true, std::memory_order_release);
        }

        bool Sealed() const { return sealed_.load(std::memory_order_acquire); }
        std::size_t PlanCount() const { return plans_.size(); }
        std::size_t StepCount() const { return steps_; }

        // The plan of that name (case-sensitive, as the names are lower case),
        // or null. Null before the seal.
        const DungeonPlan* Find(const std::string& name) const
        {
            if (!Sealed())
                return nullptr;
            auto it = plans_.find(name);
            return it == plans_.end() ? nullptr : &it->second;
        }

        // Every plan, sorted by name. Empty before the seal.
        std::vector<const DungeonPlan*> All() const
        {
            std::vector<const DungeonPlan*> out;
            if (!Sealed())
                return out;
            for (const auto& entry : plans_)
                out.push_back(&entry.second);
            std::sort(out.begin(), out.end(), [](const DungeonPlan* a, const DungeonPlan* b) { return a->name < b->name; });
            return out;
        }

        // Whether `plan` points into this table (a run's plan pointer after a
        // `.bot run reloadplans`: the reload keeps a replaced table alive while
        // a run still holds one of its plans, DungeonPlanReloadPolicy.h).
        bool Owns(const DungeonPlan* plan) const
        {
            if (!plan)
                return false;
            for (const auto& entry : plans_)
                if (&entry.second == plan)
                    return true;
            return false;
        }

        // The names of the plans on one map, sorted. Empty before the seal.
        std::vector<std::string> PlansOnMap(std::uint32_t map) const
        {
            std::vector<std::string> out;
            if (!Sealed())
                return out;
            for (const auto& entry : plans_)
                if (entry.second.map == map && !entry.second.steps.empty())
                    out.push_back(entry.first);
            std::sort(out.begin(), out.end());
            return out;
        }

        // Loading only: an `ai_dungeon_boss` row, a boss no boss step has to
        // name (one the plan skips, or the end of a scripted event). False after
        // the seal, for a zero map or entry, or for a (map, entry) already added
        // (the first row wins, as under the primary key).
        bool AddBoss(std::uint32_t map, std::uint32_t entry)
        {
            if (Sealed() || map == 0 || entry == 0)
                return false;
            return bossEntries_.insert(BossKey(map, entry)).second;
        }

        // The (map, entry) keys of the bosses: the rows plus, after the seal,
        // the boss steps' entries.
        std::size_t BossCount() const { return bossEntries_.size(); }

        // Whether a creature of `entry` on `map` is a boss: an `ai_dungeon_boss`
        // row, or the boss of a plan's boss step (its `entry`, not the adds of
        // its `entries`). The steps join at the seal; false before it.
        bool IsBossEntry(std::uint32_t map, std::uint32_t entry) const
        {
            if (!Sealed() || !entry)
                return false;
            return bossEntries_.count(BossKey(map, entry)) != 0;
        }

    private:
        static std::uint64_t BossKey(std::uint32_t map, std::uint32_t entry)
        {
            return (static_cast<std::uint64_t>(map) << 32) | entry;
        }

        std::unordered_set<std::uint64_t> bossEntries_;
        std::unordered_map<std::string, DungeonPlan> plans_;
        std::unordered_map<std::string, std::unordered_set<std::uint32_t>> stepKeys_;
        std::size_t steps_ = 0;
        std::atomic<bool> sealed_{false};
    };

    // Distance in 3D.
    inline float PlanDistance(float ax, float ay, float az, float bx, float by, float bz)
    {
        float dx = ax - bx, dy = ay - by, dz = az - bz;
        return std::sqrt(dx * dx + dy * dy + dz * dz);
    }

    // Whether a pull or boss step is meant for a party of `team`
    // (kPlanFlagAlliance or kPlanFlagHorde; 0 = unknown, everything is). A step
    // with neither team flag is for both.
    inline bool StepForTeam(const PlanStep& step, std::uint32_t team)
    {
        std::uint32_t const teams = step.flags & (kPlanFlagAlliance | kPlanFlagHorde);
        return teams == 0 || team == 0 || (teams & team) != 0;
    }

    // Whether a pull, boss or hold step owns a unit of `entry` standing at x,y,z: the step lists the entry (a hold's
    // `entry` is its done condition, instance data or object, not a unit) and the point lies in its radius.
    inline bool StepOwnsUnit(const PlanStep& step, std::uint32_t entry, float x, float y, float z)
    {
        if (step.kind != PlanStepKind::Pull && step.kind != PlanStepKind::Boss && step.kind != PlanStepKind::Hold)
            return false;
        bool listed = (step.kind != PlanStepKind::Hold && step.entry == entry) ||
            std::find(step.entries.begin(), step.entries.end(), entry) != step.entries.end();
        return listed && PlanDistance(x, y, z, step.x, step.y, step.z) <= step.radius;
    }

    // Whether a unit of `entry` standing at x,y,z belongs to a pull, boss or
    // hold step AFTER step index `from` (the run leader never pulls what a later
    // step owns: the plan's order says when it is fought; a hold owns its
    // event's units). A later step for the other team (StepForTeam) owns nothing.
    inline bool OwnedByLaterStep(const DungeonPlan& plan, std::size_t from, std::uint32_t entry,
        float x, float y, float z, std::uint32_t team = 0)
    {
        for (std::size_t i = from + 1; i < plan.steps.size(); ++i)
        {
            const PlanStep& step = plan.steps[i];
            if (!StepForTeam(step, team))
                continue;
            if (StepOwnsUnit(step, entry, x, y, z))
                return true;
        }
        return false;
    }

    // Whether a unit of `entry` standing at x,y,z belongs to any pull, boss or hold step of the plan other than step
    // index `self`, earlier or later (the pack-cleared gate leaves a residue unit where another step fights it).
    inline bool OwnedByOtherStep(const DungeonPlan& plan, std::size_t self, std::uint32_t entry,
        float x, float y, float z, std::uint32_t team = 0)
    {
        for (std::size_t i = 0; i < plan.steps.size(); ++i)
        {
            if (i == self)
                continue;
            const PlanStep& step = plan.steps[i];
            if (!StepForTeam(step, team))
                continue;
            if (StepOwnsUnit(step, entry, x, y, z))
                return true;
        }
        return false;
    }
    // Whether any gossip step of the plan names `entry` (r36 shadowfang_keep
    // slotI: the walk back replayed step 1 and pulled Deathstalker Adamant
    // 3849, the friendly prisoner whose gossip opens the courtyard door; he
    // died and step 130's gossip could not be done, respawn 4 h 36 min). Only
    // gossip steps: their entry is a creature entry, while open/use_on name a
    // game object, use_item an item and ritual encounter data (other id
    // namespaces: a hostile creature sharing such a number must still be
    // pulled). The run leader never pulls such a unit as a hop unit: it is
    // a unit the plan talks to, not a pack unit.
    inline bool PlanNamesEventUnit(const DungeonPlan& plan, std::uint32_t entry)
    {
        for (const PlanStep& step : plan.steps)
        {
            if (step.kind != PlanStepKind::Gossip)
                continue;
            if (step.entry == entry)
                return true;
        }
        return false;
    }

    // Moves that are more than maxYd from the move before them: a hop that long
    // can pass packs the plan does not name. The loader logs them (the exporter
    // should cut such a leg into via points).
    inline std::size_t CountLongHops(const DungeonPlan& plan, float maxYd)
    {
        std::size_t count = 0;
        const PlanStep* last = nullptr;
        for (const PlanStep& step : plan.steps)
        {
            if (step.kind != PlanStepKind::Move && step.kind != PlanStepKind::MoveDirect)
                continue;
            if (last && PlanDistance(last->x, last->y, last->z, step.x, step.y, step.z) > maxYd)
                ++count;
            last = &step;
        }
        return count;
    }
}
