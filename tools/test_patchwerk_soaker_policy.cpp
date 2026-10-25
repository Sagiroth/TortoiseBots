#include "../ai/playerbot/PatchwerkSoakerPolicy.h"
#include <cassert>
#include <iostream>
int main()
{
    using ai::PatchwerkThreatPlayer;
    using ai::PatchwerkMeleeSlotsSafe;
    std::vector<uint64_t> soakers{2,3,4};
    std::vector<PatchwerkThreatPlayer> rows{{1,true},{2,true},{3,true},{4,true},{5,false}};
    assert(PatchwerkMeleeSlotsSafe(1,soakers,rows,5));
    // A ranged player above the tanks is skipped by the actual melee filter.
    rows.insert(rows.begin(),{6,false});
    assert(PatchwerkMeleeSlotsSafe(1,soakers,rows,5));
    // But that same player cannot safely step in: it would displace a tank.
    assert(!PatchwerkMeleeSlotsSafe(1,soakers,rows,6));
    rows[4].eligible = false; // Soaker leaves melee, dies, or becomes immune.
    assert(!PatchwerkMeleeSlotsSafe(1,soakers,rows,5));
    rows[4].eligible = true;
    rows.insert(rows.begin()+2,{7,true});
    assert(!PatchwerkMeleeSlotsSafe(1,soakers,rows,5));
    assert(!PatchwerkMeleeSlotsSafe(1,{2,2,4},rows,5));
    assert(!PatchwerkMeleeSlotsSafe(1,{1,3,4},rows,5));
    assert(!PatchwerkMeleeSlotsSafe(0,soakers,rows,5));
    assert(!PatchwerkMeleeSlotsSafe(1,{2,3},rows,5));
    std::cout << "Patchwerk soaker policy PASS\n";
}
