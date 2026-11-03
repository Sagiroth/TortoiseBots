// Standalone check for runtime/DecisionCaptureCodec.h (decision capture C1):
//   g++ -std=c++17 -O2 -Wall -Wextra tools/test_decision_capture_codec.cpp -o /tmp/t && /tmp/t
//
// The samples come from the generated header, one per field: decoding them into
// a default struct and re-encoding must give the sample text back, field for
// field. That is the check that no field is silently missing from the encoder
// or the decoder, whatever the field's type. The tick-time check is built at
// -O2 because the capture's budget (2 ms per tick) is a release-build number.
#include "../runtime/DecisionCaptureRecorder.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <new>
#include <string>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using namespace TortoiseBots;
using namespace TortoiseBots::DecisionCapture;
using TortoiseBots::DungeonRun::Facts;
using TortoiseBots::DungeonRun::MemberFact;
using TortoiseBots::DungeonRun::Phase;
using TortoiseBots::DungeonRun::Run;
using TortoiseBots::DungeonRun::State;
using TortoiseBots::DungeonRun::UnitFact;
using ai::DungeonPlan;
using ai::PlanStep;
using ai::PlanStepKind;

static MiniJson::Value ParseText(const std::string& text)
{
    MiniJson::Value value;
    std::string error;
    if (!MiniJson::Parse(text, value, error))
    {
        std::cerr << "parse failed: " << error << "\n" << text << "\n";
        std::exit(1);
    }
    return value;
}

static const std::string* FindField(const FieldMap& fields, const std::string& name)
{
    for (const auto& field : fields)
        if (field.first == name)
            return &field.second;
    return nullptr;
}

// -- 1. samples ----------------------------------------------------------

template <typename T>
static void CheckSamples(const DecisionSampleField* samples, const char* name)
{
    std::string object = "{";
    std::size_t count = 0;
    for (const DecisionSampleField* sample = samples; sample->field; ++sample)
    {
        if (count)
            object += ',';
        object += JsonString(sample->field);
        object += ':';
        object += sample->json;
        ++count;
    }
    object += '}';

    MiniJson::Value const value = ParseText(object);
    T decoded{};
    std::string error;
    if (!DecodeStruct(value, decoded, DecodeContext{}, error))
    {
        std::cerr << name << ": decode failed: " << error << "\n";
        std::exit(1);
    }
    FieldMap const encoded = EncodeFieldsOf(decoded);
    for (const DecisionSampleField* sample = samples; sample->field; ++sample)
    {
        const std::string* text = FindField(encoded, sample->field);
        if (!text)
        {
            std::cerr << name << ": field not encoded: " << sample->field << "\n";
            std::exit(1);
        }
        if (*text != sample->json)
        {
            std::cerr << name << "." << sample->field << ": sample " << sample->json
                      << " != encoded " << *text << "\n";
            std::exit(1);
        }
    }
    CHECK(encoded.size() >= count);
}

#define CHECK_STRUCT(Mangled, Type) CheckSamples<Type>(kDecisionSamples_##Mangled, #Mangled);

static void TestSamples()
{
    TB_DCAP_STRUCTS(CHECK_STRUCT)
}

// -- 2. floats -----------------------------------------------------------

template <typename F>
static void CheckFloatRoundTrip(F value)
{
    std::string const text = EncodeValue(value);
    MiniJson::Value const parsed = ParseText(text);
    F back{};
    std::string error;
    CHECK(DecodeValue(parsed, back, DecodeContext{}, error));
    if (std::isnan(value))
        CHECK(std::isnan(back));
    else
    {
        CHECK(std::memcmp(&value, &back, sizeof(F)) == 0);
        CHECK(std::signbit(value) == std::signbit(back));
    }
}

