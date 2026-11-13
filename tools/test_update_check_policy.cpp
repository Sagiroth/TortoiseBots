// Standalone test for the startup update check helpers
// (runtime/policy/UpdateCheckPolicy.h).
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra tools/test_update_check_policy.cpp -o /tmp/test_update_check_policy
//   /tmp/test_update_check_policy

#include "../runtime/policy/UpdateCheckPolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using TortoiseBots::UpdateCheckPolicy::IsNewer;
using TortoiseBots::UpdateCheckPolicy::ParseReleaseTag;

int main()
{
    CHECK(ParseReleaseTag("{\"url\":\"x\",\"tag_name\": \"v1.21.0\",\"name\":\"y\"}") == "1.21.0");
    CHECK(ParseReleaseTag("{\"tag_name\":\"2.0.3\"}") == "2.0.3");
    CHECK(ParseReleaseTag("") == "");
    CHECK(ParseReleaseTag("{\"message\":\"API rate limit exceeded\"}") == "");
    CHECK(ParseReleaseTag("{\"tag_name\":\"2026-09-25-v8\"}") == "");
    CHECK(ParseReleaseTag("{\"tag_name\":\"v1.2\"}") == "");
    CHECK(ParseReleaseTag("{\"tag_name\":\"v1.2.3-beta\"}") == "");

    CHECK(IsNewer("1.22.0", "1.21.0"));
    CHECK(IsNewer("1.21.1", "1.21.0"));
    CHECK(IsNewer("2.0.0", "1.99.99"));
    CHECK(IsNewer("1.10.0", "1.9.0"));
    CHECK(!IsNewer("1.21.0", "1.21.0"));
    CHECK(!IsNewer("1.20.4", "1.21.0"));
    CHECK(!IsNewer("1.22.0", "dev"));
    CHECK(!IsNewer("", "1.21.0"));
    CHECK(IsNewer("v1.22.0", "1.21.0"));

    std::cout << "update check policy: all checks passed\n";
    return 0;
}
