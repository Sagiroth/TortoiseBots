#include "../ai/playerbot/strategy/paladin/PaladinBlessingOrderPolicy.h"
#include <cstdio>
#include <cstdlib>

#define CHECK(cond) do { if (!(cond)) { std::printf("FAILED: %s (line %d)\n", #cond, __LINE__); std::exit(1); } } while (0)

int main()
{
    CHECK(ai::BlessingOrderClassIndex("warrior") == 0);
    CHECK(ai::BlessingOrderClassIndex("druid") == 8);
    CHECK(ai::BlessingOrderClassIndex("deathknight") == -1);
    CHECK(ai::BlessingOrderClassId(ai::BlessingOrderClassIndex("shaman")) == 7);
    CHECK(ai::BlessingOrderClassIndexById(11) == 8);
    CHECK(ai::BlessingOrderClassIndexById(6) == -1);
    for (int i = 0; i < ai::kBlessingOrderClassCount; ++i)
        CHECK(ai::BlessingOrderClassIndexById(ai::BlessingOrderClassId(i)) == i);

    CHECK(ai::BlessingOrderSpell("kings") == "blessing of kings");
    CHECK(ai::BlessingOrderSpell("auto").empty());
    CHECK(ai::BlessingOrderSpell("fury").empty());
    CHECK(ai::IsBlessingOrderWord("auto"));
    CHECK(ai::IsBlessingOrderWord("light"));
    CHECK(!ai::IsBlessingOrderWord(""));
    CHECK(!ai::IsBlessingOrderWord("none"));
    CHECK(ai::BlessingOrderKey(1) == "blessing order paladin");

    std::printf("All blessing order policy checks PASSED!\n");
    return 0;
}
