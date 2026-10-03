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

    // Tick gate for the upkeep action (live ACTION_LOOP on 'shaman weapon
    // upkeep'): the cast gate refuses a second cast while one runs, fails
    // while sitting/kneeling (eat/drink/loot/skin), airborne or stunned -
    // and with no wait-for-spell delay every one of those ticks logged a
    // FAILED upkeep at tick speed. The action stands down unless an imbue is
    // known, the weapon lacks one, and the bot is castable right now; the
    // trigger re-fires once it is.
    inline bool ShamanUpkeepShouldAttempt(bool imbueKnown, bool alreadyImbued, bool castBlocked)
    {
        return imbueKnown && !alreadyImbued && !castBlocked;
    }
}
