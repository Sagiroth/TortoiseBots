#include "../ai/playerbot/KelThuzadInterruptPolicy.h"
#include <cassert>
#include <iostream>
int main()
{
    ai::KelThuzadInterruptWindow w;
    assert(!w.Claim(2,1,100));
    assert(w.Claim(1,1,100));
    assert(!w.Claim(1,1,600));
    assert(!w.Claim(2,2,499));
    assert(w.Claim(2,2,500));
    assert(!w.Claim(3,3,1000));
    w.Observe(4099);
    assert(!w.Ready(3,4099));
    w.Observe(4100);
    assert(w.Claim(3,3,4100));
    ai::KelThuzadInterruptWindow wrap;
    assert(wrap.Claim(1,1,0xffffff00u));
    assert(!wrap.Claim(2,2,0x8fu));
    assert(wrap.Claim(2,2,0x90u));
    std::cout << "Kel'Thuzad interrupt policy PASS\n";
}
