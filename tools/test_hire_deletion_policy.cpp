// Standalone regression test for the hired-companion deletion guard: a
// dismissed hire's character is deleted, and NOTHING else ever is. Guards the
// rules that keep player characters and untouched pool characters safe.
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra tools/test_hire_deletion_policy.cpp -o /tmp/test_hire_deletion
//   /tmp/test_hire_deletion

#include "../runtime/HireDeletionPolicy.h"

#include <cstdio>
#include <cstdlib>
#include <string>

using namespace TortoiseBots;

static int checks = 0;
#define CHECK(cond) do { \
    ++checks; \
    if (!(cond)) { \
        std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        std::exit(1); \
    } \
} while (0)

// The only allowed combination: the module created the character (ledger row),
// it sits on a registered managed pool account, and the database agrees.
static void TestAllowed()
{
    CHECK(DecideHireDeletion(true, true, true) == HireDeletionDecision::Allowed);
}

// A player character: no ledger row (Hire() never created it), and its account
// is not a managed pool account. Both orders must refuse.
static void TestPlayerCharacter()
{
    CHECK(DecideHireDeletion(false, false, true) == HireDeletionDecision::RefusedNoHireLedger);
    CHECK(DecideHireDeletion(false, false, false) == HireDeletionDecision::RefusedNoHireLedger);
}

// A pool character that Hire() did not create (roaming bot, auto-created bot,
// or one claimed with .bot add): no ledger row, so it is never deleted.
static void TestUntouchedPoolCharacter()
{
    CHECK(DecideHireDeletion(false, true, true) == HireDeletionDecision::RefusedNoHireLedger);
}

// A ledger row whose account is no longer (or was never) a managed pool
// account: refuse instead of trusting the row alone.
static void TestAccountNotManaged()
{
    CHECK(DecideHireDeletion(true, false, true) == HireDeletionDecision::RefusedAccountNotManaged);
    CHECK(DecideHireDeletion(true, false, false) == HireDeletionDecision::RefusedAccountNotManaged);
}

// The character moved to another account since the ledger row was written:
// refuse, the ledger no longer describes this character.
static void TestAccountMismatch()
{
    CHECK(DecideHireDeletion(true, true, false) == HireDeletionDecision::RefusedAccountMismatch);
}

static void TestDecisionNames()
{
    CHECK(std::string(HireDeletionDecisionName(HireDeletionDecision::Allowed)) == "allowed");
    CHECK(std::string(HireDeletionDecisionName(HireDeletionDecision::RefusedNoHireLedger)).find("no hire ledger") != std::string::npos);
    CHECK(std::string(HireDeletionDecisionName(HireDeletionDecision::RefusedAccountNotManaged)).find("not a managed pool account") != std::string::npos);
    CHECK(std::string(HireDeletionDecisionName(HireDeletionDecision::RefusedAccountMismatch)).find("differs") != std::string::npos);
}

int main()
{
    TestAllowed();
    TestPlayerCharacter();
    TestUntouchedPoolCharacter();
    TestAccountNotManaged();
    TestAccountMismatch();
    TestDecisionNames();
    std::printf("hire deletion policy: %d checks passed\n", checks);
    return 0;
}
