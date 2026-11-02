#pragma once

// The dashboard feeds whose key is set in both config files (aiplayerbot.conf and the world config the module's conf.dist
// is included into): a feed is on only while both files allow it, so a 0 in either turns it off. Read() runs at startup
// and again on the world thread after `.reload config`, when the core has re-read the world file and PlayerbotAIConfig has
// re-read aiplayerbot.conf, so the 0 in the file that was the one turning the feed on takes effect. Standard library
// only, so tools/test_observability_flag.cpp compiles it without a game header.

namespace TortoiseBots {
namespace ObservabilityFlag {

// The feed's switch as the last config read computed it.
struct Feed
{
    bool enabled = false;

    bool Read(bool aiFile, bool worldFile)
    {
        enabled = aiFile && worldFile;
        return enabled;
    }
};

} // namespace ObservabilityFlag
} // namespace TortoiseBots
