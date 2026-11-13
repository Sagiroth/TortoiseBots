#include "playerbot/playerbot.h"
#include "PlayerbotAI.h"
#include "PlayerbotAIPackets.h"
#include "../../runtime/RemoteCommanderTasks.h"
#include "../../runtime/policy/infra/DeferredDeliveryPolicy.h"
#include "../../runtime/policy/infra/SpeedChangeAckPolicy.h"
#include "strategy/actions/MovementActions.h"
#include "strategy/values/LastSpellCastValue.h"
#include "strategy/values/TargetValue.h"
#include "BotActionLog.h"
#include "playerbot/WandPolicy.h"
#include "playerbot/policy/guild/EnchantTradePolicy.h"
#include "runtime/GuildOrderStore.h"
#include <iomanip>
#include <sstream>

using namespace ai;

// The ack writer in SpeedChangeAckPolicy.h must name the same wire values the
// core table does; both sides are checked at compile time.
static_assert(TortoiseBots::SpeedChangeAck::kServerWalk == SMSG_FORCE_WALK_SPEED_CHANGE, "speed ack opcode drift");
static_assert(TortoiseBots::SpeedChangeAck::kServerRun == SMSG_FORCE_RUN_SPEED_CHANGE, "speed ack opcode drift");
static_assert(TortoiseBots::SpeedChangeAck::kServerRunBack == SMSG_FORCE_RUN_BACK_SPEED_CHANGE, "speed ack opcode drift");
static_assert(TortoiseBots::SpeedChangeAck::kServerSwim == SMSG_FORCE_SWIM_SPEED_CHANGE, "speed ack opcode drift");
static_assert(TortoiseBots::SpeedChangeAck::kServerSwimBack == SMSG_FORCE_SWIM_BACK_SPEED_CHANGE, "speed ack opcode drift");
static_assert(TortoiseBots::SpeedChangeAck::kServerTurnRate == SMSG_FORCE_TURN_RATE_CHANGE, "speed ack opcode drift");
static_assert(TortoiseBots::SpeedChangeAck::kClientWalk == CMSG_FORCE_WALK_SPEED_CHANGE_ACK, "speed ack opcode drift");
static_assert(TortoiseBots::SpeedChangeAck::kClientRun == CMSG_FORCE_RUN_SPEED_CHANGE_ACK, "speed ack opcode drift");
static_assert(TortoiseBots::SpeedChangeAck::kClientRunBack == CMSG_FORCE_RUN_BACK_SPEED_CHANGE_ACK, "speed ack opcode drift");
static_assert(TortoiseBots::SpeedChangeAck::kClientSwim == CMSG_FORCE_SWIM_SPEED_CHANGE_ACK, "speed ack opcode drift");
static_assert(TortoiseBots::SpeedChangeAck::kClientSwimBack == CMSG_FORCE_SWIM_BACK_SPEED_CHANGE_ACK, "speed ack opcode drift");
static_assert(TortoiseBots::SpeedChangeAck::kClientTurnRate == CMSG_FORCE_TURN_RATE_CHANGE_ACK, "speed ack opcode drift");
static_assert(TortoiseBots::SpeedChangeAck::MoveIndex(TortoiseBots::SpeedChangeAck::Move::Walk) == MOVE_WALK, "speed ack move order drift");
static_assert(TortoiseBots::SpeedChangeAck::MoveIndex(TortoiseBots::SpeedChangeAck::Move::Run) == MOVE_RUN, "speed ack move order drift");
static_assert(TortoiseBots::SpeedChangeAck::MoveIndex(TortoiseBots::SpeedChangeAck::Move::RunBack) == MOVE_RUN_BACK, "speed ack move order drift");
static_assert(TortoiseBots::SpeedChangeAck::MoveIndex(TortoiseBots::SpeedChangeAck::Move::Swim) == MOVE_SWIM, "speed ack move order drift");
static_assert(TortoiseBots::SpeedChangeAck::MoveIndex(TortoiseBots::SpeedChangeAck::Move::SwimBack) == MOVE_SWIM_BACK, "speed ack move order drift");
static_assert(TortoiseBots::SpeedChangeAck::MoveIndex(TortoiseBots::SpeedChangeAck::Move::TurnRate) == MOVE_TURN_RATE, "speed ack move order drift");

