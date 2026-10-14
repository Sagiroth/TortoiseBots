#include "playerbot/playerbot.h"
#include "DungeonPlanMgr.h"

using namespace ai;

DungeonPlanTable& ai::GetDungeonPlans()
{
    static DungeonPlanTable table;
    return table;
}

namespace
{
    // A move this much longer than the one before it is logged (the exporter
    // should cut the leg into via points).
    constexpr float kLongHopYd = 60.0f;
}

void ai::LoadDungeonPlans()
{
    DungeonPlanTable& table = GetDungeonPlans();
    if (table.Sealed())
        return;

    uint32 skippedPlans = 0;
    if (auto result = WorldDatabase.Query("SELECT `plan`, `map` FROM `ai_dungeon_plan`"))
    {
        do
        {
            Field* fields = result->Fetch();
            if (!table.AddPlan(fields[0].GetCppString(), fields[1].GetUInt32()))
                ++skippedPlans;
        } while (result->NextRow());
    }

    if (auto result = WorldDatabase.Query("SELECT `plan`, `leg`, `name` FROM `ai_dungeon_leg`"))
    {
        do
        {
            Field* fields = result->Fetch();
            table.AddLeg(fields[0].GetCppString(), fields[1].GetUInt8(), fields[2].GetCppString());
        } while (result->NextRow());
    }

    uint32 skippedSteps = 0;
    if (auto result = WorldDatabase.Query(
        "SELECT `plan`, `leg`, `seq`, `kind`, `entry`, `x`, `y`, `z`, `radius`, `item`, `param`, `flags`, "
        "`timeout_s`, `entries` FROM `ai_dungeon_step`"))
    {
        do
        {
            Field* fields = result->Fetch();
            std::string const plan = fields[0].GetCppString();
            PlanStep step;
            step.leg = fields[1].GetUInt8();
            step.seq = fields[2].GetUInt16();
            step.kind = ParsePlanStepKind(fields[3].GetCppString());
            step.entry = fields[4].GetUInt32();
            step.x = fields[5].GetFloat();
            step.y = fields[6].GetFloat();
            step.z = fields[7].GetFloat();
            step.radius = fields[8].GetFloat();
            step.item = fields[9].GetUInt32();
            step.param = fields[10].GetUInt32();
            step.flags = fields[11].GetUInt32();
            step.timeoutS = fields[12].GetUInt16();
            PlanStepRefusal refusal = ParsePlanEntries(fields[13].GetCppString(), step.entries)
                ? table.AddStep(plan, step) : PlanStepRefusal::MissingEntry;
            if (refusal != PlanStepRefusal::None)
            {
                ++skippedSteps;
                sLog.outError("ai_dungeon_step: plan '%s' leg %u seq %u (%s) skipped (reason %u)", plan.c_str(),
                    uint32(step.leg), uint32(step.seq), fields[3].GetCppString().c_str(), uint32(refusal));
            }
        } while (result->NextRow());
    }

    // Sealed even when the tables are empty or missing: `.bot run` then finds no plan.
    table.Seal();
    for (const DungeonPlan* plan : table.All())
    {
        std::size_t const longHops = CountLongHops(*plan, kLongHopYd);
        if (longHops)
            sLog.outError("ai_dungeon_step: plan '%s' has %u moves more than %.0f yd from the move before them",
                plan->name.c_str(), uint32(longHops), kLongHopYd);
    }
    sLog.outString(">> Loaded %u dungeon plans, %u steps (%u plans and %u steps skipped)",
        uint32(table.PlanCount()), uint32(table.StepCount()), skippedPlans, skippedSteps);
    sLog.outString();
}
