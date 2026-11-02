#include "playerbot/playerbot.h"
#include "PlayerbotAI.h"
#include "Chat/ChannelMgr.h"

using namespace ai;

namespace
{
// Prepend a space if the message starts with '.' or '!' (and is not '..' or '!!').
// ChatHandler::ParseCommands only triggers when text[0] is '.' or '!', so a leading
// space prevents command execution while allowing the message to display in chat.
static void SanitizeCommandLikeChat(std::string& msg)
{
    if (!msg.empty() && (msg[0] == '.' || msg[0] == '!') && (msg.size() < 2 || (msg[1] != '.' && msg[1] != '!')))
    {
        msg.insert(0, " ");
    }
}

} // namespace

ChatChannelSource PlayerbotAI::GetChatChannelSource(Player* bot, uint32 type, std::string channelName)
{
    if (type == CHAT_MSG_CHANNEL)
    {
        if (channelName == "World")
        {
            return ChatChannelSource::SRC_WORLD;
        }
        else
        {
            ChannelMgr* cMgr = channelMgr(bot->GetTeam());
            if (!cMgr)
            {
                return ChatChannelSource::SRC_UNDEFINED;
            }

            const Channel* channel = cMgr->GetChannel(channelName, sObjectAccessor.FindPlayerPointer(bot->GetObjectGuid()));

            if (channel)
            {
                switch (channel->GetChannelId())
                {
                case ChatChannelId::GENERAL:
                {
                    return ChatChannelSource::SRC_GENERAL;
                }
                case ChatChannelId::TRADE:
                {
                    return ChatChannelSource::SRC_TRADE;
                }
                case ChatChannelId::LOCAL_DEFENSE:
                {
                    return ChatChannelSource::SRC_LOCAL_DEFENSE;
                }
                case ChatChannelId::WORLD_DEFENSE:
                {
                    return ChatChannelSource::SRC_WORLD_DEFENSE;
                }
                case ChatChannelId::LOOKING_FOR_GROUP:
                {
                    return ChatChannelSource::SRC_LOOKING_FOR_GROUP;
                }
                case ChatChannelId::GUILD_RECRUITMENT:
                {
                    return ChatChannelSource::SRC_GUILD_RECRUITMENT;
                }
                default:
                    return ChatChannelSource::SRC_UNDEFINED;
                }
            }
        }
    }
    else
    {
        switch (type)
        {
        case CHAT_MSG_WHISPER:
        {
            return ChatChannelSource::SRC_WHISPER;
        }
        case CHAT_MSG_SAY:
        {
            return ChatChannelSource::SRC_SAY;
        }
        case CHAT_MSG_YELL:
        {
            return ChatChannelSource::SRC_YELL;
        }
        case CHAT_MSG_GUILD:
        {
            return ChatChannelSource::SRC_GUILD;
        }
        case CHAT_MSG_PARTY:
        {
            return ChatChannelSource::SRC_PARTY;
        }
        case CHAT_MSG_RAID:
        case CHAT_MSG_RAID_LEADER:
        {
            return ChatChannelSource::SRC_RAID;
        }
        case CHAT_MSG_EMOTE:
        {
            return ChatChannelSource::SRC_EMOTE;
        }
        case CHAT_MSG_TEXT_EMOTE:
        {
            return ChatChannelSource::SRC_TEXT_EMOTE;
        }
        default:
            return ChatChannelSource::SRC_UNDEFINED;
        }
    }

    return ChatChannelSource::SRC_UNDEFINED;
}

bool PlayerbotAI::SayToGuild(std::string msg, bool likePlayer)
{
    if (msg.empty())
    {
        return false;
    }
    SanitizeCommandLikeChat(msg);

    if (bot->GetGuildId())
    {
        if (Guild* guild = sGuildMgr.GetGuildById(bot->GetGuildId()))
        {

            for (auto& player : sRandomBotFacade.GetPlayers())
            {
                // The facade view can outlive removed bots; resolve by GUID.
                Player* guildBot = sObjectAccessor.FindPlayer(ObjectGuid(HIGHGUID_PLAYER, player.first));
                if (!guildBot || !guildBot->IsInWorld())
                    continue;

                if (guildBot->GetGuildId() == bot->GetGuildId())
                {
                    if (likePlayer)
                    {
                        WorldPacket packet_template(CMSG_MESSAGECHAT);

                        uint32 lang = LANG_UNIVERSAL;

                        packet_template << CHAT_MSG_GUILD;
                        packet_template << LANG_UNIVERSAL;
                        packet_template << msg;

                        std::unique_ptr<WorldPacket> packetPtr(new WorldPacket(packet_template));

                        bot->GetSession()->QueuePacket(packetPtr.release());
                        return true;
                    }
                    break;
                }
            }
            if (!guild->HasRankRight(bot->GetRank(), GR_RIGHT_GCHATSPEAK))
            {
                return false;
            }

            guild->BroadcastToGuild(bot->GetSession(), msg.c_str(), LANG_UNIVERSAL);
            return true;
        }
    }

    return false;
}

