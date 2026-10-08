// Standalone regression test for runtime/PlayerLagWindow.h: the world-tick
// window behind the dashboard's player lag. Checks that the window keeps only
// the last 30 s of ticks and that the percentiles are the wait for the rest of
// the tick an action lands in, so a few long ticks show up in p95 even when
// most ticks are short.
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra tools/test_player_lag_window.cpp -o /tmp/test_player_lag_window
//   /tmp/test_player_lag_window

#include "../runtime/PlayerLagWindow.h"

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

int main()
{
    {
        PlayerLagWindow w;
        PlayerLagWindow::Stats s = w.Compute();
        CHECK(s.avgMs == 0 && s.worstMs == 0 && s.p50Ms == 0 && s.p95Ms == 0);
    }

    {
        // Steady 50 ms ticks: the wait is uniform over 0-50 ms.
        PlayerLagWindow w;
        for (int i = 0; i < 100; ++i)
            w.Add(50);
        PlayerLagWindow::Stats s = w.Compute();
        CHECK(s.avgMs == 50 && s.worstMs == 50);
        CHECK(s.p50Ms == 25);
        CHECK(s.p95Ms == 48);
    }

    {
        // 90 ticks of 50 ms and 10 of 300 ms: the long ticks are 10 % of the
        // ticks but 40 % of the time, so p95 lands deep in them while the
        // plain average stays low.
        PlayerLagWindow w;
        for (int i = 0; i < 90; ++i)
            w.Add(50);
        for (int i = 0; i < 10; ++i)
            w.Add(300);
        PlayerLagWindow::Stats s = w.Compute();
        CHECK(s.avgMs == 75);
        CHECK(s.worstMs == 300);
        CHECK(s.p50Ms == 38);  // (7500 - 100x) <= 3750
        CHECK(s.p95Ms == 263); // 10 * (300 - x) <= 375
    }

    {
        // Only the last 30 s count: old spikes age out.
        PlayerLagWindow w;
        w.Add(2000);
        for (int i = 0; i < 700; ++i) // 35 s of 50 ms ticks
            w.Add(50);
        PlayerLagWindow::Stats s = w.Compute();
        CHECK(s.worstMs == 50);
        CHECK(s.p95Ms == 48);
    }

    {
        // A single tick longer than the window is kept, not dropped.
        PlayerLagWindow w(1000);
        w.Add(5000);
        PlayerLagWindow::Stats s = w.Compute();
        CHECK(s.worstMs == 5000 && s.p95Ms == 4750 && s.p50Ms == 2500);
        w.Add(0); // ignored
        CHECK(w.Compute().avgMs == 5000);
    }

    std::printf("test_player_lag_window: %d checks passed\n", checks);
    return 0;
}
