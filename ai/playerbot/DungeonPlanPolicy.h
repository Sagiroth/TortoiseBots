#pragma once

// Dungeon plans for the opt-in run leader (docs/guides/dungeon-run-leader.md).
//
// The rows live in the world tables `ai_dungeon_plan`, `ai_dungeon_leg` and
// `ai_dungeon_step` and are read ONCE at startup into DungeonPlanTable (loader:
// ai/playerbot/DungeonPlanMgr.cpp). Lookups never touch the database. The data
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

namespace ai
{
    enum class PlanStepKind : std::uint8_t
    {
        None = 0,
        Move,        // walk (navmesh) to x,y,z
        MoveDirect,  // straight walk to x,y,z (a gap the navmesh does not bridge)
        Pull,        // pull and kill the pack of `entries` within `radius` of x,y,z
        Boss,        // kill `entries` in order, then `entry`, near x,y,z
        Loot,        // loot chest or corpse `entry` near x,y,z; `item` must end in the bags
        Open,        // use game object `entry` near x,y,z; `item` is what its lock needs
        UseItem,     // use item `entry` from the bags; `item` is what it must make
        WaitObject,  // wait until object `entry` near x,y,z is in state `param`
        WaitClear,   // wait until no living hostile stands within `radius` of x,y,z
        Rest,        // a rest gate even when nobody is low
        Ritual,      // a summoning-style object for several bots (loaded, not run yet)
    };

    inline PlanStepKind ParsePlanStepKind(const std::string& text)
    {
        static const struct { const char* name; PlanStepKind kind; } kinds[] = {
            {"move", PlanStepKind::Move}, {"move_direct", PlanStepKind::MoveDirect},
            {"pull", PlanStepKind::Pull}, {"boss", PlanStepKind::Boss},
            {"loot", PlanStepKind::Loot}, {"open", PlanStepKind::Open},
            {"use_item", PlanStepKind::UseItem}, {"wait_object", PlanStepKind::WaitObject},
            {"wait_clear", PlanStepKind::WaitClear}, {"rest", PlanStepKind::Rest},
            {"ritual", PlanStepKind::Ritual},
        };
        for (const auto& k : kinds)
            if (text == k.name)
                return k.kind;
        return PlanStepKind::None;
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
            default: return "none";
        }
    }

    // Step flags (column `flags`).
    constexpr std::uint32_t kPlanFlagAmbush = 1;    // pull: the pack is stealthed; walk through, fight what shows
    constexpr std::uint32_t kPlanFlagAlliance = 2;  // pull/boss: only for an Alliance party
    constexpr std::uint32_t kPlanFlagHorde = 4;     // pull/boss: only for a Horde party

    // Defaults and bounds the loader applies.
    constexpr float kPlanDefaultPackRadius = 15.0f;
    constexpr float kPlanDefaultBossRadius = 30.0f;
    constexpr float kPlanMaxRadius = 100.0f;
    constexpr std::uint32_t kPlanDefaultTimeoutS = 60;
    constexpr std::uint32_t kPlanMaxTimeoutS = 600;
    constexpr std::size_t kPlanMaxEntries = 16;

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
    };

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
        std::vector<PlanStep> steps;  // (leg, seq) order after the seal

        // The leg's name, or "" when it has no ai_dungeon_leg row.
        const std::string& LegName(std::uint8_t leg) const
        {
            static const std::string none;
            for (const PlanLeg& l : legs)
                if (l.leg == leg)
                    return l.name;
            return none;
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
        MissingEntry,     // pull without entries; boss/loot/open/use_item/wait_object/ritual without entry
        BadRadius,        // negative, over kPlanMaxRadius, or a wait_clear without one
        BadTimeout,       // over kPlanMaxTimeoutS
        BadParam,         // wait_object state other than 0-2
        BadPoint,         // not a finite point
        BadEntries,       // `entries` is not a comma list of non-zero creature entries
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
        switch (step.kind)
        {
            case PlanStepKind::Pull:
                if (step.entries.empty())
                    return PlanStepRefusal::MissingEntry;
                break;
            case PlanStepKind::Boss:
            case PlanStepKind::Loot:
            case PlanStepKind::Open:
            case PlanStepKind::UseItem:
            case PlanStepKind::Ritual:
                if (step.entry == 0)
                    return PlanStepRefusal::MissingEntry;
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
            default:
                break;
        }
        if (step.kind == PlanStepKind::Pull && step.radius == 0.0f)
            step.radius = kPlanDefaultPackRadius;
        if (step.kind == PlanStepKind::Boss && step.radius == 0.0f)
            step.radius = kPlanDefaultBossRadius;
        if (step.timeoutS == 0)
            step.timeoutS = kPlanDefaultTimeoutS;
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

    private:
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

    // Whether a unit of `entry` standing at x,y,z belongs to a pull or boss
    // step AFTER step index `from` (the run leader never pulls what a later step
    // owns: the plan's order says when it is fought). A later step for the
    // other team (StepForTeam) owns nothing.
    inline bool OwnedByLaterStep(const DungeonPlan& plan, std::size_t from, std::uint32_t entry,
        float x, float y, float z, std::uint32_t team = 0)
    {
        for (std::size_t i = from + 1; i < plan.steps.size(); ++i)
        {
            const PlanStep& step = plan.steps[i];
            if (step.kind != PlanStepKind::Pull && step.kind != PlanStepKind::Boss)
                continue;
            if (!StepForTeam(step, team))
                continue;
            bool listed = step.entry == entry ||
                std::find(step.entries.begin(), step.entries.end(), entry) != step.entries.end();
            if (listed && PlanDistance(x, y, z, step.x, step.y, step.z) <= step.radius)
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
