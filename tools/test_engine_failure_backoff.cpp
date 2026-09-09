// Standalone regression test for issue #84: ActionFailureBackoff policy math.
// Self-contained (mocks nothing - the header is pure std). Build and run:
//   g++ -std=c++17 -Wall -Wextra -I ai/playerbot/strategy tools/test_engine_failure_backoff.cpp -o /tmp/test_backoff && /tmp/test_backoff
#include <cstdio>
#include <string>

#include "ActionFailureBackoff.h"

static int checks = 0;
#define CHECK(cond) do { ++checks; if (!(cond)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); return 1; } } while (0)

using ai::ActionFailureBackoff;

static const uint32_t IMPOSSIBLE = 2; // matches Engine::ACTION_RESULT_IMPOSSIBLE
static const uint32_t FAILED = 4;     // matches Engine::ACTION_RESULT_FAILED

int main()
{
    // 1. Unknown keys never back off.
    {
        ActionFailureBackoff b;
        CHECK(!b.IsBackedOff("nothing|here|0|0|4", 1000));
        CHECK(b.Size() == 0);
    }

    // 2. Exponential growth then cap: base 250, max 2000.
    {
        ActionFailureBackoff b;
        const std::string k = ActionFailureBackoff::Key("gather", "node", 7, 0, FAILED);
        b.Record(k, 10000, 250, 2000, 64, 30000);
        CHECK(b.IsBackedOff(k, 10249));
        CHECK(!b.IsBackedOff(k, 10250));
        b.Record(k, 10250, 250, 2000, 64, 30000); // +500
        CHECK(b.IsBackedOff(k, 10749));
        CHECK(!b.IsBackedOff(k, 10750));
        b.Record(k, 10750, 250, 2000, 64, 30000); // +1000
        CHECK(!b.IsBackedOff(k, 11750));
        b.Record(k, 11750, 250, 2000, 64, 30000); // +2000 (shift 3)
        CHECK(!b.IsBackedOff(k, 13750));
        b.Record(k, 13750, 250, 2000, 64, 30000); // shift 4 -> 4000 capped to 2000
        CHECK(b.IsBackedOff(k, 15749));
        CHECK(!b.IsBackedOff(k, 15750));
        // Failure count saturates: 20 more records must not extend past the cap.
        for (int i = 0; i < 20; ++i)
            b.Record(k, 15750 + i, 250, 2000, 64, 30000);
        CHECK(!b.IsBackedOff(k, 15750 + 19 + 2000));
        CHECK(b.Size() == 1);
    }

    // 3. Success clears both result variants.
    {
        ActionFailureBackoff b;
        const std::string kf = ActionFailureBackoff::Key("gather", "node", 7, 0, FAILED);
        const std::string ki = ActionFailureBackoff::Key("gather", "node", 7, 0, IMPOSSIBLE);
        b.Record(kf, 1000, 250, 2000, 64, 30000);
        b.Record(ki, 1000, 250, 2000, 64, 30000);
        CHECK(b.Size() == 2);
        b.Clear(kf, ki);
        CHECK(b.Size() == 0);
        CHECK(!b.IsBackedOff(kf, 1001));
    }

    // 4. TTL expiry prunes stale entries, keeps fresh ones.
    {
        ActionFailureBackoff b;
        b.Record("old", 0, 250, 2000, 64, 30000);
        b.Record("new", 30900, 250, 2000, 64, 30000);
        b.Prune(31000, 30000, true);
        CHECK(b.Size() == 1);
        CHECK(!b.IsBackedOff("old", 31000));
        CHECK(b.IsBackedOff("new", 31000));
    }

    // 5. Overflow evicts the stalest entry first; size stays bounded.
    {
        ActionFailureBackoff b;
        b.Record("a", 0, 5000, 20000, 2, 30000);
        b.Record("b", 100, 5000, 20000, 2, 30000);
        b.Record("c", 200, 5000, 20000, 2, 30000);
        CHECK(b.Size() == 2);
        CHECK(!b.IsBackedOff("a", 1000)); // evicted: stalest
        CHECK(b.IsBackedOff("b", 1000));
        CHECK(b.IsBackedOff("c", 1000));
    }

    // 6. Zero base/max disables and empties the cache.
    {
        ActionFailureBackoff b;
        b.Record("x", 0, 250, 2000, 64, 30000);
        CHECK(b.Size() == 1);
        b.Record("y", 0, 0, 2000, 64, 30000);
        CHECK(b.Size() == 0);
    }

    // 7. Key separates target, destination and result.
    {
        std::string a = ActionFailureBackoff::Key("go", "s", 1, 2, FAILED);
        std::string b1 = ActionFailureBackoff::Key("go", "s", 1, 3, FAILED);
        std::string c = ActionFailureBackoff::Key("go", "s", 1, 2, IMPOSSIBLE);
        CHECK(a != b1);
        CHECK(a != c);
    }

    using ai::TransitionTracker;

    // 8. Steady presence never drains, including zone-line walking.
    {
        TransitionTracker t;
        CHECK(t.Update(true, false, 0, 0.0f, 0.0f, 0.0f) == TransitionTracker::NONE);
        CHECK(t.Update(true, false, 0, 5.0f, 0.0f, 0.0f) == TransitionTracker::NONE);
        CHECK(t.Update(true, false, 0, 40.0f, 30.0f, 5.0f) == TransitionTracker::NONE);
        CHECK(t.LastMap() == 0);
    }

    // 9. Teleport away and back on the same map drains on arrival.
    {
        TransitionTracker t;
        CHECK(t.Update(true, false, 1, 0.0f, 0.0f, 0.0f) == TransitionTracker::NONE);
        CHECK(t.Update(true, true, 1, 0.0f, 0.0f, 0.0f) == TransitionTracker::AWAY);
        CHECK(t.Update(true, true, 1, 0.0f, 0.0f, 0.0f) == TransitionTracker::AWAY);
        CHECK(t.Update(true, false, 1, 500.0f, 500.0f, 0.0f) == TransitionTracker::ARRIVED);
        CHECK(t.Update(true, false, 1, 501.0f, 500.0f, 0.0f) == TransitionTracker::NONE);
    }

    // 10. Leaving the world and returning drains even without teleport flags.
    {
        TransitionTracker t;
        CHECK(t.Update(true, false, 1, 0.0f, 0.0f, 0.0f) == TransitionTracker::NONE);
        CHECK(t.Update(false, false, 1, 0.0f, 0.0f, 0.0f) == TransitionTracker::AWAY);
        CHECK(t.Update(true, false, 1, 10.0f, 0.0f, 0.0f) == TransitionTracker::ARRIVED);
    }

    // 11. Map change drains and re-baselines.
    {
        TransitionTracker t;
        CHECK(t.Update(true, false, 0, 0.0f, 0.0f, 0.0f) == TransitionTracker::NONE);
        CHECK(t.Update(true, false, 1, 0.0f, 0.0f, 0.0f) == TransitionTracker::MAP_CHANGED);
        CHECK(t.LastMap() == 1);
        CHECK(t.Update(true, false, 1, 1.0f, 0.0f, 0.0f) == TransitionTracker::NONE);
    }

    // 12. Impossible jump on the same map drains; sub-threshold never does.
    {
        TransitionTracker t;
        CHECK(t.Update(true, false, 0, 0.0f, 0.0f, 0.0f) == TransitionTracker::NONE);
        CHECK(t.Update(true, false, 0, 99.0f, 0.0f, 0.0f) == TransitionTracker::NONE);
        CHECK(t.Update(true, false, 0, 500.0f, 0.0f, 0.0f) == TransitionTracker::JUMPED);
        CHECK(t.Update(true, false, 0, 501.0f, 0.0f, 0.0f) == TransitionTracker::NONE);
    }

    std::printf("PASS tools/test_engine_failure_backoff (%d checks)\n", checks);
    return 0;
}
