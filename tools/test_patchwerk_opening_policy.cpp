#include "../ai/playerbot/PatchwerkOpeningPolicy.h"
#include <cassert>
#include <iostream>

int main()
{
    using ai::PatchwerkOpeningWindow;
    PatchwerkOpeningWindow window;

    // No encounter: nothing holds.
    window.Observe(0, false, false, 1000);
    assert(!window.Holds(1000, 4000));

    // An ordered pull holds DPS during the approach, before combat.
    window.Observe(7, false, true, 1000);
    assert(window.Holds(1000, 4000));
    assert(!window.Holds(1000, 0));   // 0 = hold off

    // Combat starts the clock once; the hold ends after the delay.
    window.Observe(7, true, true, 2000);
    assert(window.Holds(5999, 4000));
    assert(!window.Holds(6000, 4000));

    // Later observations do not restart the clock for a late joiner.
    window.Observe(7, true, false, 9000);
    assert(!window.Holds(9000, 4000));

    // Leaving combat (a reset) or a new encounter starts over.
    window.Observe(7, false, false, 20000);
    assert(!window.Holds(20000, 4000));
    window.Observe(7, true, false, 21000);
    assert(window.Holds(21000, 4000));
    window.Observe(8, true, false, 30000);
    assert(window.Holds(33999, 4000) && !window.Holds(34000, 4000));

    std::cout << "patchwerk opening policy PASS\n";
}
