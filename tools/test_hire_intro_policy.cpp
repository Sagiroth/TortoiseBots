// Standalone regression test for issue #382: every hired bot posts its spec
// and spells in party chat once. Guards the pure shaping/once rules behind
// the dedicated hire intro (runtime/HireIntroPolicy.h):
//   - spec line wording ("My spec is <spec> <class> [(role)], talents a/b/c."),
//   - spell-list joining: capped names/bytes, leftovers collapse into
//     ", and N more", at least one name kept whole, empty input yields "",
//   - the intro fires exactly once and only when both ends are live.
//
// Build and run (from the repo root):
//   g++ -std=c++17 -Wall -Wextra tools/test_hire_intro_policy.cpp -o /tmp/test_hire_intro
//   /tmp/test_hire_intro

#include "../runtime/HireIntroPolicy.h"

#include <cstdio>
#include <cstdlib>

using namespace TortoiseBots;

static int checks = 0;
#define CHECK(cond) do { \
    ++checks; \
    if (!(cond)) { \
        std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        std::exit(1); \
    } \
} while (0)

static void TestSpecLine()
{
    // Frost mage from the report: spec, class, role, trees.
    CHECK(ComposeHireSpecLine("frost", "mage", "dps", 0, 7, 0) ==
        "My spec is frost mage (dps), talents 0/7/0.");
    // No role known: omitted, never invented.
    CHECK(ComposeHireSpecLine("assassination", "rogue", "", 31, 8, 12) ==
        "My spec is assassination rogue, talents 31/8/12.");
}

static void TestSpellListShort()
{
    std::vector<std::string> names = { "[Gouge] (1776)", "[Kick] (1766)" };
    CHECK(FormatHireSpellList(names) == "[Gouge] (1776), [Kick] (1766)");
}

static void TestSpellListCap()
{
    // More names than the cap: first 10 kept, the rest collapse to one tail.
    std::vector<std::string> names;
    for (int i = 0; i < 15; ++i)
        names.push_back("[Spell] (" + std::to_string(1000 + i) + ")");
    std::string joined = FormatHireSpellList(names);
    CHECK(joined.find("[Spell] (1009)") != std::string::npos);
    CHECK(joined.find(", and 5 more") != std::string::npos);
    CHECK(joined.find("(1014)") == std::string::npos);
}

static void TestSpellListChars()
{
    // A handful of long names: bytes still cap, but one name stays whole.
    std::vector<std::string> names(4, std::string(80, 'x'));
    std::string joined = FormatHireSpellList(names);
    CHECK(joined.size() <= 200 + std::string(", and 3 more").size());
    CHECK(joined.find(std::string(80, 'x')) != std::string::npos);
    CHECK(joined.find(", and") != std::string::npos);
}

static void TestSpellListEmpty()
{
    CHECK(FormatHireSpellList({}) == "");
}

static void TestOnceGuard()
{
    // Fires when both ends are live and it never went out.
    CHECK(ShouldAnnounceHireIntro(false, true, true) == true);
    // Exactly once: a sent intro never re-fires.
    CHECK(ShouldAnnounceHireIntro(true, true, true) == false);
    // And never before both ends are live (mid-teleport, master offline).
    CHECK(ShouldAnnounceHireIntro(false, false, true) == false);
    CHECK(ShouldAnnounceHireIntro(false, true, false) == false);
    CHECK(ShouldAnnounceHireIntro(false, false, false) == false);
}

int main()
{
    TestSpecLine();
    TestSpellListShort();
    TestSpellListCap();
    TestSpellListChars();
    TestSpellListEmpty();
    TestOnceGuard();
    std::printf("hire intro policy: %d checks passed\n", checks);
    return 0;
}
