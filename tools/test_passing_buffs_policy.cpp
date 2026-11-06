// Meeting-driven buffs from pool bots to real players (issue #1531):
// spell choice, chance gates, cooldown bookkeeping and meeting detection.
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra tools/test_passing_buffs_policy.cpp -o /tmp/test_passing_buffs
//   /tmp/test_passing_buffs

#include "../ai/playerbot/PassingBuffsPolicy.h"

#include <string>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

using namespace ai;

static int checks = 0;
#define CHECK(cond) do { \
    ++checks; \
    if (!(cond)) { \
        std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        std::exit(1); \
    } \
} while (0)

static void TestSpellChoice()
{
    CHECK(std::string(PassingBuffName(PassingBuffClass::Priest, true)) == "power word: fortitude");
    CHECK(std::string(PassingBuffName(PassingBuffClass::Druid, false)) == "mark of the wild");
    CHECK(std::string(PassingBuffName(PassingBuffClass::Mage, true)) == "arcane intellect");
    CHECK(PassingBuffName(PassingBuffClass::Mage, false) == nullptr);
    CHECK(PassingBuffName(PassingBuffClass::None, true) == nullptr);
    CHECK(std::string(PassingHealName(PassingBuffClass::Priest)) == "renew");
    CHECK(std::string(PassingHealName(PassingBuffClass::Druid)) == "rejuvenation");
    CHECK(PassingHealName(PassingBuffClass::Mage) == nullptr);
    CHECK(PassingHealName(PassingBuffClass::Paladin) == nullptr);
    std::printf("  [PASS] spell choice\n");
}

static void TestBlessingChoice()
{
    // Tanks: paladin tanks keep Wisdom, everyone else gets Kings.
    CHECK(std::string(PassingBlessingName(true, false, false, 2)) == "blessing of wisdom");
    CHECK(std::string(PassingBlessingName(true, false, false, 1)) == "blessing of kings");
    // Healers: Wisdom.
    CHECK(std::string(PassingBlessingName(false, true, false, 5)) == "blessing of wisdom");
    // Ranged: hunters keep Wisdom, other casters get Kings.
    CHECK(std::string(PassingBlessingName(false, false, true, 3)) == "blessing of wisdom");
    CHECK(std::string(PassingBlessingName(false, false, true, 8)) == "blessing of kings");
    CHECK(std::string(PassingBlessingName(false, false, true, 5)) == "blessing of wisdom");
    // Melee: paladins keep Wisdom, other melee get Might.
    CHECK(std::string(PassingBlessingName(false, false, false, 2)) == "blessing of wisdom");
    CHECK(std::string(PassingBlessingName(false, false, false, 1)) == "blessing of might");
    std::printf("  [PASS] blessing choice\n");
}

static void TestChanceGates()
{
    // 1 in 5 buff: rolls 0, 5, 10 win; 1-4 lose.
    CHECK(PassingRollWon(0, kPassingBuffChanceDivisor));
    CHECK(!PassingRollWon(1, kPassingBuffChanceDivisor));
    CHECK(!PassingRollWon(4, kPassingBuffChanceDivisor));
    CHECK(PassingRollWon(5, kPassingBuffChanceDivisor));
    // 1 in 2 heal: even rolls win.
    CHECK(PassingRollWon(0, kPassingHealChanceDivisor));
    CHECK(!PassingRollWon(1, kPassingHealChanceDivisor));
    CHECK(PassingRollWon(2, kPassingHealChanceDivisor));
    // Degenerate divisors never block.
    CHECK(PassingRollWon(7, 0));
    CHECK(PassingRollWon(7, 1));
    std::printf("  [PASS] chance gates\n");
}

static void TestCooldowns()
{
    // Never cast: ready. Just cast: not ready until the window passes.
    CHECK(PassingCooldownReady(0, 1000, kPassingBuffCooldownSec));
    CHECK(!PassingCooldownReady(1000, 1000 + kPassingBuffCooldownSec - 1, kPassingBuffCooldownSec));
    CHECK(PassingCooldownReady(1000, 1000 + kPassingBuffCooldownSec, kPassingBuffCooldownSec));
    CHECK(!PassingCooldownReady(1000, 1000 + kPassingHealCooldownSec - 1, kPassingHealCooldownSec));
    CHECK(PassingCooldownReady(1000, 1000 + kPassingHealCooldownSec, kPassingHealCooldownSec));
    std::printf("  [PASS] cooldowns\n");
}

static void TestHealBand()
{
    CHECK(PassingHealWanted(34, false));
    CHECK(!PassingHealWanted(35, false));
    CHECK(!PassingHealWanted(10, true));
    CHECK(!PassingHealWanted(100, false));
    std::printf("  [PASS] heal band\n");
}

static void TestMeeting()
{
    CHECK(PassingIsNewMeeting(false));
    CHECK(!PassingIsNewMeeting(true));
    CHECK(PassingInMeetingRange(10.0f, 60.0f));
    CHECK(PassingInMeetingRange(30.0f, 60.0f));
    CHECK(!PassingInMeetingRange(30.1f, 60.0f));
    CHECK(!PassingInMeetingRange(-1.0f, 60.0f));
    CHECK(!PassingInMeetingRange(10.0f, 5.0f));
    std::printf("  [PASS] meeting detection\n");
}

int main()
{
    std::printf("Starting TortoiseBots passing-buffs policy tests...\n");
    TestSpellChoice();
    TestBlessingChoice();
    TestChanceGates();
    TestCooldowns();
    TestHealBand();
    TestMeeting();
    std::printf("All passing-buffs policy tests passed (%d checks).\n", checks);
    return 0;
}
