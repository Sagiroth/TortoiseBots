// Standalone regression test for the issue #473 opt-in owned/hired-bot
// quality of life: pool bots are never eligible, every part is gated on its
// config flag, and the autogear argument parser clamps to the caps.
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra tools/test_owned_bot_qol_policy.cpp -o /tmp/test_owned_bot_qol
//   /tmp/test_owned_bot_qol

#include "../runtime/OwnedBotQolPolicy.h"

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

static void TestOwnedBotAllowed()
{
    CHECK(DecideOwnedBotQol(true, false, false, true) == OwnedBotQolDecision::Allowed);
}

static void TestHiredBotNeedsActiveHire()
{
    // Hired companions live on pool accounts (random=true): eligible only
    // while the hire is active.
    CHECK(DecideOwnedBotQol(true, true, true, true) == OwnedBotQolDecision::Allowed);
    CHECK(DecideOwnedBotQol(true, true, false, true) == OwnedBotQolDecision::RefusedPoolBot);
}

static void TestPoolBotRefused()
{
    CHECK(DecideOwnedBotQol(true, true, false, true) == OwnedBotQolDecision::RefusedPoolBot);
    CHECK(DecideOwnedBotQol(false, false, false, true) == OwnedBotQolDecision::RefusedPoolBot);
}

static void TestFeatureOff()
{
    CHECK(DecideOwnedBotQol(true, false, false, false) == OwnedBotQolDecision::RefusedFeatureOff);
    CHECK(DecideOwnedBotQol(true, true, true, false) == OwnedBotQolDecision::RefusedFeatureOff);
}

static void TestAutogearDefaults()
{
    OwnedBotAutogearRequest request = ParseOwnedBotAutogearArg("", 2, 0);
    CHECK(request.ok && request.quality == 2 && request.ilvlCap == 0);
}

static void TestAutogearQualityClamped()
{
    OwnedBotAutogearRequest request = ParseOwnedBotAutogearArg("epic", 2, 0);
    CHECK(request.ok && request.quality == 2);
    request = ParseOwnedBotAutogearArg("green", 2, 0);
    CHECK(request.ok && request.quality == 2);
    request = ParseOwnedBotAutogearArg("white", 2, 0);
    CHECK(request.ok && request.quality == 1);
}

static void TestAutogearIlvlClamped()
{
    OwnedBotAutogearRequest request = ParseOwnedBotAutogearArg("200", 2, 100);
    CHECK(request.ok && request.ilvlCap == 100);
    request = ParseOwnedBotAutogearArg("80", 2, 0);
    CHECK(request.ok && request.ilvlCap == 80);
}

static void TestAutogearQualityAsNumber()
{
    OwnedBotAutogearRequest request = ParseOwnedBotAutogearArg("4", 2, 0);
    CHECK(!request.ok && request.error != nullptr);
}

static void TestAutogearGarbage()
{
    OwnedBotAutogearRequest request = ParseOwnedBotAutogearArg("bis", 2, 0);
    CHECK(!request.ok && request.error != nullptr);
}

int main()
{
    TestOwnedBotAllowed();
    TestHiredBotNeedsActiveHire();
    TestPoolBotRefused();
    TestFeatureOff();
    TestAutogearDefaults();
    TestAutogearQualityClamped();
    TestAutogearIlvlClamped();
    TestAutogearQualityAsNumber();
    TestAutogearGarbage();
    std::printf("owned bot qol policy: %d checks passed\n", checks);
    return 0;
}
