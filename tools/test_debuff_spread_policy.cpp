#include "../ai/playerbot/DebuffSpreadPolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::DebuffAnchor;
using ai::DebuffEscapeClearance;
using ai::IsDebuffSafePoint;
using ai::kDebuffSpreadOvershootYd;
using ai::kDebuffSpreadStepYd;
using ai::NeedsDebuffSpread;

int main()
{
    std::cout << "Starting TortoiseBots debuff-spread policy tests...\n";

    // Trigger gate: fire only inside the blast radius, never on the edge.
    CHECK(NeedsDebuffSpread(9.9f, 10.0f));
    CHECK(!NeedsDebuffSpread(10.0f, 10.0f));
    CHECK(!NeedsDebuffSpread(30.0f, 10.0f));
    std::cout << "  [PASS] trigger fires only inside the blast radius\n";

    // Single carrier: clearance is the 2D distance, safe only past range.
    {
        DebuffAnchor carriers[] = { { 0.0f, 0.0f } };
        float clearance = DebuffEscapeClearance(13.0f, 0.0f, carriers, 1);
        CHECK(clearance > 12.9f && clearance < 13.1f);
        CHECK(IsDebuffSafePoint(clearance, 10.0f));
        CHECK(!IsDebuffSafePoint(DebuffEscapeClearance(9.9f, 0.0f, carriers, 1), 10.0f));
        CHECK(!IsDebuffSafePoint(DebuffEscapeClearance(10.0f, 0.0f, carriers, 1) - 0.001f, 10.0f));
    }
    std::cout << "  [PASS] single-carrier clearance and safety boundary\n";

    // Worst case wins: two carriers bracketing the candidate, the nearer
    // one decides. A point safe from one carrier can still be lethal.
    {
        DebuffAnchor carriers[] = { { 0.0f, 0.0f }, { 20.0f, 0.0f } };
        float mid = DebuffEscapeClearance(10.0f, 0.0f, carriers, 2);
        CHECK(mid > 9.9f && mid < 10.1f);
        CHECK(IsDebuffSafePoint(mid, 10.0f));
        float nearFirst = DebuffEscapeClearance(5.0f, 0.0f, carriers, 2);
        CHECK(nearFirst > 4.9f && nearFirst < 5.1f);
        CHECK(!IsDebuffSafePoint(nearFirst, 10.0f));
    }
    std::cout << "  [PASS] nearest carrier decides among several\n";

    // No anchors: negative clearance, never safe, never moved on.
    {
        CHECK(DebuffEscapeClearance(0.0f, 0.0f, nullptr, 0) < 0.0f);
        DebuffAnchor carriers[] = { { 0.0f, 0.0f } };
        CHECK(DebuffEscapeClearance(0.0f, 0.0f, carriers, 0) < 0.0f);
        CHECK(!IsDebuffSafePoint(-1.0f, 10.0f));
    }
    std::cout << "  [PASS] empty anchor set is never safe\n";

    // Search bounds pin the donor loop: 3yd steps, 5yd past the blast.
    CHECK(kDebuffSpreadStepYd == 3.0f);
    CHECK(kDebuffSpreadOvershootYd == 5.0f);
    std::cout << "  [PASS] search step and overshoot match the donor loop\n";

    std::cout << "All debuff-spread policy tests passed.\n";
    return 0;
}
