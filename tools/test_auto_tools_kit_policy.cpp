// Standalone regression test for the auto-tools kit policy (owner
// request): bots auto-carry the items and skills their behaviours need.
// Server data, not guesses: 5060 Thieves' Tools (req 15), lockpicking
// 5/level cap 300, 19183 Hourglass Sand, Bronze 23170, BWL map 469,
// 17333 Aqual / 22754 Eternal Quintessence, MC map 409.
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra tools/test_auto_tools_kit_policy.cpp -o /tmp/test_auto_tools_kit
//   /tmp/test_auto_tools_kit

#include "../runtime/AutoToolsKitPolicy.h"

#include <cstdio>
#include <cstdlib>

using namespace TortoiseBots;

static int checks = 0;
#define CHECK(cond) do { \
    ++checks; \
    if (!(cond)) { \
        std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        std::exit(1); \
    } \
} while (0)

// Lockpicking is max-for-level: 5 per level, capped at 300.
static void TestLockpickCurve()
{
    CHECK(THIEVES_TOOLS_ITEM_ID == 5060);
    CHECK(LOCKPICK_SKILL_CAP == 300);
    CHECK(LockpickSkillForLevel(1) == 5);
    CHECK(LockpickSkillForLevel(15) == 75);
    CHECK(LockpickSkillForLevel(59) == 295);
    CHECK(LockpickSkillForLevel(60) == 300);
    CHECK(LockpickSkillForLevel(61) == 300);
}

// Only rogues 15+ want the skill and the tools.
static void TestRogueGates()
{
    CHECK(RogueWantsLockpickSkill(4, 15));
    CHECK(RogueWantsLockpickSkill(4, 60));
    CHECK(!RogueWantsLockpickSkill(4, 14));
    CHECK(!RogueWantsLockpickSkill(4, 1));
    CHECK(!RogueWantsLockpickSkill(3, 60));
    CHECK(!RogueWantsLockpickSkill(9, 60));
    CHECK(RogueWantsThievesTools(4, 15));
    CHECK(!RogueWantsThievesTools(4, 14));
    CHECK(!RogueWantsThievesTools(1, 60));
}

// Hourglass Sand: only Bronze carriers holding none.
static void TestHourglassSand()
{
    CHECK(HOURGLASS_SAND_ITEM_ID == 19183);
    CHECK(BRONZE_AFFLICTION_SPELL_ID == 23170);
    CHECK(BWL_MAP_ID == 469);
    CHECK(ShouldEnsureHourglassSand(true, 0));
    CHECK(!ShouldEnsureHourglassSand(true, 1));
    CHECK(!ShouldEnsureHourglassSand(true, 200));
    CHECK(!ShouldEnsureHourglassSand(false, 0));
}

// MC Quintessence: only inside MC holding neither kind.
static void TestQuintessence()
{
    CHECK(AQUAL_QUINTESSENCE_ITEM_ID == 17333);
    CHECK(ETERNAL_QUINTESSENCE_ITEM_ID == 22754);
    CHECK(MC_MAP_ID == 409);
    CHECK(ShouldEnsureQuintessence(409, 0, 0));
    CHECK(!ShouldEnsureQuintessence(409, 1, 0));
    CHECK(!ShouldEnsureQuintessence(409, 0, 1));
    CHECK(!ShouldEnsureQuintessence(469, 0, 0));
    CHECK(!ShouldEnsureQuintessence(0, 0, 0));
    CHECK(QuintessenceEnsureId(60) == 22754);
    CHECK(QuintessenceEnsureId(59) == 17333);
}

int main()
{
    std::printf("Starting TortoiseBots auto-tools kit policy tests...\n");
    TestLockpickCurve();
    std::printf("  [PASS] lockpick curve pinned\n");
    TestRogueGates();
    std::printf("  [PASS] rogue skill/tool gates\n");
    TestHourglassSand();
    std::printf("  [PASS] hourglass sand ensure\n");
    TestQuintessence();
    std::printf("  [PASS] quintessence ensure\n");
    std::printf("All auto-tools kit policy tests passed (%d checks).\n", checks);
    return 0;
}
