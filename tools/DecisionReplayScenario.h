#pragma once

// The synthetic decision-capture scenario: a four-step plan (move, move, a
// three-unit pull, move) walked through in 40 ticks, recorded with the real
// recorder and ticked with the real policy. The replay test and
// `replay_decisions synth` share it, so the committed fixture is the same run.

#include "../runtime/DecisionCaptureRecorder.h"

#include <cstdint>
#include <string>
#include <vector>

namespace TortoiseBots
{
namespace DecisionReplay
{

inline constexpr std::uint64_t kScenarioStartUnixMs = 1700000000000ull;
inline constexpr std::uint64_t kScenarioTicks = 40;
inline constexpr std::uint64_t kScenarioGlueAfterSeq = 8; // the run field the glue writes after this tick
inline constexpr std::uint64_t kScenarioGlueDeltaSeq = kScenarioGlueAfterSeq + 1; // ... it is this record's run delta
inline constexpr std::uint64_t kScenarioPullSeq = 11;     // the pullback order
inline constexpr std::uint64_t kScenarioFightSeq = 20;

inline ai::PlanStep ScenarioStep(ai::PlanStepKind kind, float x, float y = 0.0f, float z = 0.0f)
{
    ai::PlanStep step;
    step.kind = kind;
    step.x = x;
    step.y = y;
    step.z = z;
    ai::NormalizePlanStep(step);
    return step;
}

inline void BuildScenarioPlan(ai::DungeonPlan& plan, ai::BossTacticTable& tactics)
{
    plan.name = "synthetic";
    plan.map = 309;
    plan.legs.push_back(ai::PlanLeg{0, "walk"});
    plan.steps.push_back(ScenarioStep(ai::PlanStepKind::Move, 10.0f));
    plan.steps.push_back(ScenarioStep(ai::PlanStepKind::Move, 30.0f));
    ai::PlanStep pull = ScenarioStep(ai::PlanStepKind::Pull, 40.0f);
    pull.radius = 10.0f;
    pull.entries = {4850, 4851, 4852};
    plan.steps.push_back(pull);
    plan.steps.push_back(ScenarioStep(ai::PlanStepKind::Move, 60.0f));

    ai::BossTacticRow row;
    row.map = plan.map;
    row.bossEntry = 14507;
    row.kind = ai::BossTacticKind::Interrupt;
    row.spellId = 12345;
    row.priority = 1;
    tactics.Add(row);
    tactics.Seal();
}

inline void ScenarioFacts(std::uint64_t seq, DungeonRun::Facts& facts)
{
    facts.total = 5;
    facts.alive = 5;
    facts.leadAlive = true;
    facts.carrier = "Tanky";
    facts.seen = true;
    facts.leadX = seq < 6 ? 0.0f : seq < 10 ? 8.0f : seq < 28 ? 30.0f : 0.0f;
    if (seq >= 28 && seq < kScenarioTicks)
        facts.leadX = 30.0f + static_cast<float>(seq - 27) * 2.0f;
    if (seq == kScenarioTicks)
        facts.leadX = 60.0f;

    static const char* const names[] = {"Tanky", "Healy", "Sneaky", "Firey", "Shieldy"};
    for (std::size_t i = 0; i < 5; ++i)
    {
        DungeonRun::MemberFact member;
        member.name = names[i];
        member.yd = 0.0f;
        member.x = facts.leadX;
        member.inCombat = seq >= kScenarioFightSeq && seq < 26;
        facts.members.push_back(member);
    }

    if (seq >= 9)
    {
        DungeonRun::UnitFact unit;
        unit.guid = 101;
        unit.entry = 4850;
        unit.distLead = 10.0f;
        unit.x = 50.0f;
        unit.hostile = true;
        facts.units.push_back(unit);
        unit.guid = 102;
        unit.entry = 4851;
        unit.distLead = 11.0f;
        unit.x = 51.0f;
        facts.units.push_back(unit);
        unit.guid = 103;
        unit.entry = 4852;
        unit.distLead = 12.0f;
        unit.x = 52.0f;
        facts.units.push_back(unit);
    }
    if (seq >= kScenarioFightSeq && seq < 26)
        facts.anyInCombat = true;
}

inline DecisionCapture::HeaderInfo ScenarioHeader()
{
    DecisionCapture::HeaderInfo header;
    header.captureId = "20261102T161939Z-synthetic-Fionaa";
    header.commander = "Fionaa";
    header.module = "replay-test";
    header.settingsJson = "{\"segmentMiB\":32,\"captureMiB\":512,\"totalMiB\":4096,\"recordKiB\":512,"
                          "\"keyframeEvery\":60}";
    header.startedUnixMs = kScenarioStartUnixMs;
    return header;
}

inline void RecordScenario(DecisionCapture::Sink& sink)
{
    ai::DungeonPlan plan;
    ai::BossTacticTable tactics;
    BuildScenarioPlan(plan, tactics);

    DecisionCapture::Limits limits;
    limits.keyframeEvery = 60;
    limits.segmentBytes = 1ull << 30;
    DecisionCapture::Recorder recorder(ScenarioHeader(), limits, sink);

    DungeonRun::Run run = DungeonRun::Start(&plan);
    run.liveTactics = &tactics;
    for (std::uint64_t seq = 1; seq <= kScenarioTicks; ++seq)
    {
        DungeonRun::Facts facts;
        ScenarioFacts(seq, facts);
        std::uint64_t const now = kScenarioStartUnixMs + seq * 1000ull;
        recorder.BeforeTick(run, facts, 1000, now);
        std::string const order = DungeonRun::Tick(run, facts, 1000);
        recorder.AfterTick(run, order);
        if (seq == kScenarioGlueAfterSeq)
            run.leadWalkReset = true;
    }
    recorder.End(kScenarioStartUnixMs + (kScenarioTicks + 1) * 1000ull, "stopped");
}

} // namespace DecisionReplay
} // namespace TortoiseBots
