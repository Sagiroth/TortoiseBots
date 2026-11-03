// Standalone check for runtime/SpellSchoolImmunePolicy.h (a spell's school bit
// against a creature template's school_immune_mask):
//   g++ -std=c++17 -Wall -Wextra -Werror=return-type tools/test_spell_school_immune_policy.cpp -o /tmp/t && /tmp/t
#include "../runtime/SpellSchoolImmunePolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using namespace TortoiseBots;

// SpellEntry::School indices (SpellDefines.h): normal 0, holy 1, fire 2,
// nature 3, frost 4, shadow 5, arcane 6.
static std::uint32_t SchoolBit(std::uint32_t school)
{
    return std::uint32_t(1) << school;
}

int main()
{
    // Fire (bit 4) against a fire-immune template (mask 4): skippable.
    CHECK(SpellSchoolImmune(SchoolBit(2), 4));
    // Every other school is still worth casting at it.
    CHECK(!SpellSchoolImmune(SchoolBit(5), 4)); // shadow
    CHECK(!SpellSchoolImmune(SchoolBit(4), 4)); // frost
    CHECK(!SpellSchoolImmune(SchoolBit(3), 4)); // nature
    CHECK(!SpellSchoolImmune(SchoolBit(6), 4)); // arcane
    CHECK(!SpellSchoolImmune(SchoolBit(1), 4)); // holy
    CHECK(!SpellSchoolImmune(SchoolBit(0), 4)); // physical
    // A template with no school immunity takes every school.
    CHECK(!SpellSchoolImmune(SchoolBit(2), 0));
    CHECK(!SpellSchoolImmune(0, 0));
    // Any bit shared with the mask counts: fire + frost against fire.
    CHECK(SpellSchoolImmune(SchoolBit(2) | SchoolBit(4), 4));
    // A different immunity (nature 8) does not block fire.
    CHECK(!SpellSchoolImmune(SchoolBit(2), 8));
    CHECK(SpellSchoolImmune(SchoolBit(3), 8));
    std::cout << "test_spell_school_immune_policy: ok\n";
    return 0;
}