void PacketHandlingHelper::DropQueuedOpcode(uint16 opcode)
{
    m_botPacketMutex.lock();
    // The stack drains LIFO; rebuilding from the bottom preserves the
    // survivors' relative order. Move-only packets travel by move.
    std::stack<std::unique_ptr<WorldPacket>> kept;
    while (!queue.empty())
    {
        if (queue.top()->getOpcode() != opcode)
            kept.push(std::move(queue.top()));
        queue.pop();
    }
    while (!kept.empty())
    {
        queue.push(std::move(kept.top()));
        kept.pop();
    }
    m_botPacketMutex.unlock();
}

void PacketHandlingHelper::AddPacket(const WorldPacket& packet)
{
    if (packet.empty() && packet.getOpcode() != MSG_RAID_READY_CHECK)
        return;

    m_botPacketMutex.lock(); //We are going to add packets. Stop any new handling and add them.

	if (handlers.find(packet.getOpcode()) != handlers.end())
        queue.push(std::make_unique<WorldPacket>(packet));

    m_botPacketMutex.unlock();
}

namespace ai
{
// The AI's packet handlers by opcode. One list feeds both the per-AI
// PacketHandlingHelper registration and the static opcode filters the packet
// hooks use before copying a packet (issue #844), so the two cannot drift.
PacketHandlerSpec const kMasterIncomingPacketHandlers[] =
{
    { CMSG_GAMEOBJ_USE, "use game object", false },
    { CMSG_AREATRIGGER, "area trigger", false },
    { CMSG_LOOT_ROLL, "loot roll", true },
    { CMSG_GOSSIP_HELLO, "gossip hello", false },
    { CMSG_QUESTGIVER_HELLO, "gossip hello", false },
    { CMSG_QUESTGIVER_COMPLETE_QUEST, "complete quest", false },
    { CMSG_QUESTGIVER_ACCEPT_QUEST, "accept quest", false },
    { CMSG_QUEST_CONFIRM_ACCEPT, "confirm quest", false },
    { CMSG_ACTIVATETAXI, "activate taxi", false },
    { CMSG_ACTIVATETAXIEXPRESS, "activate taxi", false },
    { CMSG_TAXICLEARALLNODES, "taxi done", false },
    { CMSG_TAXICLEARNODE, "taxi done", false },
    { CMSG_GROUP_UNINVITE, "uninvite", false },
    { CMSG_GROUP_UNINVITE_GUID, "uninvite guid", false },
    { CMSG_PUSHQUESTTOPARTY, "quest share", false },
    { CMSG_REPOP_REQUEST, "release spirit", false },
    { CMSG_RECLAIM_CORPSE, "revive from corpse", false },
    { CMSG_USE_ITEM, "master use item", false },
};
size_t const kMasterIncomingPacketHandlerCount = std::size(kMasterIncomingPacketHandlers);

PacketHandlerSpec const kBotOutgoingPacketHandlers[] =
{
    { SMSG_PETITION_SHOW_SIGNATURES, "petition offer", false },
    { SMSG_BATTLEFIELD_STATUS, "bg status", false },
    { SMSG_GROUP_INVITE, "group invite", false },
    { SMSG_GUILD_INVITE, "guild accept", false },
    { BUY_ERR_NOT_ENOUGHT_MONEY, "not enough money", false },
    { BUY_ERR_REPUTATION_REQUIRE, "not enough reputation", false },
    { SMSG_GROUP_SET_LEADER, "group set leader", false },
    { SMSG_FORCE_RUN_SPEED_CHANGE, "check mount state", false },
    { SMSG_RESURRECT_REQUEST, "resurrect request", false },
    { SMSG_INVENTORY_CHANGE_FAILURE, "cannot equip", false },
    { SMSG_TRADE_STATUS, "trade status", false },
    { SMSG_TRADE_STATUS_EXTENDED, "trade status extended", false },
    { SMSG_LOOT_RESPONSE, "loot response", true },
    { SMSG_QUESTUPDATE_ADD_KILL, "quest update add kill", true },
    { SMSG_QUESTUPDATE_ADD_ITEM, "quest update add item", true },
    { SMSG_QUESTUPDATE_FAILED, "quest update failed", true },
    { SMSG_QUESTUPDATE_FAILEDTIMER, "quest update failed timer", true },
    { SMSG_QUESTUPDATE_COMPLETE, "quest update complete", true },
    { SMSG_ITEM_PUSH_RESULT, "item push result", true },
    { SMSG_PARTY_COMMAND_RESULT, "party command", false },
    { SMSG_LEVELUP_INFO, "levelup", true },
    { SMSG_LOG_XPGAIN, "xpgain", true },
    { SMSG_PVP_CREDIT, "honorgain", true },
    { SMSG_TEXT_EMOTE, "receive text emote", false },
    { SMSG_EMOTE, "receive emote", false },
    { SMSG_LOOT_START_ROLL, "loot start roll", true },
    { SMSG_LOOT_ROLL, "loot vote", true },
    { SMSG_LOOT_ROLL_WON, "loot roll won", true },
    { SMSG_SUMMON_REQUEST, "summon request", false },
    { MSG_RAID_READY_CHECK, "ready check", false },
    { SMSG_QUEST_CONFIRM_ACCEPT, "confirm quest", false },
    { SMSG_QUESTGIVER_QUEST_DETAILS, "quest details", false },
    { SMSG_CAST_RESULT, "cast failed", false },
    { SMSG_DUEL_REQUESTED, "duel requested", false },
    { SMSG_INVENTORY_CHANGE_FAILURE, "inventory change failure", false },
};
size_t const kBotOutgoingPacketHandlerCount = std::size(kBotOutgoingPacketHandlers);

PacketHandlerSpec const kMasterOutgoingPacketHandlers[] =
{
    { SMSG_PARTY_COMMAND_RESULT, "party command", false },
};
size_t const kMasterOutgoingPacketHandlerCount = std::size(kMasterOutgoingPacketHandlers);

// Built once, read-only afterwards: a constant-time test on packet threads.

// Plain pointer + count, no array-reference templates and no defaulted
// initializer_list: MSVC 19.44 rejected the template form of these helpers
// (issue #918) while GCC accepted it.
OpcodeSet BuildOpcodeSet(PacketHandlerSpec const* specs, size_t count, std::initializer_list<uint16> extra)
{
    OpcodeSet set;
    for (size_t i = 0; i < count; ++i)
        set.set(specs[i].opcode);
    for (uint16 opcode : extra)
        set.set(opcode);
    return set;
}

void RegisterPacketHandlers(PacketHandlingHelper& helper, PacketHandlerSpec const* specs, size_t count)
{
    for (size_t i = 0; i < count; ++i)
        helper.AddHandler(specs[i].opcode, specs[i].handler, specs[i].delay);
}
} // namespace


