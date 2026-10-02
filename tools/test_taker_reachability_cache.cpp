#include "../ai/playerbot/TakerReachabilityCache.h"

#include <cstdlib>
#include <iostream>
#include <thread>
#include <vector>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::TakerReachabilityCache;

namespace
{
    // The Tower of Azora's Antonas Riftgaze, the spawn that started this: every
    // bot stalls 18-29 yd from it and its quest had never been handed in.
    std::int32_t const ANTONAS = 62634;
    std::int32_t const OTHER_TAKER = 12345;

    std::time_t const NOW = 1'000'000;
    std::time_t const TTL = TakerReachabilityCache::UNREACHABLE_TTL;
}

int main()
{
    std::cout << "Starting TortoiseBots taker-reachability cache tests...\n";

    TakerReachabilityCache& cache = TakerReachabilityCache::Instance();

    // 1. Unknown takers are reachable (no mark).
    CHECK(!cache.IsUnreachable(ANTONAS, NOW));
    CHECK(!cache.IsUnreachable(OTHER_TAKER, NOW));

    // 2. A mark holds for the whole TTL and expires exactly at it.
    cache.MarkUnreachable(ANTONAS, NOW);
    CHECK(cache.IsUnreachable(ANTONAS, NOW));
    CHECK(cache.IsUnreachable(ANTONAS, NOW + TTL - 1));
    CHECK(!cache.IsUnreachable(ANTONAS, NOW + TTL));
    CHECK(!cache.IsUnreachable(ANTONAS, NOW + TTL + 1));

    // 3. Entries are independent: one taker's mark is not another's.
    cache.MarkUnreachable(OTHER_TAKER, NOW);
    cache.Clear(ANTONAS);
    CHECK(!cache.IsUnreachable(ANTONAS, NOW));
    CHECK(cache.IsUnreachable(OTHER_TAKER, NOW));

    // 4. Re-marking refreshes the window instead of shortening it.
    cache.MarkUnreachable(OTHER_TAKER, NOW + TTL / 2);
    CHECK(cache.IsUnreachable(OTHER_TAKER, NOW + TTL - 1));
    CHECK(!cache.IsUnreachable(OTHER_TAKER, NOW + TTL + TTL / 2));

    // 5. Clear removes a mark and is idempotent.
    cache.Clear(OTHER_TAKER);
    cache.Clear(OTHER_TAKER);
    CHECK(!cache.IsUnreachable(OTHER_TAKER, NOW));

    // 6. Concurrent probes and reads stay consistent: every thread owns an entry,
    //    so a mark must be visible to its own reader, while all threads hammer
    //    the same lock.
    std::vector<std::thread> threads;
    for (std::int32_t index = 0; index < 8; ++index)
    {
        threads.emplace_back([&cache, index]()
        {
            std::int32_t const entry = 100 + index;

            for (int i = 0; i < 200; ++i)
            {
                cache.MarkUnreachable(entry, NOW);
                CHECK(cache.IsUnreachable(entry, NOW));
                CHECK(cache.IsUnreachable(entry, NOW + TTL - 1));
                CHECK(!cache.IsUnreachable(entry, NOW + TTL));
                cache.Clear(entry);
                CHECK(!cache.IsUnreachable(entry, NOW));
            }
        });
    }
    for (std::thread& thread : threads)
        thread.join();

    for (std::int32_t index = 0; index < 8; ++index)
        CHECK(!cache.IsUnreachable(100 + index, NOW));

    std::cout << "TortoiseBots taker-reachability cache tests passed.\n";
    return 0;
}
