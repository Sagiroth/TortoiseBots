// Standalone check for runtime/DecisionCaptureCodec.h (decision capture C1):
//   g++ -std=c++17 -Wall -Wextra tools/test_decision_capture_codec.cpp -o /tmp/t && /tmp/t
//
// The samples come from the generated header, one per field: decoding them into
// a default struct and re-encoding must give the sample text back, field for
// field. That is the check that no field is silently missing from the encoder
// or the decoder, whatever the field's type.
#include "../runtime/DecisionCaptureCodec.h"

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
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
    FieldMap const base = EncodeFields(facts);
    CHECK(DeltaObject(base, base) == "{}");

    Facts changed = facts;
    MemberFact member;
    member.name = "Nathil";
    member.yd = 3.0f;
    changed.members.push_back(member);
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
    CHECK(std::string(kDecisionSchemaText).find("enum Phase: Begin,Gather,Walk") != std::string::npos);
    CHECK(std::string(kDecisionSchemaText).find("enum CombatDropKind: Unattackable") != std::string::npos);
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

    Facts big = facts;
    for (int i = 0; i < 15; ++i)
        big.members.push_back(Mem("Bot" + std::to_string(i), 4.0f + static_cast<float>(i)));
    for (int i = 0; i < 27; ++i)
        big.units.push_back(Unit(100 + static_cast<std::uint64_t>(i), 4851, 30.0f + static_cast<float>(i)));
    big.patrollers = big.units;
    big.engagedUnits = big.units;
    ReportSizes("20 members, 30 units", big, run);
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
    DungeonPlan other;
    other.name = "b";
    Run run;
    run.plan = &plan;
    FieldMap const fields = EncodeFields(run);
    MiniJson::Value const runValue = ParseText(FullObject(fields));
    Run decoded;
    CHECK(!DecodeFields(runValue, decoded, &other, nullptr, error));
    CHECK(error.find("plan") != std::string::npos);

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
    TestDecodeErrors();
    TestRowsOnMap();
    std::cout << "test_decision_capture_codec: ok\n";
    return 0;
}
