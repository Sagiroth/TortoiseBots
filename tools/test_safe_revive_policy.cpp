#include "../ai/playerbot/policy/death/SafeRevivePolicy.h"

#include <cstdio>
#include <cstdlib>

using namespace TortoiseBots;

static int checks = 0;
#define CHECK(cond) do { ++checks; if (!(cond)) { std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); std::exit(1); } } while (0)

int main()
{
    CHECK(DecideSafeRevive(0) == SafeReviveDecision::Revive);
    CHECK(DecideSafeRevive(1) == SafeReviveDecision::Wait);
    CHECK(DecideSafeRevive(5) == SafeReviveDecision::Wait);

    CHECK(!SafeReviveWaitExpired(0, 999999999u));
    CHECK(!SafeReviveWaitExpired(1000, 1000));
    CHECK(!SafeReviveWaitExpired(1000, 1000 + SAFE_REVIVE_MAX_WAIT_MS - 1));
    CHECK(SafeReviveWaitExpired(1000, 1000 + SAFE_REVIVE_MAX_WAIT_MS));
    // world timer wrap
    CHECK(SafeReviveWaitExpired(0xFFFFFF00u, 0xFFFFFF00u + SAFE_REVIVE_MAX_WAIT_MS));

    std::printf("test_safe_revive_policy: %d checks passed\n", checks);
    return 0;
}
