#include "../ai/playerbot/ViscidusPolicy.h"
#include <cassert>
#include <iostream>
int main()
{
    using namespace ai;
    assert(ViscidusGlobBefore(5, 25, 2, 40, 1, 1));
    assert(!ViscidusGlobBefore(40, 1, 1, 5, 25, 2));
    for (unsigned i=0;i<13;++i)
    {
        float angle=ViscidusRingAngle(i,13);
        assert(angle>=0 && angle<6.283186f);
        if(i)assert(angle>ViscidusRingAngle(i-1,13));
    }
    assert(ViscidusShatterWindow(531, 15299, true, true, true, true));
    assert(!ViscidusShatterWindow(531, 15299, false, true, true, true));
    assert(!ViscidusShatterWindow(531, 15299, true, true, false, true));
    assert(!ViscidusShatterWindow(0, 15299, true, true, true, true));
    assert(!ViscidusShatterWindow(531, 15667, true, true, true, true));
    assert(ViscidusHoldDamage(true, true, true, false));
    assert(ViscidusHoldDamage(true, true, false, true));
    assert(!ViscidusHoldDamage(true, true, false, false)); // physical melee
    assert(!ViscidusHoldDamage(true, false, true, true)); // heal/buff
    assert(!ViscidusHoldDamage(false, true, true, true)); // thaw/globs
    for (bool ranged : {false, true})
    {
        (void)ranged; // Both DPS roles use the same gate.
        assert(ViscidusDpsGlob(true, true, false, false, true, true));
    }
    assert(!ViscidusDpsGlob(true, true, true, false, true, true));
    assert(!ViscidusDpsGlob(true, true, false, true, true, true));
    assert(!ViscidusDpsGlob(true, false, false, false, true, true));
    assert(!ViscidusDpsGlob(true, true, false, false, false, true));
    assert(!ViscidusDpsGlob(true, true, false, false, true, false));
    std::cout << "Viscidus scope, freeze damage, glob roles: PASS\n";
}