static void TestFloats()
{
    float const floats[] = {0.1f, 1e-7f, -0.0f, std::numeric_limits<float>::max(),
                            std::numeric_limits<float>::quiet_NaN(),
                            std::numeric_limits<float>::infinity(),
                            -std::numeric_limits<float>::infinity()};
    for (float value : floats)
        CheckFloatRoundTrip(value);
    double const doubles[] = {0.1, 1e-7, -0.0, std::numeric_limits<double>::max(),
                              std::numeric_limits<double>::quiet_NaN(),
                              std::numeric_limits<double>::infinity(),
                              -std::numeric_limits<double>::infinity()};
    for (double value : doubles)
        CheckFloatRoundTrip(value);
    CHECK(EncodeValue(0.5f) == "0.5");
    CHECK(EncodeValue(std::numeric_limits<float>::quiet_NaN()) == "\"nan\"");
    CHECK(EncodeValue(-std::numeric_limits<double>::infinity()) == "\"-inf\"");

    // The same through a whole struct, and through a keyframe JSON text.
    Facts facts;
    facts.maxMemberYd = -0.0f;
    facts.leadX = 1e-7f;
    facts.leadY = std::numeric_limits<float>::infinity();
    facts.leadZ = std::numeric_limits<float>::quiet_NaN();
    MiniJson::Value const value = ParseText(FullObject(EncodeFields(facts)));
    Facts back;
    std::string error;
    CHECK(DecodeFields(value, back, error));
    CHECK(std::memcmp(&facts.maxMemberYd, &back.maxMemberYd, sizeof(float)) == 0);
    CHECK(std::memcmp(&facts.leadX, &back.leadX, sizeof(float)) == 0);
    CHECK(std::isinf(back.leadY) && std::isnan(back.leadZ));
}

// -- 3. integers ---------------------------------------------------------

template <typename I>
static void CheckIntegerRoundTrip(I value)
{
    MiniJson::Value const parsed = ParseText(EncodeValue(value));
    I back{};
    std::string error;
    CHECK(DecodeValue(parsed, back, DecodeContext{}, error));
    CHECK(back == value);
}

static void TestIntegers()
{
    CheckIntegerRoundTrip(std::numeric_limits<std::uint64_t>::max());
    CheckIntegerRoundTrip(static_cast<std::size_t>(-1));
    CheckIntegerRoundTrip(std::numeric_limits<std::int64_t>::min());
    CheckIntegerRoundTrip(std::numeric_limits<std::int64_t>::max());
    CheckIntegerRoundTrip(std::numeric_limits<std::uint8_t>::max());
    CheckIntegerRoundTrip(false);
    CheckIntegerRoundTrip(true);
}

// -- 4. strings ----------------------------------------------------------

static void TestStrings()
{
    std::string bytes = "quo\"te\\back\nnew\x01one UTF-8 \xC3\xA9 high ";
    bytes.push_back(static_cast<char>(0x80));
    bytes.push_back(static_cast<char>(0xFF));
    Facts facts;
    facts.itemPlayer = bytes;
    facts.leadMoveFailReason = "line\r\ttab";
    std::string const text = FullObject(EncodeFields(facts));
    MiniJson::Value const value = ParseText(text);
    Facts back;
    std::string error;
    CHECK(DecodeFields(value, back, error));
    CHECK(back.itemPlayer == bytes);
    CHECK(back.leadMoveFailReason == "line\r\ttab");
    CHECK(JsonString("a\"b") == "\"a\\\"b\"");
    CHECK(JsonString(std::string(1, '\x01')) == "\"\\u0001\"");
}

// -- 5. unordered containers --------------------------------------------

static void TestContainerOrder()
{
    Run first;
    Run second;
    for (std::uint64_t guid = 1; guid <= 100; ++guid)
        first.passed.insert(guid);
    for (std::uint64_t guid = 100; guid >= 1; --guid)
        second.passed.insert(guid);
    first.pullTries[7] = 1;
    first.pullTries[3] = 2;
    second.pullTries[3] = 2;
    second.pullTries[7] = 1;
    FieldMap const one = EncodeFields(first);
    FieldMap const two = EncodeFields(second);
    CHECK(*FindField(one, "passed") == *FindField(two, "passed"));
    CHECK(*FindField(one, "pullTries") == *FindField(two, "pullTries"));
    CHECK(FullObject(one) == FullObject(two));
}

