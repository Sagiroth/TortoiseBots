#include "../ai/playerbot/MaexxnaRescuePolicy.h"
#include <cassert>
#include <limits>
int main()
{
    using namespace ai;
    assert(MaexxnaRescueEligible(true, true, false, false, false));
    assert(!MaexxnaRescueEligible(false, true, false, false, false));
    assert(!MaexxnaRescueEligible(true, false, false, false, false));
    assert(!MaexxnaRescueEligible(true, true, true, false, false));
    assert(!MaexxnaRescueEligible(true, true, false, true, false));
    assert(!MaexxnaRescueEligible(true, true, false, false, true));
    assert(MaexxnaWrapAccessible(true, false, true, 25, 30));
    assert(!MaexxnaWrapAccessible(true, false, false, 25, 30));
    assert(!MaexxnaWrapAccessible(true, false, true, 35, 30));
    assert(!MaexxnaWrapAccessible(false, false, true, 10, 30));
    assert(MaexxnaWrapAccessible(false, true, true, 1, 30));
    assert(!MaexxnaWrapAccessible(true, false, true, std::numeric_limits<float>::quiet_NaN(), 30));
    assert(MaexxnaWrapScore(true, 30) > MaexxnaWrapScore(false, 1));
    assert(MaexxnaWrapScore(false, 10) > MaexxnaWrapScore(false, 20));
}
