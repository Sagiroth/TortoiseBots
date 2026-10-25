#include "../ai/playerbot/VaelTankHandoverPolicy.h"
#include <cassert>
#include <iostream>

int main()
{
    using namespace ai;
    assert(VaelTankHandoverActive(true, true, true, true, true, true));
    assert(!VaelTankHandoverActive(false, true, true, true, true, true));
    assert(!VaelTankHandoverActive(true, false, true, true, true, true));
    assert(!VaelTankHandoverActive(true, true, false, true, true, true));
    assert(!VaelTankHandoverActive(true, true, true, false, true, true));
    assert(!VaelTankHandoverActive(true, true, true, true, false, true));
    assert(!VaelTankHandoverActive(true, true, true, true, true, false));
    assert(VaelBlockAutomaticTaunt(true, false, false));
    assert(VaelBlockAutomaticTaunt(false, true, false));
    assert(VaelBlockAutomaticTaunt(false, false, true));
    assert(!VaelBlockAutomaticTaunt(false, false, false));
    std::cout << "Vaelastrasz tank handover policy: passed\n";
}
