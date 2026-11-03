// Standalone regression test for issue #265: managed random-bot pool reset
// policy. Exercises the pure decision rules that must not regress — setting
// parsing, one-shot generation comparison, username normalization/prefix
// matching, and the adoption challenge — without a database or a running core.
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra tools/test_pool_reset_policy.cpp -o /tmp/test_pool_reset
//   /tmp/test_pool_reset

#include "../runtime/policy/infra/PoolResetPolicy.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace TortoiseBots;

static int checks = 0;
#define CHECK(cond) do { \
    ++checks; \
    if (!(cond)) { \
        std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        std::exit(1); \
    } \
} while (0)

static void TestSettingParsing()
{
    // Default and explicit off: never schedule deletion.
    CHECK(ParsePoolResetSetting("").mode == PoolResetMode::Off);
    CHECK(ParsePoolResetSetting("  ").mode == PoolResetMode::Off);
    CHECK(ParsePoolResetSetting("off").mode == PoolResetMode::Off);
    CHECK(ParsePoolResetSetting("OFF").mode == PoolResetMode::Off);
    CHECK(ParsePoolResetSetting(" off ").mode == PoolResetMode::Off);

    // Development mode.
    CHECK(ParsePoolResetSetting("always").mode == PoolResetMode::Always);
    CHECK(ParsePoolResetSetting("Always").mode == PoolResetMode::Always);

    // One-shot tokens keep their exact case: the token is data.
    PoolResetSetting once = ParsePoolResetSetting("once:gear-seeding-v2");
    CHECK(once.mode == PoolResetMode::Once);
    CHECK(once.token == "gear-seeding-v2");
    CHECK(ParsePoolResetSetting("ONCE:GearV2").mode == PoolResetMode::Once);
    CHECK(ParsePoolResetSetting("ONCE:GearV2").token == "GearV2");

    // Invalid values fail closed.
    CHECK(ParsePoolResetSetting("once:").mode == PoolResetMode::Invalid);
    CHECK(ParsePoolResetSetting("once:   ").mode == PoolResetMode::Invalid);
    CHECK(!ParsePoolResetSetting("once:").error.empty());
    CHECK(ParsePoolResetSetting("yes").mode == PoolResetMode::Invalid);
    CHECK(ParsePoolResetSetting("once").mode == PoolResetMode::Invalid);
    CHECK(ParsePoolResetSetting("1").mode == PoolResetMode::Invalid);

    // Token bounds.
    CHECK(ParsePoolResetSetting("once:" + std::string(kPoolResetTokenMaxLength, 'a')).mode == PoolResetMode::Once);
    CHECK(ParsePoolResetSetting("once:" + std::string(kPoolResetTokenMaxLength + 1, 'a')).mode == PoolResetMode::Invalid);
    CHECK(ParsePoolResetSetting("once:two words").mode == PoolResetMode::Invalid);
    CHECK(ParsePoolResetSetting("once:tab\there").mode == PoolResetMode::Invalid);
}

static void TestGenerationDecision()
{
    PoolResetSetting off = ParsePoolResetSetting("off");
    CHECK(!ShouldResetForGeneration(off, ""));
    CHECK(!ShouldResetForGeneration(off, "anything"));

    PoolResetSetting always = ParsePoolResetSetting("always");
    CHECK(ShouldResetForGeneration(always, ""));
    CHECK(ShouldResetForGeneration(always, "gear-v2"));

    PoolResetSetting once = ParsePoolResetSetting("once:gear-v2");
    CHECK(ShouldResetForGeneration(once, ""));          // never applied
    CHECK(ShouldResetForGeneration(once, "gear-v1"));   // different generation
    CHECK(!ShouldResetForGeneration(once, "gear-v2"));  // already applied: no-op
    CHECK(ShouldResetForGeneration(once, "Gear-V2"));   // case-sensitive comparison
    CHECK(!ShouldResetForGeneration(ParsePoolResetSetting("once:always"), "always"));

    PoolResetSetting invalid = ParsePoolResetSetting("bogus");
    CHECK(!ShouldResetForGeneration(invalid, ""));
}

