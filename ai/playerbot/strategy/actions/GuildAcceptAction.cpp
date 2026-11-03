
#include "playerbot/playerbot.h"
#include "GuildAcceptAction.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/RandomBotFacade.h"
#include "Guild/GuildMgr.h"
#include "../../../runtime/ClaimLifecycle.h"
#include "../../../runtime/policy/hire/ClaimedBotPolicy.h"
#include "../../../runtime/HireLifecycle.h"
#include "runtime/AltBotPolicy.h"
#include "runtime/AltBotService.h"

using namespace ai;

bool GuildAcceptAction::Execute(Event& event)
{
    Player* requester = event.GetOwner() ? event.GetOwner() : GetMaster();
    WorldPacket p(event.GetPacket());
    p.rpos(0);
    Player* inviter = nullptr;
    std::string Invitedname;
    p >> Invitedname;

    if (normalizePlayerName(Invitedname))
        inviter = ObjectAccessor::FindPlayerByName(Invitedname.c_str());

    if (!inviter)
        return false;

    bool accept = true;
    uint32 guildId = inviter->GetGuildId();
    if (!guildId)
    {
        ai->TellError(requester, "You are not in a guild!");

        accept = false;
    }
    else if (bot->GetGuildId())
    {
        ai->TellError(requester, "Sorry, I am in a guild already");

        accept = false;
    }
    else if (TortoiseBots::HireLifecycle::Instance().IsHired(bot->GetObjectGuid()))
    {
        ai->TellError(requester, "I am a hired mercenary and cannot join a guild.");

        accept = false;
    }
    // Issue #872 (F40): an autonomous alt accepts a guild invite only from
    // the owner's account. A pool bot takes any invite its security level
    // allows; an alt never does.
    else if (TortoiseBots::AltBotService::Instance().IsAutonomous(bot->GetObjectGuid()))
    {
        WorldSession const* inviterSession = inviter->GetSession();
        bool const isGm = inviterSession && inviterSession->GetSecurity() > SEC_PLAYER;
        uint32_t const ownerAccount =
            TortoiseBots::AltBotService::Instance().GetOwnerAccount(bot->GetGUIDLow());
        bool const sameAccount = inviterSession && ownerAccount != 0 &&
            inviterSession->GetAccountId() == ownerAccount;
        if (!TortoiseBots::AltBotMayAcceptGuildInvite(sameAccount, isGm))
            accept = false;
        else if (!ai->GetSecurity()->CheckLevelFor(PlayerbotSecurityLevel::PLAYERBOT_SECURITY_GUILD, false, inviter, true))
        {
            ai->TellError(requester, "Sorry, I don't want to join your guild :(");

            accept = false;
        }
    }
    else if (!ai->GetSecurity()->CheckLevelFor(PlayerbotSecurityLevel::PLAYERBOT_SECURITY_GUILD, false, inviter, true))
    {
        ai->TellError(requester, "Sorry, I don't want to join your guild :(");

        accept = false;
    }

    Guild* guild = sGuildMgr.GetGuildById(guildId);

    if(guild && guild->GetMemberSize() > 1000)
    {
        ai->TellError(requester, "This guild has over 1000 members. To stop it from reaching the 1064 member limit I refuse to join it.");

        accept = false;
    }

    WorldPacket packet;
    if (accept)
    {
        bot->GetSession()->HandleGuildAcceptOpcode(packet);

        TalentSpec::SetPublicNote(bot);

        // Issue #489: Claim a wandering world bot into the player's guild.
        // The bot already passed the unguilded gates above (inviter has a guild,
        // bot has none). Enforce the remaining MayClaimBot gates here: pool bot,
        // not already claimed, same faction.
        if (inviter && inviter->GetSession() && !inviter->GetSession()->IsHeadless() &&
            sRandomBotFacade.IsRandomBot(bot))
        {
            uint32 inviterAccountId = inviter->GetSession()->GetAccountId();
            uint32 inviterGuidLow = inviter->GetGUIDLow();
            uint32 botGuidLow = bot->GetGUIDLow();
            bool const isAlreadyClaimed = TortoiseBots::ClaimLifecycle::Instance().IsClaimed(botGuidLow);
            bool const isOpposingFaction = bot->GetTeam() != inviter->GetTeam();
            bool const isHired = TortoiseBots::HireLifecycle::Instance().IsHired(bot->GetObjectGuid());
            if (!TortoiseBots::MayClaimBot(true, true, isAlreadyClaimed, isOpposingFaction, isHired))
            {
                sLog.outBasic("TortoiseBots: claim refused for bot %u (already claimed, opposing faction, or hired)", botGuidLow);
            }
            else if (TortoiseBots::ClaimLifecycle::Instance().Claim(botGuidLow, inviterAccountId, inviterGuidLow, guildId))
            {
                ai->Whisper("I am honored to join your guild! I will keep my gear ready for your raids.", inviter->GetName());
            }
        }

        sPlayerbotAIConfig.logEvent(ai, "GuildAcceptAction", guild->GetName(), std::to_string(guild->GetMemberSize()));
    }
    else
    {
        bot->GetSession()->HandleGuildDeclineOpcode(packet);
    }
    return true;
}