// -- 6. deltas -----------------------------------------------------------

static void TestDeltas()
{
    Facts facts;
    MemberFact first;
    first.name = "Nathil";
    first.yd = 3.0f;
    MemberFact second;
    second.name = "Jinkla";
    second.yd = 4.0f;
    facts.members = {first, second};
    FieldMap const base = EncodeFields(facts);
    CHECK(DeltaObject(base, base) == "{}");

    // One element of a vector changes: the whole vector resends, and the other
    // element survives the apply.
    Facts changed = facts;
    changed.members[1].yd = 9.0f;
    changed.members[1].x = 9.0f;
    FieldMap const next = EncodeFields(changed);
    std::string const delta = DeltaObject(base, next);
    MiniJson::Value const parsed = ParseText(delta);
    CHECK(parsed.kind == MiniJson::Value::Object);
    CHECK(parsed.obj.size() == 1);
    CHECK(parsed.obj[0].first == "members");

    FieldMap rebuilt = base;
    ApplyDelta(rebuilt, parsed);
    CHECK(FullObject(rebuilt) == FullObject(next));
    CHECK(DeltaObject(next, next) == "{}");
}

// -- 7. schema -----------------------------------------------------------

static void TestSchema()
{
    CHECK(Hex64(Fnv1a64(kDecisionSchemaText)) == std::string(kDecisionSchemaHash));
    CHECK(std::strlen(kDecisionSchemaHash) == 16);
    CHECK(std::string(kDecisionSchemaText).find("struct Facts\n") != std::string::npos);
    CHECK(std::string(kDecisionSchemaText).find("struct Run\n") != std::string::npos);
    CHECK(std::string(kDecisionSchemaText).find("  plan: const DungeonPlan*\n") != std::string::npos);
    CHECK(std::string(kDecisionSchemaText).find("enum Phase: Begin=0,Gather=1,Walk=2") != std::string::npos);
    CHECK(std::string(kDecisionSchemaText).find("struct Run::Straggle\n") != std::string::npos);
    CHECK(std::string(kDecisionSchemaText).find("enum Run::CombatDropKind: Unattackable=0") != std::string::npos);
}

// -- 8. realistic sizes --------------------------------------------------

static PlanStep Step(PlanStepKind kind, float x = 0, float y = 0, float z = 0)
{
    PlanStep step;
    step.kind = kind;
    step.x = x;
    step.y = y;
    step.z = z;
    ai::NormalizePlanStep(step);
    return step;
}

static PlanStep PullStep(float x, std::vector<std::uint32_t> entries)
{
    PlanStep step;
    step.kind = PlanStepKind::Pull;
    step.x = x;
    step.entries = entries;
    ai::NormalizePlanStep(step);
    return step;
}

static Facts Party(float x = 0)
{
    Facts facts;
    facts.total = 5;
    facts.alive = 5;
    facts.leadX = x;
    facts.carrier = "Tanky";
    facts.seen = true;
    return facts;
}

static MemberFact Mem(const std::string& name, float yd, float dz = 0.0f)
{
    MemberFact member;
    member.name = name;
    member.yd = yd;
    member.dz = dz;
    member.x = yd;
    return member;
}

static UnitFact Unit(std::uint64_t guid, std::uint32_t entry, float dist)
{
    UnitFact unit;
    unit.guid = guid;
    unit.entry = entry;
    unit.distLead = dist;
    return unit;
}

