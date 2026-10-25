#include "../ai/playerbot/RaidDpsExclusionPolicy.h"
#include <cassert>
#include <iostream>

int main()
{
    using ai::NefarianBoneFocusActive;
    assert(NefarianBoneFocusActive(true, true, true, true, false, true, true, 19.99f, true));
    assert(!NefarianBoneFocusActive(false, true, true, true, false, true, true, 10.0f, true));
    assert(!NefarianBoneFocusActive(true, false, true, true, false, true, true, 10.0f, true));
    assert(!NefarianBoneFocusActive(true, true, false, true, false, true, true, 10.0f, true));
    assert(!NefarianBoneFocusActive(true, true, true, false, false, true, true, 10.0f, true));
    assert(!NefarianBoneFocusActive(true, true, true, true, true, true, true, 10.0f, true));
    assert(!NefarianBoneFocusActive(true, true, true, true, false, false, true, 10.0f, true));
    assert(!NefarianBoneFocusActive(true, true, true, true, false, true, false, 10.0f, true));
    assert(!NefarianBoneFocusActive(true, true, true, true, false, true, true, 20.0f, true));
    assert(!NefarianBoneFocusActive(true, true, true, true, false, true, true, 10.0f, false));
    std::cout << "Nefarian bone focus policy: passed\n";
}
