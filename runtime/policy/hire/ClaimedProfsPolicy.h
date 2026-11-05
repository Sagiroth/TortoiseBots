#pragma once

// Wire format for the trailing `<profs>` field on every TBM:CLAIMED row:
// `skillId:value` pairs in ascending skillId order joined by `;`, or `-`
// when the bot holds no profession. Header-only and core-free on purpose so
// the sort/join rules stay unit tested on their own
// (tools/test_claimed_profs_policy.cpp); commands/Info.cpp feeds plain
// values read from the bot or the characters database.

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace TortoiseBots
{
namespace ClaimedProfsPolicy
{

struct ProfessionSkill
{
    uint32_t skillId = 0;
    uint32_t value = 0;
};

inline std::string FormatProfs(std::vector<ProfessionSkill> const& skills)
{
    if (skills.empty())
        return "-";
    std::vector<ProfessionSkill> sorted = skills;
    for (size_t i = 1; i < sorted.size(); ++i)
    {
        ProfessionSkill key = sorted[i];
        size_t j = i;
        while (j > 0 && sorted[j - 1].skillId > key.skillId)
        {
            sorted[j] = sorted[j - 1];
            --j;
        }
        sorted[j] = key;
    }
    std::string out;
    for (ProfessionSkill const& skill : sorted)
    {
        char pair[32];
        std::snprintf(pair, sizeof(pair), "%u:%u", skill.skillId, skill.value);
        if (!out.empty())
            out += ";";
        out += pair;
    }
    return out;
}

} // namespace ClaimedProfsPolicy
} // namespace TortoiseBots