static void ReportSizes(const char* label, const Facts& facts, const Run& run)
{
    FieldMap const factFields = EncodeFields(facts);
    FieldMap const runFields = EncodeFields(run);
    std::string const factKeyframe = FullObject(factFields);
    std::string const runKeyframe = FullObject(runFields);

    std::string const factDelta = DeltaObject(factFields, factFields);
    std::string const runDelta = DeltaObject(runFields, runFields);

    auto const start = std::chrono::steady_clock::now();
    std::size_t total = 0;
    for (int i = 0; i < 1000; ++i)
        total += EncodeFields(facts).size() + EncodeFields(run).size();
    auto const us = std::chrono::duration_cast<std::chrono::microseconds>(
                        std::chrono::steady_clock::now() - start)
                        .count();

    std::cout << label << ": facts keyframe " << factKeyframe.size() << " B (" << factFields.size()
              << " fields), run keyframe " << runKeyframe.size() << " B (" << runFields.size()
              << " fields), empty fact delta " << factDelta.size()
              << " B, empty run delta " << runDelta.size()
              << " B, 1000 encodes " << us << " us (" << (total ? total : 0) << " fields)\n";
}

static void ReportDeltas(const char* label, const Facts& before, const Facts& after, const Run& beforeRun,
                         const Run& afterRun)
{
    std::string const facts = DeltaObject(EncodeFields(before), EncodeFields(after));
    std::string const run = DeltaObject(EncodeFields(beforeRun), EncodeFields(afterRun));
    std::cout << label << ": facts delta " << facts.size() << " B, run delta " << run.size() << " B\n";
}

static void TestRealisticSizes()
{
    DungeonPlan plan;
    plan.name = "zul_gurub";
    plan.map = 309;
    plan.steps = {Step(PlanStepKind::Move, 0, 0, 0), PullStep(40, {4851, 4852}), Step(PlanStepKind::Move, 60)};
    Run run = TortoiseBots::DungeonRun::Start(&plan);
    run.liveTactics = nullptr;
    Facts facts = Party(0);
    facts.members = {Mem("Tanky", 0), Mem("Healy", 5), Mem("Stabby", 6), Mem("Nathil", 3), Mem("Jinkla", 9)};
    facts.units = {Unit(11, 4851, 45), Unit(12, 4852, 38), Unit(13, 4853, 41)};
    facts.maxMemberYd = 9;
    facts.anyInCombat = true;
    ReportSizes("5 members, 3 units", facts, run);
    {
        Facts moved = facts;
        moved.members[1].yd = 12.0f;
        moved.members[1].x = 12.0f;
        ReportDeltas("5/3 one member moved", facts, moved, run, run);
        Facts joined = facts;
        joined.units.push_back(Unit(14, 4851, 50));
        ReportDeltas("5/3 one unit joined", facts, joined, run, run);
        Run next = run;
        TortoiseBots::DungeonRun::Tick(next, facts);
        ReportDeltas("5/3 one tick", facts, facts, run, next);
    }

    Facts big = facts;
    for (int i = 0; i < 15; ++i)
        big.members.push_back(Mem("Bot" + std::to_string(i), 4.0f + static_cast<float>(i)));
    for (int i = 0; i < 27; ++i)
        big.units.push_back(Unit(100 + static_cast<std::uint64_t>(i), 4851, 30.0f + static_cast<float>(i)));
    big.patrollers = big.units;
    big.engagedUnits = big.units;
    ReportSizes("20 members, 30 units", big, run);
    {
        Facts moved = big;
        moved.members[1].yd = 12.0f;
        ReportDeltas("20/30 one member moved", big, moved, run, run);
        Facts joined = big;
        joined.units.push_back(Unit(300, 4851, 50));
        ReportDeltas("20/30 one unit joined", big, joined, run, run);
        Run next = run;
        TortoiseBots::DungeonRun::Tick(next, big);
        ReportDeltas("20/30 one tick", big, big, run, next);
    }
}

// -- 8b. a full recorder tick on a live-size plan -------------------------

struct CountingSink : Sink
{
    std::uint64_t bytes = 0;
    std::uint64_t pushes = 0;

    bool Push(std::string line) override
    {
        bytes += line.size();
        ++pushes;
        return true;
    }

    bool NewSegment(std::uint32_t, std::uint64_t) override
    {
        return true;
    }
};