static bool SayToNamedChannel(Player* bot, std::string const& name, std::string const& msg)
{
    if (!bot || msg.empty()) return false;
    ChannelMgr* mgr = channelMgr(bot->GetTeam());
    Channel* channel = mgr ? mgr->GetOrCreateChannel(name) : nullptr;
    if (!channel) return false;
    ObjectGuid guid = bot->GetObjectGuid();
    channel->Join(guid, "", false);
    channel->AsyncSay(guid, msg.c_str(), LANG_UNIVERSAL);
    return true;
}

bool PlayerbotAI::SayToWorld(std::string msg) { return SayToNamedChannel(bot, "World", msg); }
bool PlayerbotAI::SayToGeneral(std::string msg) { return SayToNamedChannel(bot, "General", msg); }
bool PlayerbotAI::SayToTrade(std::string msg) { return SayToNamedChannel(bot, "Trade", msg); }
bool PlayerbotAI::SayToLFG(std::string msg) { return SayToNamedChannel(bot, "LookingForGroup", msg); }
bool PlayerbotAI::SayToLocalDefense(std::string msg) { return SayToNamedChannel(bot, "LocalDefense", msg); }
bool PlayerbotAI::SayToWorldDefense(std::string msg) { return SayToNamedChannel(bot, "WorldDefense", msg); }
bool PlayerbotAI::SayToGuildRecruitment(std::string msg) { return SayToNamedChannel(bot, "GuildRecruitment", msg); }

bool PlayerbotAI::SayToParty(std::string msg, bool likePlayer)
{
    if (msg.empty())
        return false;

    SanitizeCommandLikeChat(msg);
    if (!bot->GetGroup())
    {
        return false;
    }

    if (likePlayer)
    {
        WorldPacket packet_template(CMSG_MESSAGECHAT);

        uint32 lang = LANG_UNIVERSAL;

        packet_template << CHAT_MSG_PARTY;
        packet_template << LANG_UNIVERSAL;
        packet_template << msg;

        std::unique_ptr<WorldPacket> packetPtr(new WorldPacket(packet_template));

        bot->GetSession()->QueuePacket(packetPtr.release());
        return true;
    }

    WorldPacket data;
    ChatHandler::BuildChatPacket(data, CHAT_MSG_PARTY, msg.c_str(), LANG_UNIVERSAL, CHAT_TAG_NONE, bot->getObjectGuid(), bot->GetName());

    for (auto reciever : GetPlayersInGroup())
    {
        sServerFacade.SendPacket(reciever, data);
    }

    return true;
}

bool PlayerbotAI::SayToRaid(std::string msg)
{
    if (msg.empty() || !bot->GetGroup() || !bot->GetGroup()->isRaidGroup())
    {
        return false;
    }

    WorldPacket data;
    ChatHandler::BuildChatPacket(data, CHAT_MSG_RAID, msg.c_str(), LANG_UNIVERSAL, CHAT_TAG_NONE, bot->getObjectGuid(), bot->GetName());

    for (auto reciever : GetPlayersInGroup())
    {
        sServerFacade.SendPacket(reciever, data);
    }

    return true;
}

bool PlayerbotAI::Yell(std::string msg, bool likePlayer)
{
    if (msg.empty())
        return false;

    SanitizeCommandLikeChat(msg);
    uint32 lang = LANG_UNIVERSAL;
    if (bot->GetTeam() == ALLIANCE)
    {
        lang =  LANG_COMMON;
    }
    else
    {
        lang = LANG_ORCISH;
    }

    if (likePlayer)
    {
        WorldPacket packet_template(CMSG_MESSAGECHAT);

        packet_template << CHAT_MSG_YELL;
        packet_template << lang;
        packet_template << msg;

        std::unique_ptr<WorldPacket> packetPtr(new WorldPacket(packet_template));

        bot->GetSession()->QueuePacket(packetPtr.release());
        return true;
    }

    bot->Yell(msg, lang);

    return true;
}

