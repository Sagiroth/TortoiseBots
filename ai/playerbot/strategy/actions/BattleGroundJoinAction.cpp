#include "playerbot/playerbot.h"
#include "BattleGroundJoinAction.h"

#include "Battlegrounds/BattleGround.h"
#include "Battlegrounds/BattleGroundMgr.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/strategy/values/PositionValue.h"
#include "runtime/BotManager.h"
#include "playerbot/RandomBotFacade.h"

using namespace ai;

namespace
{
const char* BattlegroundName(BattleGroundTypeId type)
{
    switch (type)
    {
        case BATTLEGROUND_AV: return "AV";
        case BATTLEGROUND_WS: return "WSG";
        case BATTLEGROUND_AB: return "AB";
        case BATTLEGROUND_SV: return "SV";
        default: return "BG";
    }
}

void ResetBattlegroundState(PlayerbotAI* ai)
{
    ai->ResetStrategies();
    ai->GetAiObjectContext()->GetValue<uint32>("bg type")->Set(0);
    ai->GetAiObjectContext()->GetValue<uint32>("bg role")->Set(0);

    ai::PositionMap& positions = ai->GetAiObjectContext()->GetValue<ai::PositionMap&>("position")->Get();
    ai::PositionEntry objective = positions["bg objective"];
    objective.Reset();
    positions["bg objective"] = objective;
}

void ReleaseOwnedBot(PlayerbotAI* ai)
{
    Player* bot = ai->GetBot();
    if (!bot || !sRandomBotFacade.IsFreeBot(bot))
        return;

    if (!TortoiseBots::BotManager::Instance().ClearBotMaster(bot->GetObjectGuid()))
        sLog.outError("TortoiseBots: failed to clear durable master for %s after battleground transition", bot->GetName());
}
}

bool BGLeaveAction::Execute(Event& event)
{
    if (!(bot->InBattleGroundQueue() || bot->InBattleGround()))
        return false;

    BattleGroundQueueTypeId queueType = bot->GetBattleGroundQueueTypeId(0);
    BattleGroundTypeId bgType = sServerFacade.BGTemplateId(queueType);

    if (bot->InBattleGround())
    {
        WorldPacket packet(CMSG_LEAVE_BATTLEFIELD);
        packet << uint8(0) << uint8(0) << uint16(0);
        bot->GetSession()->HandleLeaveBattlefieldOpcode(packet);
    }
    else
    {
        uint32 mapId = GetBattleGrounMapIdByTypeId(bgType);
        if (!mapId && event.GetSource().empty())
            return false;

        WorldPacket packet(CMSG_BATTLEFIELD_PORT, 8);
        packet << mapId << uint8(0);
        if (!event.GetSource().empty())
            bot->GetSession()->HandleLeaveBattlefieldOpcode(packet);
        else
            bot->GetSession()->HandleBattleFieldPortOpcode(packet);
    }

    ReleaseOwnedBot(ai);
    ResetBattlegroundState(ai);
    return true;
}

bool BGStatusAction::isUseful()
{
    return bot->InBattleGroundQueue();
}

bool BGStatusAction::Execute(Event& event)
{
    WorldPacket packet(event.GetPacket());
    if (packet.empty())
        return false;
    packet.rpos(0);

    uint32 queueSlot = 0;
    uint32 mapId = 0;
    uint8 unknown = 0;
    uint32 instanceId = 0;
    uint32 status = 0;
    uint32 time1 = 0;
    uint32 time2 = 0;

    packet >> queueSlot >> mapId >> unknown >> instanceId >> status;
    if (!mapId)
        return false;

    switch (status)
    {
        case STATUS_WAIT_QUEUE:
            packet >> time1 >> time2;
            return true;
        case STATUS_WAIT_JOIN:
            packet >> time1;
            break;
        case STATUS_IN_PROGRESS:
            packet >> time1 >> time2;
            return false;
        default:
            sLog.outError("TortoiseBots: unknown Vanilla battleground status %u", status);
            return false;
    }

    BattleGroundQueueTypeId queueType = bot->GetBattleGroundQueueTypeId(queueSlot);
    BattleGroundTypeId bgType = sServerFacade.BGTemplateId(queueType);
    sLog.outDetail("Bot #%u <%s> received battleground invite for %s", bot->GetGUIDLow(), bot->GetName(), BattlegroundName(bgType));

    if (time1 == TIME_TO_AUTOREMOVE)
    {
        ReleaseOwnedBot(ai);
        ResetBattlegroundState(ai);
        return false;
    }

    ai->Unmount();
    ai->GetAiObjectContext()->GetValue<uint32>("bg type")->Set(queueType);

    WorldPacket port(CMSG_BATTLEFIELD_PORT, 8);
    port << mapId << uint8(1);
    bot->GetSession()->HandleBattleFieldPortOpcode(port);

    ai->ResetStrategies(false);
    ai->ChangeStrategy("+pvp", BotState::BOT_STATE_COMBAT);
    ai->ChangeStrategy("+pvp", BotState::BOT_STATE_NON_COMBAT);
    ai->GetAiObjectContext()->GetValue<uint32>("bg role")->Set(urand(0, 9));
    return true;
}

bool BGStatusCheckAction::Execute(Event& /*event*/)
{
    if (bot->IsBeingTeleported())
        return false;

    WorldPacket packet(CMSG_BATTLEFIELD_STATUS);
    bot->GetSession()->HandleBattlefieldStatusOpcode(packet);
    return true;
}

bool BGStatusCheckAction::isUseful()
{
    return bot->InBattleGroundQueue();
}