static void TestRecorderTickTime()
{
    DungeonPlan plan;
    plan.name = "zul_gurub";
    plan.map = 309;
    for (int i = 0; i < 400; ++i)
        plan.steps.push_back(i % 3 == 1 ? PullStep(static_cast<float>(i % 50), {4851, 4852})
                                        : Step(PlanStepKind::Move, static_cast<float>(i % 50), float(i % 7)));
    Run run = TortoiseBots::DungeonRun::Start(&plan);
    run.liveTactics = nullptr;

    Facts facts = Party(0);
    for (int i = 0; i < 20; ++i)
        facts.members.push_back(Mem("Bot" + std::to_string(i), 4.0f + static_cast<float>(i)));
    for (int i = 0; i < 30; ++i)
        facts.units.push_back(Unit(100 + static_cast<std::uint64_t>(i), 4851, 30.0f + static_cast<float>(i)));
    facts.patrollers = facts.units;
    facts.engagedUnits = facts.units;
    facts.maxMemberYd = 23;

    // The plan field carries the cached hash, and a same-address content change
    // is noticed rather than served from the cache.
    FieldMap const runFields = EncodeFields(run);
    CHECK(*FindField(runFields, "plan") ==
          "{\"name\":\"zul_gurub\",\"hash\":\"" + PlanHash(plan) + "\"}");
    std::string const before = CachedPlanHash(&plan);
    plan.name = "zul_gurub_reloaded";
    CHECK(CachedPlanHash(&plan) != before);
    plan.name = "zul_gurub";

    CountingSink sink;
    Limits limits;
    HeaderInfo header;
    header.captureId = "20261102T161939Z-zul_gurub-Fionaa";
    header.commander = "Fionaa";
    header.module = "test";
    Recorder recorder(header, limits, sink);

    int const ticks = 1000;
    auto const start = std::chrono::steady_clock::now();
    std::uint32_t minCapUs = std::numeric_limits<std::uint32_t>::max();
    for (int i = 0; i < ticks; ++i)
    {
        // A live run's facts churn every tick: members and units move.
        facts.members[i % 20].yd = 4.0f + static_cast<float>(i % 7);
        facts.units[i % 30].distLead = 30.0f + static_cast<float>(i % 11);
        recorder.BeforeTick(run, facts, 1000, 2000 + static_cast<std::uint64_t>(i));
        recorder.AfterTick(run, "move 40,0 0,0");
        minCapUs = std::min(minCapUs, recorder.stats().lastCapUs);
    }
    auto const us = std::chrono::duration_cast<std::chrono::microseconds>(
                        std::chrono::steady_clock::now() - start)
                        .count();
    double const perTick = static_cast<double>(us) / ticks;
    std::cout << "400-step plan, 20 members, 30 units: recorder tick " << perTick << " us ("
              << sink.bytes / static_cast<std::uint64_t>(ticks) << " B/tick, min capUs "
              << minCapUs << ")\n";
    // The budget is 2 ms per tick. The encode alone must sit well under it:
    // hashing the 400-step plan on every tick costs about 1.1 ms, so the
    // cheapest tick is the one that catches a missing per-segment cache (a
    // busy machine inflates single ticks, so the fastest is the fair sample).
    CHECK(perTick < 2000.0);
    CHECK(minCapUs < 1000);
}

// -- 9. decode errors ----------------------------------------------------