bool PlayerbotAI::Say(std::string msg, bool likePlayer)
{
    if (msg.empty())
        return false;

    SanitizeCommandLikeChat(msg);
    uint32 lang = LANG_UNIVERSAL;
    if (bot->GetTeam() == ALLIANCE)
    {
        lang =  LANG_COMMON;
    }
    else
    {
        lang = LANG_ORCISH;
    }

    if (likePlayer)
    {
        WorldPacket packet_template(CMSG_MESSAGECHAT);

        packet_template << CHAT_MSG_SAY;
        packet_template << lang;
        packet_template << msg;

        std::unique_ptr<WorldPacket> packetPtr(new WorldPacket(packet_template));

        bot->GetSession()->QueuePacket(packetPtr.release());
        return true;
    }

    bot->Say(msg, lang);

    return true;
}

bool PlayerbotAI::Whisper(std::string msg, std::string receiverName, bool likePlayer)
{
    if (msg.empty())
        return false;

    ObjectGuid receiver = sObjectMgr.GetPlayerGuidByName(receiverName);
    Player* rPlayer = sObjectMgr.GetPlayer(receiver);

    if (!rPlayer)
    {
        return false;
    }

    if (rPlayer == bot)
    {
        WorldPacket data;
        ChatHandler::BuildChatPacket(data, CHAT_MSG_WHISPER, msg.c_str(), LANG_UNIVERSAL, bot->GetChatTag(), bot->getObjectGuid(), bot->GetName());
        rPlayer->GetSession()->SendPacket(&data);

        return true;
    }

    WorldPacket data;
    ChatHandler::BuildChatPacket(data, CHAT_MSG_WHISPER, msg.c_str(), LANG_UNIVERSAL, bot->GetChatTag(), bot->GetObjectGuid(), bot->GetName());
    rPlayer->GetSession()->SendPacket(&data);
    return true;
}

bool PlayerbotAI::TellPlayerNoFacing(Player* player, std::string text, PlayerbotSecurityLevel securityLevel, bool isPrivate, bool noRepeat, bool ignoreSilent)
{
    if(!player)
        return false;

    if (!ignoreSilent && HasStrategy("silent", BotState::BOT_STATE_NON_COMBAT))
        return false;

    time_t lastSaid = whispers[text];
    if (!noRepeat || !lastSaid || (time(0) - lastSaid) >= sPlayerbotAIConfig.repeatDelay / 1000)
    {
        whispers[text] = time(0);

        if (m_recordMessages)
        {
            m_recordedMessages.push_back(text);
        }

        std::vector<Player*> recievers;

        ChatMsg type = CHAT_MSG_SYSTEM;

        if (!isPrivate && bot->GetGroup())
        {
            recievers = GetPlayersInGroup();
            if(!recievers.empty())
            {
                type = bot->GetGroup()->isRaidGroup() ? CHAT_MSG_RAID : CHAT_MSG_PARTY;
            }
        }

        if (type == CHAT_MSG_SYSTEM && HasRealPlayerMaster())
            type = CHAT_MSG_WHISPER;

        // A bot without a master that has nobody to answer stays silent; only
        // a bot in the debug strategy says its diagnostic line in /say.
        if (type == CHAT_MSG_SYSTEM && HasStrategy("debug", BotState::BOT_STATE_NON_COMBAT))
            type = CHAT_MSG_SAY;

        if (type == CHAT_MSG_SYSTEM && isRealPlayer_Helper(player))
            type = CHAT_MSG_WHISPER;

        if ((sPlayerbotAIConfig.hasLog("chat_log.csv") && HasStrategy("debug log", BotState::BOT_STATE_NON_COMBAT)) || HasStrategy("debug logname", BotState::BOT_STATE_NON_COMBAT))
        {
            std::ostringstream out;
            out << sPlayerbotAIConfig.GetTimestampStr() << "+00,";
            out << bot->GetName() << ",";
            out << std::fixed << std::setprecision(2);

            out << std::to_string(bot->GetRace()) << ",";
            out << std::to_string(bot->GetClass()) << ",";
            float subLevel = GetLevelFloat();

            out << subLevel << ",";
            out << std::fixed << std::setprecision(2);
            WorldPosition(bot).printWKT(out);

            out << type << ",";
            out << text;

            if (HasStrategy("debug logname", BotState::BOT_STATE_NON_COMBAT))
            {
                std::string fileName = "chat_log_";
                fileName += bot->GetName();
                fileName += ".csv";
                if (!sPlayerbotAIConfig.isLogOpen(fileName))
                    sPlayerbotAIConfig.openLog(fileName, "a", true);
                sPlayerbotAIConfig.log(fileName, out.str().c_str());
            }
            else
                sPlayerbotAIConfig.log("chat_log.csv", out.str().c_str());
        }

        WorldPacket data;

        switch (type)
        {
            case CHAT_MSG_SAY:
            {
                bot->Say(text, (bot->GetTeam() == ALLIANCE ? LANG_COMMON : LANG_ORCISH));
                return true;
            }

            case CHAT_MSG_RAID:
            {
                this->SayToRaid(text.c_str());

                return true;
            }
            case CHAT_MSG_PARTY:
            {
                SayToParty(text.c_str());

                return true;
            }

            case CHAT_MSG_WHISPER:
            {
                if (!IsTellAllowed(player, securityLevel))
                    return false;

                if (!HasRealPlayerMaster() && !isRealPlayer_Helper(player))
                    return false;

                whispers[text] = time(0);

                if (currentChat.second >= time(0))
                   type = currentChat.first;

                if (type == CHAT_MSG_ADDON)
                {
                    text = "BOT\t" + text;

                    ChatHandler::BuildChatPacket(data, type == CHAT_MSG_ADDON ? CHAT_MSG_PARTY : type, text.c_str(), type == CHAT_MSG_ADDON ? LANG_ADDON : LANG_UNIVERSAL, CHAT_TAG_NONE, bot->getObjectGuid(), bot->GetName());
                    sServerFacade.SendPacket(player, data);
                    return true;
                }

                this->Whisper(text, player->GetName());
                return true;
            }

            default:
                return true;
        }
    }

    return true;
}

