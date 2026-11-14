#include "../ai/playerbot/strategy/generic/OffSpecPolicy.h"
#include <cstdio>
#include <cstdlib>

#define CHECK(cond) do { if (!(cond)) { std::printf("FAILED: %s (line %d)\n", #cond, __LINE__); std::exit(1); } } while (0)

int main()
{
    CHECK(ai::ClassHasOffSpec(2));
    CHECK(ai::ClassHasOffSpec(5));
    CHECK(ai::ClassHasOffSpec(7));
    CHECK(ai::ClassHasOffSpec(11));
    for (std::uint8_t cls : { 1, 3, 4, 8, 9 })
        CHECK(!ai::ClassHasOffSpec(cls));

    CHECK(ai::OffSpecChangeForRole(5, 2, true) == "+offdps,-offheal");
    CHECK(ai::OffSpecChangeForRole(5, 4, true) == "+offheal,-offdps");
    CHECK(ai::OffSpecChangeForRole(11, 1, true) == "-offheal,-offdps");
    CHECK(ai::OffSpecChangeForRole(2, 1, true) == "-offheal,-offdps");
    CHECK(ai::OffSpecChangeForRole(7, 2, false) == "-offheal,-offdps");
    CHECK(ai::OffSpecChangeForRole(7, 4, false) == "-offheal,-offdps");
    CHECK(ai::OffSpecChangeForRole(1, 4, true).empty());
    CHECK(ai::OffSpecChangeForRole(8, 4, true).empty());

    std::printf("test_off_spec_policy: OK\n");
    return 0;
}