static void TestDecodeErrors()
{
    Facts facts;
    std::string object = FullObject(EncodeFields(facts));
    object.insert(object.size() - 1, ",\"bogus\":1");
    MiniJson::Value const value = ParseText(object);
    Facts back;
    std::string error;
    CHECK(!DecodeFields(value, back, error));
    CHECK(error.find("bogus") != std::string::npos);

    DungeonPlan plan;
    plan.name = "a";
    DungeonPlan sameName;
    sameName.name = "a"; // same name, different body: only the hash differs
    sameName.map = 7;
    Run run;
    run.plan = &plan;
    FieldMap const fields = EncodeFields(run);
    MiniJson::Value const runValue = ParseText(FullObject(fields));
    Run decoded;
    CHECK(!DecodeFields(runValue, decoded, &sameName, nullptr, error));
    CHECK(error.find("plan") != std::string::npos);

    // A null plan is only valid when the capture has none.
    {
        Run nullPlan;
        Run nullDecoded;
        MiniJson::Value const value = ParseText(FullObject(EncodeFields(nullPlan)));
        CHECK(!DecodeFields(value, nullDecoded, &plan, nullptr, error));
        CHECK(error.find("plan") != std::string::npos);
        CHECK(DecodeFields(value, nullDecoded, nullptr, nullptr, error));
    }

    // Integer ranges, enum membership and float overflow are errors, not
    // silent wraps.
    {
        std::uint8_t small = 0;
        std::int8_t signedSmall = 0;
        std::uint64_t big = 0;
        CHECK(!DecodeValue(ParseText("300"), small, DecodeContext{}, error));
        CHECK(!DecodeValue(ParseText("128"), signedSmall, DecodeContext{}, error));
        CHECK(!DecodeValue(ParseText("-1"), big, DecodeContext{}, error));
        CHECK(!DecodeValue(ParseText("18446744073709551616"), big, DecodeContext{}, error));
        CHECK(DecodeValue(ParseText("255"), small, DecodeContext{}, error));

        Phase phase = Phase::Begin;
        CHECK(!DecodeValue(ParseText("99"), phase, DecodeContext{}, error));
        CHECK(error.find("enumerator") != std::string::npos);
        CHECK(DecodeValue(ParseText("7"), phase, DecodeContext{}, error));

        double huge = 0;
        CHECK(!DecodeValue(ParseText("1e400"), huge, DecodeContext{}, error));
        float hugeFloat = 0;
        CHECK(!DecodeValue(ParseText("1e40"), hugeFloat, DecodeContext{}, error));
        // A subnormal is not an overflow: the printed form round-trips.
        float tiny = 0;
        CHECK(DecodeValue(ParseText("1.40129846e-45"), tiny, DecodeContext{}, error));
        CHECK(tiny > 0.0f);
    }

    // A whole plan file round-trips, and a wrong hash is refused.
    MiniJson::Value const planValue = ParseText(EncodePlan(plan));
    DungeonPlan planBack;
    CHECK(DecodePlan(planValue, planBack, error));
    CHECK(planBack.name == plan.name && planBack.map == plan.map);
    MiniJson::Value broken = planValue;
    for (auto& entry : broken.obj)
        if (entry.first == "hash")
            entry.second.text = "0000000000000000";
    CHECK(!DecodePlan(broken, planBack, error));
}

// -- RowsOnMap -----------------------------------------------------------

static void TestRowsOnMap()
{
    ai::BossTacticTable table;
    CHECK(table.RowsOnMap(309).empty());

    ai::BossTacticRow later;
    later.map = 309;
    later.bossEntry = 14517;
    later.kind = ai::BossTacticKind::KillFirst;
    later.targetEntry = 14532;
    ai::BossTacticRow earlier = later;
    earlier.bossEntry = 14515;
    ai::BossTacticRow anotherMap = later;
    anotherMap.map = 33;
    // Add order is not the output order.
    CHECK(table.Add(later));
    CHECK(table.Add(earlier));
    CHECK(table.Add(anotherMap));
    CHECK(table.RowsOnMap(309).empty()); // not sealed
    table.Seal();
    std::vector<ai::BossTacticRow> const rows = table.RowsOnMap(309);
    CHECK(rows.size() == 2);
    CHECK(rows[0].bossEntry == 14515 && rows[1].bossEntry == 14517);
    CHECK(table.RowsOnMap(33).size() == 1);
    CHECK(table.RowsOnMap(1).empty());

    std::string const tactics = EncodeTactics(&table, 309);
    CHECK(tactics.find("\"set\":true") == 1);
    MiniJson::Value const value = ParseText(tactics);
    ai::BossTacticTable rebuilt;
    bool set = false;
    std::string error;
    CHECK(DecodeTactics(value, rebuilt, set, error));
    CHECK(set);
    CHECK(rebuilt.RowsOnMap(309).size() == 2);
    CHECK(EncodeTactics(&rebuilt, 309) == tactics);
    CHECK(EncodeTactics(nullptr, 309) == "{\"set\":false,\"rows\":[]}");
}

