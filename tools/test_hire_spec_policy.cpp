// Standalone regression test for issue #386: the recruiter spec step must be
// honoured for every class, not only druids. Guards
//  - the gossip menu table (label/role/pathName per class, druid gets 4),
//  - the spec-index -> premade path-name mapping used by the provisioner,
//  - the post-provision check that decides whether an applied build honours
//    the request (and so whether falling back to the role is sanctioned),
//  - and that every pathName still matches a configured AiPlayerbot.PremadeSpecName
//    (a renamed/removed premade would silently degrade every hire to role-only).
//
// Build and run (from the repo root):
//   g++ -std=c++17 -Wall -Wextra tools/test_hire_spec_policy.cpp -o /tmp/test_hire_spec
//   /tmp/test_hire_spec

#include "../runtime/HireSpecPolicy.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace TortoiseBots;

static int checks = 0;
#define CHECK(cond) do { \
    ++checks; \
    if (!(cond)) { \
        std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        std::exit(1); \
    } \
} while (0)

static uint8_t const kClasses[] = {
    kHireClassWarrior, kHireClassPaladin, kHireClassHunter, kHireClassRogue, kHireClassPriest,
    kHireClassShaman, kHireClassMage, kHireClassWarlock, kHireClassDruid,
};

// Every hireable class offers options whose role is exactly one bit and whose
// name resolves; the menu order and the provisioner's mapping agree.
static void TestTableShape()
{
    for (uint8_t cls : kClasses)
    {
        uint32_t count = 0;
        HireSpecOption const* options = HireSpecOptions(cls, count);
        CHECK(options != nullptr);
        CHECK(count == (cls == kHireClassDruid ? 4u : 3u));
        for (uint32_t i = 0; i < count; ++i)
        {
            CHECK(options[i].label != nullptr && options[i].label[0] != '\0');
            CHECK(options[i].pathName != nullptr && options[i].pathName[0] != '\0');
            uint8_t role = options[i].role;
            CHECK(role == kHireRoleTank || role == kHireRoleHealer || role == kHireRoleDps);
            CHECK(HireSpecPathName(cls, static_cast<int>(i)) == options[i].pathName);
        }
    }
}

static void TestUnknownClass()
{
    uint32_t count = 123;
    CHECK(HireSpecOptions(6, count) == nullptr);  // class 6 does not exist
    CHECK(count == 0);
    CHECK(HireSpecOptions(0, count) == nullptr);
    CHECK(count == 0);
    CHECK(HireSpecPathName(6, 0) == nullptr);
    CHECK(HireSpecPathName(kHireClassWarrior, -1) == nullptr);
    CHECK(HireSpecPathName(kHireClassWarrior, 3) == nullptr);
    CHECK(HireSpecPathName(kHireClassDruid, 4) == nullptr);
}

// The reported bug: an "Arms" warrior and a "Fury" warrior are both DPS, so
// before this fix the role filter left both premade trees in the draw and the
// choice was a coin flip. The spec index must pin them apart.
static void TestWarriorSpecsPinned()
{
    CHECK(std::string(HireSpecPathName(kHireClassWarrior, 1)) == "arms");
    CHECK(std::string(HireSpecPathName(kHireClassWarrior, 2)) == "fury");
    CHECK(std::string(HireSpecPathName(kHireClassWarrior, 0)) == "protection");
    // Every class resolves each of its menu options to a non-empty name.
    for (uint8_t cls : kClasses)
        for (int i = 0; i < 4; ++i)
        {
            char const* name = HireSpecPathName(cls, i);
            if (i < 3 || cls == kHireClassDruid)
                CHECK(name != nullptr && name[0] != '\0');
        }
}

// A role-only request is honoured by any role build; a named request only by a
// build carrying that name (so a silent role fallback is detectable).
static void TestHonoured()
{
    CHECK(HireSpecHonoured(nullptr, "fury") == true);
    CHECK(HireSpecHonoured("", "fury") == true);
    CHECK(HireSpecHonoured("arms", "arms") == true);
    CHECK(HireSpecHonoured("feral", "feral") == true);   // Bear and Cat share the feral path
    CHECK(HireSpecHonoured("arms", "fury") == false);    // the reported arms->fury mix-up
    CHECK(HireSpecHonoured("fury", "arms") == false);
    CHECK(HireSpecHonoured("arms", nullptr) == false);
}

// Parse AiPlayerbot.PremadeSpecName.<class>.<n> = <name> from the shipped
// template. Returns false when the file cannot be found (test still passes).
static bool LoadPremadeSpecNames(std::map<int, std::set<std::string>>& out)
{
    char const* candidates[] = {
        "ai/playerbot/aiplayerbot.conf.dist.in",
        "../ai/playerbot/aiplayerbot.conf.dist.in",
        "../../ai/playerbot/aiplayerbot.conf.dist.in",
    };
    std::ifstream in;
    for (char const* path : candidates)
    {
        in.open(path);
        if (in.is_open())
            break;
    }
    if (!in.is_open())
        return false;

    std::string const prefix = "AiPlayerbot.PremadeSpecName.";
    std::string line;
    while (std::getline(in, line))
    {
        size_t start = line.find_first_not_of(" \t");
        if (start == std::string::npos || line.compare(start, prefix.size(), prefix) != 0)
            continue;
        size_t eq = line.find('=', start);
        if (eq == std::string::npos)
            continue;
        std::string key = line.substr(start + prefix.size(), eq - (start + prefix.size()));
        int cls = std::atoi(key.c_str());
        std::string value = line.substr(eq + 1);
        size_t vs = value.find_first_not_of(" \t");
        if (vs == std::string::npos)
            continue;
        size_t ve = value.find_last_not_of(" \t\r\n");
        out[cls].insert(value.substr(vs, ve - vs + 1));
    }
    return true;
}

// Every menu option must resolve against the shipped template, or the hire
// silently falls back to the role for that spec.
static void TestConfiguredNames()
{
    std::map<int, std::set<std::string>> configured;
    if (!LoadPremadeSpecNames(configured))
    {
        std::printf("hire spec policy: premade spec template not found; template check skipped\n");
        return;
    }
    CHECK(!configured.empty());
    for (uint8_t cls : kClasses)
    {
        uint32_t count = 0;
        HireSpecOption const* options = HireSpecOptions(cls, count);
        for (uint32_t i = 0; i < count; ++i)
        {
            bool found = false;
            for (std::string const& name : configured[static_cast<int>(cls)])
            {
                if (name.find(options[i].pathName) != std::string::npos)
                {
                    found = true;
                    break;
                }
            }
            if (!found)
                std::fprintf(stderr, "class %u option %u ('%s'): no premade spec contains '%s'\n",
                    unsigned(cls), unsigned(i), options[i].label, options[i].pathName);
            CHECK(found);
        }
    }
}

int main()
{
    TestTableShape();
    TestUnknownClass();
    TestWarriorSpecsPinned();
    TestHonoured();
    TestConfiguredNames();
    std::printf("hire spec policy: %d checks passed\n", checks);
    return 0;
}
