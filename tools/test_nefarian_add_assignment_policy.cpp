#include "../ai/playerbot/NefarianAddAssignmentPolicy.h"
#include <cassert>
#include <iostream>

int main()
{
    using namespace ai;
    auto plan = PlanNefarianAdds({
        {20, 0, {101, 102}}, {10, 0, {101, 102, 103}}}, {103, 101, 102, 101}, {});
    assert(plan.size() == 3);
    assert(plan[0] == std::make_pair(std::uint64_t(101), std::uint64_t(10)));
    assert(plan[1] == std::make_pair(std::uint64_t(102), std::uint64_t(20)));
    assert(plan[2] == std::make_pair(std::uint64_t(103), std::uint64_t(10)));

    plan = PlanNefarianAdds({{10, 0, {101, 102, 103}}, {20, 0, {101, 102, 103}}},
        {101, 102, 103}, {});
    assert(plan.size() == 3);
    assert(plan[0].second != plan[1].second);
    assert(plan[1].second != plan[2].second);

    plan = PlanNefarianAdds({{10, 102, {101, 102, 103}}, {20, 0, {101, 102, 103}}},
        {101, 102, 103}, {});
    assert(plan[0].second == 20);
    assert(plan[1].second == 10);
    assert(plan[2].second == 20);

    plan = PlanNefarianAdds({{10, 0, {101, 102}}, {20, 0, {101, 102}}},
        {101, 102, 103}, {{103, 99}});
    assert(plan[0].second == 10 && plan[1].second == 20 && plan[2].second == 99);

    plan = PlanNefarianAdds({{10, 999, {101, 102}}}, {101, 102}, {});
    assert(plan[0].second == 0 && plan[1].second == 0);
    std::cout << "Nefarian add assignment policy: passed\n";
}