// A cached tactics text is keyed by the table's revision: a table destroyed and a changed table built at the same address
// (placement new) must give the new table's text and hash, not the freed table's.
static void TestTacticsAddressReuse()
{
    alignas(ai::BossTacticTable) unsigned char storage[sizeof(ai::BossTacticTable)];
    ai::BossTacticRow row;
    row.map = 309;
    row.bossEntry = 14517;
    row.kind = ai::BossTacticKind::KillFirst;
    row.targetEntry = 14532;

    auto* first = new (storage) ai::BossTacticTable();
    CHECK(first->Add(row));
    first->Seal();
    std::string const before = CachedTacticsText(first, 309);
    std::string const hashBefore = CachedTacticsHash(first, 309);
    CHECK(before == EncodeTactics(first, 309));
    first->~BossTacticTable();

    row.targetEntry = 14533;  // the reloaded table lists another target
    auto* second = new (storage) ai::BossTacticTable();
    CHECK(second == first);
    CHECK(second->Add(row));
    second->Seal();
    CHECK(CachedTacticsText(second, 309) == EncodeTactics(second, 309));
    CHECK(CachedTacticsText(second, 309) != before);
    CHECK(CachedTacticsHash(second, 309) != hashBefore);
    CHECK(CachedTacticsHash(second, 309) == Hex64(Fnv1a64(EncodeTactics(second, 309))));
    second->~BossTacticTable();
}

// Destroying a plan table moves the generation the plan hash cache keys on; so does sealing (steps are sorted in place).
// A plan rebuilt in freed memory with other steps is hashed from its own content.
static void TestPlanTableGeneration()
{
    std::uint64_t const start = ai::DungeonPlanTableGeneration().load();
    alignas(ai::DungeonPlanTable) unsigned char storage[sizeof(ai::DungeonPlanTable)];

    auto* first = new (storage) ai::DungeonPlanTable();
    CHECK(ai::DungeonPlanTableGeneration().load() == start);
    CHECK(first->AddPlan("zul_gurub", 309));
    CHECK(first->AddStep("zul_gurub", Step(PlanStepKind::Move, 1, 2)) == ai::PlanStepRefusal::None);
    first->Seal();
    CHECK(ai::DungeonPlanTableGeneration().load() == start + 1);
    std::string const hashBefore = CachedPlanHash(first->Find("zul_gurub"));
    CHECK(hashBefore == PlanHash(*first->Find("zul_gurub")));
    first->~DungeonPlanTable();
    CHECK(ai::DungeonPlanTableGeneration().load() == start + 2);

    auto* second = new (storage) ai::DungeonPlanTable();
    CHECK(second->AddPlan("zul_gurub", 309));
    CHECK(second->AddStep("zul_gurub", Step(PlanStepKind::Move, 1, 3)) == ai::PlanStepRefusal::None);
    second->Seal();
    const ai::DungeonPlan* plan = second->Find("zul_gurub");
    CHECK(plan != nullptr);
    CHECK(CachedPlanHash(plan) == PlanHash(*plan));
    CHECK(CachedPlanHash(plan) != hashBefore);
    second->~DungeonPlanTable();
}

int main()
{
    TestSamples();
    TestFloats();
    TestIntegers();
    TestStrings();
    TestContainerOrder();
    TestDeltas();
    TestSchema();
    TestRealisticSizes();
    TestRecorderTickTime();
    TestDecodeErrors();
    TestRowsOnMap();
    TestTacticsAddressReuse();
    TestPlanTableGeneration();
    std::cout << "test_decision_capture_codec: ok\n";
    return 0;
}
