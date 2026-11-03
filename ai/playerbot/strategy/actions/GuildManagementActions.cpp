
#include "playerbot/playerbot.h"
#include "GuildManagementActions.h"
#include "playerbot/ServerFacade.h"
#include "runtime/BotActivityLease.h"
#include "../../../runtime/AltBotService.h"

using namespace ai;

Player* GuidManageAction::GetPlayer(Event event)
{
    Player* player = nullptr;
    ObjectGuid guid = event.getObject();

    if (guid)
    {
        player = sObjectMgr.GetPlayer(guid);

        if (player)
            return player;
    }

    std::string text = event.GetParam();

    if (!text.empty())
    {
        if (normalizePlayerName(text))
        {
            player = ObjectAccessor::FindPlayerByName(text.c_str());

            if (player)
                return player;
        }

        return nullptr;
    }

    Player* master = GetMaster();
    if (master && master == event.GetOwner())
        guid = bot->GetSelectionGuid();

    player = sObjectMgr.GetPlayer(guid);

    if (player)
        return player;

    player = event.GetOwner();

    if (player)
       return player;

    return nullptr;
}

bool GuidManageAction::Execute(Event& event)
{
    Player* player = GetPlayer(event);

    if (!player || !PlayerIsValid(player) || player == bot)
        return false;

    WorldPacket data = GetPacket(player);

    SendPacket(data, event);

    return true;
}

bool GuildManageNearbyAction::Execute(Event& event)
{
    if (!bot->GetGuildId() || TortoiseBots::GuildOrders::BotRankBlocksManagement(bot->GetGuildId()))
        return false;
    Guild* guild = sGuildMgr.GetGuildById(bot->GetGuildId());
    MemberSlot* botMember = guild->GetMemberSlot(bot->getObjectGuid());

    std::list<ObjectGuid> nearGuids = ai->GetAiObjectContext()->GetValue<std::list<ObjectGuid> >("nearest friendly players")->Get();
    for (auto& guid : nearGuids)
    {
        Player* player = sObjectMgr.GetPlayer(guid);

        if (!player || bot == player)
            continue;

        if (player->IsDND())
            continue;


        if(player->GetGuildId()) //Promote or demote nearby members based on chance.
        {
            MemberSlot* member = guild->GetMemberSlot(player->getObjectGuid());
            uint32 dCount = AI_VALUE(uint32, "death count");

            if (!urand(0, 30) && dCount < 2 && guild->HasRankRight(botMember->RankId, GR_RIGHT_PROMOTE) && bot->GetRank() + 1 < player->GetRank())
            {
                ai->DoSpecificAction("guild promote", Event("guild management", guid), true);
                continue;
            }

            if (!urand(0, 30) && dCount > 2 && guild->HasRankRight(botMember->RankId, GR_RIGHT_DEMOTE) && bot->GetRank() < player->GetRank() && player->GetRank() > guild->GetLowestRank()) {
                ai->DoSpecificAction("guild demote", Event("guild management", guid), true);
                continue;
            }

            continue;
        }

        if (!sPlayerbotAIConfig.randomBotGuildNearby)
            return false;

        if (guild->GetMemberSize() >= sPlayerbotAIConfig.guildMaxBotLimit)
            return false;

        if (!guild->HasRankRight(botMember->RankId, GR_RIGHT_INVITE))
            continue;

        if (guild->GetMemberSize() >= ai->GetMaxPreferedGuildSize())
            continue;

        if (player->GetGuildIdInvited())
            continue;

        if (!sPlayerbotAIConfig.randomBotInvitePlayer && isRealPlayer_Helper(player))
            continue;

        PlayerbotAI* botAi = PlayerbotAIStorage::Instance().GetAI(player);

        if (botAi)
        {
            if (botAi->GetGuilderType() == GuilderType::SOLO) //Do not invite solo players.
                continue;

            if (botAi->HasActivePlayerMaster() && !sRandomBotFacade.IsRandomBot(player)) //Do not invite alts of active players.
                continue;

            if (guild->GetMemberSize() >= botAi->GetMaxPreferedGuildSize() || guild->GetMemberSize() < botAi->GetMaxPreferedGuildSize() / 4)
                continue;


        }

        bool sameGroup = bot->GetGroup() && bot->GetGroup()->IsMember(player->getObjectGuid());

        if (!sameGroup && sServerFacade.getDistance2d(bot, player) > sPlayerbotAIConfig.spellDistance)
            continue;

        if (ai->DoSpecificAction("guild invite", Event("guild management", guid), true))
            return true;
    }

    return false;
}

bool GuildManageNearbyAction::isUseful()
{
    // Lease gate (issue #89): Idle/Grinding/PlayerMaster only.
    TortoiseBots::BotActivity activity = TortoiseBots::BotActivityLeaseManager::Instance().GetActivity(bot->GetGUIDLow());
    if (activity != TortoiseBots::BotActivity::Idle && activity != TortoiseBots::BotActivity::Grinding &&
        activity != TortoiseBots::BotActivity::PlayerMaster)
        return false;
    if (!bot->GetGuildId())
        return false;
    // Bots in a guild with a real player member hold one shared bot rank:
    // no promoting, demoting or inviting, so the old behaviour stays and
    // the bots never fight the rank setting.
    if (TortoiseBots::GuildOrders::BotRankBlocksManagement(bot->GetGuildId()))
        return false;

    Guild* guild = sGuildMgr.GetGuildById(bot->GetGuildId());
    MemberSlot* botMember = guild->GetMemberSlot(bot->getObjectGuid());

    return  guild->HasRankRight(botMember->RankId, GR_RIGHT_DEMOTE) || guild->HasRankRight(botMember->RankId, GR_RIGHT_PROMOTE) || guild->HasRankRight(botMember->RankId, GR_RIGHT_INVITE);
}

bool GuildLeaveAction::Execute(Event& event)
{
    Player* requester = event.GetOwner() ? event.GetOwner() : GetMaster();
    Player* owner = event.GetOwner();
    if (owner && !ai->GetSecurity()->CheckLevelFor(PlayerbotSecurityLevel::PLAYERBOT_SECURITY_GUILD, false, owner, true))
    {
        ai->TellError(requester, "Sorry, I am happy in my guild :)");
        return false;
    }
    // Issue #872 (F40): an autonomous alt stays in its guild; it never
    // leaves on its own. The owner still may (their "guild leave" passes the
    // chat gate); anyone else is refused here as well, so automation and
    // trigger paths can never take it out either.
    if (TortoiseBots::AltBotService::Instance().IsAutonomous(bot->GetObjectGuid()) &&
        owner && !ai->IsBotOwnerOrGm(*owner))
    {
        ai->TellError(requester, "Sorry, I am happy in my guild :)");
        return false;
    }

    Guild* guild = sGuildMgr.GetGuildById(bot->GetGuildId());

    sPlayerbotAIConfig.logEvent(ai, "GuildLeaveAction", guild->GetName(), std::to_string(guild->GetMemberSize()));

    WorldPacket packet;
    bot->GetSession()->HandleGuildLeaveOpcode(packet);
    return true;
}
