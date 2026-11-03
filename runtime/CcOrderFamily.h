#pragma once

// The family an automatic crowd control order names (runtime/DungeonRunPolicy.h
// writes it after `cc <mark> <guid> <bot>`). The bot's class rows read it so
// only the spell the run picked fires on the mark: a succubus Seduction order
// never turns into the warlock's higher-priority Fear. A hand-authored
// order carries none, and then every row keeps its own rules.
//
// Pure and tiny: the run policy, the command parser and the class triggers
// share it without dragging the policy header into the AI.

#include <cctype>
#include <string>

namespace TortoiseBots
{
namespace DungeonRun
{
// A family token is its spell name without spaces or case ("shackleundead",
// "freezingtrap"), so one token survives the whitespace-split order text.
inline std::string CcFamilyTokenNorm(std::string const& text)
{
    std::string out;
    out.reserve(text.size());
    for (char c : text)
    {
        unsigned char const u = static_cast<unsigned char>(c);
        if (std::isalnum(u))
            out.push_back(static_cast<char>(std::tolower(u)));
    }
    return out;
}

// "" and "none" mean no run cc family is assigned: nothing is gated.
inline bool CcOrderFamilyAllows(std::string const& orderFamily, std::string const& spell)
{
    std::string const token = CcFamilyTokenNorm(orderFamily);
    return token.empty() || token == "none" || CcFamilyTokenNorm(spell) == token;
}
}
}