void PlayerbotAI::HandleBotOutgoingPacket(const WorldPacket& packet)
{
    //if (packet.empty())
    //    return;

    // A bot between maps (far teleport not acked yet) has no map for the
    // handlers below that read it: the cast result looks its target up and the
    // knock-back reads terrain. The drain holds such a packet, but a packet
    // handed over on the world thread reaches this function directly.
    if (TortoiseBots::DeferredDelivery::ReadsBotMap(packet.getOpcode()) && !bot->FindMap())
        return;

	// Issue #844: a case here that does not end in botOutgoingPacketHandlers
	// (AddPacket) must also be added to the hand-kept extras list in
	// PlayerbotAI::HandlesBotOutgoingOpcode, or packet hooks off the world
	// thread never copy that opcode to the bot.
	switch (packet.getOpcode())
	{
	case SMSG_SPELL_FAILURE:
	{
		WorldPacket p(packet);
		p.rpos(0);
		ObjectGuid casterGuid;
        p >> casterGuid.ReadAsPacked();
		if (casterGuid != bot->getObjectGuid())
			return;

		uint32 spellId;
		p >> spellId;
		SpellInterrupted(spellId);
		return;
	}
	case SMSG_SPELL_DELAYED:
	{
		WorldPacket p(packet);
		p.rpos(0);
		ObjectGuid casterGuid;
        p >> casterGuid.ReadAsPacked();

		if (casterGuid != bot->getObjectGuid())
			return;

		uint32 delaytime;
		p >> delaytime;
		if (delaytime <= 1000)
            IncreaseAIInternalUpdateDelay(delaytime);
		return;
	}
    case SMSG_EMOTE: // do not react to NPC emotes
    {
        WorldPacket p(packet);
        ObjectGuid source;
        uint32 emoteId;
        p.rpos(0);
        p >> emoteId >> source;
        if (source.IsPlayer())
            botOutgoingPacketHandlers.AddPacket(packet);

        return;
    }
    case SMSG_MESSAGECHAT: // do not react to self or if not ready to reply
    {
        if (!AllowActivity())
            return;

        WorldPacket p(packet);
        if (!p.empty() && p.getOpcode() == SMSG_MESSAGECHAT)
        {
            p.rpos(0);
            uint8 msgtype, chatTag;
            uint32 lang, textLen, unused;
            ObjectGuid guid1, guid2;
            std::string chanName, message;
            p >> msgtype >> lang;

            // filter msg type
            switch (msgtype)
            {
            case CHAT_MSG_CHANNEL:
            case CHAT_MSG_SAY:
            case CHAT_MSG_PARTY:
            case CHAT_MSG_YELL:
            case CHAT_MSG_WHISPER:
            case CHAT_MSG_GUILD:
                break;
            default:
                return;
                break;
            }

            switch (msgtype)
            {
            case CHAT_MSG_SAY:
            case CHAT_MSG_PARTY:
            case CHAT_MSG_YELL:
                p >> guid1 >> guid2;
                break;
            case CHAT_MSG_CHANNEL:
                p >> chanName >> unused >> guid1;
                break;
            default:
                p >> guid1;
                break;
            }

            if (guid1.IsEmpty() || p.size() > 0x1000)
                return;

            p >> textLen >> message >> chatTag;

            if (m_recordIncommingMessages)
            {
                std::string recievedMessage = message;

                std::string senderName;
                if(!sObjectMgr.GetPlayerNameByGUID(guid1, senderName))
                    senderName = "unknown";

                std::string recievedChatType;

                //Add chat type prefix before recieved message. Ie. for Guilg "guild:".
                switch (msgtype)
                {
                    case CHAT_MSG_SAY:
                        recievedChatType = "says";
                        break;
                    case CHAT_MSG_YELL:
                        recievedChatType = "yells";
                        break;
                    case CHAT_MSG_PARTY:
                        recievedChatType = "party";
                        break;
                    case CHAT_MSG_GUILD:
                        recievedChatType = "guild";
                        break;
                    case CHAT_MSG_OFFICER:
                        recievedChatType = "officer";
                        break;
                    case CHAT_MSG_WHISPER:
                        //Get player name for whisper and add "<whispe:" before message.
                        recievedChatType = "whispers";
                        break;
                    case CHAT_MSG_CHANNEL: //Channel needs channel name to be added as prefix, so we add "c:" before channel name.
                        recievedChatType = chanName;
                        break;
                    default:
                        break;
                }

                recievedMessage = "[" + senderName + "] " + recievedChatType + ": " + recievedMessage;

                m_recordedMessages.push_back(recievedMessage);
            }

            if (guid1 != bot->getObjectGuid())
            {
                // Enchant-order replace answer: the giver's "yes" confirms the
                // open askreplace question even when they never grouped with
                // the bot (meet in the capital), so this runs before the
                // master/group gate below. Giver-only, Enchanting only.
                if (msgtype == CHAT_MSG_WHISPER && lang != LANG_ADDON)
                {
                    if (Player* sender = sObjectAccessor.FindPlayer(guid1))
                    {
                        std::optional<TortoiseBots::GuildOrders::OrderRecord> enchantRow =
                            TortoiseBots::GuildOrders::Find(bot->GetGUIDLow());
                        TortoiseBots::GuildOrders::EnchantTradeSession enchantSession =
                            TortoiseBots::GuildOrders::GetEnchantTrade(bot->GetGUIDLow());
                        if (enchantRow && enchantRow->type == TortoiseBots::GuildOrders::OrderType::Enchant &&
                            enchantRow->state == TortoiseBots::GuildOrders::OrderState::Enchanting &&
                            sender->getObjectGuid() == ObjectGuid(HIGHGUID_PLAYER, enchantRow->giverGuidLow) &&
                            enchantSession.replaceAskedMs != 0 && !enchantSession.replaceConfirmed)
                        {
                            if (ai::ReplaceAnswerIsYes(message))
                                TortoiseBots::GuildOrders::NoteEnchantReplaceConfirmed(bot->GetGUIDLow());
                        }
                        // Disenchant drop-off: a guild member whispers
                        // `disenchant` (colour codes stripped), opening a
                        // 2-minute window for a trade of items only. A bot
                        // without Enchanting answers at once and does nothing.
                        if (TortoiseBots::GuildOrders::IsDisenchantDropOffWhisper(
                                ai::StripChatColours(message)) &&
                            isRealPlayer_Helper(sender) && bot->GetGuildId() &&
                            sender->GetGuildId() == bot->GetGuildId())
                        {
                            if (!HasSkill(SKILL_ENCHANTING))
                            {
                                // Telling needs the world thread (this hook
                                // runs on it through the deferred chat queue).
                                TellPlayer(sender, "I can't disenchant.",
                                    PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
                            }
                            else
                            {
                                TortoiseBots::GuildOrders::NoteDisenchantDropOff(
                                    bot->GetGUIDLow(), sender->GetGUIDLow(),
                                    WorldTimer::getMSTime());
                            }
                        }
                    }
                }
                // Dispatch party chat and whispers from authorized real players (group master / members) to HandleCommand
                if ((msgtype == CHAT_MSG_PARTY || msgtype == CHAT_MSG_WHISPER) && lang != LANG_ADDON)
                {
                    if (Player* sender = sObjectAccessor.FindPlayer(guid1))
                    {
                        if (isRealPlayer_Helper(sender))
                        {
                            bool isAuthorized = (GetMaster() && GetMaster()->GetObjectGuid() == guid1) ||
                                                (bot->GetGroup() && bot->GetGroup()->IsMember(guid1));
                            if (isAuthorized)
                            {
                                HandleCommand(msgtype, message, *sender, lang);
                            }
                        }
                    }
                }
            }
        }

        return;
    }
    // A bot's world session never answered the core's forced speed change, so
    // every change waited out the 2+2 s resend timeout before it applied. Echo
    // the client ACK at once, exactly as a real client does.
    case SMSG_FORCE_WALK_SPEED_CHANGE:
    case SMSG_FORCE_RUN_SPEED_CHANGE:
    case SMSG_FORCE_RUN_BACK_SPEED_CHANGE:
    case SMSG_FORCE_SWIM_SPEED_CHANGE:
    case SMSG_FORCE_SWIM_BACK_SPEED_CHANGE:
    case SMSG_FORCE_TURN_RATE_CHANGE:
    {
        TortoiseBots::SpeedChangeAck::Move move;
        if (!TortoiseBots::SpeedChangeAck::MoveForServerOpcode(packet.getOpcode(), move) ||
            !bot->GetSession() || !bot->GetSession()->IsHeadless() || !bot->FindMap())
        {
            // A real player's client sends this ack itself.
            botOutgoingPacketHandlers.AddPacket(packet);
            return;
        }

        WorldPacket p(packet);
        p.rpos(0);
        ObjectGuid guid;
        uint32 counter = 0;
        float speed = 0.f;
        p >> guid.ReadAsPacked() >> counter >> speed;
        if (guid != bot->getObjectGuid())
        {
            // The change belongs to a unit the bot moves (a pet); the bot's own
            // ack would name the wrong mover.
            botOutgoingPacketHandlers.AddPacket(packet);
            return;
        }

        WorldPacket ack(TortoiseBots::SpeedChangeAck::ClientOpcode(move), 8 + 4 + 30 + 4);
        TortoiseBots::SpeedChangeAck::WriteAck(ack, guid.GetRawValue(), counter, bot->m_movementInfo, speed);
        bot->GetSession()->HandleForceSpeedChangeAckOpcodes(ack);

        if (sPlayerbotAIConfig.raidBossDiagnostics && sPlayerbotAIConfig.hasLog("bot_events.csv"))
        {
            uint32 const now = WorldTimer::getMSTime();
            if (!speedChangeAckLogMs || now - speedChangeAckLogMs >= 1000)
            {
                speedChangeAckLogMs = now;
                UnitMoveType const type = static_cast<UnitMoveType>(TortoiseBots::SpeedChangeAck::MoveIndex(move));
                std::ostringstream detail;
                detail << "rate=" << std::fixed << std::setprecision(3) << bot->GetSpeedRate(type)
                    << " counter=" << counter
                    << " slowed=" << (bot->HasAuraType(SPELL_AURA_MOD_DECREASE_SPEED) ? 1 : 0);
                sPlayerbotAIConfig.logEvent(this, "SpeedChange", TortoiseBots::SpeedChangeAck::MoveName(move), detail.str());
            }
        }

        // Keep the run-speed "check mount state" dispatch.
        botOutgoingPacketHandlers.AddPacket(packet);
        return;
    }
    case SMSG_MOVE_KNOCK_BACK: // handle knockbacks
    {
        if (!sPlayerbotAIConfig.useKnockback || !HasPlayerNearby())
            return;

        WorldPacket p(packet);
        p.rpos(0);

        ObjectGuid guid;
        uint32 counter;
        float vcos, vsin, horizontalSpeed, verticalSpeed = 0.f;

        p >> guid.ReadAsPacked() >> counter >> vcos >> vsin >> horizontalSpeed >> verticalSpeed;
        verticalSpeed = -verticalSpeed;

        // stop casting
        InterruptSpell(false);

        // stop movement
        StopMoving();

        float timeToLand, distToLand, maxHeight;
        bool goodLanding = true;
        float angleRadian = (vsin > 0) ? acos(vcos) : -acos(vcos);
        float angle = angleRadian;
        std::vector<WorldPosition> path;
        WorldPosition dest_calculated = JumpAction::CalculateJumpParameters(WorldPosition(bot), bot, angle, verticalSpeed, horizontalSpeed, timeToLand, distToLand, maxHeight, goodLanding, path, 200.f);
        bool jumpInPlace = horizontalSpeed == 0.f;
        if (!dest_calculated)
        {
            sLog.outDetail("Knockback fail to calculate");
            return;
        }

        // set fall height for fall damage calculations
        bot->SetFallInformation(maxHeight);

        // fix height
        if (goodLanding)
        {
            float ox = dest_calculated.getX();
            float oy = dest_calculated.getY();
            float oz = dest_calculated.getZ();
            bot->UpdateAllowedPositionZ(ox, oy, oz);
            // set to fall after land if not at the ground
            if (fabs(oz - dest_calculated.getZ()) > 5.0f)
            {
                SetFallAfterJump();
            }
            else
            {
                dest_calculated = WorldPosition(dest_calculated.GetMapId(), ox, oy, oz);
            }

        }
        else
            SetFallAfterJump();


        // add moveflags
        bot->m_movementInfo.SetMovementFlags(MOVEFLAG_JUMPING);
        bot->m_movementInfo.AddMovementFlag(MOVEFLAG_BACKWARD);

        // send ack
        WorldPacket ack(CMSG_MOVE_KNOCK_BACK_ACK);
        bot->m_movementInfo.jump.cosAngle = vcos;
        bot->m_movementInfo.jump.sinAngle = vsin;
        bot->m_movementInfo.jump.zspeed = -verticalSpeed;
        bot->m_movementInfo.jump.xyspeed = horizontalSpeed;
        ack << bot->getObjectGuid();
        ack << uint32(0);
        ack << bot->m_movementInfo;
        bot->GetSession()->HandleMoveKnockBackAck(ack);

        // write jump time
        uint32 curTime = WorldTimer::getMSTime();
        jumpTime = curTime + sWorld.GetAverageDiff() + (uint32)(timeToLand * static_cast<uint32>(IN_MILLISECONDS)) + 1000;
        SetJumpDestination(dest_calculated);

        // set highest jump point to relocate
        WorldPosition highestPoint = dest_calculated;
        for (auto& point : path)
        {
            if (point.getZ() > highestPoint.getZ())
                highestPoint = point;
        }

        bot->Relocate(highestPoint.getX(), highestPoint.getY(), highestPoint.getZ());
        //bot->m_movementInfo.ChangePosition(dest_calculated.getX(), dest_calculated.getY(), dest_calculated.getZ(), bot->getOrientation());
        sLog.outDetail("%s: KNOCKBACK x: %f, y: %f, z: %f, time: %f, dist: %f, maxHeight: %f inPlace: %u, landTime: %u", bot->GetName(), dest_calculated.getX(), dest_calculated.getY(), dest_calculated.getZ(), timeToLand, distToLand, maxHeight, jumpInPlace, jumpTime);
        return;
    }
    case SMSG_CAST_RESULT:
    {
        // Failed crowd-control casts go to the bot action log (only open
        // with AiPlayerbot.EnableActionLog = 1), so a sheep that never lands
        // shows why. This core sends no SMSG_SPELL_FAILURE: a cast that
        // fails at the end of its cast bar, or is cancelled (moving,
        // interrupted), arrives here. 1.12 layout (Spell::SendCastResult):
        // uint32 spellId, uint8 status (0 = ok, 2 = failed), uint8 result.
        if (botdiag::BotActionLog::GetHandle(this) && packet.size() >= 6)
        {
            WorldPacket p(packet);
            p.rpos(0);
            uint32 spellId;
            uint8 status;
            p >> spellId >> status;
            if (status != 0)
            {
                uint8 result;
                p >> result;
                SpellEntry const* info = sServerFacade.LookupSpellInfo(spellId);
                if (info && FindNonCcTargetStrategy::IsCcSpell(info))
                {
                    // The cast's own target, from the start of this cast.
                    LastSpellCast const& last = aiObjectContext->GetValue<LastSpellCast&>("last spell cast")->Get();
                    Unit* ccTarget = last.id == spellId ? GetUnit(last.target) : nullptr;
                    botdiag::BotActionLog::Write(this, "CC_CAST_FAILED",
                        "spell=%u result=%u targetGuid=0x%llx dist=%.1f moving=%u",
                        spellId, (unsigned)result,
                        ccTarget ? (unsigned long long)ccTarget->getObjectGuid().GetRawValue() : 0ULL,
                        ccTarget ? bot->GetDistance(ccTarget) : -1.0f,
                        sServerFacade.isMoving(bot) ? 1u : 0u);
                }
            }
        }
        botOutgoingPacketHandlers.AddPacket(packet);
        return;
    }
	default:
		// log group invite arrivals so we
		// can verify the auto-accept handler fires. If we see this line in
		// the server log but no subsequent "AcceptInvitationAction" event,
		// the strategy/trigger is registered but the action isn't being
		// scheduled — points us at engine priority or trigger.Check() bugs.
		if (packet.getOpcode() == SMSG_GROUP_INVITE) {
			SC_LOG("bot=%s received SMSG_GROUP_INVITE — queued for 'group invite' trigger",
			       bot ? bot->GetName() : "(null)");
		}
		botOutgoingPacketHandlers.AddPacket(packet);
	}
}

void PlayerbotAI::HandleMasterIncomingPacket(const WorldPacket& packet)
{
    masterIncomingPacketHandlers.AddPacket(packet);
}

void PlayerbotAI::HandleMasterOutgoingPacket(const WorldPacket& packet)
{
    masterOutgoingPacketHandlers.AddPacket(packet);
}

namespace ai
{
// Issue #550: the core wraps GM chat (CHAT_TAG_GM) as `|c1049e6ff<message>|r`,
// so strip one whole-message colour wrapper before parsing commands. Item and
// spell links (`|cff...|Hitem:...|h[..]|h|r`) always carry an inner |H and are
// left untouched.
void StripGmBadgeColor(std::string& text)
{
    // `|c` + 8 hex digits + message + `|r`.
    if (text.size() < 12 || text.compare(0, 2, "|c") != 0 || text.compare(text.size() - 2, 2, "|r") != 0)
        return;
    for (size_t i = 2; i < 10; ++i)
    {
        char c = text[i];
        bool isHex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
        if (!isHex)
            return;
    }
    std::string inner = text.substr(10, text.size() - 12);
    if (inner.find("|c") != std::string::npos || inner.find("|H") != std::string::npos)
        return;
    text = inner;
}

} // namespace ai
