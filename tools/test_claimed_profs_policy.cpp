// Standalone test for the claimed-roster profession field
// (runtime/policy/hire/ClaimedProfsPolicy.h): the trailing `<profs>` token
// on every TBM:CLAIMED row.
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra tools/test_claimed_profs_policy.cpp -o /tmp/test_claimed_profs_policy
//   /tmp/test_claimed_profs_policy

#include "../runtime/policy/hire/ClaimedProfsPolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using TortoiseBots::ClaimedProfsPolicy::FormatProfs;
using TortoiseBots::ClaimedProfsPolicy::ProfessionSkill;

int main()
{
    CHECK(FormatProfs({}) == "-");
    CHECK(FormatProfs({{333, 165}}) == "333:165");
    CHECK(FormatProfs({{197, 140}, {333, 165}}) == "197:140;333:165");
    CHECK(FormatProfs({{333, 165}, {197, 140}}) == "197:140;333:165");
    CHECK(FormatProfs({{186, 245}, {182, 230}, {164, 237}}) == "164:237;182:230;186:245");
    CHECK(FormatProfs({{185, 60}, {129, 300}, {356, 75}}) == "129:300;185:60;356:75");

    std::cout << "claimed profs policy ok\n";
    return 0;
}
