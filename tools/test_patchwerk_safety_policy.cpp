#include "../ai/playerbot/PatchwerkSafetyPolicy.h"
#include <cassert>
int main()
{
    using namespace ai;
    std::vector<PatchwerkTankState> tanks{{1,15000},{2,20000},{3,12000},{4,11000}};
    assert(PatchwerkChooseMain(0,2,tanks)==2); // actual soaker holder, not configured starter
    assert(PatchwerkChooseMain(2,1,tanks)==2); // no owner churn / taunt war
    tanks.erase(tanks.begin()+1);
    assert(PatchwerkChooseMain(2,3,tanks)==3); // holder death
    assert(PatchwerkChooseMain(2,99,tanks)==1); // recover from non-tank holder
    assert(PatchwerkChooseMain(0,99,tanks)==0); // do not invent a pre-pull main
    unsigned seats[4]{};
    for(unsigned i=0;i<10;++i)++seats[PatchwerkHealerSeat(i,4)];
    assert(seats[0]==3&&seats[1]==3&&seats[2]==2&&seats[3]==2);
    assert(!PatchwerkDamageBudget(0,20000,10000,4,3999,false));
    assert(!PatchwerkDamageBudget(0,9000,8000,4,5000,false));
    assert(!PatchwerkDamageBudget(0,20000,4999,4,5000,false));
    assert(PatchwerkDamageBudget(4000,20000,10000,4,5000,false));
    assert(!PatchwerkDamageBudget(5000,20000,10000,4,5000,false)); // 3k burst reserve
    assert(!PatchwerkDamageBudget(0,20000,10000,3,5000,false));
    assert(PatchwerkDamageBudget(4000,20000,10000,3,5000,false,1)); // ranged salvage
    assert(PatchwerkDamageBudget(6500,20000,20000,4,5000,true));
    assert(!PatchwerkDamageBudget(12500,20000,20000,4,5000,true));
    assert(PatchwerkDamageBudget(12500,20000,20000,4,5000,false));
    assert(!PatchwerkSoakerHealthSafe(8400,8400));
    assert(!PatchwerkSoakerHealthSafe(8900,8400));
    assert(PatchwerkSoakerHealthSafe(10849,8400));
}