static void TestUsernameRules()
{
    CHECK(NormalizeAccountUsername("  RNDBOT000123 ") == "rndbot000123");
    CHECK(AccountUsernameEquals("RNDBOT000123", "rndbot000123"));
    CHECK(!AccountUsernameEquals("rndbot000123", "rndbot000124"));

    // Prefix matching is case-insensitive and never matches an empty prefix.
    CHECK(AccountUsernameHasPrefix("RNDBOTPersonal", "rndbot"));
    CHECK(AccountUsernameHasPrefix("rndbot000123", "RNDBOT"));
    CHECK(!AccountUsernameHasPrefix("personal", "rndbot"));
    CHECK(!AccountUsernameHasPrefix("anything", ""));
    // A prefix that resembles SQL wildcards stays a literal string.
    CHECK(!AccountUsernameHasPrefix("admin", "%"));
    CHECK(AccountUsernameHasPrefix("%admin", "%"));
}

static void TestAdoptionChallenge()
{
    std::vector<std::pair<uint32_t, std::string>> accounts = {
        { 12, "RNDBOT000012" },
        { 7, "RNDBOT000007" },
    };

    std::string challenge = AdoptionChallenge(accounts);
    CHECK(challenge.size() == 12);

    // Order independent: the challenge is derived from the set, not the list.
    std::vector<std::pair<uint32_t, std::string>> reordered = {
        { 7, "rndbot000007" },
        { 12, "RNDBOT000012" },
    };
    CHECK(AdoptionChallenge(reordered) == challenge);

    // Any change to the previewed set invalidates the challenge.
    std::vector<std::pair<uint32_t, std::string>> added = accounts;
    added.emplace_back(99, "RNDBOT000099");
    CHECK(AdoptionChallenge(added) != challenge);

    std::vector<std::pair<uint32_t, std::string>> renamed = {
        { 12, "RNDBOT000012X" },
        { 7, "RNDBOT000007" },
    };
    CHECK(AdoptionChallenge(renamed) != challenge);

    CHECK(AdoptionChallenge({}) != challenge);
    CHECK(AdoptionChallenge({}) == AdoptionChallenge({}));

    // Comparison tolerates operator casing but not a different value.
    CHECK(AdoptionChallengeMatches(challenge, challenge));
    std::string lower = challenge;
    for (char& c : lower)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    CHECK(AdoptionChallengeMatches(challenge, lower));
    CHECK(!AdoptionChallengeMatches(challenge, "000000000000"));
    CHECK(!AdoptionChallengeMatches(challenge, ""));
    CHECK(!AdoptionChallengeMatches("", ""));
}

static void TestAccountScanVerdict()
{
    // The core returns a null result for zero rows and for a failed query
    // alike; the COUNT probe's answer decides between the two.
    CHECK(VerdictForAccountScanProbe(false, 0) == AccountScanVerdict::Failed);
    CHECK(VerdictForAccountScanProbe(false, 42) == AccountScanVerdict::Failed);
    CHECK(VerdictForAccountScanProbe(true, 0) == AccountScanVerdict::Empty);
    CHECK(VerdictForAccountScanProbe(true, 1) == AccountScanVerdict::Populate);
    CHECK(VerdictForAccountScanProbe(true, 5000) == AccountScanVerdict::Populate);
}

static void TestModeNames()
{
    CHECK(std::string(PoolResetModeName(PoolResetMode::Off)) == "off");
    CHECK(std::string(PoolResetModeName(PoolResetMode::Once)) == "once");
    CHECK(std::string(PoolResetModeName(PoolResetMode::Always)) == "always");
    CHECK(std::string(PoolResetModeName(PoolResetMode::Invalid)) == "invalid");
}

int main()
{
    TestSettingParsing();
    TestGenerationDecision();
    TestUsernameRules();
    TestAccountScanVerdict();
    TestAdoptionChallenge();
    TestModeNames();
    std::printf("OK: %d checks passed\n", checks);
    return 0;
}