bool PlayerbotAI::TellError(Player* player, std::string text, PlayerbotSecurityLevel securityLevel, bool ignoreSilent)
{
    if (!IsTellAllowed(player, securityLevel) || !IsSafe(player) || PlayerbotAIStorage::Instance().GetAI(player))
        return false;

    if (!ignoreSilent && HasStrategy("silent", BotState::BOT_STATE_NON_COMBAT))
        return false;

    if (player)
        TellPlayer(player, text);

    return false;
}

bool PlayerbotAI::IsTellAllowed(Player* player, PlayerbotSecurityLevel securityLevel)
{
    if (!player || player->IsBeingTeleported())
        return false;

    if (!GetSecurity()->CheckLevelFor(securityLevel, true, player))
        return false;

    if (sPlayerbotAIConfig.whisperDistance && !bot->GetGroup() && sRandomBotFacade.IsFreeBot(bot) &&
            player->GetSession()->GetSecurity() < SEC_GAMEMASTER &&
            (bot->GetMapId() != player->GetMapId() || sServerFacade.getDistance2d(bot, player) > sPlayerbotAIConfig.whisperDistance))
        return false;

    return true;
}

bool PlayerbotAI::TellPlayer(Player* player, std::string text, PlayerbotSecurityLevel securityLevel, bool isPrivate, bool ignoreSilent)
{
    if (!TellPlayerNoFacing(player, text, securityLevel, isPrivate, ignoreSilent))
        return false;

    if (player && !player->IsBeingTeleported() && !sServerFacade.isMoving(bot) && !sServerFacade.IsInCombat(bot) && bot->GetMapId() == player->GetMapId() && !bot->IsTaxiFlying() && !bot->IsFlying())
    {
        if (!sServerFacade.isInFront(bot, player, sPlayerbotAIConfig.sightDistance, EMOTE_ANGLE_IN_FRONT))
            sServerFacade.SetFacingTo(bot, player);

        bot->HandleEmoteCommand(EMOTE_ONESHOT_TALK);
    }

    return true;
}


bool PlayerbotAI::ChannelHasRealPlayer(std::string channelName)
{
    (void)channelName;
    for (auto const& entry : sObjectAccessor.GetPlayers())
    {
        Player* player = entry.second;
        if (player && player != bot && player->IsInWorld() && !PlayerbotAIStorage::Instance().GetAI(player))
            return true;
    }
    return false;
}


void PlayerbotAI::Ping(float x, float y)
{
    WorldPacket data(MSG_MINIMAP_PING, (8 + 4 + 4));
    data << bot->getObjectGuid();
    data << x;
    data << y;

    if (bot->GetGroup())
    {
        bot->GetGroup()->BroadcastPacket(
#ifdef MANGOS
            & data,
#endif
#ifdef CMANGOS
            &data,
#endif
            true, -1, bot->getObjectGuid());
    }
    else
    {
        bot->GetSession()->SendPacket(
#ifdef MANGOS
            & data
#endif
#ifdef CMANGOS
            &data
#endif
            );
    }
}

void PlayerbotAI::Poi(float x, float y, std::string icon_name, Player* player, uint32 flags, uint32 icon, uint32 icon_data)
{
    if (!player)
        player = master;

    if (!player)
        return;

    WorldPacket data(SMSG_GOSSIP_POI, (4 + 4 + 4 + 4 + 4 + 10)); // guess size
    data << flags;
    data << x;
    data << y;
    data << icon;
    data << icon_data;
    data << icon_name;

    sServerFacade.SendPacket(player, data);
}
