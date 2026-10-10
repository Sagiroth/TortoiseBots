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

    // Donor order (Aq20Triggers.cpp:16-36): decide on buff/debuff timing
    // BEFORE scanning — the 200yd GO sweep runs only inside the planning
    // window, never during 45s hold phases.
    const bool buffUp = ai->HasAura(25176, boss);
    if (buffUp)
        return true;
    const int32 debuffMs = OssirianWeaknessMsLeft(ai, boss);
    if (debuffMs < 5000)
        return true;
    if (debuffMs >= 30000)
        return false;

    // Boss-relative crystal (the raid kites Ossirian to one of the two;
    // a bot-relative pick strands bots at the wrong crystal). Bail when
    // no crystal is up rather than feeding runMs = 0.
    GameObject* crystal = NearestOssirianCrystalToBoss(boss);
    if (!crystal)
        return false;

    // Run time from the bot to the boss's crystal (true run speed, so
    // slows/speed effects estimate correctly).
    const float speed = bot->GetSpeed(MOVE_RUN);
    const int32 runMs = speed > 0.0f
        ? int32(bot->GetDistance(crystal) / speed * 1000.0f)
        : 0;

    return ShouldRunToOssirianCrystal(true, false, debuffMs, runMs);
}
