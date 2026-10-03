#pragma once

#include <cstdint>

namespace ai
{
    // Pull cadence vs regen cadence (pool death fix, m-deaths findings 1/3/7):
    // a third of pool deaths land within 60 s of the victim's previous kill
    // and the median gap between attack orders is 31 s, while nothing checked
    // HP/mana before the next pull - AttackAnythingAction::isUseful had no
    // health term and GrindTargetValue none either. The rules below are pure
    // data (thresholds stay in PlayerbotAIConfig), so they are decided here
    // and tested on their own.
    //
    // Donor comparison (mod-playerbots, read-only reference): the donor has
    // neither gate - its GrindTargetValue and AttackAnythingAction::isUseful
    // pick without a regen check - so there is nothing to port; both rules
    // are local, measured on the level 1-12 pool (night2-4h: 6492 deaths).

    // Highest level above the bot a grind order may have. A character below
    // level 10 has weapon skill 5 and no abilities, so of the orders it
    // placed on a mob two or more levels above it about 1% ended in a kill
    // (0.3% for melee) against 16-28% at the bot's own level - and those
    // orders were 15% of all grind orders in a measured level-1 pool. Only
    // the solo grind is restricted: a bot following a real player is told
    // what to fight, and the battleground exemption stays where it always
    // was, on the check itself.
    inline int PullGrindLevelCap(std::uint32_t botLevel, bool hasRealPlayerMaster)
    {
        if (botLevel < 10 && !hasRealPlayerMaster)
            return 1;

        return 4;
    }

    inline bool PullLevelWithinCap(int targetLevel, std::uint32_t botLevel, bool hasRealPlayerMaster)
    {
        return targetLevel - (int)botLevel <= PullGrindLevelCap(botLevel, hasRealPlayerMaster);
    }

    // Whether a quest objective / quest-loot creature is a valid destination
    // for a pool bot: a creature the bot would refuse as a grind order is no
    // quest destination either. Live pool (Oct 2026, fresh level-1 Elwynn
    // bots): 62% of level 1-4 deaths were by mobs 2+ levels above, the worst
    // a level-2 bot on a quest-loot trip (item 750, entry 69, level_max 2)
    // dying to the level 5-6 neighbours sharing its field (Defias Cutpurse,
    // Mangy Wolf, Forest Spider). The check runs on the static creature
    // template, so it stays safe wherever the destination filter runs (async
    // search). Vendors are exempt: buying the item needs no fight.
    // Owned/hired bots keep today's behaviour: their player decides.
    inline bool QuestObjectiveLevelFits(int creatureLevelMax, std::uint32_t botLevel,
        bool masterlessRandom, bool vendorObjective)
    {
        if (vendorObjective || !masterlessRandom)
            return true;

        return PullLevelWithinCap(creatureLevelMax, botLevel, false);
    }

    // Whether a wounded bot must sit out the next NEW pull: health below
    // mediumHealth, or (for mana users only) mana below mediumMana. The call
    // site skips this for revenge targets (the mob already attacks the bot),
    // and for owned/hired bots and battlegrounds, which keep today's
    // behaviour.
    inline bool ShouldDeferGrindPull(std::uint8_t healthPct, bool hasMana, std::uint8_t manaPct,
        std::uint32_t mediumHealth, std::uint32_t mediumMana)
    {
        if (healthPct < mediumHealth)
            return true;

        return hasMana && manaPct < mediumMana;
    }

    // The pre-emptive "attack before being attacked" strike while travelling:
    // it only starts a fresh pull when no possible adds lurk nearby and the
    // mob is inside the grind level cap. A mob already fighting the bot keeps
    // the old rule at the call site.
    inline bool AllowPreemptiveStrike(int targetLevel, std::uint32_t botLevel, bool hasRealPlayerMaster,
        bool possibleAdds)
    {
        if (possibleAdds)
            return false;

        return PullLevelWithinCap(targetLevel, botLevel, hasRealPlayerMaster);
    }
}
