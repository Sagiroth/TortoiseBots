
#include "playerbot/playerbot.h"
#include "PetitionSignAction.h"

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
        bot->Say("Thanks for the invite!", LANG_UNIVERSAL);
        sLog.outDetail("Bot #%d <%s> accepts guild invite", bot->GetGUIDLow(), bot->GetName());
        return true;
    }
    return false;
}
