#include "playerbot/playerbot.h"
#include "RuinsOfAhnqirajDungeonActions.h"
#include "playerbot/OssirianCrystalPolicy.h"
#include "playerbot/strategy/OssirianCrystalHelper.h"

using namespace ai;

bool UseOssirianCrystalAction::Execute(Event& event)
{
    if (!IsOssirianCrystalRunner(ai, bot))
        return false;

    Unit* boss = FindOssirianBoss(ai, bot);
    if (!boss)
        return false;

    GameObject* crystal = NearestOssirianCrystalToBoss(boss);
    if (!crystal)
        return false;

    if (bot->GetDistance(crystal) > INTERACTION_DISTANCE)
        return MoveTo(bot->GetMapId(), crystal->GetPositionX(), crystal->GetPositionY(), crystal->GetPositionZ());

    // In range: hold position until the buff is up (or the weakness is
    // nearly out) and Ossirian is close. Returning true keeps the engine
    // from falling back to combat actions that would walk the bot back
    // to the boss and oscillate.
    if (!ShouldUseOssirianCrystal(boss->GetDistance(crystal),
        crystal->HasFlag(GAMEOBJECT_FLAGS, GO_FLAG_IN_USE),
        ai->HasAura(25176, boss), OssirianWeaknessMsLeft(ai, boss)))
    {
        ai->StopMoving();
        return true;
    }

    if (!bot->GetGameObjectIfCanInteractWith(crystal->getObjectGuid()))
        return false;

    std::unique_ptr<WorldPacket> packet(new WorldPacket(CMSG_GAMEOBJ_USE));
    *packet << crystal->getObjectGuid();
    bot->GetSession()->QueuePacket(packet.release());
    return true;
}

bool UseOssirianCrystalAction::isPossible()
{
    return IsOssirianCrystalRunner(ai, bot) && ai->CanMove();
}
