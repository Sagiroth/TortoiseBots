#include "playerbot/playerbot.h"
#include "RuinsOfAhnqirajDungeonTriggers.h"
#include "playerbot/OssirianCrystalPolicy.h"
#include "playerbot/strategy/OssirianCrystalHelper.h"

using namespace ai;

bool OssirianCrystalRunTrigger::IsActive()
{
    // Cheap role gate first: tanks/healers never run, before any scan.
    if (!IsOssirianCrystalRunner(ai, bot))
        return false;

    Unit* boss = FindOssirianBoss(ai, bot);
    if (!boss || !boss->IsInCombat())
        return false;

    // Boss-relative crystal (the raid kites Ossirian to one of the two;
    // a bot-relative pick strands bots at the wrong crystal). Bail when
    // no crystal is up rather than feeding runMs = 0.
    GameObject* crystal = NearestOssirianCrystalToBoss(boss);
    if (!crystal)
        return false;

    const bool buffUp = ai->HasAura(25176, boss);
    const int32 debuffMs = OssirianWeaknessMsLeft(ai, boss);

    // Run time from the bot to the boss's crystal (~7yd/s run speed).
    const int32 runMs = int32(bot->GetDistance(crystal) / 7.0f * 1000.0f);

    return ShouldRunToOssirianCrystal(true, buffUp, debuffMs, runMs);
}
