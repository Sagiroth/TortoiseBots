#include "../ai/playerbot/EbonrocSwapPolicy.h"
#include <cassert>
#include <limits>
#include <iostream>

int main()
{
    using namespace ai;
    EbonrocSwapWindow swap;
    swap.Begin(10, 100);
    assert(!swap.Claim(20, 0, 1000)); // no replacement
    assert(!swap.Claim(30, 20, 1000)); // only designated tank
    assert(swap.Claim(20, 20, 1000));
    assert(!swap.Claim(30, 30, 1001)); // another ready tank cannot race the pending cast
    assert(!swap.ObserveVictim(10)); // CastSpell success alone is not a victim change
    assert(!swap.Ready(2999));
    assert(swap.Ready(3000));
    assert(swap.ObserveVictim(20));
    assert(!swap.Ready(3000)); // confirmed handover
    assert(!swap.ObserveVictim(10)); // original debuffed tank regained threat
    assert(swap.Claim(30, 30, 3000));
    assert(!swap.Claim(20, 20, 4999));
    assert(swap.Claim(20, 20, 5000));
    assert(!swap.Claim(30, 30, 7000)); // third attempt exhausts the window
    swap.Begin(10, 100);
    assert(!swap.Ready(9000)); // repeated observations do not replenish budget
    swap.Begin(10, 101);
    assert(swap.Claim(20, 20, 9000)); // new aura application
    swap.Begin(20, 102);
    assert(swap.Claim(10, 10, 9001)); // reverse handover uses a new victim/application
    swap.Begin(10, 103);
    assert(swap.Claim(20, 20, std::numeric_limits<std::uint32_t>::max() - 999));
    assert(!swap.Ready(500));
    assert(swap.Ready(1000)); // millisecond clock wrap
    for (auto name : {"taunt", "taunt on snare target", "growl", "hand of reckoning",
        "righteous defense", "challenging shout", "mocking blow"})
        assert(EbonrocTauntAction(name));
    assert(!EbonrocTauntAction("heal"));
    assert(!EbonrocTauntAction("shield slam"));
    assert(EbonrocBlockAutomaticTaunt(true, false, false));
    assert(EbonrocBlockAutomaticTaunt(false, true, false));
    assert(EbonrocBlockAutomaticTaunt(false, false, true));
    assert(!EbonrocBlockAutomaticTaunt(false, false, false)); // normal rescue of a non-tank
    std::cout << "Ebonroc swap policy: passed\n";
}
