#pragma once
#include <string>

// Pure policy for shaman weapon-imbue upkeep (issue #406): the buff
// strategies used to queue one fixed imbue while ShamanWeaponTrigger stayed
// true on any known imbue, so every shaman below 30 failed the windfury cast
// every tick (ACTION_LOOP telemetry) and enhancement bots at 10-29 fell back
// to rockbiter instead of flametongue. Rank the imbues best-first and take
// the first the bot knows, so upkeep never stalls on an untrained rank.
// Order matches the class doc (Windfury/Flametongue for Enhancement,
// Flametongue otherwise, Rockbiter/Frostbrand fallbacks) and the donor
// fallback chains: flametongue outranks frostbrand (gated 10 vs 20, and rank
// 2/3 flametongue is known by the time frostbrand is). Level gates for
// reference: rockbiter 1/8/16, flametongue 10/18/26, frostbrand 20/28,
// windfury 30 (tw_world.spell_template). Windfury stays a last resort for
// non-enhancement so a bot that somehow knows only windfury still keeps an
// imbue rather than none.

namespace ai
{
    inline std::string BestKnownShamanImbue(bool enhancement, bool windfury, bool flametongue, bool frostbrand, bool rockbiter)
    {
        if (enhancement && windfury)
            return "windfury weapon";
        if (flametongue)
            return "flametongue weapon";
        if (frostbrand)
            return "frostbrand weapon";
        if (rockbiter)
            return "rockbiter weapon";
        if (windfury)
            return "windfury weapon";
        return "";
    }
}
