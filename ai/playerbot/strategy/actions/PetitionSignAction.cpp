
#include "playerbot/playerbot.h"
#include "PetitionSignAction.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "runtime/AltBotPolicy.h"
#include "runtime/AltBotService.h"
#include "../../../runtime/HireLifecycle.h"

using namespace ai;

bool PetitionSignAction::Execute(Event& event)
{
    Player* requester = event.GetOwner() ? event.GetOwner() : GetMaster();
    WorldPacket p(event.GetPacket());
    p.rpos(0);
    ObjectGuid petitionGuid;
    ObjectGuid inviter;
    uint8 unk = 0;
    p >> petitionGuid >> inviter;
    uint32 type = 9;


    bool accept = true;

    if (type != 9)
    {
    }
    else
    {
        if (bot->GetGuildId())
        {
            ai->TellError(requester, "Sorry, I am in a guild already");
            accept = false;
        }

        if (bot->GetGuildIdInvited())
        {
            ai->TellError(requester, "Sorry, I am invited to a guild already");
            accept = false;
        }

        if (TortoiseBots::HireLifecycle::Instance().IsHired(bot->GetObjectGuid()))
        {
            ai->TellError(requester, "I am a hired mercenary and cannot sign a guild charter.");
            accept = false;
        }

        // check for same acc id
        /*QueryResult* result = CharacterDatabase.PQuery("SELECT playerguid FROM petition_sign WHERE player_account = '%u' AND petitionguid = '%u'", bot->GetSession()->GetAccountId(), petitionGuid.GetCounter());

        if (result)
        {
            ai->TellError("Sorry, I already signed this pettition");
            accept = false;
        }
        delete result;*/
    }

    Player* _inviter = sObjectMgr.GetPlayer(inviter);
    if (!_inviter)
        return false;

    if (_inviter == bot)
        return false;
    // Issue #872 (F40): an autonomous alt signs a charter only for the
    // owner's account. Same gate as the guild invite (GuildAcceptAction).
    if (TortoiseBots::AltBotService::Instance().IsAutonomous(bot->GetObjectGuid()))
    {
        WorldSession const* inviterSession = _inviter->GetSession();
        bool const isGm = inviterSession && inviterSession->GetSecurity() > SEC_PLAYER;
        uint32_t const ownerAccount =
            TortoiseBots::AltBotService::Instance().GetOwnerAccount(bot->GetGUIDLow());
        bool const sameAccount = inviterSession && ownerAccount != 0 &&
            inviterSession->GetAccountId() == ownerAccount;
        if (!TortoiseBots::AltBotMayAcceptGuildInvite(sameAccount, isGm))
            accept = false;
    }

    if (!accept || !ai->GetSecurity()->CheckLevelFor(PlayerbotSecurityLevel::PLAYERBOT_SECURITY_GUILD, false, _inviter, true))
    {
        WorldPacket data(MSG_PETITION_DECLINE);
        data << petitionGuid;
        bot->GetSession()->HandlePetitionDeclineOpcode(data);
        sLog.outDetail("Bot #%d <%s> declines guild invite", bot->GetGUIDLow(), bot->GetName());
        return false;
    }
    if (accept)
    {
        WorldPacket data(CMSG_PETITION_SIGN, 20);
        data << petitionGuid << unk;
        bot->GetSession()->HandlePetitionSignOpcode(data);
        sLog.outDetail("Bot #%d <%s> accepts guild invite", bot->GetGUIDLow(), bot->GetName());
        return true;
    }
    return false;
}
