#pragma once

// Issue #382: some hired bots never posted their spec and spells in party
// chat. The observed chat was never a hire message at all: it is the
// levelup-triggered automation ("Upgrading spec ..." in AutoSelectTalents,
// "I have learned the spells: ..." in AutoLearnSpellAction) firing off the
// SMSG_LEVELUP_INFO the provisioner's GiveLevel sends. That path is racy
// (it can run before the bot is grouped, while the master pointer is stale,
// or across maps past WhisperDistance, and TellPlayer drops all of those
// silently) and content-dependent (no chat below level 10, no spells chat
// when the factory already taught everything). Nothing in it is
// class-specific, which is why the failure looked inconsistent.
//
// The fix posts a dedicated intro once the hire is confirmed grouped, and
// swallows the provision jump's levelup echo so it cannot double-post.
// This header is the pure, core-free part of that: message shaping and the
// once-guard, unit-tested standalone (tools/test_hire_intro_policy.cpp).

#include <cstddef>
#include <string>
#include <vector>

namespace TortoiseBots
{

// "My spec is frost mage (dps), talents 0/7/0." Role is omitted when empty
// (unknown), never invented.
inline std::string ComposeHireSpecLine(std::string const& specName, std::string const& className,
    std::string const& roleWord, int tree0, int tree1, int tree2)
{
    std::string line = "My spec is " + specName + " " + className;
    if (!roleWord.empty())
        line += " (" + roleWord + ")";
    line += ", talents " + std::to_string(tree0) + "/" + std::to_string(tree1) + "/" +
        std::to_string(tree2) + ".";
    return line;
}

// Join preformatted spell names ("[Sinister Strike Rank 2] (1757)") for one
// chat line. Caps both count and bytes so the line is never truncated by the
// client: leftovers collapse into ", and N more". At least one name is always
// kept whole; empty input yields "" (caller posts the spec line alone).
inline std::string FormatHireSpellList(std::vector<std::string> const& names, size_t maxNames = 10,
    size_t maxChars = 200)
{
    if (names.empty())
        return "";
    size_t shown = names.size() > maxNames ? maxNames : names.size();
    std::string joined;
    auto join = [&]()
    {
        joined.clear();
        for (size_t i = 0; i < shown; ++i)
        {
            if (i)
                joined += ", ";
            joined += names[i];
        }
    };
    join();
    while (shown > 1 && joined.size() > maxChars)
    {
        --shown;
        join();
    }
    size_t omitted = names.size() - shown;
    if (omitted > 0)
        joined += ", and " + std::to_string(omitted) + " more";
    return joined;
}

// The intro goes out exactly once, and only when both ends are live. Callers
// hold the actual send; this only decides.
inline bool ShouldAnnounceHireIntro(bool introSent, bool botReady, bool masterReady)
{
    return !introSent && botReady && masterReady;
}

} // namespace TortoiseBots
