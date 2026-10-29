#include <vector>
#include "playerbot/GroupMembers.h"
#include "playerbot/playerbot.h"
#include "playerbot/PerformanceMonitor.h"
#include "../../runtime/CommandedGroupQuery.h"
#include <stdarg.h>
#include <deque>
#include <iomanip>
#include <memory>
#include <mutex>
#include <thread>

#include "playerbot/AiFactory.h"
#include "playerbot/BotAreaTriggers.h"

#include "../../runtime/ObservabilityEmitter.h"
#include "../../runtime/WorldPvpPolicy.h"
#include "../../runtime/AltBotPolicy.h"
#include "../../runtime/AltBotService.h"
#include "../../runtime/RunTrace.h"
#include "../../runtime/BotActivityLease.h"
#include "../../runtime/HireLifecycle.h"
#include "../../runtime/HireMasterPolicy.h"
#include "../../runtime/PoolBotTradePolicy.h"
#include "../../runtime/QueuedOrderPolicy.h"
#include "../../runtime/RemoteCommanderTasks.h"
#include "../../runtime/AltBotService.h"
#include "ByteBuffer.h"
#include "ObjectAccessor.h"

#include "Movement/MovementGenerator.h"
#include "Maps/GridNotifiers.h"
#include "Maps/GridNotifiersImpl.h"
#include "Maps/CellImpl.h"
#include "strategy/values/LastMovementValue.h"
#include "strategy/actions/LogLevelAction.h"
#include "strategy/actions/SayAction.h"
#include "strategy/actions/EmoteAction.h"
#include "strategy/values/LastSpellCastValue.h"
#include "strategy/values/TargetValue.h"
#include "LootObjectStack.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "PlayerbotAI.h"
#include "BotAreaTriggers.h"
#include "playerbot/InterruptCastingPolicy.h"
#include "playerbot/strategy/values/MountValues.h"
#include "playerbot/MountDismountPolicy.h"
#include "playerbot/CastKeepsRunningPolicy.h"
#include "playerbot/WandPolicy.h"
#include "BotDiagnostics.h"
#include "BotActionLog.h"
#include "playerbot/PlayerbotFactory.h"
#include "PlayerbotSecurity.h"
#include "Group/Group.h"
#include "Objects/Pet.h"
#include "Spells/SpellAuras.h"
#include "Spells/SpellMgr.h"
#include "PlayerbotDbStore.h"
#include "strategy/values/PositionValue.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/TravelMgr.h"
#include "playerbot/strategy/actions/FishAction.h"
#include "playerbot/DeathClusterPolicy.h"
#include "playerbot/SurvivePolicy.h"
#include "playerbot/MovementModePolicy.h"
#include "../../runtime/AutoToolsKitPolicy.h"
#include "../../behavior/PlayerConvenience.h"
#include "Movement/spline/MoveSplineInitArgs.h"
#include "Movement/spline/MoveSpline.h"
#include "Maps/InstanceData.h"
#include <bitset>
#include <iterator>
#include <initializer_list>
#include "ChatHelper.h"
#include "strategy/values/BudgetValues.h"
#include "SocialMgr.h"
#include "PlayerbotTextMgr.h"
#include "RandomItemMgr.h"
#include "strategy/ItemVisitors.h"
#include "strategy/values/LootValues.h"
#include "strategy/values/AttackersValue.h"
#include "Transports/Transport.h"
#include "Guild/GuildMgr.h"
#include "Chat/ChannelMgr.h"
#include "PlayerbotLLMInterface.h"

#include <string_view>


#ifdef BUILD_ELUNA
#include "LuaEngine/LuaEngine.h"
#endif
#include "strategy/values/GuildValues.h"
#include "PlayerbotAIPackets.h"

using namespace ai;

std::vector<std::string>& split(const std::string &s, char delim, std::vector<std::string> &elems);
std::vector<std::string> split(const std::string &s, char delim);
char * strstri (std::string str1, std::string str2);
uint64 extractGuid(WorldPacket& packet);
std::string &trim(std::string &s);
bool istarts_with(std::string_view text, std::string_view prefix); // Helpers.cpp

std::set<std::string> PlayerbotAI::unsecuredCommands;

uint32 PlayerbotChatHandler::extractQuestId(std::string str)
{
    char* source = (char*)str.c_str();
    char* cId = ExtractKeyFromLink(&source,"Hquest");
    return cId ? atol(cId) : 0;
}

void PacketHandlingHelper::AddHandler(uint16 opcode, std::string handler, bool shouldDelay)
{
    handlers[opcode] = handler;
    delay[opcode] = shouldDelay;
}

void PacketHandlingHelper::Handle(ExternalEventHelper &helper)
{
    if (!m_botPacketMutex.try_lock()) //Packets do not have to be handled now. Handle them later.
        return;

    // queue holds unique_ptr<WorldPacket> due to Penqle's move-only WorldPacket.
    std::stack<std::unique_ptr<WorldPacket>> delayed;

    while (!queue.empty())
    {
        if (!helper.HandlePacket(handlers, *queue.top()))
            if (delay[queue.top()->getOpcode()])
                delayed.push(std::move(queue.top()));
        queue.pop();
    }

    queue = std::move(delayed);

    m_botPacketMutex.unlock();
}

bool PlayerbotAI::HandlesBotOutgoingOpcode(uint16 opcode)
{
    // HandleBotOutgoingPacket also reads these directly, outside the
    // PacketHandlingHelper queue.
    static OpcodeSet const set = BuildOpcodeSet(kBotOutgoingPacketHandlers, kBotOutgoingPacketHandlerCount,
        { SMSG_SPELL_FAILURE, SMSG_SPELL_DELAYED, SMSG_MESSAGECHAT, SMSG_MOVE_KNOCK_BACK });
    return set.test(opcode);
}

bool PlayerbotAI::HandlesMasterIncomingOpcode(uint16 opcode)
{
    static OpcodeSet const set = BuildOpcodeSet(kMasterIncomingPacketHandlers, kMasterIncomingPacketHandlerCount, {});
    return set.test(opcode);
}

bool PlayerbotAI::HandlesMasterOutgoingOpcode(uint16 opcode)
{
    static OpcodeSet const set = BuildOpcodeSet(kMasterOutgoingPacketHandlers, kMasterOutgoingPacketHandlerCount, {});
    return set.test(opcode);
}

PlayerbotAI::PlayerbotAI() : PlayerbotAIBase(), bot(NULL), aiObjectContext(NULL),
    currentEngine(NULL), chatHelper(this), chatFilter(this), accountId(0), security(NULL), master(NULL), currentState(BotState::BOT_STATE_NON_COMBAT), faceTargetUpdateDelay(0), jumpTime(0), fallAfterJump(false)
{
    for (uint8 i = 0 ; i < (uint8)BotState::BOT_STATE_ALL; i++)
        engines[i] = NULL;

    for (int i = 0; i < MAX_ACTIVITY_TYPE; i++)
    {
        allowActiveCheckTimer[i] = time(nullptr);
        allowActive[i] = false;
    }
}

PlayerbotAI::PlayerbotAI(Player* bot) :
    PlayerbotAIBase(), chatHelper(this), chatFilter(this), security(bot), master(NULL), faceTargetUpdateDelay(0), jumpTime(0), fallAfterJump(false)
{
    this->bot = bot;
    if (!bot->IsTaxiCheater() && HasCheat(BotCheatMask::taxi))
        bot->SetTaxiCheater(true);

    for (uint8 i = 0; i < (uint8)BotState::BOT_STATE_ALL; i++)
        engines[i] = NULL;

    for (int i = 0; i < MAX_ACTIVITY_TYPE; i++)
    {
        allowActiveCheckTimer[i] = time(nullptr);
        allowActive[i] = false;
    }

	accountId = sObjectMgr.GetPlayerAccountIdByGUID(bot->getObjectGuid());

    aiObjectContext = AiFactory::createAiObjectContext(bot, this);

    UpdateTalentSpec();

    engines[(uint8)BotState::BOT_STATE_COMBAT] = AiFactory::createCombatEngine(bot, this, aiObjectContext);
    engines[(uint8)BotState::BOT_STATE_NON_COMBAT] = AiFactory::createNonCombatEngine(bot, this, aiObjectContext);
    engines[(uint8)BotState::BOT_STATE_DEAD] = AiFactory::createDeadEngine(bot, this, aiObjectContext);
    engines[(uint8)BotState::BOT_STATE_REACTION] = reactionEngine = AiFactory::createReactionEngine(bot, this, aiObjectContext);

    for (uint8 e = 0; e < (uint8)BotState::BOT_STATE_ALL; e++)
    {
        engines[e]->initMode = false;
        engines[e]->Init();
    }

    currentEngine = engines[(uint8)BotState::BOT_STATE_NON_COMBAT];
    currentState = BotState::BOT_STATE_NON_COMBAT;

    RegisterPacketHandlers(masterIncomingPacketHandlers, kMasterIncomingPacketHandlers, kMasterIncomingPacketHandlerCount);
    RegisterPacketHandlers(botOutgoingPacketHandlers, kBotOutgoingPacketHandlers, kBotOutgoingPacketHandlerCount);
    RegisterPacketHandlers(masterOutgoingPacketHandlers, kMasterOutgoingPacketHandlers, kMasterOutgoingPacketHandlerCount);

    // Issue #872: autonomous alts never auto-spend talents (B12). The
    // IsOwnedBot guard already covers companions; this covers the
    // masterless autonomous state (master was cleared at session start).
    if (!HasRealPlayerMaster() && !IsOwnedBot() && bot->GetFreeTalentPoints() > 0 &&
        !TortoiseBots::AltBotService::Instance().IsAutonomous(bot->GetObjectGuid()))
    {
        DoSpecificAction("auto talents");
    }
}

bool PlayerbotAI::IsOwnedBot() const
{
    return bot && TortoiseBots::BotManager::Instance().IsBot(bot->GetObjectGuid()) &&
        !TortoiseBots::BotManager::Instance().IsRandomBot(bot->GetObjectGuid());
}

PlayerbotAI::~PlayerbotAI()
{
    for (uint8 i = 0 ; i < (uint8)BotState::BOT_STATE_ALL; i++)
    {
        if (engines[i])
            delete engines[i];
    }

    if (aiObjectContext)
        delete aiObjectContext;
}

Player* PlayerbotAI::GetLiveMaster()
{
    if (!master)
        return nullptr;

    Player* live = masterGuid ? sObjectAccessor.FindPlayer(masterGuid) : nullptr;
    if (live != master)
    {
        master = nullptr;
        return nullptr;
    }
    return master;
}

Player* PlayerbotAI::GetGroupMaster()
{
    if (bot->InBattleGround())
        return master;

    Group* group = bot->GetGroup();
    if (group)
    {
        // 1. If the bot already has an active real player master in this group,
        // that master takes supreme authority.
        Player* liveMaster = GetLiveMaster();
        if (liveMaster && IsRealPlayer(liveMaster) && liveMaster->IsInWorld() &&
            group->IsMember(liveMaster->GetObjectGuid()))
            return liveMaster;

        // 2. If the group leader is a real player, that leader takes authority.
        Player* leader = sObjectMgr.GetPlayer(group->GetLeaderGuid());
        if (leader && leader->IsInWorld() && IsRealPlayer(leader))
            return leader;

        // 3. If the group leader is a bot, look for any real player in the group.
        // A human player in the party always takes authority over a bot leader.
        // Iterate the Group's own member slots (ObjectGuids) and resolve each
        // through the ObjectAccessor: a Player* from the GroupReference list can
        // outlive the Player object during a headless bot's relogin window, and
        // dereferencing it reads freed memory (issue #225).
        for (Group::MemberSlot const& slot : group->GetMemberSlots())
        {
            Player* member = ObjectAccessor::FindPlayer(slot.guid);
            if (member && member->IsInWorld() && IsRealPlayer(member))
                return member;
        }

        // 4. No real player in the group: fall back to the bot group leader.
        if (leader)
            return leader;
    }

    return master;
}

// A food or drink aura, by the core's own test (SpellEntry food/drink
// specific): the aura's spell is removed by standing up. That covers the
// cheat meal spells (24005/24355, aura types 20/21) and real food and drink
// items (433/430 and their kin, aura types 84/85) alike.
static bool HasSeatedAura(Player* bot)
{
    for (auto const& entry : bot->GetSpellAuraHolderMap())
    {
        SpellEntry const* spell = entry.second ? entry.second->GetSpellProto() : nullptr;
        if (spell && (spell->AuraInterruptFlags & AURA_INTERRUPT_FLAG_NOT_SEATED))
            return true;
    }

    return false;
}

void PlayerbotAI::UpdateAI(uint32 elapsed, bool minimal)
{
    AiObjectContext* context = aiObjectContext;
    std::unique_ptr<PerformanceMonitorOperation> pmo;
    if (sPlayerbotAIConfig.perfMonEnabled)
    {
        std::string mapString = WorldPosition(bot).isInstance() ? "I" : std::to_string(bot->GetMapId());
        pmo = sPerformanceMonitor.start(PERF_MON_TOTAL, "PlayerbotAI::UpdateAI " + mapString, nullptr, bot->GetMapId(), bot->GetInstanceId());
    }

    SC_PHASE("UpdateAI.entry", bot ? bot->GetName() : "(null)");

    // Action log scope (AiPlayerbot.ActionLogScope = commanded): re-read at most
    // every 15 s per bot, here because the group, binding and hire lookups are
    // world-thread state. One branch while the log is off.
    botdiag::BotActionLog::RefreshScope(this);

    // A Headless bot has no client to finish a cinematic
    // (CMSG_COMPLETE_CINEMATIC), so any cinematic started mid-session - e.g.
    // a camera game object used before RpgUseTrigger excluded the type -
    // would leave it untargetable (Unit::IsTargetable) until logout. Member
    // read only, no DB/scan; the OnPlayerLogin call keeps owning first login.
    if (bot->watching_cinematic_entry)
        bot->CinematicEnd();

    // Issue #984: a Headless bot never sends CMSG_AREATRIGGER, so a scripted
    // area trigger (Zum'rah's at_zumrah) never fired for an all-bot party. Run
    // the script of a trigger the bot just walked into, as the client would
    // (dungeons only, not raids).
    // A few position tests against the bot's map's index, twice a second: the listed
    // triggers, and those the bot's dungeon run has armed (its move steps' triggers until they fired).
    if (areaTriggerCheckDelay > elapsed)
        areaTriggerCheckDelay -= elapsed;
    else
    {
        areaTriggerCheckDelay = ai::kBotAreaTriggerCheckMs;
        std::vector<uint32_t> const* runArmed = TortoiseBots::BotCommands::RunArmedAreaTriggers(bot);
        if (!sPlayerbotAIConfig.botAreaTriggerIds.empty() || runArmed)
            ai::UpdateBotAreaTriggers(bot, areaTriggerState, runArmed);
    }

    // revalidate the
    // cached master pointer against ObjectAccessor BEFORE any code
    // path can deref it. If the master Player was destroyed since
    // SetMaster() was called (typical when master logs out /
    // disconnects), FindPlayer returns nullptr or a different live
    // pointer. We null `master` defensively. The masterGuid shadow
    // (set in SetMaster, see PlayerbotAI.h) is the safe lookup key —
    // we never deref the cached `master` pointer in this check.
    if (master)
    {
        Player* live = masterGuid ? sObjectAccessor.FindPlayer(masterGuid) : nullptr;
        if (live != master)
        {
            master = nullptr;
        }
    }

    // Issue #846: after the master check, so the gate sees the live master.
    PublishChatGate();

    if(aiInternalUpdateDelay > elapsed)
    {
        aiInternalUpdateDelay -= elapsed;
    }
    else
    {
        aiInternalUpdateDelay = 0;
        isWaiting = false;
    }

    // cancel logout in combat
    if (bot->GetSession()->isLogingOut())
    {
        if (sServerFacade.IsInCombat(bot) || (master && sServerFacade.IsInCombat(master) && sServerFacade.getDistance2d(bot, master) < 30.0f))
        {
            WorldPacket p;
            bot->GetSession()->HandleLogoutCancelOpcode(p);
            TellPlayer(GetMaster(), BOT_TEXT("logout_cancel"));
        }
    }

    // Leontiesh - fix movement desync
    bool botMoving = false;
    if (!bot->IsStopped() || bot->GetMotionMaster()->GetCurrentMovementGeneratorType() != IDLE_MOTION_TYPE)
        botMoving = true;
    if (!bot->GetMotionMaster()->empty())
        if (MovementGenerator* movgen = bot->GetMotionMaster()->top())
            botMoving = true;


    if (botMoving && !bot->IsBeingTeleported() && bot->IsInWorld())
    {
        isMoving = true;

        GuidPosition lootObject(bot->GetLootGuid(), bot);

        bool shouldRelease = true;

        if (lootObject)
        {
            if (lootObject.IsGameObject() && lootObject.GetGameObjectInfo()->type == GAMEOBJECT_TYPE_FISHINGNODE)
                shouldRelease = false;
            else if (lootObject.GetWorldObject(bot->GetInstanceId()) && bot->GetDistance(lootObject.GetWorldObject(bot->GetInstanceId())) < INTERACTION_DISTANCE)
                shouldRelease = false;


            if (Loot* loot = sLootMgr.GetLoot(bot, bot->GetLootGuid()))
            {
                if (lootObject.IsUnit())
                {
                    Unit* unitTarget = (Unit*)loot->GetLootTarget();
                    Loot const* utLoot = (unitTarget && unitTarget->GetTypeId() == TYPEID_UNIT) ? &static_cast<Creature const*>(unitTarget)->loot : nullptr;
                    if (utLoot && utLoot->GetLootingPlayers().count(bot->getObjectGuid()) == 0)
                    {
                        // You shouldn't release if you aren't looting already
                        shouldRelease = false;
                    }
                }
                else if (lootObject.IsGameObject())
                {
                    GameObject* gameobjectTarget = (GameObject*)loot->GetLootTarget();
                    Loot const* goLoot = gameobjectTarget ? &gameobjectTarget->loot : nullptr;
                    if (goLoot && !goLoot->GetLootingPlayers().count(bot->getObjectGuid()))
                    {
                        // You shouldn't release if you aren't looting already
                        shouldRelease = false;
                    }
                }
                else
                {
                    // If it's not a unit or GO, what looting would this be?
                    // safe to assume you probably shouldn't release
                    shouldRelease = false;
                }
                // release loot if moving and far from object.
                if (shouldRelease)
                {
                    bot->GetSession()->DoLootRelease(bot->GetLootGuid());
                }
            }
        }
    }
    else if (isMoving)
    {
        if (!bot->IsTaxiFlying())
            StopMoving();

        isMoving = false;
    }


    // wake up if in combat
    bool isCasting = bot->IsNonMeleeSpellCasted(true);
    if (sServerFacade.IsInCombat(bot))
    {
        // Attacked while fishing: the Fishing channel has no damage interrupt flag, so the bot
        // stood channelling for up to twenty seconds while it was hit, and afterwards fought
        // with the fishing pole in hand (23 % of all deaths, warriors first). The moment combat
        // starts the channel is dropped and the weapon comes back before anything else.
        if (!inCombat)
        {
            if (Spell* channel = bot->GetCurrentSpell(CURRENT_CHANNELED_SPELL))
                if (channel->m_spellInfo && std::string(channel->m_spellInfo->SpellName[0]).find("Fishing") == 0)
                {
                    bot->InterruptSpell(CURRENT_CHANNELED_SPELL);
                    isCasting = bot->IsNonMeleeSpellCasted(true);
                }
            if (Item* mainHand = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_MAINHAND))
                if (mainHand->GetProto()->Class == ITEM_CLASS_WEAPON && mainHand->GetProto()->SubClass == ITEM_SUBCLASS_WEAPON_FISHING_POLE)
                    DoSpecificAction("equip upgrades", Event("fishing pole recovery"), true);
            // Combat ends any open-water fishing session at once: the spot is
            // dropped (no walk-back after the fight) and the hourly cooldown
            // starts now, so the bot levels instead of re-casting.
            ai::EndFishingSession(this, "combat");
        }
        if (!inCombat && !isCasting && !isWaiting)
        {
            ResetAIInternalUpdateDelay();
            // REACT_ACTIVITY has its own cache slot (separate from ALL_ACTIVITY above) that
            // can still be holding a stale "inactive" verdict from just before combat started -
            // force a fresh check now instead of waiting up to 5s for it to expire on its own.
            AllowActivity(REACT_ACTIVITY, true);
        }
        else if (!AllowActivity())
        {
            if (AllowActivity(ALL_ACTIVITY, true))
                ResetAIInternalUpdateDelay();
        }

        inCombat = true;
    }
    else
    {
        if (inCombat && !isCasting && !isWaiting)
            ResetAIInternalUpdateDelay();

        inCombat = false;

        // Wake up a resting bot when its group starts fighting (#133). A meal
        // puts the AI to sleep for up to 27 s and the bot's own combat flag
        // only rises once it hits something, so a caster sitting behind the
        // tank kept eating through the pull and its CC order. Cheap checks
        // first: only a bot that is asleep, seated and has a food or drink
        // aura looks at its group. Standing drops the aura, so this fires
        // once; the fresh attackers make "combat start" fire on the next tick.
        if (aiInternalUpdateDelay > 0 && !isWaiting && bot->IsSittingDown() &&
            HasSeatedAura(bot) && IsGroupMemberNearInCombat())
        {
            bot->SetStandState(UNIT_STAND_STATE_STAND);
            aiObjectContext->GetValue<std::list<ObjectGuid>>("attackers", 1)->Reset();
            aiObjectContext->GetValue<bool>("has attackers")->Reset();
            ResetAIInternalUpdateDelay();
        }
    }

    // force stop if moving but should not
    if (!bot->IsStopped() && !IsJumping() && !CanMove() && !bot->IsTaxiFlying() && !bot->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_FLEEING) && !bot->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_CONFUSED))
    {
        StopMoving();
    }

    // land after knockback/jump
    uint32 curTime = WorldTimer::getMSTime();
    if (jumpTime && (jumpTime < curTime || (jumpTime + 10000 < curTime)))
    {
        // might be not needed
        if (GetJumpDestination())
        {
            bot->Relocate(jumpDestination.getX(), jumpDestination.getY(), jumpDestination.getZ());
        }

        // normal landing
        if (!fallAfterJump)
        {
            bot->m_movementInfo.AddMovementFlag(MOVEFLAG_FALLINGFAR);

            WorldPacket stop(MSG_MOVE_STOP);
            stop << bot->m_movementInfo;
            QueuePacket(stop);

            bot->m_movementInfo.SetMovementFlags(MOVEFLAG_NONE);
            bot->m_movementInfo.jump = MovementInfo::JumpInfo();

            WorldPacket land(MSG_MOVE_FALL_LAND);
            land << bot->m_movementInfo;
            QueuePacket(land);
            sLog.outDetail("%s: Jump: Landed, landTime: %u", bot->GetName(), curTime);

            // Player::IsFalling() (m_fallStartZ != 0, set by SetFallInformation() above
            // this block and in the knockback handler) is what actually gates CanMove().
            // It's meant to clear via Player::UpdateFallInformationIfNeed() when the
            // MSG_MOVE_FALL_LAND packet just queued above is processed by
            // WorldSession::HandleMovementOpcodes - but that processing is unreliable for
            // an AI-synthesized packet (dropped if the bot's movespline isn't finalized,
            // rejected by anti-cheat, etc.), and when it's dropped m_fallStartZ is never
            // reset, permanently blocking the bot from moving again. Clear it here
            // directly so landing doesn't depend on that fragile round-trip.
            bot->SetFallInformation(0.0f);
            jumpTime = 0;
            fallAfterJump = false;
            ResetJumpDestination();

            bot->StopMoving();
        }
        // falling after hitting something
        else
        {
            //bot->SetFallInformation(0, bot->m_movementInfo.pos.z);
            bot->m_movementInfo.AddMovementFlag(MOVEFLAG_JUMPING);
            // simulate falling
            float landingHeight = bot->m_movementInfo.pos.z;
            bot->UpdateAllowedPositionZ(bot->m_movementInfo.pos.x, bot->m_movementInfo.pos.y, landingHeight);
            // A long way down is not always a fall. Where the map's floor lookup misses the
            // structure the bot stands on - a WMO deck over terrain: a custom cave 180yd over
            // its terrain, Stormwind's streets 36yd over theirs - UpdateAllowedPositionZ answers
            // with the terrain beneath, and the bot "falls" through the floor onto ground that
            // has no way back up (seen: a dungeon tank that hopped once in a chase, and the
            // random bots that collect under Stormwind). The navmesh knows better: a walkable
            // polygon AT OR UNDER the feet means the bot has landed. Failing that, a fall
            // deeper than AiPlayerbot.JumpHeightLimit is refused - the bot stays at the
            // height the jump ended on.
            if (bot->m_movementInfo.pos.z - landingHeight > 5.0f)
            {
                // ClosestCorrectPoint's box spans kFeetProbeDepth either side of its query
                // point; centering the query kFeetProbeDepth below the feet puts the top of
                // the box AT the feet, so only floor at or under the bot can be found. A deck
                // over the head (a canal bridge) must not count as landing ground - it would
                // snap the landing above where the jump ended.
                constexpr float kFeetProbeDepth = 4.0f;
                WorldPosition feet(bot->GetMapId(), bot->m_movementInfo.pos.x, bot->m_movementInfo.pos.y, bot->m_movementInfo.pos.z - kFeetProbeDepth);
                if (feet.isMmapLoaded(bot->GetInstanceId()) && feet.ClosestCorrectPoint(2.0f, kFeetProbeDepth, bot->GetInstanceId()))
                    landingHeight = std::min(feet.getZ(), bot->m_movementInfo.pos.z);
                else if (bot->m_movementInfo.pos.z - landingHeight > sPlayerbotAIConfig.jumpHeightLimit)
                    landingHeight = bot->m_movementInfo.pos.z;
            }

            // calculate fall time
            float gravity = 19.2911f;
            float terminalVelocity = 60.148f;
            float time;

            const float terminal_length = float(terminalVelocity * terminalVelocity) / (2.f * gravity);
            const float terminalFallTime = float(terminalVelocity / gravity);

            float path_length = fabs(bot->m_movementInfo.pos.z - landingHeight);
            if (path_length >= terminal_length)
                time = (path_length - terminal_length) / terminalVelocity + terminalFallTime;
            else
                time = sqrtf(2.f * path_length / gravity);

            SetJumpTime(curTime + uint32(time * static_cast<uint32>(IN_MILLISECONDS)) + 1000);
            fallAfterJump = false;
            jumpDestination = WorldPosition(bot->GetMapId(), bot->m_movementInfo.pos.x, bot->m_movementInfo.pos.y, landingHeight);
            sLog.outDetail("%s: Jump: Falling simulated, height: %f, timeToLand %u", bot->GetName(), landingHeight, jumpTime);
        }
    }

    // cheat options
    if (bot->IsAlive() && ((uint32)GetCheat() > 0 || (uint32)sPlayerbotAIConfig.botCheatMask > 0 || (uint32)sPlayerbotAIConfig.rndBotCheatMask > 0))
    {
        if (HasCheat(BotCheatMask::health))
            bot->SetHealthPercent(100);
        if (HasCheat(BotCheatMask::mana) && bot->GetPowerType() == POWER_MANA)
            bot->SetPower(POWER_MANA, bot->GetMaxPower(POWER_MANA));
        if (HasCheat(BotCheatMask::power) && bot->GetPowerType() != POWER_MANA)
            bot->SetPower(bot->GetPowerType(), bot->GetMaxPower(bot->GetPowerType()));
        if (HasCheat(BotCheatMask::cooldown))
            bot->RemoveAllSpellCooldown();
        if (HasCheat(BotCheatMask::repair))
        {
            // The "repair" cheat is meant to mean gear never actually breaks, but nothing
            // ever called PlayerbotAI::DurabilityLoss() (the only place that checked this
            // cheat before applying durability loss) - normal combat/death durability loss
            // goes through the core engine directly and bypasses it entirely. Left
            // unrepaired, a broken (0-durability) weapon makes every spell that needs it
            // fail CanCastSpell with SPELL_FAILED_EQUIPPED_ITEM_CLASS forever, regardless of
            // range, since that failure has nothing to do with distance - this is what was
            // permanently soft-locking hunters on their current target. Only pay for the
            // repair scan when something is actually below full durability.
            bool needsRepair = false;
            for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
            {
                Item* item = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
                if (item && item->GetUInt32Value(ITEM_FIELD_MAXDURABILITY) &&
                    item->GetUInt32Value(ITEM_FIELD_DURABILITY) < item->GetUInt32Value(ITEM_FIELD_MAXDURABILITY))
                {
                    needsRepair = true;
                    break;
                }
            }
            if (needsRepair)
                bot->DurabilityRepairAll(false, 0.0f);
        }
        if (HasCheat(BotCheatMask::movespeed))
        {
            bot->UpdateSpeed(MOVE_WALK, true, 10);
            bot->UpdateSpeed(MOVE_RUN, true, 10);
            bot->UpdateSpeed(MOVE_SWIM, true, 10);
        }
        if (HasCheat(BotCheatMask::breath))
        {
            bot->SetWaterBreathingIntervalMultiplier(0);
        }
        if (HasCheat(BotCheatMask::item) && (bot->GetClass() == CLASS_HUNTER || bot->GetClass() == CLASS_ROGUE || bot->GetClass() == CLASS_WARRIOR))
        {
            // Server-managed ammo (Issue #401): InitAmmo picks the
            // level/weapon-appropriate ammo, and a gun<->bow swap leaves a
            // stale ammo id behind. Resync through InitAmmo first so the
            // refill below tops up the right stack instead of the old one.
            // Throttled to one attempt a minute: when bags are full the
            // mismatch cannot resolve and must not busy-loop every tick.
            bool ammoMismatch = false;
            if (Item* ranged = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_RANGED))
            {
                uint32 wantSub = 0;
                switch (ranged->GetProto()->SubClass)
                {
                case ITEM_SUBCLASS_WEAPON_GUN:
                    wantSub = ITEM_SUBCLASS_BULLET;
                    break;
                case ITEM_SUBCLASS_WEAPON_BOW:
                case ITEM_SUBCLASS_WEAPON_CROSSBOW:
                    wantSub = ITEM_SUBCLASS_ARROW;
                    break;
                case ITEM_SUBCLASS_WEAPON_THROWN:
                    break;
                }
                uint32 ammoId = bot->GetUInt32Value(PLAYER_AMMO_ID);
                ItemPrototype const* ammoProto = ammoId ? sObjectMgr.GetItemPrototype(ammoId) : nullptr;
                if (wantSub && (!ammoProto || ammoProto->Class != ITEM_CLASS_PROJECTILE || ammoProto->SubClass != wantSub))
                    ammoMismatch = true;
            }
            if (ammoMismatch)
            {
                uint32 nowMs = WorldTimer::getMSTime();
                if (!m_lastAmmoResyncMs || nowMs - m_lastAmmoResyncMs >= 60 * 1000)
                {
                    m_lastAmmoResyncMs = nowMs;
                    PlayerbotFactory(bot, bot->GetLevel(), 0).InitAmmo();
                }
            }
            uint32 itemId = bot->GetUInt32Value(PLAYER_AMMO_ID);
            if (itemId && bot->GetItemCount(itemId))
            {
                std::list<Item*> items = AI_VALUE2(std::list<Item*>, "inventory items", this->chatHelper.formatQItem(itemId));

                for (auto item : items)
                {
                    if (bot->GetClass() == CLASS_HUNTER && item->GetProto()->SubClass == ITEM_SUBCLASS_WEAPON_THROWN) //Do not replenish thrown weapons for hunters.
                        break;

                    item->SetCount(item->GetProto()->GetMaxStackSize());
                    break;
                }
            }
            else
            {
                PlayerbotFactory(bot, bot->GetLevel(), 0).InitAmmo();
            }
        }
        // Never-empty kit rations (owner addendum): pool-only sibling of the
        // ammo refill above. The cheat eat/drink runs on spells, so the stack
        // only shrinks through sharing/selling/destroying - top it back up
        // every tick, same as ammo. One stack each; bags fit (3x14 now).
        RefillPoolRations();
        // Owner rule (auto-tools kit): same cheat block, same tick. Rogues
        // keep max lockpicking + Thieves' Tools; Bronze carriers in BWL get
        // one Hourglass Sand; MC visitors get one Quintessence.
        EnsureAutoToolsKit();
    }

    // Alt level sync - level up non-random bots to match master level
    if (sPlayerbotAIConfig.syncAltLevelToMaster && bot->IsAlive() && master && !sRandomBotFacade.IsRandomBot(bot)
        && bot->GetGroup() && master->GetGroup() && IsSafe(master) && bot->GetGroup()->GetLeaderGuid() == master->getObjectGuid())
    {
        uint32 botLevel = bot->GetLevel();
        uint32 masterLevel = master->GetLevel();
        uint32 maxLevel = sWorld.getConfig(CONFIG_UINT32_MAX_PLAYER_LEVEL);

        if (botLevel < masterLevel && botLevel < maxLevel)
        {
            bot->GiveLevel(botLevel + 1);

            if (sPlayerbotAIConfig.hasLog("bot_events.csv"))
            {
                sPlayerbotAIConfig.logEvent(this, "AltLevelSync", ObjectGuid(),
                    "Leveled from " + std::to_string(botLevel) + " to " + std::to_string(bot->GetLevel()) +
                    " (master: " + std::to_string(masterLevel) + ")");
            }
        }
    }

    if (master && IsSafe(master) && bot->GetDistance(master) < INTERACTION_DISTANCE * 2.5 && master->GetTransport() != bot->GetTransport() && bot->GetMotionMaster()->GetCurrentMovementGeneratorType() == FOLLOW_MOTION_TYPE)
    {
        bot->StopMoving();
        if (master->GetTransport() && WorldPosition(bot).isOnTransport(master->GetTransport()))
            master->GetTransport()->AddPassenger(bot);
        else if (bot->GetTransport())
        {
            WorldPosition botPos(bot);
            bot->GetTransport()->RemovePassenger(bot);
            // RemovePassenger leaves MOVEFLAG_ONTRANSPORT set in the core;
            // a stale flag with no transport freezes the bot (issue #216).
            bot->m_movementInfo.RemoveMovementFlag(MOVEFLAG_ONTRANSPORT);
            bot->NearTeleportTo(bot->m_movementInfo.pos.x, bot->m_movementInfo.pos.y, bot->m_movementInfo.pos.z, bot->m_movementInfo.pos.o);
            MANGOS_ASSERT(botPos.fDist(bot) < 500.0f);
        }
    }
    else if (!HasRealPlayerMaster() && !bot->IsBeingTeleported() && bot->GetTransport() && bot->GetMapId() == bot->GetTransport()->GetMapId() && !WorldPosition(bot).isOnTransport(bot->GetTransport()) && !isMovingToTransport)
    {
        if (HasStrategy("debug move", BotState::BOT_STATE_NON_COMBAT))
        {
            TellPlayer(GetMaster(), "Jumping off " + std::string(bot->GetTransport()->GetName()));
        }

        WorldPosition botPos(bot);
        bot->GetTransport()->RemovePassenger(bot);
        // RemovePassenger leaves MOVEFLAG_ONTRANSPORT set in the core;
        // a stale flag with no transport freezes the bot (issue #216).
        bot->m_movementInfo.RemoveMovementFlag(MOVEFLAG_ONTRANSPORT);
        bot->NearTeleportTo(bot->m_movementInfo.pos.x, bot->m_movementInfo.pos.y, bot->m_movementInfo.pos.z, bot->m_movementInfo.pos.o);
        MANGOS_ASSERT(botPos.fDist(bot) < 500.0f);
        bot->StopMoving();
    }

    // Update facing
    SC_PHASE("UpdateAI.UpdateFaceTarget", bot ? bot->GetName() : "(null)");
    UpdateFaceTarget(elapsed, minimal);

    bool doMinimalReaction = minimal || !AllowActivity(REACT_ACTIVITY);

#ifdef PLAYERBOT_ELUNA
    // used by eluna
    if (Eluna* e = bot->GetEluna())
        e->OnUpdateAI(this, bot->GetName());
#endif

    // Only update the internal ai when no reaction is running and the internal ai can be updated
    SC_PHASE("UpdateAI.UpdateAIReaction", bot ? bot->GetName() : "(null)");
    if(!UpdateAIReaction(elapsed, doMinimalReaction, bot->IsTaxiFlying()) && CanUpdateAIInternal())
    {
        // Update the delay with the spell cast time
        Spell* currentSpell = bot->GetCurrentSpell(CURRENT_GENERIC_SPELL);
        if (currentSpell && (currentSpell->getState() == SPELL_STATE_CASTING) && (currentSpell->GetCastedTime() > 0U))
        {
            SetAIInternalUpdateDelay(currentSpell->GetCastedTime() + sPlayerbotAIConfig.reactDelay + sWorld.GetAverageDiff());

            // Cancel the update if the new delay increased
            if (!CanUpdateAIInternal())
            {
                return;
            }
        }

        SC_PHASE("UpdateAI.UpdateAIInternal", bot ? bot->GetName() : "(null)");
        UpdateAIInternal(elapsed, minimal);

        bool min = minimal;
        // test fix lags because of BG
        if (!inCombat)
            min = true;

        if (bot && HasRealPlayerMaster())
            min = false;

        SC_PHASE("UpdateAI.YieldAIInternalThread", bot ? bot->GetName() : "(null)");
        YieldAIInternalThread(min);
    }
    SC_PHASE("UpdateAI.exit", bot ? bot->GetName() : "(null)");
}

bool PlayerbotAI::UpdateAIReaction(uint32 elapsed, bool minimal, bool isStunned)
{
    bool reactionFound;
    std::unique_ptr<PerformanceMonitorOperation> pmo;
    if (sPlayerbotAIConfig.perfMonEnabled)
    {
        std::string mapString = WorldPosition(bot).isInstance() ? "I" : std::to_string(bot->GetMapId());
        pmo = sPerformanceMonitor.start(PERF_MON_TOTAL, "PlayerbotAI::UpdateAIReaction " + mapString, nullptr, bot->GetMapId(), bot->GetInstanceId());
    }
    const bool reactionInProgress = reactionEngine->Update(elapsed, minimal, isStunned, reactionFound);
    pmo.reset();

    if(reactionFound)
    {
        // If new reaction found force stop current actions (if required)
        const Reaction* reaction = reactionEngine->GetReaction();
        if(reaction)
        {
            if(reaction->ShouldInterruptCast())
            {
                InterruptSpell();
            }

            if (reaction->ShouldInterruptMovement())
            {
                StopMoving();
            }
        }
    }

    return reactionInProgress;
}

void PlayerbotAI::UpdateFaceTarget(uint32 elapsed, bool minimal)
{
    faceTargetUpdateDelay = faceTargetUpdateDelay > elapsed ? faceTargetUpdateDelay - elapsed : 0U;
    if (faceTargetUpdateDelay <= 0U)
    {
        // Only update the target facing when in combat
        if (IsStateActive(BotState::BOT_STATE_COMBAT))
        {
            // Don't update facing if bot is moving
            if (!sServerFacade.isMoving(bot) && !bot->IsMoving())
            {
                AiObjectContext* context = GetAiObjectContext();
                Unit* target = AI_VALUE(Unit*, "current target");
                if(target)
                {
                    // Do not update the facing while pulling
                    Unit* pullTarget = AI_VALUE(Unit*, "pull target");
                    if (pullTarget == nullptr)
                    {
                        if (!AI_VALUE2(bool, "facing", "current target"))
                        {
                            if (!sServerFacade.UnitIsDead(bot) &&
                                !sServerFacade.IsFrozen(bot) &&
                                !sServerFacade.IsCharmed(bot) &&
                                !bot->IsPolymorphed() &&
                                !bot->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_GHOST) &&
                                !bot->IsBeingTeleported() &&
                                !bot->HasAuraType(SPELL_AURA_MOD_CONFUSE) &&
                                !bot->HasAuraType(SPELL_AURA_MOD_STUN) &&
                                !bot->IsTaxiFlying() &&
                                !bot->HasUnitState(UNIT_STAT_CAN_NOT_REACT_OR_LOST_CONTROL))
                            {
                                sServerFacade.SetFacingTo(bot, target);
                                bot->SetInFront(target);
                                WorldPacket data(MSG_MOVE_SET_FACING);
                                data << bot->GetPackGUID();
                                data << bot->m_movementInfo;
                                bot->GetMover()->SendMessageToSetExcept(&data, bot);
                            }
                        }
                    }
                }
            }
        }

        faceTargetUpdateDelay = minimal ? sPlayerbotAIConfig.reactDelay * 10 : sPlayerbotAIConfig.reactDelay * 5;
    }
}

void PlayerbotAI::SetActionDuration(const Action* action)
{
    if (action)
    {
        if(action->IsReaction())
        {
            reactionEngine->SetReactionDuration(action);
        }
        else
        {
            SetAIInternalUpdateDelay(action->GetDuration());
        }
    }
}

void PlayerbotAI::SetActionDuration(uint32 duration)
{
    SetAIInternalUpdateDelay(duration);
}

std::string const& PlayerbotAI::GetLastExecutedTrigger(BotState state) const
{
    static std::string const empty;
    const Engine* engine = engines[(uint8)state];
    return engine ? engine->GetLastExecutedTrigger() : empty;
}

const Action* PlayerbotAI::GetLastExecutedAction(BotState state) const
{
    const Engine* engine = engines[(uint8)state];
    if(engine)
    {
        return engine->GetLastExecutedAction();
    }

    return nullptr;
}

bool PlayerbotAI::IsImmuneToSpell(uint32 spellId) const
{
    for (std::list<uint32>::iterator i = sPlayerbotAIConfig.immuneSpellIds.begin(); i != sPlayerbotAIConfig.immuneSpellIds.end(); ++i)
    {
        if (spellId == (*i))
        {
            return true;
        }
    }

    return false;
}

bool PlayerbotAI::IsInPve()
{
    return !IsInPvp() && !IsInRaid();
}

bool PlayerbotAI::IsInPvp()
{
    if (IsSafe(bot))
    {
        const bool inDuel = bot->m_duel && bot->m_duel->opponent;
        if (!inDuel)
        {
            const bool inBattleground = bot->InBattleGround();
            if (!inBattleground)
            {
                AiObjectContext* context = aiObjectContext;
                const bool isPlayerNear = AI_VALUE(bool, "has enemy player targets");
                if (!isPlayerNear)
                {
                    return false;
                }
            }
        }

        return true;
    }

    return false;
}

bool PlayerbotAI::IsInRaid()
{
    bool inRaidFight = false;
    if (IsSafe(bot))
    {
        const Map* map = bot->GetMap();
        if (map && (map->IsDungeon() || map->IsRaid()))
        {
            inRaidFight = true;
        }
        else if (!IsInPvp() && GetState() == BotState::BOT_STATE_COMBAT)
        {
            AiObjectContext* context = GetAiObjectContext();
            const std::list<ObjectGuid>& attackers = AI_VALUE(std::list<ObjectGuid>, "attackers");
            for (const ObjectGuid& attackerGuid : attackers)
            {
                Creature* creature = GetCreature(attackerGuid);
                if (creature)
                {
                    const CreatureInfo* creatureInfo = creature->GetCreatureInfo();
                    if (creatureInfo)
                    {
                        if (creatureInfo->rank == CREATURE_ELITE_WORLDBOSS)
                        {
                            inRaidFight = true;
                            break;
                        }
                    }
                }
            }
        }
    }

    return inRaidFight;
}

PlayerTalentSpec PlayerbotAI::GetTalentSpec()
{
    return aiObjectContext->GetValue<PlayerTalentSpec>("talent spec")->Get();
}

void PlayerbotAI::UpdateTalentSpec(PlayerTalentSpec spec)
{
    if(spec == PlayerTalentSpec::TALENT_SPEC_INVALID)
    {
        int talentsTab = 0;
        if(bot->GetLevel() < 10)
        {
            switch (bot->GetClass())
            {
                case CLASS_MAGE:
                {
                    talentsTab = 1;
                    break;
                }

                case CLASS_PALADIN:
                {
                    talentsTab = 0;
                    break;
                }

                case CLASS_PRIEST:
                {
                    talentsTab = 1;
                    break;
                }

                case CLASS_WARRIOR:
                {
                    talentsTab = 2;
                    break;
                }

                case CLASS_ROGUE:
                {
                    talentsTab = 1;
                    break;
                }
            }
        }
        else
        {
            talentsTab = AiFactory::GetPlayerSpecTab(bot);
        }

        spec = PlayerTalentSpec(((bot->GetClass() * 3) - 2) + talentsTab);
    }

    aiObjectContext->GetValue<PlayerTalentSpec>("talent spec")->Set(spec);
}

bool PlayerbotAI::CanEnterArea(const AreaTrigger* area)
{
    if (sRandomBotFacade.IsRandomBot(GetBot()))
    {
        DungeonPersistentState* state = bot->GetBoundInstanceSaveForSelfOrGroup(area->mapid);
        Map* map = sMapMgr.FindMap(area->mapid, state ? state->GetInstanceId() : 0);
        const MapEntry* mapEntry = sMapStore.LookupEntry(area->mapid);

        // check if this account try to abuse reseting instance
        if (mapEntry->IsNonRaidDungeon())
        {
            // Tortoise performs the instance admission check in the normal
            // teleport/login path; no separate bot-only check is needed here.
        }

        // Map's state check
        if (map && map->IsDungeon())
        {
            // cannot enter if the instance is full (player cap), GMs don't count, must not check when teleporting around the same map
            if (bot->GetMapId() != area->mapid)
            {
                if (((DungeonMap*)map)->GetPlayersCountExceptGMs() >= ((DungeonMap*)map)->GetMaxPlayers())
                {
                    return false;
                }

                // In Combat check
                if (map && map->GetInstanceData() && map->GetInstanceData()->IsEncounterInProgress())
                {
                    return false;
                }

                // Bind Checks
                InstancePlayerBind* pBind = bot->GetBoundInstance(area->mapid);
                if (pBind && pBind->perm && pBind->state != state)
                {
                    return false;
                }

                if (pBind && pBind->perm && pBind->state != map->GetPersistentState())
                {
                    return false;
                }
            }
        }

        return true;
    }

    return false;
}

void PlayerbotAI::Unmount()
{
    if ((bot->IsMounted() || bot->GetMountID()) && !bot->IsTaxiFlying())
    {
        bot->RemoveSpellsCausingAura(SPELL_AURA_MOUNTED);
        bot->Unmount();

        bot->UpdateSpeed(MOVE_RUN, true);
        bot->UpdateSpeed(MOVE_RUN, false);

        if (bot->IsFlying())
        {
            bot->StopMoving();
        }
    }
}

bool PlayerbotAI::IsStateActive(BotState state) const
{
    return currentEngine == engines[(uint8)state];
}

time_t PlayerbotAI::GetCombatStartTime() const
{
    return aiObjectContext->GetValue<time_t>("combat start time")->Get();
}

bool PlayerbotAI::IsGroupMemberNearInCombat()
{
    // Battlegrounds keep their own rhythm: the attackers value does not pool
    // a battleground raid either.
    Group* group = bot->GetGroup();
    if (!group || bot->InBattleGround())
        return false;

    for (Player* member : LiveGroupMembers(group))
    {
        if (member == bot || !IsSafe(member) || !member->IsInWorld() || !sServerFacade.IsAlive(member))
            continue;

        // A combat flag alone can be a stuck flag; a real fight has a victim
        // or something attacking the member.
        if (!sServerFacade.IsInCombat(member) || (!member->GetVictim() && member->GetAttackers().empty()))
            continue;

        // A duel is not the group's fight.
        if (member->m_duel && member->GetVictim() && member->GetVictim()->GetObjectGuid() == member->m_duel->opponent)
            continue;

        if (sServerFacade.GetDistance2d(bot, member) <= sPlayerbotAIConfig.sightDistance)
            return true;
    }

    return false;
}

void PlayerbotAI::OnCombatStarted()
{
    if(!IsStateActive(BotState::BOT_STATE_COMBAT))
    {
        // Reset the combat start timestamp
        aiObjectContext->GetValue<time_t>("combat start time")->Set(time(0));

        // Update stay position on location when combat starts
        if (HasStrategy("stay", BotState::BOT_STATE_COMBAT) && !HasStrategy("stay", BotState::BOT_STATE_NON_COMBAT))
        {
            aiObjectContext->GetValue<PositionEntry>("pos", "stay")->Set(PositionEntry(bot->getPositionX(), bot->getPositionY(), bot->getPositionZ(), bot->GetMapId()));
        }

        // Stop follow movement on combat start
        if (!(HasStrategy("follow", BotState::BOT_STATE_COMBAT) || HasStrategy("wander", BotState::BOT_STATE_COMBAT)) &&
            (HasStrategy("follow", BotState::BOT_STATE_NON_COMBAT) || HasStrategy("wander", BotState::BOT_STATE_NON_COMBAT)))
        {
            StopMoving();
        }

        aiObjectContext->GetValue<std::list<ObjectGuid>>("attackers", 1)->Reset();
        aiObjectContext->GetValue<bool>("has attackers")->Reset();

        ChangeEngine(BotState::BOT_STATE_COMBAT);
        if (bot->GetStandState() == UNIT_STAND_STATE_SIT)
        {
            ResetAIInternalUpdateDelay();
        }
    }
}
void PlayerbotAI::StartFightStartHold()
{
    if (!bot || !aiObjectContext)
        return;
    ai::FightStartHoldFacts facts;
    facts.runningRun = TortoiseBots::BotCommands::IsInRunningDungeonRun(bot);
    facts.pullWindow = HasPullWindow();
    facts.isTank = IsTank(bot, true);
    facts.isHealer = IsHeal(bot, true);
    std::vector<ai::FightStartMember> members;
    for (Player* member : LiveGroupMembers(bot->GetGroup()))
    {
        if (!member)
            continue;
        ai::FightStartMember described;
        described.self = member == bot;
        described.alive = member->IsAlive();
        described.inWorld = member->IsInWorld();
        described.sameMap = member->GetMapId() == bot->GetMapId();
        described.tank = IsTank(member, true);
        described.realPlayer = IsRealPlayer(member);
        members.push_back(described);
    }
    ai::FillFightStartGroupFacts(facts, members);
    for (Unit* attacker : bot->GetAttackers())
    {
        if (!attacker || !attacker->IsAlive())
            continue;
        if (attacker->GetVictim() == bot)
        {
            facts.botIsVictim = true;
            break;
        }
    }
    Unit* ccMark = nullptr;
    if (auto* ccValue = aiObjectContext->GetValue<Unit*>("rti cc target"))
        ccMark = ccValue->Get();
    facts.ccMark = ccMark != nullptr;
    const char* skip = ai::FightStartHoldSkipReason(facts);
    if (skip == nullptr)
    {
        StartPullWindow(ai::kFightStartHoldSeconds, true, 0, true);
        RestampPullWindow(true);
        sLog.outBasic("[BOT] fight-start-hold open %s", bot->GetName());
    }
    else if (facts.runningRun)
        sLog.outBasic("[BOT] fight-start-hold skip %s reason=%s", bot->GetName(), skip);
}

void PlayerbotAI::OnCombatEnded()
{
    if (!IsStateActive(BotState::BOT_STATE_NON_COMBAT))
    {
        // The command-only priority is scoped to one combat engagement. The
        // normal combat-end reaction can run without a full Reset(), so clear
        // it here when the target dies or otherwise becomes invalid.
        if (aiObjectContext)
        {
            if (auto* explicitAttackTarget = aiObjectContext->GetValue<ObjectGuid>("explicit attack target"))
                explicitAttackTarget->Reset();
        }

        // Failure evidence belongs to this engagement only.
        LastMovement& movement = aiObjectContext->GetValue<LastMovement&>("last movement")->Get();
        movement.fleeFailures.Clear();
        movement.spreadFailures.Clear();

        // Reset the combat start timestamp
        aiObjectContext->GetValue<time_t>("combat start time")->Set(0);

        // Stop following on combat end
        if ((HasStrategy("follow", BotState::BOT_STATE_COMBAT) || HasStrategy("wander", BotState::BOT_STATE_COMBAT)) &&
            !(HasStrategy("follow", BotState::BOT_STATE_NON_COMBAT) || HasStrategy("wander", BotState::BOT_STATE_NON_COMBAT)))
        {
            StopMoving();
        }

        // The combat engine drops the "pull anchor done" trigger with it: a
        // pullback parks the tank on stay at the anchor for the fight it brings
        // in, and that hold is over the moment the fight is. Releasing it here
        // (rather than from a trigger in one engine) is what makes the release
        // engine-independent - without it the tank kept the stay in
        // BOT_STATE_NON_COMBAT, where no pull trigger exists, and never
        // followed the party again.
        ReleasePullHold();

        ChangeEngine(BotState::BOT_STATE_NON_COMBAT);
    }
}

void PlayerbotAI::NoteFightAttackers()
{
    // Sampled from DoNextAction while the fight is live: at death time the
    // attacker set is already drained (SetDeathState -> CombatStop), so the
    // death writer reads this snapshot instead. Cheap by design: one copy
    // of the live set per tick, dead creatures filtered out, capped at 8.
    if (!bot || !bot->IsInWorld() || !sServerFacade.IsInCombat(bot))
        return;
    deathAttackers_.clear();
    for (Unit* attacker : bot->GetAttackers())
    {
        if (!attacker || !sServerFacade.IsAlive(attacker))
            continue;
        ai::DeathAttackerEntry entry;
        entry.name = attacker->GetName();
        entry.level = attacker->GetLevel();
        deathAttackers_.push_back(entry);
        if (deathAttackers_.size() >= ai::kDeathAttackerListCap)
            break;
    }
    deathAttackersMs_ = WorldTimer::getMSTime();
}

void PlayerbotAI::RefillPoolRations()
{
    // Never-empty kit food/drink (owner addendum): the cheat-block sibling of
    // the ammo refill. Cheat-bot eat/drink runs on spells (24005/24355), so
    // stacks only shrink through sharing, selling or destroying - but the
    // theft paths exist (mage sharing, vendor churn), so top the level-tier
    // stack back up every tick like ammo. Pool-only (random record + item
    // cheat); owned/hired bots without the cheat keep the earned restock
    // path (RestockCompanion/OutOfRations vendor errand).
    if (!bot || !bot->IsAlive() || !bot->IsInWorld())
        return;
    if (!HasCheat(BotCheatMask::item) || !sRandomBotFacade.IsRandomBot(bot))
        return;
    uint32 const nowMs = WorldTimer::getMSTime();
    if (m_lastRationRefillMs && nowMs - m_lastRationRefillMs < 30 * 1000)
        return;
    m_lastRationRefillMs = nowMs;
    uint32 categories[] = { 11, 59 };
    for (int i = 0; i < 2; ++i)
    {
        uint32 category = categories[i];
        if (category == 59 && !ai::ShouldSeedDrink(bot->GetPowerType() == POWER_MANA))
            continue;
        uint32 wantId = sRandomItemMgr.GetFood(bot->GetLevel(), category, bot->GetGUIDLow());
        if (!wantId)
            continue;
        ItemPrototype const* wantProto = sObjectMgr.GetItemPrototype(wantId);
        if (!wantProto)
            continue;
        // Stale lower tier in the bags: drop it so the current tier is the
        // only food/drink the bot carries (no bag clutter, same rule as the
        // seed). Anything at or above the tier stays - player gifts, raid
        // drops and the mage's own conjured stack are never touched.
        FindFoodVisitor visitor(bot, category);
        InventoryIterateItems(&visitor, IterateItemsMask::ITERATE_ITEMS_IN_BAGS);
        for (Item* foodItem : visitor.GetResult())
        {
            if (!foodItem || !foodItem->GetProto() || foodItem->GetEntry() == wantId)
                continue;
            if (foodItem->GetProto()->ItemLevel < wantProto->ItemLevel)
                bot->DestroyItem(foodItem->GetBagSlot(), foodItem->GetSlot(), true);
        }
        uint32 have = bot->GetItemCount(wantId);
        uint32 want = wantProto->GetMaxStackSize();
        if (have >= want)
            continue;
        ItemPosCountVec dest;
        if (bot->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, wantId, want - have) != EQUIP_ERR_OK)
            continue;
        bot->StoreNewItem(dest, wantId, true, Item::GenerateItemRandomPropertyId(wantId));
    }
}

bool PlayerbotAI::EnsureCheatItem(uint32 itemId, uint32 count)
{
    // Generic cheat-gated mint the KIT_TASK brief asks for: #578's sand use
    // (and any future behaviour item) calls this instead of reimplementing
    // the cheat gate. No-op without the item cheat or with the item held;
    // returns true when the bot holds (or now holds) the item.
    if (!bot || !bot->IsAlive() || !bot->IsInWorld() || !itemId || !count)
        return false;
    if (bot->HasItemCount(itemId, count))
        return true;
    if (!HasCheat(BotCheatMask::item))
        return false;
    ItemPrototype const* proto = sObjectMgr.GetItemPrototype(itemId);
    if (!proto)
        return false;
    uint32 have = bot->GetItemCount(itemId);
    if (have >= count)
        return true;
    ItemPosCountVec dest;
    if (bot->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, itemId, count - have) != EQUIP_ERR_OK)
        return false;
    bot->StoreNewItem(dest, itemId, true, Item::GenerateItemRandomPropertyId(itemId));
    return bot->HasItemCount(itemId, count);
}

void PlayerbotAI::EnsureAutoToolsKit()
{
    // Owner rule (auto-tools kit), inside the cheat block: the bot carries
    // what its behaviours need. Two speeds: the Bronze/sand branch is
    // combat-critical and runs every tick (cheap when unafflicted: map +
    // aura checks short-circuit before any bag count); the rest is
    // throttled to one pass per 30 s per bot since those ensures walk the
    // bags and nothing there drains faster than that.
    if (!bot || !bot->IsAlive() || !bot->IsInWorld())
        return;
    if (!HasCheat(BotCheatMask::item))
        return;
    uint32 const mapId = bot->GetMapId();
    // Chromaggus Bronze (PR #578): a bot carrying the affliction gets sand
    // if it holds none, then the fight strategy uses it. Before the
    // throttle: Bronze re-applies through the fight, so a bot afflicted
    // just after a kit pass must not wait out stun ticks for the next one.
    if (mapId == TortoiseBots::BWL_MAP_ID &&
        HasAura(TortoiseBots::BRONZE_AFFLICTION_SPELL_ID, bot) &&
        TortoiseBots::ShouldEnsureHourglassSand(true, bot->GetItemCount(TortoiseBots::HOURGLASS_SAND_ITEM_ID)))
    {
        EnsureCheatItem(TortoiseBots::HOURGLASS_SAND_ITEM_ID, 1);
    }
    uint32 const nowMs = WorldTimer::getMSTime();
    if (m_lastAutoToolsKitMs && nowMs - m_lastAutoToolsKitMs < 30 * 1000)
        return;
    m_lastAutoToolsKitMs = nowMs;
    // Rogues: lockpicking at max-for-level plus Thieves' Tools in the bags,
    // so UnlockItemAction and PR #614 trade unlocks never fail on kit.
    // Pure value: enchant/racial bonuses must not mask a low base skill.
    // Pick Lock 1804 rides along: its ability row has learn_on_get_skill =
    // 0, so the skill alone never teaches it (verified in core).
    if (TortoiseBots::RogueWantsLockpickSkill(bot->GetClass(), bot->GetLevel()))
    {
        uint32 lockpickMax = TortoiseBots::LockpickSkillForLevel(bot->GetLevel());
        if (bot->GetSkillValuePure(SKILL_LOCKPICKING) < lockpickMax)
            bot->SetSkill(SKILL_LOCKPICKING, lockpickMax, lockpickMax);
        if (!bot->HasSpell(TortoiseBots::PICK_LOCK_SPELL_ID))
            bot->LearnSpell(TortoiseBots::PICK_LOCK_SPELL_ID, false);
        EnsureCheatItem(TortoiseBots::THIEVES_TOOLS_ITEM_ID, 1);
    }
    // MC rune douse: map gate first so open-world bots skip the bag counts;
    // one Quintessence while inside Molten Core so the rune actions never
    // silently skip for lack of the item.
    if (mapId == TortoiseBots::MC_MAP_ID &&
        TortoiseBots::ShouldEnsureQuintessence(mapId,
        bot->GetItemCount(TortoiseBots::ETERNAL_QUINTESSENCE_ITEM_ID),
        bot->GetItemCount(TortoiseBots::AQUAL_QUINTESSENCE_ITEM_ID)))
    {
        EnsureCheatItem(TortoiseBots::QuintessenceEnsureId(bot->GetLevel()), 1);
    }
}

void PlayerbotAI::NoteDamager(Unit* damager)
{
    if (!damager || damager == bot)
        return;
    lastDamager_.name = damager->GetName();
    lastDamager_.level = damager->GetLevel();
    lastDamager_.isEnvironment = false;
    lastDamager_.time = WorldTimer::getMSTime();
    lastDamager_.entry = damager->IsCreature() ? damager->GetEntry() : 0;
    lastDamager_.healthPct = damager->GetHealthPercent();
}

void PlayerbotAI::SetLastKiller(Unit* killer)
{
    lastKiller_.time = WorldTimer::getMSTime();
    // Spirit of Redemption: the core saves the priest as a spirit (aura
    // 27827) instead of killing it, then self-kills the spirit when the
    // aura expires (spell 27965) - so the death hook arrives with killer
    // == the bot itself and the real killing blow is only in the damager
    // memory above. Fall back to it (96% of "Environment" deaths are
    // priests 30+, Oct 2026 pool); a true self-kill with no remembered
    // damager keeps the old Environment verdict. Only a recent damager counts
    // (the spirit lasts 15 s), so a fall long after a fight stays Environment.
    if ((!killer || killer == bot) && !lastDamager_.name.empty() &&
        WorldTimer::getMSTimeDiff(lastDamager_.time, lastKiller_.time) <= 30000)
    {
        lastKiller_ = lastDamager_;
        lastKiller_.time = WorldTimer::getMSTime();
        return;
    }
    if (!killer || killer == bot)
    {
        lastKiller_.name = "Environment";
        lastKiller_.level = 0;
        lastKiller_.isEnvironment = true;
        lastKiller_.entry = 0;
    }
    else
    {
        lastKiller_.name = killer->GetName();
        lastKiller_.level = killer->GetLevel();
        lastKiller_.isEnvironment = false;
        lastKiller_.entry = killer->IsCreature() ? killer->GetEntry() : 0;
        lastKiller_.healthPct = killer->GetHealthPercent();
    }
}

bool PlayerbotAI::ShouldAvoidPlayerKiller(std::string const& name) const
{
    // 10-minute window; empty name or expired window never avoids.
    if (name.empty() || avoidPlayerKiller_.empty() || name != avoidPlayerKiller_)
        return false;
    return WorldTimer::getMSTime() - avoidPlayerKillerMs_ <= 10 * MINUTE * IN_MILLISECONDS;
}

void PlayerbotAI::NotePvpKill(Player* victim)
{
    if (!bot || !victim || bot->InBattleGround() || !sPlayerbotAIConfig.worldPvpSeek)
        return;
    if (!HasStrategy("world pvp", BotState::BOT_STATE_COMBAT) &&
        !HasStrategy("world pvp", BotState::BOT_STATE_NON_COMBAT))
        return;

    uint32 const nowMs = WorldTimer::getMSTime();
    worldPvpCampGuidLow_ = victim->GetGUIDLow();
    worldPvpCampStartMs_ = nowMs;
    worldPvpPursuitGuidLow_ = 0;
    worldPvpPursuitStartMs_ = 0;

    sObservabilityEmitter.EmitBotActivity("WorldPvpKill", victim->GetName(), std::to_string(victim->GetLevel()), bot);
}

void PlayerbotAI::NotePvpDeath(Player* killer)
{
    if (!bot || !killer || bot->InBattleGround() || !sPlayerbotAIConfig.worldPvpSeek)
        return;

    worldPvpCampGuidLow_ = 0;
    worldPvpCampStartMs_ = 0;
    worldPvpPursuitGuidLow_ = 0;
    worldPvpPursuitStartMs_ = 0;

    sObservabilityEmitter.EmitBotActivity("WorldPvpDeath", killer->GetName(), std::to_string(killer->GetLevel()), bot);
}

bool PlayerbotAI::WorldPvpGapAccepted(uint32 targetGuidLow, uint32 nowSec, bool& known) const
{
    for (WorldPvpGapDecision const& slot : worldPvpGapDecisions_)
    {
        if (slot.targetGuidLow != targetGuidLow)
            continue;
        if (!TortoiseBots::WorldPvpLevelGapDecisionFresh(slot.decidedAtSec, nowSec))
            break;
        known = true;
        return slot.accept;
    }
    known = false;
    return false;
}

void PlayerbotAI::WorldPvpNoteGapDecision(uint32 targetGuidLow, uint32 nowSec, bool accept)
{
    WorldPvpGapDecision* oldest = &worldPvpGapDecisions_[0];
    for (WorldPvpGapDecision& slot : worldPvpGapDecisions_)
    {
        if (slot.targetGuidLow == targetGuidLow || slot.targetGuidLow == 0)
        {
            slot.targetGuidLow = targetGuidLow;
            slot.decidedAtSec = nowSec;
            slot.accept = accept;
            return;
        }
        if (slot.decidedAtSec < oldest->decidedAtSec)
            oldest = &slot;
    }
    oldest->targetGuidLow = targetGuidLow;
    oldest->decidedAtSec = nowSec;
    oldest->accept = accept;
}

bool PlayerbotAI::IsDeathSpotAvoided(uint32 mapId, float x, float y, uint32 nowMs) const
{
    return ai::IsDeathPositionAvoided(mapId, x, y, nowMs, deathAvoidSpots_, ai::kDeathAvoidSpots);
}

// Death-cluster escape (OnDeath): how many deaths inside one hunting ground,
// within how long, before the bot leaves it, and for how long the
// destination/kind stays blacklisted. Calibrated on the cycle-3 pool's 769
// deaths: a bot's consecutive deaths sit a median 142 s and 142 yd apart (p90
// 492 yd), so a tight camp radius catches almost nothing (60 yd: 6 clusters) -
// 300 yd is the hunting-ground size that matters, where 3 deaths in 15 minutes
// covers 109 of the 769 deaths (14%) across 25 bots. Half an hour off matches
// the lethal-kind rule's cooldown.
static constexpr uint32 kDeathClusterDeaths = 3;
static constexpr uint32 kDeathClusterWindowMs = 15 * MINUTE * IN_MILLISECONDS;
static constexpr float kDeathClusterRadiusYd = 300.0f;
static constexpr uint32 kDeathClusterBlacklistMs = 30 * MINUTE * IN_MILLISECONDS;

void PlayerbotAI::OnDeath()
{
    if (!IsStateActive(BotState::BOT_STATE_DEAD) && !sServerFacade.IsAlive(bot))
    {
        // A bot that already has a corpse AND the ghost flag did not just die — it died
        // earlier and is corpse-running. The engine transiently leaves DEAD state during
        // the corpse run (e.g. on the graveyard teleport), so DeathTrigger
        // (!inDeadState && !isAlive) re-fires and OnDeath would otherwise log/count the
        // same death a second time — the "killer=none phantom death at the graveyard"
        // that inflated every death metric ~2x. A genuine fresh death always has no corpse
        // and no ghost flag yet (both spawn/set only after death, during release-spirit).
        // Restore the DEAD engine state so the corpse run continues, then bail without
        // re-logging or re-incrementing the death count.
        if (bot->GetCorpse() && bot->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_GHOST))
        {
            ChangeEngine(BotState::BOT_STATE_DEAD);
            return;
        }

        // Whatever the corpse and the ghost flag say, one death is logged and counted once:
        // the mark below is cleared only when the bot is seen alive again (OnResurrected).
        // What re-fires here in between - the graveyard teleport, a spirit-healer revive in
        // flight, bones already spawned - is the same death; it was logged as "killer=none"
        // at the graveyard up to six times a second apart, and the death count, the
        // hopeless-relocation threshold and the BOT_DEATH telemetry counted every one.
        if (deathHandled_)
        {
            ChangeEngine(BotState::BOT_STATE_DEAD);
            return;
        }
        deathHandled_ = true;

        StopMoving();

        Player* master = GetMaster();
        AiObjectContext* context = aiObjectContext;
        if (!HasActivePlayerMaster() && !bot->InBattleGround())
        {
            SET_AI_VALUE(uint32, "death count", AI_VALUE(uint32, "death count") + 1);

            // Two deaths in a row to the same kind of creature within fifteen minutes: the bot
            // leaves that kind alone for half an hour - it is not picked as a target and the
            // grind destinations for it drop (the same per-bot list the unreachable give-up
            // uses) - instead of walking back into it. Measured before: one mage died seven
            // times in 37 minutes to Razormane Thornweavers, a hunter five times to Prairie
            // Stalkers; 18 of 83 dead bots had three or more deaths in 22 minutes.
            if (lastKiller_.entry && context)
            {
                uint32 const nowMs = WorldTimer::getMSTime();
                if (lastKiller_.entry == prevKillerEntry_ && nowMs - prevKillerMs_ <= 15 * MINUTE * IN_MILLISECONDS)
                {
                    context->GetValue<std::map<uint32, uint32>&>("unreachable entries")->Get()[lastKiller_.entry] = nowMs + 30 * MINUTE * IN_MILLISECONDS;
                    TellDebug(GetMaster(), "Leaving " + lastKiller_.name + " alone for a while - it killed me twice", "debug move");
                    if (sPlayerbotAIConfig.hasLog("unreachable_targets.csv"))
                    {
                        time_t const nowLethal = time(nullptr);
                        char stampLethal[32];
                        strftime(stampLethal, sizeof(stampLethal), "%Y-%m-%d %H:%M:%S", localtime(&nowLethal));
                        std::ostringstream outLethal;
                        outLethal << stampLethal << "," << bot->GetName() << "," << bot->GetLevel() << ",LETHAL " << lastKiller_.name << "," << lastKiller_.level << ",lethal";
                        sPlayerbotAIConfig.log("unreachable_targets.csv", outLethal.str().c_str());
                    }
                }
                prevKillerEntry_ = lastKiller_.entry;
                prevKillerMs_ = nowMs;
            }
            // Errand death park: a second death inside fifteen minutes while
            // pursuing the same non-grind destination (hand-in, quest giver or
            // objective, node, trainer, vendor...) parks that destination for
            // half an hour and drops the target, so the next pick goes
            // elsewhere instead of walking the same deadly route again. Runs
            // before the cluster escape below, which may drop the target.
            // Pool bots without a real player master only; a forced target
            // (an explicit order) is never counted or parked.
            if (context && !HasRealPlayerMaster() && sRandomBotFacade.IsFreeBot(bot))
            {
                TravelTarget* errandTarget = AI_VALUE(TravelTarget*, "travel target");
                TravelDestination* errandDest = errandTarget ? errandTarget->GetDestination() : nullptr;
                uint32 const errandPurpose = errandDest ? (uint32)errandDest->GetPurpose() : 0;
                int32 const errandEntry = errandDest ? errandDest->GetEntry() : 0;
                if (errandDest && !errandTarget->IsForced() && errandEntry != 0 &&
                    errandPurpose != (uint32)TravelDestinationPurpose::None &&
                    errandPurpose != (uint32)TravelDestinationPurpose::Grind &&
                    typeid(*errandDest) != typeid(NullTravelDestination))
                {
                    uint32 const nowErrandMs = WorldTimer::getMSTime();
                    errandDeathCount_ = ai::NextErrandDeathCount(errandDeathCount_, errandDeathPurpose_,
                        errandDeathEntry_, errandPurpose, errandEntry, nowErrandMs, errandDeathMs_);
                    errandDeathPurpose_ = errandPurpose;
                    errandDeathEntry_ = errandEntry;
                    errandDeathMs_ = nowErrandMs;
                    if (errandDeathCount_ >= ai::kErrandDeathsToPark)
                    {
                        errandDeathCount_ = 0; // the next death on it starts a fresh streak
                        ai::AddErrandPark(errandParks_, ai::kErrandParkSlots, errandPurpose, errandEntry, nowErrandMs);
                        sPlayerbotAIConfig.logEvent(this, "ErrandParked", errandDest->GetTitle(), WorldPosition(bot).GetAreaName());
                        TellDebug(GetMaster(), "Giving up on " + errandDest->GetTitle() + " for a while - I died on the way twice", "debug travel");
                        sTravelMgr.SetNullTravelTarget(errandTarget);
                        errandTarget->SetStatus(TravelStatus::TRAVEL_STATUS_EXPIRED);
                        errandTarget->SetExpireIn(1000);
                    }
                }
            }
            // Death-cluster escape: kDeathClusterDeaths deaths inside one small area
            // within a window mean the bot is looping on a camp it cannot survive
            // there, whatever the killer kind is. The per-kind rule above only trips
            // on two consecutive deaths to the same creature, so a camp whose mobs
            // rotate as they kill the bot slips through it (cycle-3 measurement: a
            // third of all deaths arrive within two kills of the previous death).
            // Blacklist the grind destination the bot was working - the same per-bot
            // list the unreachable give-up and the lethal-kind rule use - so its
            // travel target drops and the destination picker sends the bot elsewhere.
            if (context)
            {
                uint32 const nowClusterMs = WorldTimer::getMSTime();
                float const dxCluster = bot->GetPositionX() - deathClusterX_;
                float const dyCluster = bot->GetPositionY() - deathClusterY_;
                bool const sameSpot = deathClusterCount_ > 0 &&
                    bot->GetMapId() == deathClusterMapId_ &&
                    nowClusterMs - deathClusterMs_ <= kDeathClusterWindowMs &&
                    dxCluster * dxCluster + dyCluster * dyCluster <= kDeathClusterRadiusYd * kDeathClusterRadiusYd;

                if (sameSpot)
                    deathClusterCount_++;
                else
                {
                    deathClusterCount_ = 1;
                    deathClusterMs_ = nowClusterMs;
                    deathClusterMapId_ = bot->GetMapId();
                    deathClusterX_ = bot->GetPositionX();
                    deathClusterY_ = bot->GetPositionY();
                }

                if (deathClusterCount_ >= kDeathClusterDeaths)
                {
                    deathClusterCount_ = 0; // the next death starts a fresh cluster

                    uint32 clusterEntry = 0;
                    TravelTarget* clusterTarget = AI_VALUE(TravelTarget*, "travel target");
                    GrindTravelDestination* clusterGrind = clusterTarget
                        ? dynamic_cast<GrindTravelDestination*>(clusterTarget->GetDestination()) : nullptr;
                    if (clusterGrind)
                        clusterEntry = clusterGrind->GetEntry();
                    else if (lastKiller_.entry)
                        clusterEntry = lastKiller_.entry; // no Grind destination: leave the kind that kills here

                    if (clusterEntry)
                    {
                        context->GetValue<std::map<uint32, uint32>&>("unreachable entries")->Get()[clusterEntry] = nowClusterMs + kDeathClusterBlacklistMs;
                        sPlayerbotAIConfig.logEvent(this, "DeathClusterEscape", std::to_string(clusterEntry), WorldPosition(bot).GetAreaName());
                        TellDebug(GetMaster(), "Leaving this hunting ground for a while - it killed me " + std::to_string(kDeathClusterDeaths) + " times", "debug move");
                        // Rotating killers defeat the kind blacklist above (issue
                        // #398: median 7 killer kinds per loop bot), so the escape
                        // escalates to the spot itself at once: grind and
                        // quest-objective picks inside the camp are refused for
                        // a while and the bot walks elsewhere. Lowbies (<= 5,
                        // stuck in their starter valley) avoid a smaller camp
                        // for a shorter while; a ding clears the list. Owned
                        // bots and bots with a real player master stay out -
                        // their player decides where to hunt.
                        if (!HasRealPlayerMaster())
                        {
                            deathEscapeCount_ = ai::NextDeathEscapeCount(deathEscapeCount_, nowClusterMs, deathLastEscapeMs_);
                            deathLastEscapeMs_ = nowClusterMs;
                            if (ai::DeathAvoidanceEscalated(deathEscapeCount_))
                            {
                                uint32 const botLevel = bot->GetLevel();
                                ai::AddDeathAvoidSpot(deathAvoidSpots_, ai::kDeathAvoidSpots, bot->GetMapId(),
                                    bot->GetPositionX(), bot->GetPositionY(), nowClusterMs,
                                    ai::DeathAvoidDurationMs(botLevel), ai::DeathAvoidRadiusYd(botLevel));
                                sPlayerbotAIConfig.logEvent(this, "DeathSpotAvoided", WorldPosition(bot).GetAreaName());
                                // The bot is standing in the camp it must leave:
                                // drop the current target now so the corpse run
                                // and the next pick start from a clean slate
                                // instead of walking back to the same point.
                                // SetBestTarget's per-point filter (not the
                                // whole-destination IsActive gates) decides
                                // where the next pick lands.
                                TravelTarget* escapeTarget = AI_VALUE(TravelTarget*, "travel target");
                                if (escapeTarget && escapeTarget->GetDestination() &&
                                    typeid(*escapeTarget->GetDestination()) != typeid(NullTravelDestination))
                                {
                                    WorldPosition* escapePoint = escapeTarget->getPosition();
                                    if (escapePoint && IsDeathSpotAvoided(escapePoint->GetMapId(), escapePoint->getX(),
                                        escapePoint->getY(), nowClusterMs))
                                    {
                                        sTravelMgr.SetNullTravelTarget(escapeTarget);
                                        escapeTarget->SetStatus(TravelStatus::TRAVEL_STATUS_EXPIRED);
                                        escapeTarget->SetExpireIn(1000);
                                    }
                                }
                            }
                        }
                    }
                }
            }
            // Hostile-town death loop: a bot killed by the guards of an enemy town
            // revives at the nearest spirit healer, often just outside that town
            // (Horde at the Redridge graveyard beside Lakeshire: 25 Lakeshire Guard
            // kills in 16 minutes, one rogue 12 times), and the travel target it
            // still holds walks it straight back through the guards - the route
            // check only covers named route nodes, never the graveyard leg or a
            // short hop. Count deaths within 120 yd of hostile guards (neutral-town
            // bruisers excluded); the post-rez rescue moves the bot away at
            // kHostileTownLoopDeaths (BotManager::RelocateHopelessBot).
            if (sPlayerbotAIConfig.avoidHostileTowns && WorldPosition(bot).IsGuardedHostileTownFor(bot, 120.0f, false))
            {
                uint32 const nowTownMs = WorldTimer::getMSTime();
                if (!GetRecentHostileTownDeaths())
                    hostileTownDeathCount_ = 0;
                ++hostileTownDeathCount_;
                hostileTownDeathMs_ = nowTownMs;
            }
            // Killed by a player (entry == 0, not the environment): avoid that
            // named killer for a while so two masterless random bots grinding the
            // same spot stop trading kills every ~60 s. Same shape as the
            // lethal-kind rule above, keyed on the killer name instead of the
            // creature entry. Retaliation still works: an actual attacker stays
            // a valid target through the threat/victim path.
            if (!lastKiller_.entry && !lastKiller_.isEnvironment && !lastKiller_.name.empty())
            {
                SetAvoidPlayerKiller(lastKiller_.name);
                TellDebug(GetMaster(), "Leaving " + lastKiller_.name + " alone for a while - it killed me", "debug move");
            }

            // Determine accurate killer name & level
            std::string killerName;
            uint32 killerLevel = 0;
            if (!lastKiller_.name.empty())
            {
                killerName = lastKiller_.name;
                killerLevel = lastKiller_.level;
            }
            else
            {
                // Fallback: check attackers set
                for (Unit* attacker : bot->GetAttackers())
                {
                    if (attacker)
                    {
                        killerName = attacker->GetName();
                        killerLevel = attacker->GetLevel();
                        break;
                    }
                }
                if (killerName.empty())
                {
                    Unit* deathTarget = AI_VALUE(Unit*, "current target");
                    if (deathTarget)
                    {
                        killerName = deathTarget->GetName();
                        killerLevel = deathTarget->GetLevel();
                    }
                }
            }

            if (sObservabilityEmitter.IsEnabled())
            {
                std::ostringstream deathDetails;
                deathDetails << "Died (death #" << AI_VALUE(uint32, "death count") << ")";
                if (lastKiller_.isEnvironment)
                    deathDetails << " to Falling / Environment";
                else if (!killerName.empty())
                    deathDetails << " to " << killerName << " (" << killerLevel << ")";

                sObservabilityEmitter.EmitAnomaly("BOT_DEATH", "INFO", bot, deathDetails.str(),
                    killerName, "", "death", killerLevel);
            }

            // Death gets its own bot_events.csv row: logEvent also records the
            // position and the bot's level, info1/info2 carry the killer and its
            // level. RepopAction only says a stuck rescue ran, so a dead bot and a
            // teleported live bot are otherwise identical in the event stream and a
            // death is visible only in deaths.csv.
            sPlayerbotAIConfig.logEvent(this, "BotDeath",
                !killerName.empty() ? killerName : (lastKiller_.isEnvironment ? "Environment" : "none"),
                std::to_string(killerLevel));

            if (sPlayerbotAIConfig.hasLog("deaths.csv"))
            {
                WorldPosition botPos(bot);

                std::ostringstream out;
                out << sPlayerbotAIConfig.GetTimestampStr() << "+00,";
                out << bot->GetName() << ",";
                out << std::fixed << std::setprecision(2);

                out << std::to_string(bot->GetRace()) << ",";
                out << std::to_string(bot->GetClass()) << ",";
                float subLevel = ((float)bot->GetLevel() + ((float)bot->GetUInt32Value(PLAYER_XP) / (float)bot->GetUInt32Value(PLAYER_NEXT_LEVEL_XP)));

                out << subLevel << ",";

                botPos.printWKT(out);

                // Killer health is read at the killing blow (death hook), not
                // from "current target" here: the old read wrote 100 for 92%
                // of deaths, even for bots seen fighting ~36 s before dying
                // (Oct 2026 roster poll), so the target no longer matches by
                // the time the AI handles the death.
                float killerHealth = 100.0f;
                if (!lastKiller_.name.empty() && lastKiller_.name == killerName)
                    killerHealth = lastKiller_.healthPct;
                else
                {
                    Unit* ctarget = AI_VALUE(Unit*, "current target");
                    if (ctarget && (!killerName.empty() && ctarget->GetName() == killerName))
                        killerHealth = ctarget->GetHealthPercent();
                }

                if (!killerName.empty())
                {
                    out << "\"" << killerName << "\"," << killerLevel << "," << killerHealth << ",";
                }
                else
                    out << "\"none\",0,100,";

                // Multi-attacker count from the live snapshot (NoteFightAttackers):
                // the old victim-filtered "all targets" loop was structurally
                // always 0 here - SetDeathState drains the attacker set and
                // clears victim pointers before OnDeath runs. A stale (>30 s)
                // or empty snapshot writes 0, same as before.
                std::string addsText;
                uint32 adds = 0;
                if (ai::IsDeathAttackerSnapshotFresh(WorldTimer::getMSTime(), deathAttackersMs_))
                {
                    auto formatted = ai::FormatDeathAttackers(deathAttackers_, killerName);
                    addsText = formatted.first;
                    adds = formatted.second;
                }
                out << "\"" << addsText << "\"," << std::to_string(adds);

                TravelTarget* travelTarget = AI_VALUE(TravelTarget*, "travel target");
                std::string travelInfo = "none";
                if (travelTarget && travelTarget->GetDestination())
                    travelInfo = travelTarget->GetDestination()->GetTitle();
                out << ",\"" << travelInfo << "\"";

                sPlayerbotAIConfig.log("deaths.csv", out.str().c_str());
            }
        }

        ClearLastKiller();
        deathAttackers_.clear();
        deathAttackersMs_ = 0;

        SET_AI_VALUE(Unit*, "current target", nullptr);
        SET_AI_VALUE(Unit*, "enemy player target", nullptr);
        SET_AI_VALUE(Unit*, "pull target", nullptr);
        SET_AI_VALUE(ObjectGuid, "attack target", ObjectGuid());
        SET_AI_VALUE(ObjectGuid, "explicit attack target", ObjectGuid());
        SET_AI_VALUE(LootObject, "loot target", LootObject());
        SET_AI_VALUE(time_t, "combat start time", 0);
        SET_AI_VALUE2(bool, "manual bool", "enemies near corpse", false);
        SET_AI_VALUE2(bool, "manual bool", "enemies near graveyard", false);
        // A corpse cannot hold an anchor, and the dead engine is no place to
        // keep one: the tank would come back from the spirit healer still
        // parked on the stay that pull placed it on, instead of following.
        ReleasePullHold();
        ChangeEngine(BotState::BOT_STATE_DEAD);
    }
}

void PlayerbotAI::OnResurrected()
{
    if (sServerFacade.IsAlive(bot))
    {
        deathHandled_ = false; // alive again: the next death is a new one
        // 60 s full-rate scan grace after revive (spawn-camp guard): the revive
        // lands at 50% HP on the death spot and re-dies fast live, so the first
        // minute runs the full scan. Teleport grace stays 15 s (HandleTeleportAck).
        m_reviveGraceUntilMs = WorldTimer::getMSTime() + PlayerbotAI::kReviveGraceMs;
    }

    if (IsStateActive(BotState::BOT_STATE_DEAD) && sServerFacade.IsAlive(bot))
    {
        // Stop following on resurrected
        if ((HasStrategy("follow", BotState::BOT_STATE_COMBAT) || HasStrategy("wander", BotState::BOT_STATE_COMBAT)) &&
            !(HasStrategy("follow", BotState::BOT_STATE_NON_COMBAT) || HasStrategy("wander", BotState::BOT_STATE_NON_COMBAT)))
        {
            StopMoving();
        }

        ClearLastKiller();
        // The corpse run is over whichever path revived the bot: without this,
        // a revive that bypasses ReviveFromCorpseAction (combat res, GM command)
        // leaves "corpse run" set and the next death skips the wait-for-master
        // gate via the manual override.
        if (aiObjectContext)
            aiObjectContext->GetValue<bool>("corpse run")->Set(false);
        ChangeEngine(BotState::BOT_STATE_NON_COMBAT);
    }
}

void PlayerbotAI::HandleCommands()
{
    ExternalEventHelper helper(aiObjectContext);
    std::list<ChatCommandHolder> delayed;
    while (!chatCommands.empty())
    {
        ChatCommandHolder holder = chatCommands.front();
        time_t checkTime = holder.GetTime();
        if (checkTime && time(0) < checkTime)
        {
            delayed.push_back(holder);
            chatCommands.pop();
            continue;
        }

        std::string command = holder.GetCommand();
        Player* owner = holder.GetOwner();
        if (!helper.ParseChatCommand(command, owner) && holder.GetType() == CHAT_MSG_WHISPER)
        {
            //ostringstream out; out << "Unknown command " << command;
            //TellPlayer(out);
            //helper.ParseChatCommand("help");
        }

        chatCommands.pop();
    }

    for (std::list<ChatCommandHolder>::iterator i = delayed.begin(); i != delayed.end(); ++i)
    {
        chatCommands.push(*i);
    }
}

void PlayerbotAI::UpdateAIInternal(uint32 elapsed, bool minimal)
{
    SC_PHASE("UpdateAIInternal.entry", bot ? bot->GetName() : "(null)");
    if (bot->IsBeingTeleported() || !bot->IsInWorld())
        return;

    // Observation only (see BotDiagnostics.h): no action, value or movement is touched.
    UpdateEvadeProbe();
    // Self-heal for the "alive but engine DEAD" mismatch: a bot the core reports
    // alive while the engine still sits in DEAD runs only the dead strategy, so it
    // never travels, never grinds, and reads dead on the dashboard for minutes.
    // The normal flip lives in OnResurrected (engine flip + 60 s revive grace +
    // follow/stay stop), which the reaction engine fires every tick via the
    // "resurrect" trigger - but the flip is missed when the alive transition
    // lands outside the resurrect path (teleport while dead, instance-entrance
    // revive, core/AI ordering). If the core has reported alive for a full 5 s
    // window and the engine is still DEAD, call OnResurrected() directly - same
    // path as the trigger, not a parallel flip, so grace and the follow/stay
    // stop apply too.
    if (sServerFacade.IsAlive(bot) && IsStateActive(BotState::BOT_STATE_DEAD))
    {
        uint32 nowMs = WorldTimer::getMSTime();
        if (!m_aliveWhileDeadSinceMs)
            m_aliveWhileDeadSinceMs = nowMs;
        else if (WorldTimer::getMSTimeDiff(m_aliveWhileDeadSinceMs, nowMs) >= 5000)
        {
            sLog.outDetail("[BOT CORPSE] %s: alive for 5s+ while engine DEAD, running OnResurrected", bot->GetName());
            m_aliveWhileDeadSinceMs = 0;
            OnResurrected();
        }
    }
    else
        m_aliveWhileDeadSinceMs = 0;

    std::unique_ptr<PerformanceMonitorOperation> pmo;
    if (sPlayerbotAIConfig.perfMonEnabled)
    {
        std::string mapString = WorldPosition(bot).isInstance() ? "I" : std::to_string(bot->GetMapId());
        pmo = sPerformanceMonitor.start(PERF_MON_TOTAL, "PlayerbotAI::UpdateAIInternal " + mapString, nullptr, bot->GetMapId(), bot->GetInstanceId());
    }

    ExternalEventHelper helper(aiObjectContext);

    // chat replies
    std::list<ChatQueuedReply> delayedResponses;
    {
        std::scoped_lock lock(chatRepliesMutex);
        while (!chatReplies.empty())
        {
            ChatQueuedReply holder = chatReplies.front();
            time_t checkTime = holder.m_time;
            if (checkTime && time(0) < checkTime)
            {
                delayedResponses.push_back(holder);
                chatReplies.pop();
                continue;
            }
            ChatReplyAction::ChatReplyDo(bot, holder.m_type, holder.m_guid1, holder.m_guid2, holder.m_msg, holder.m_chanName, holder.m_name);
            chatReplies.pop();
        }

        for (std::list<ChatQueuedReply>::iterator i = delayedResponses.begin(); i != delayedResponses.end(); ++i)
        {
            chatReplies.push(*i);
        }
    }
    // logout if logout timer is ready or if instant logout is possible
    if (bot->GetSession()->isLogingOut())
    {
        WorldSession* botWorldSessionPtr = bot->GetSession();
        bool logout = botWorldSessionPtr->ShouldLogOut(time(nullptr));
        if (!master || master->GetSession()->PlayerLoading())
            logout = true;

        if (bot->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_RESTING) || bot->IsTaxiFlying() ||
            botWorldSessionPtr->GetSecurity() >= (AccountTypes)sWorld.getConfig(CONFIG_UINT32_INSTANT_LOGOUT))
        {
            logout = true;
        }

        if (master && (master->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_RESTING) || master->IsTaxiFlying() ||
            (master->GetSession() && master->GetSession()->GetSecurity() >= (AccountTypes)sWorld.getConfig(CONFIG_UINT32_INSTANT_LOGOUT))))
        {
            logout = true;
        }

        if (logout && !bot->GetSession()->ShouldLogOut(time(nullptr)))
        {
            TortoiseBots::BotManager::Instance().RemoveBot(bot->GetObjectGuid(), true);
            return;
        }

        SetAIInternalUpdateDelay(sPlayerbotAIConfig.reactDelay);
        return;
    }

    SC_PHASE("UpdateAIInternal.botOutgoingPackets", bot ? bot->GetName() : "(null)");
    botOutgoingPacketHandlers.Handle(helper);
    SC_PHASE("UpdateAIInternal.masterIncomingPackets", bot ? bot->GetName() : "(null)");
    masterIncomingPacketHandlers.Handle(helper);
    SC_PHASE("UpdateAIInternal.masterOutgoingPackets", bot ? bot->GetName() : "(null)");
    masterOutgoingPacketHandlers.Handle(helper);

    // A bot that joined a commanded summoning ritual stays a participant only while its helper channel
    // runs, and any cast, drink or step of its own ends it: its engine waits until the order lets it go.
    if (TortoiseBots::RemoteCommander::RitualHolds().Holds(bot->GetObjectGuid().GetRawValue()))
    {
        SetAIInternalUpdateDelay(sPlayerbotAIConfig.reactDelay);
        return;
    }

    SC_PHASE("UpdateAIInternal.DoNextAction", bot ? bot->GetName() : "(null)");
	DoNextAction(minimal);
    SC_PHASE("UpdateAIInternal.exit", bot ? bot->GetName() : "(null)");
}

void PlayerbotAI::HandleTeleportAck()
{
    // Issue #84 (P2): the single choke point every BotManager-driven ack
    // flows through (UpdateBots skips AI updates this tick). Engines compare
    // this counter to drain stale queues on arrival, however short the hop.
    ++transitionGeneration;
    // Phase 2: 15s full-rate scan grace after any teleport (spawn-camp guard).
    m_teleportGraceUntilMs = WorldTimer::getMSTime() + PlayerbotAI::kReviveTeleportGraceMs;
    if (IsRealPlayer() && bot->IsBeingTeleportedFar())
        return;

    StopMoving();

	if (bot->IsBeingTeleportedNear())
	{
        if (!bot->GetMotionMaster()->empty())
            if (MovementGenerator* movgen = bot->GetMotionMaster()->top())
                movgen->Interrupt(*bot);

       /* WorldLocation dest = bot->GetTeleportDest();
        bot->Relocate(dest.x, dest.y, dest.z, dest.orientation);*/

		WorldPacket p = WorldPacket(MSG_MOVE_TELEPORT_ACK, 8 + 4 + 4);
        p << bot->getObjectGuid();
		p << bot->GetLastCounterForMovementChangeType(TELEPORT); // movement counter the core is waiting to ACK
		p << (uint32) time(0); // time - not currently used
        bot->GetSession()->HandleMoveTeleportAckOpcode(p);

        // add delay to simulate teleport delay
        SetAIInternalUpdateDelay(urand(1000, 2000));
	}
	else if (bot->IsBeingTeleportedFar())
	{
        bot->GetSession()->HandleMoveWorldportAckOpcode();

        // add delay to simulate teleport delay
        SetAIInternalUpdateDelay(urand(2000, 5000));
	}

    if (IsRealPlayer())
        bot->SendHeartBeat();

    Reset();
}

void PlayerbotAI::Reset(bool full)
{
    AiObjectContext* context = aiObjectContext;

    if (bot->IsTaxiFlying())
        return;

    // A stale MOVEFLAG_ONTRANSPORT (flag set, no transport) freezes all later
    // bot movement: core Transport::RemovePassenger unregisters the passenger
    // but leaves the move flag behind (issue #216). Drop stale state here so
    // reset/unstuck can always break the lock. Never touches a bot that is
    // legitimately riding a transport.
    if (!bot->GetTransport() && bot->m_movementInfo.HasMovementFlag(MOVEFLAG_ONTRANSPORT))
    {
        bot->m_movementInfo.RemoveMovementFlag(MOVEFLAG_ONTRANSPORT);
        bot->m_movementInfo.ClearTransportData();
    }

    if (!HasActivePlayerMaster() && currentEngine == engines[(uint8)BotState::BOT_STATE_COMBAT] && sServerFacade.IsInCombat(bot) && time(0) - AI_VALUE(time_t,"combat start time") > 5 * MINUTE)
    {
        bot->CombatStop();
    }

    // A dead bot stays in the dead engine: the spirit healer's graveyard teleport ends in
    // HandleTeleportAck -> Reset(), and a ghost dropped into non-combat travelled and
    // grinded as a ghost instead of going for its corpse, for hours.
    BotState const resetState = sServerFacade.IsAlive(bot) ? BotState::BOT_STATE_NON_COMBAT : BotState::BOT_STATE_DEAD;
    currentEngine = engines[(uint8)resetState];
    currentState = resetState;
    ResetAIInternalUpdateDelay();
    reactionEngine->Reset();
    whispers.clear();

    PullStrategy* strategy = PullStrategy::Get(this);
    if (strategy)
        strategy->OnPullEnded();

    RESET_AI_VALUE(Unit*,"old target");
    RESET_AI_VALUE(Unit*,"current target");
    RESET_AI_VALUE(Unit*,"pull target");
    RESET_AI_VALUE(ObjectGuid,"attack target");
    RESET_AI_VALUE(ObjectGuid,"explicit attack target");
    RESET_AI_VALUE(GuidPosition,"rpg target");
    RESET_AI_VALUE(LootObject,"loot target");
    RESET_AI_VALUE(time_t,"combat start time");
    bot->SetSelectionGuid(ObjectGuid());

    LastMovement& movement = AI_VALUE(LastMovement&, "last movement");
    movement.fleeFailures.Clear();
    movement.spreadFailures.Clear();

    LastSpellCast & lastSpell = AI_VALUE(LastSpellCast&,"last spell cast");
    lastSpell.Reset();

    if (bot->GetTradeData())
        bot->TradeCancel(true);

    if (full)
    {
        RESET_AI_VALUE(LastMovement&,"last movement");
        RESET_AI_VALUE(LastMovement&,"last area trigger");
        RESET_AI_VALUE(LastMovement&,"last taxi");

        TravelTarget* target = AI_VALUE(TravelTarget*, "travel target");
        sTravelMgr.SetNullTravelTarget(target);
        target->SetStatus(TravelStatus::TRAVEL_STATUS_EXPIRED);
        target->SetExpireIn(1000);

        *AI_VALUE(FutureDestinations*, "future travel destinations") = FutureDestinations();
        RESET_AI_VALUE2(std::string, "manual string", "future travel purpose");
        RESET_AI_VALUE2(int, "manual int", "future travel relevance");

        InterruptSpell();

        StopMoving();

        jumpTime = 0;
        fallAfterJump = false;
        ResetJumpDestination();

        WorldSession* botWorldSessionPtr = bot->GetSession();
        bool logout = botWorldSessionPtr->ShouldLogOut(time(nullptr));

        // cancel logout
        if (!logout && bot->GetSession()->isLogingOut())
        {
            WorldPacket p;
            bot->GetSession()->HandleLogoutCancelOpcode(p);
            TellPlayer(GetMaster(), BOT_TEXT("logout_cancel"));
        }
    }

    AI_VALUE(std::set<ObjectGuid>&,"ignore rpg target").clear();

    if (bot->IsTaxiFlying())
    {
#ifdef MANGOS
        bot->GetTaxi().ClearTaxiDestinations();
#endif
        bot->StopMoving();    }

    if (full)
    {
        for (uint8 i = 0; i < (uint8)BotState::BOT_STATE_ALL; i++)
        {
            engines[i]->Init();
        }
    }
}

std::map<std::string,ChatMsg> chatMap;

bool PlayerbotAI::IsAllowedCommand(std::string text)
{
    if (unsecuredCommands.empty())
    {
        unsecuredCommands.insert("who");
        unsecuredCommands.insert("where");
        unsecuredCommands.insert("wts");
        unsecuredCommands.insert("sendmail");
        unsecuredCommands.insert("invite");
        unsecuredCommands.insert("leave");
        unsecuredCommands.insert("join");
        unsecuredCommands.insert("lfg");
        unsecuredCommands.insert("guild invite");
        unsecuredCommands.insert("guild leave");
    }

    for (std::set<std::string>::iterator i = unsecuredCommands.begin(); i != unsecuredCommands.end(); ++i)
    {
        if (text.find(*i) == 0)
        {
            return true;
        }
    }

    return false;
}

// Issue #294: tiered bot command authorization. Single choke point keyed by
// the command's first word. GM = sender session security > SEC_PLAYER (this
// core has no separate GM rank: SEC_GAMEMASTER aliases SEC_DEVELOPER,
// so `> SEC_PLAYER` is the documented "any elevated account" check). Owner =
// sender account == the bot's durable owner account (BotManager ownership
// record, falling back to the character account for pre-ownership records).
// Random bots grouped with a player are group members, never owners.
namespace
{
std::string BotCommandFirstWordLower(std::string const& command)
{
    size_t begin = command.find_first_not_of(" \t");
    if (begin == std::string::npos)
        return {};
    size_t end = command.find_first_of(" \t", begin);
    std::string word = command.substr(begin, end == std::string::npos ? std::string::npos : end - begin);
    for (char& c : word)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return word;
}
}
PlayerbotAI::BotCommandAuthTier PlayerbotAI::AuthTierForCommand(std::string const& command) const
{
    size_t begin = command.find_first_not_of(" \t");
    if (begin == std::string::npos)
        return BotCommandAuthTier::Tactical;
    std::string lower = command.substr(begin);
    for (char& c : lower)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    size_t end = lower.find_first_of(" \t");
    std::string word = lower.substr(0, end);
    // GM-only: admin/debug surface. The bot's owner is deliberately not
    // included (issue #294 owner decision).
    if (word == "cheat" || word == "debug" || word == "cdebug" || word == "cs" ||
        word == "log" || word == "teleport" ||
        (word == "set" && lower.compare(0, 9, "set value") == 0) ||
        ((word == "load" || word == "save" || word == "list" || word == "reset") &&
            (lower.find(" ai") != std::string::npos || lower.find(" strat") != std::string::npos ||
                lower.find(" value") != std::string::npos)))
        return BotCommandAuthTier::GmOnly;
    // Owner-or-GM: account/inventory affecting commands and durable AI
    // configuration (mail, bank, AH, trade, guild, craft, strategy edits).
    // Listed explicitly so the mapping is auditable; anything unlisted
    // below falls through to OwnerOrGm (fail closed).
    if (word == "sendmail" || word == "mail" ||
        word == "bank" || word == "gb" || word == "gbank" ||
        word == "ah" ||
        word == "t" || word == "trade" || word == "nt" ||
        word == "s" || word == "sell" || word == "b" || word == "buy" || word == "bb" ||
        word == "repair" || word == "destroy" || word == "drop" ||
        word == "e" || word == "equip" || word == "ue" || word == "unequip" || word == "keep" ||
        word == "u" || word == "use" ||
        word == "craft" ||
        word == "guild" || word == "gi" || word == "gk" || word == "gl" || word == "gp" ||
        word == "ginvite" || word == "gremove" || word == "gleave" || word == "gleader" ||
        word == "gpromote" || word == "gdemote" ||
        word == "invite" || word == "join" || word == "lfg" || word == "leave" ||
        word == "summon" || word == "taxi" ||
        word == "co" || word == "nc" || word == "de" || word == "react" || word == "all" ||
        word == "talents" || word == "reset" ||
        word == "release" || word == "revive" || word == "corpse" || word == "trainer" ||
        word == "skill" || word == "faction" || word == "outfit" ||
        word == "go" || word == "range" || word == "flag" || word == "speak" ||
        word == "cast" || word == "castnc" || word == "spell" ||
        word == "pet" || word == "buff" || word == "share" || word == "accept" || word == "talk" ||
        word == "roll" || word == "ll" || word == "ss" || word == "chat" || word == "home" ||
        word == "logout" || word == "r" || word == "reward" ||
        word == "d" || word == "do" || word == "doquest" || word == "grind" ||
        word == "bg" || word == "move" || word == "focus" || word == "boost" ||
        word == "self" || word == "give" || word == "ra")
        return BotCommandAuthTier::OwnerOrGm;
    // Tactical/info surface (group members): combat movement and read-only
    // queries that never move money/items or rewrite AI configuration.
    // `hire` stays tactical: HireAction self-gates to random bots (level and
    // cost checks) and hiring is how a stranger becomes an owner.
    if (word == "follow" || word == "stay" || word == "guard" || word == "free" ||
        word == "wander" || word == "flee" || word == "runaway" || word == "grind" ||
        word == "attack" || word == "pull" || word == "tank" || word == "rti" ||
        word == "formation" || word == "stance" || word == "save" || word == "max" ||
        word == "possible" || word == "attackers" || word == "position" ||
        word == "who" || word == "where" || word == "wts" || word == "stats" ||
        word == "c" || word == "items" || word == "inv" || word == "inventory" ||
        word == "spells" || word == "hire" ||
        word == "emote" || word == "help" || word == "warning" || word == "ready" || word == "rebuff" ||
        word == "queue" || word == "los" ||
        word == "wait" || word == "jump")
        return BotCommandAuthTier::Tactical;
    // Unknown commands default to owner-or-GM so new debug/economy surface
    // fails closed.
    return BotCommandAuthTier::OwnerOrGm;
}
bool PlayerbotAI::IsBotOwnerOrGm(Player const& fromPlayer) const
{
    WorldSession const* session = fromPlayer.GetSession();
    if (!session)
        return false;
    if (session->GetSecurity() > SEC_PLAYER)
        return true;
    uint32 senderAccount = session->GetAccountId();
    if (!senderAccount)
        return false;
    TortoiseBots::BotManager& manager = TortoiseBots::BotManager::Instance();
    if (TortoiseBots::BotRecord* record = manager.FindBot(bot->GetObjectGuid()))
    {
        uint32 ownerAccount = record->ownerAccountId ? record->ownerAccountId : record->accountId;
        if (ownerAccount != 0 && ownerAccount == senderAccount)
            return true;
        // Issue #872: a stranger's group binds them as the alt-bot's master
        // (AcceptInvitationAction), but only the owner's account owns it.
        if (record->masterGuid && record->masterGuid == fromPlayer.GetObjectGuid() &&
            !TortoiseBots::AltBotService::Instance().IsAutonomous(bot->GetObjectGuid()))
            return true;
    }
    TortoiseBots::OwnedCharacter owned;
    if (manager.GetOwnedCharacter(bot->GetObjectGuid(), owned) &&
        owned.ownerAccountId != 0 && owned.ownerAccountId == senderAccount)
        return true;
    // Pre-ownership records (same character account as the bot itself).
    return sObjectMgr.GetPlayerAccountIdByGUID(bot->getObjectGuid()) == senderAccount;
}
bool PlayerbotAI::CheckBotCommandAuth(Player& fromPlayer, std::string const& command, bool silentDeny)
{
    BotCommandAuthTier tier = AuthTierForCommand(command);
    if (tier == BotCommandAuthTier::Tactical)
        return true;
    WorldSession const* session = fromPlayer.GetSession();
    bool isGm = session && session->GetSecurity() > SEC_PLAYER;
    if (tier == BotCommandAuthTier::GmOnly)
    {
        if (isGm)
            return true;
        if (!silentDeny)
            TellPlayer(&fromPlayer, "That command is restricted to GameMasters.");
        return false;
    }
    if (IsBotOwnerOrGm(fromPlayer))
        return true;
    // Issue #872: an autonomous alt-bot in another player's group takes
    // combat orders (follow, attack, ...) from strangers, but only the
    // owner's account (or a GM) may touch anything outside combat. The
    // master binding alone must not grant it: joining the stranger's group
    // is what put the stranger's guid in the record.
    if (TortoiseBots::AltBotService::Instance().IsAutonomous(bot->GetObjectGuid()) &&
        TortoiseBots::CheckAltBotStrangerCommand(command) == TortoiseBots::AltBotStrangerVerdict::Allowed)
        return true;
    if (!silentDeny)
        TellPlayer(&fromPlayer, "Only this bot's owner (or a GameMaster) may do that.");
    return false;
}

void PlayerbotAI::HandleCommand(uint32 type, const std::string& text, Player& fromPlayer, const uint32 lang)
{
    std::string filtered = text;
    StripGmBadgeColor(filtered);

    if (!IsAllowedCommand(filtered) && !GetSecurity()->CheckLevelFor(PlayerbotSecurityLevel::PLAYERBOT_SECURITY_INVITE, type != CHAT_MSG_WHISPER, &fromPlayer))
        return;

    if (type == CHAT_MSG_ADDON)
        return;

    if (filtered.find("BOT\t") == 0) //Mangosbot has BOT prefix so we remove that.
        filtered = filtered.substr(4);
    else if (lang == LANG_ADDON) //Other addon messages should not command bots.
        return;

    if (type == CHAT_MSG_SYSTEM)
        return;

    if (filtered.find(sPlayerbotAIConfig.commandSeparator) != std::string::npos)
    {
        std::vector<std::string> commands;
        split(commands, filtered, sPlayerbotAIConfig.commandSeparator.c_str());
        for (std::vector<std::string>::iterator i = commands.begin(); i != commands.end(); ++i)
        {
            HandleCommand(type, *i, fromPlayer);
        }
        return;
    }

    if (!sPlayerbotAIConfig.commandPrefix.empty())
    {
        if (filtered.find(sPlayerbotAIConfig.commandPrefix) != 0)
            return;

        filtered = filtered.substr(sPlayerbotAIConfig.commandPrefix.size());
    }

    if (chatMap.empty())
    {
        chatMap["#w "] = CHAT_MSG_WHISPER;
        chatMap["#p "] = CHAT_MSG_PARTY;
        chatMap["#r "] = CHAT_MSG_RAID;
        chatMap["#a "] = CHAT_MSG_ADDON;
        chatMap["#g "] = CHAT_MSG_GUILD;
    }
    currentChat = std::pair<ChatMsg, time_t>(CHAT_MSG_WHISPER, 0);
    for (std::map<std::string,ChatMsg>::iterator i = chatMap.begin(); i != chatMap.end(); ++i)
    {
        if (filtered.find(i->first) == 0)
        {
            filtered = filtered.substr(3);
            currentChat = std::pair<ChatMsg, time_t>(i->second, time(0) + 3);
            break;
        }
    }

    filtered = chatFilter.Filter(trim((std::string&)filtered));
    if (filtered.empty())
        return;

    if (filtered.substr(0, 6) == "debug " && &fromPlayer != bot)
    {
        // `debug <remote>` diagnostics stay GM-only; refusals keep the exact
        // addon packet shape so TBM parsers never see a second line.
        WorldSession const* session = fromPlayer.GetSession();
        if (!session || session->GetSecurity() <= SEC_PLAYER)
        {
            fromPlayer.SendAddonMessage("TBM", "ACTION_ERR|debug|denied|GameMasters only.");
            TellPlayer(&fromPlayer, "That command is restricted to GameMasters.");
            return;
        }
        std::string response = HandleRemoteCommand(filtered.substr(6));
        WorldPacket data;
        ChatHandler::BuildChatPacket(data, CHAT_MSG_ADDON, response.c_str(), LANG_ADDON,
                CHAT_TAG_NONE, bot->getObjectGuid(), bot->GetName());
        sServerFacade.SendPacket(&fromPlayer, data);
        return;
    }
    if (filtered.substr(0, 6) == "debug ")
    {
        std::string response = HandleRemoteCommand(filtered.substr(6));
        WorldPacket data;
        ChatHandler::BuildChatPacket(data, CHAT_MSG_ADDON, response.c_str(), LANG_ADDON,
                CHAT_TAG_NONE, bot->getObjectGuid(), bot->GetName());
        sServerFacade.SendPacket(&fromPlayer, data);
        return;
    }
    // Issue #294 single choke-point gate: tiered authorization for every
    // queued/action command from a real player. Internal self-commands and
    // commands from other bots (headless sessions, e.g. a bot group leader
    // ordering its members) bypass it; the existing security level below
    // still applies to them.
    WorldSession const* fromSession = fromPlayer.GetSession();
    if (&fromPlayer != bot && fromSession && !fromSession->IsHeadless())
    {
        bool silentDeny = type != CHAT_MSG_WHISPER;
        if (!CheckBotCommandAuth(fromPlayer, filtered, silentDeny))
        {
            if (!silentDeny)
                fromPlayer.SendAddonMessage("TBM", std::string("ACTION_ERR|") +
                    BotCommandFirstWordLower(filtered) + "|denied|Not authorized for this bot command.");
            return;
        }
        // A player's order to a bot of a party a run leader drives: the commander's whisper pauses the run;
        // party chat is chatting (dropped as an order while the run runs); another player's whisper is refused.
        if (!TortoiseBots::BotCommands::NoteHumanChatOrder(bot, fromPlayer, filtered, type == CHAT_MSG_WHISPER))
            return;
    }
    if (!IsAllowedCommand(filtered) && !GetSecurity()->CheckLevelFor(PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, type != CHAT_MSG_WHISPER, &fromPlayer))
        return;
    if (type == CHAT_MSG_RAID_WARNING && filtered.find(bot->GetName()) != std::string::npos && filtered.find("award") == std::string::npos)
    {
        ChatCommandHolder cmd("warning", &fromPlayer, type);
        chatCommands.push(cmd);
        return;
    }

    if ((filtered.size() > 2 && filtered.substr(0, 2) == "d ") || (filtered.size() > 3 && filtered.substr(0, 3) == "do "))
    {
        Event event("do", "", &fromPlayer);
        std::string action = filtered.substr(filtered.find(" ") + 1);
        DoSpecificAction(action, event);
    }
    if (ChatHelper::parseValue("command", filtered).substr(0, 3) == "do ")
    {
        Event event("do", "", &fromPlayer);
        std::string action = ChatHelper::parseValue("command", filtered);
        action = action.substr(3);
        DoSpecificAction(action, event);
    }
    else if (type != CHAT_MSG_WHISPER && filtered.size() > 6 && filtered.substr(0, 6) == "queue ")
    {
        std::string remaining = filtered.substr(filtered.find(" ") + 1);
        int index = 1;
        Group* group = bot->GetGroup();
        if (group)
        {
            for (Player* member : LiveGroupMembers(group))
            {
                if (member == master)
                    continue;

                if (member == bot)
                    break;

                index++;
            }
        }
        ChatCommandHolder cmd(remaining, &fromPlayer, type, time(0) + index);
        chatCommands.push(cmd);
    }
    else if (filtered == "reset")
    {
        Reset(true);
    }
    else if (filtered == "logout")
    {
        if (!bot->GetSession()->isLogingOut())
        {
            if (type == CHAT_MSG_WHISPER)
                TellPlayer(&fromPlayer, BOT_TEXT("logout_start"));

            if (master && TortoiseBots::BotManager::Instance().IsBot(bot->GetObjectGuid()))
                SetShouldLogOut(true);
        }
    }
    else if (filtered == "logout cancel")
    {
        if (bot->GetSession()->isLogingOut())
        {
            if (type == CHAT_MSG_WHISPER)
                TellPlayer(&fromPlayer, BOT_TEXT("logout_cancel"));

            WorldPacket p;
            bot->GetSession()->HandleLogoutCancelOpcode(p);
            SetShouldLogOut(false);
        }
    }
    else if ((filtered.size() > 5) && (filtered.substr(0, 5) == "wait ") && (filtered.find("wait for attack") == std::string::npos))
    {
        std::string remaining = filtered.substr(filtered.find(" ") + 1);
        uint32 delay = atof(remaining.c_str()) * static_cast<uint32>(IN_MILLISECONDS);
        if (delay > 20000)
        {
            TellPlayer(&fromPlayer, "Max wait time is 20 seconds!");
            return;
        }

        IncreaseAIInternalUpdateDelay(delay);
        isWaiting = true;
        TellPlayer(&fromPlayer, "Waiting for " + remaining + " seconds!");
        return;
    }
    else
    {
        SC_LOG("chat-cmd queued bot=%s from=%s type=%u text='%s'",
               bot ? bot->GetName() : "(null)",
               fromPlayer.GetName(),
               (unsigned)type,
               filtered.c_str());
        ChatCommandHolder cmd(filtered, &fromPlayer, type);
        chatCommands.push(cmd);
    }
}

void PlayerbotAI::PublishChatGate()
{
    // Issue #846: the two early exits of the SMSG_MESSAGECHAT case below that
    // the packet hook can apply before copying a line (runtime/BotChatGate.h).
    // Same conditions, in the same terms: AllowActivity(), and the #469 Trade
    // gate for a masterless, unhired pool bot (recording sees Trade lines
    // before that gate, so a recording bot keeps them). Republished on change,
    // and every 30 s so a re-registered AI cannot keep the storage default.
    bool const ignoreAll = !AllowActivity();
    bool const ignoreTrade = !m_recordIncommingMessages && !HasRealPlayerMaster() &&
        sRandomBotFacade.IsFreeBot(bot) &&
        !TortoiseBots::HireLifecycle::Instance().IsHired(bot->GetObjectGuid());
    time_t const now = time(nullptr);
    if (m_chatGatePublished && ignoreAll == m_chatGateIgnoreAll && ignoreTrade == m_chatGateIgnoreTrade &&
        now < m_chatGatePublishedAt + 30)
        return;

    TortoiseBots::BotChatGate gate;
    gate.ignoreAllChat = ignoreAll;
    gate.ignoreUnaddressedTrade = ignoreTrade;
    gate.name = bot->GetName();
    PlayerbotAIStorage::Instance().SetBotChatGate(bot, gate);
    m_chatGatePublished = true;
    m_chatGateIgnoreAll = ignoreAll;
    m_chatGateIgnoreTrade = ignoreTrade;
    m_chatGatePublishedAt = now;
}

void PlayerbotAI::SpellInterrupted(uint32 spellid)
{
    // Run trace (opt-in): the bot's own cast was cancelled.
    if (TortoiseBots::RunTraceService::Instance().Active())
        TortoiseBots::RunTraceService::Instance().NoteSpellInterrupted(this, spellid);

    LastSpellCast& lastSpell = aiObjectContext->GetValue<LastSpellCast&>("last spell cast")->Get();
    if (!spellid || lastSpell.id != spellid)
        return;

    time_t now = time(0);
    if (now <= lastSpell.time)
        return;

    uint32 castTimeSpent = 1000 * (now - lastSpell.time);
    uint32 globalCooldown = CalculateGlobalCooldown(lastSpell.id);
    if (castTimeSpent < globalCooldown)
        SetAIInternalUpdateDelay(globalCooldown - castTimeSpent);
    else
        SetAIInternalUpdateDelay(sPlayerbotAIConfig.reactDelay);

    lastSpell.id = 0;
}

void PlayerbotAI::NoteSpellGlobalCooldown(SpellEntry const* spellInfo)
{
    // Mirrors Spell::TriggerGlobalCooldown for category 133
    // (SPELLCATEGORY_GLOBAL): a 1.0-1.5 s GCD scales with cast speed and
    // stays inside that range.
    if (!spellInfo || spellInfo->StartRecoveryCategory != SPELLCATEGORY_GLOBAL || !spellInfo->StartRecoveryTime)
        return;
    int32 gcd = spellInfo->StartRecoveryTime;
    if (gcd >= 1000 && gcd <= 1500)
        gcd = std::min(1500, std::max(1000, int32(float(gcd) * bot->GetFloatValue(UNIT_MOD_CAST_SPEED))));
    spellGcdStartMs_ = WorldTimer::getMSTime();
    spellGcdDurationMs_ = uint32(gcd);
}

uint32 PlayerbotAI::SpellGlobalCooldownRemainingMs() const
{
    if (!spellGcdDurationMs_)
        return 0;
    return CooldownRemainingMs(spellGcdStartMs_, spellGcdDurationMs_, WorldTimer::getMSTime());
}

int32 PlayerbotAI::CalculateGlobalCooldown(uint32 spellid)
{
    if (!spellid)
        return 0;

    int32 globalCooldown = 0;
    SpellEntry const* spellEntry = sSpellTemplate.LookupEntry<SpellEntry>(spellid);
    if (spellEntry)
    {
        globalCooldown = spellEntry->StartRecoveryTime;
    }

    return globalCooldown > 0 ? globalCooldown : sPlayerbotAIConfig.reactDelay;
}

void PlayerbotAI::ChangeEngine(BotState type)
{
    Engine* engine = engines[(uint8)type];

    if (currentEngine != engine)
    {
        currentEngine = engine;
        currentState = type;
        ReInitCurrentEngine();

        switch (type)
        {
        case BotState::BOT_STATE_COMBAT:
            sLog.outDebug( "=== %s COMBAT ===", bot->GetName());
            break;
        case BotState::BOT_STATE_NON_COMBAT:
            sLog.outDebug( "=== %s NON-COMBAT ===", bot->GetName());
            break;
        case BotState::BOT_STATE_DEAD:
            sLog.outDebug( "=== %s DEAD ===", bot->GetName());
            break;
        case BotState::BOT_STATE_REACTION:
            sLog.outDebug("=== %s REACTION ===", bot->GetName());
            break;
        default: break;
        }
    }
}


void PlayerbotAI::DoNextAction(bool min, bool forceActivity)
{
    SC_PHASE("DoNextAction.entry", bot ? bot->GetName() : "(null)");
    const bool previousActivityOverride = explicitActivityOverride;
    explicitActivityOverride = previousActivityOverride || forceActivity;

    if (!bot->IsInWorld() || bot->IsBeingTeleported() || (GetMaster() && GetMaster()->IsBeingTeleported()))
    {
        LastMovement& movement = aiObjectContext->GetValue<LastMovement&>("last movement")->Get();
        movement.fleeFailures.Clear();
        movement.spreadFailures.Clear();
        explicitActivityOverride = previousActivityOverride;
        SetAIInternalUpdateDelay(sPlayerbotAIConfig.globalCoolDown);
        return;
    }

    // Sample actual separation before another flee/spread can replace its
    // pending observation. A new heading is never bad merely for being used.
    LastMovement& movement = aiObjectContext->GetValue<LastMovement&>("last movement")->Get();
    if (movement.fleeFailures.IsPending() || movement.spreadFailures.IsPending())
    {
        bool const canObserve = sServerFacade.IsInCombat(bot) && CanMove();
        uint32 const nowMs = WorldTimer::getMSTime();
        for (FleeFailureMemory* memory : { &movement.fleeFailures, &movement.spreadFailures })
        {
            if (!memory->IsPending())
                continue;
            Unit* anchor = GetUnit(ObjectGuid(memory->Anchor()));
            if (!canObserve || !anchor || !sServerFacade.IsAlive(anchor) ||
                anchor->GetMapId() != bot->GetMapId())
                memory->Clear();
            else
                memory->Observe(memory->Anchor(), bot->GetMapId(),
                    sServerFacade.getDistance2d(bot, anchor), nowMs, bot->movespline->GetId());
        }
    }

    // if in combat but stuck with old data - clear targets
    SC_PHASE("DoNextAction.staleTargetCheck", bot ? bot->GetName() : "(null)");
    if (currentEngine == engines[(uint8)BotState::BOT_STATE_NON_COMBAT] && sServerFacade.IsInCombat(bot))
    {
        if (aiObjectContext->GetValue<Unit*>("current target")->Get() != NULL ||
            aiObjectContext->GetValue<ObjectGuid>("attack target")->Get() != ObjectGuid() ||
            aiObjectContext->GetValue<ObjectGuid>("explicit attack target")->Get() != ObjectGuid() ||
            aiObjectContext->GetValue<Unit*>("dps target")->Get() != NULL)
        {
            Reset();
        }
    }

    bool minimal = !AllowActivity();

    // Live attacker snapshot for the deaths.csv 'adds' column (telemetry
    // fix): the victim-filtered "all targets" loop at death time sees an
    // already-drained attacker set, so sample here while the fight is live.
    // One GetAttackers() copy per tick, dead creatures filtered, capped at
    // 8; the death writer reads it only if fresh (30 s window).
    NoteFightAttackers();

    // A leftover walk flag would both walk this tick's splines and leave the
    // client's walking view of the bot stuck (the core sends a launch's
    // walk/run change with the pre-launch mode restored last, so a run spline
    // launched on walk flags leaves watching clients walking until they
    // reload). Clear it before any move of the new tick is dispatched.
    if (bot->m_movementInfo.HasMovementFlag(MOVEFLAG_WALK_MODE))
        bot->m_movementInfo.RemoveMovementFlag(MOVEFLAG_WALK_MODE);

    SC_PHASE("DoNextAction.engineDoNextAction", bot ? bot->GetName() : "(null)");
    currentEngine->DoNextAction(NULL, 0, (minimal || min), bot->IsTaxiFlying());
    SC_PHASE("DoNextAction.afterEngine", bot ? bot->GetName() : "(null)");

    if (!bot->IsInWorld()) //Teleport out of bg
    {
        explicitActivityOverride = previousActivityOverride;
        return;
    }

    if (minimal)
    {
        if (!MovementAction::MinimalMove(this) && !bot->IsAFK() && !bot->InBattleGround() && !HasRealPlayerMaster())
            bot->ToggleAFK();

        SetAIInternalUpdateDelay(sPlayerbotAIConfig.passiveDelay);
        explicitActivityOverride = previousActivityOverride;
        return;
    }
    else if (bot->IsAFK())
        bot->ToggleAFK();


    Group *group = bot->GetGroup();

    // A bot leased to another module's dungeon crew keeps its master state as it is:
    // no strip, no adoption of the bot group leader. Both would rebuild the strategies
    // mid-fight and, through the BotManager (ClaimForMaster / ReleaseMaster), replace
    // the Dungeon lease that keeps the random-bot services off the crew.
    bool const dungeonCrew = TortoiseBots::BotActivityLeaseManager::Instance().GetActivity(bot->GetGUIDLow()) == TortoiseBots::BotActivity::Dungeon;
    // A bot of a group a remote commander bound keeps its master and its group's leader as the binding has them: no
    // master strip below, no hand-over of the lead to a real player of the group and no adoption of a real player as
    // master (`.bot commander bind ... inparty` keeps players in the group; the lead bot leads it, and another player
    // must never become the bots' master, even while the commander is offline or gone until the binding is released;
    // the release then takes the bots out of that party, BotCommands ReleaseBinding).
    bool const commanded = TortoiseBots::BotCommands::IsInCommandedGroup(bot);

    // Issue #1014: a hire keeps its hirer. While the hirer changes maps it is out
    // of the world (FindPlayer misses it, so UpdateAI dropped the pointer) but
    // still exists (the not-in-world lookup finds it); the binding stays and the
    // pointer comes back once the hirer is in the world again. Non-hires skip it.
    bool const hired = TortoiseBots::HireLifecycle::Instance().IsHired(bot->GetObjectGuid());
    ObjectGuid const hirerGuid = hired ? TortoiseBots::HireLifecycle::Instance().GetMaster(bot->GetObjectGuid()) : ObjectGuid();
    Player* const hirer = hirerGuid.IsEmpty() ? nullptr : ObjectAccessor::FindPlayerNotInWorld(hirerGuid);
    bool const hirerBetweenMaps = hirer && !hirer->IsInWorld();
    if (hired && !dungeonCrew)
    {
        TortoiseBots::BotRecord const* record = TortoiseBots::BotManager::Instance().FindBot(bot->GetObjectGuid());
        bool const boundToHirer = !hirerGuid.IsEmpty() && masterGuid == hirerGuid &&
            record && record->masterGuid == hirerGuid;
        if (TortoiseBots::HireRestoresHirer(hired, boundToHirer, master != nullptr, hirer && hirer->IsInWorld()))
            master = hirer;
    }

    //Remove bot masters not in our group.
    if (!dungeonCrew && !commanded && master && master != bot && !HasActivePlayerMaster() && (!group || group->GetLeaderGuid() != master->getObjectGuid()))
    {
        Player* replacementMaster = IsRealPlayer() ? bot : nullptr;
        if (TortoiseBots::BotManager::Instance().IsBot(bot->GetObjectGuid()))
        {
            bool transitioned = replacementMaster
                ? TortoiseBots::BotManager::Instance().BindBotMaster(
                    bot->GetObjectGuid(), replacementMaster->GetObjectGuid())
                : TortoiseBots::BotManager::Instance().ClearBotMaster(bot->GetObjectGuid());
            if (!transitioned)
                sLog.outError("TortoiseBots: durable master transition failed for %s; retaining the existing ownership state",
                    bot->GetName());
            else
                master = replacementMaster;
        }
        else
        {
            master = replacementMaster;
            SetMaster(master);
        }
        if (master == replacementMaster)
            ResetStrategies();
    }

    // test BG master set
    if (!dungeonCrew && group && !IsRealPlayer())
    {
        // If this bot is group leader and there is a real player in the group, yield leadership to the real player.
        // (Not in a commanded group, see `commanded`: handing the lead over would end the binding.)
        if (group->IsLeader(bot->GetObjectGuid()) && !commanded)
        {
            // Resolve the new leader through the Group's member slots before any
            // change: the GroupReference list can hand out a Player* that is
            // already freed during a headless bot's relogin window (issue #225),
            // and mutating the group mid-iteration is unsafe regardless.
            ObjectGuid realPlayerGuid;
            for (Group::MemberSlot const& slot : group->GetMemberSlots())
            {
                Player* member = ObjectAccessor::FindPlayer(slot.guid);
                if (member && member->IsInWorld() && IsRealPlayer(member))
                {
                    realPlayerGuid = slot.guid;
                    break;
                }
            }
            if (!realPlayerGuid.IsEmpty())
                group->ChangeLeader(realPlayerGuid);
        }

        if (!master || !HasActivePlayerMaster())
        {
            // Ideally we want to have the real player in the group as master.
            Player* newMaster = GetGroupMaster();

            // Are there any non-bot players in the group?
            if (!newMaster || !IsRealPlayer(newMaster))
                for (Player* member : LiveGroupMembers(group))
                {

                    if (!member)
                        continue;

                    if (member == bot)
                        continue;

                    if (member == newMaster)
                        continue;

                    if (!member->IsInWorld())
                        continue;

                    if (!IsInGroup_Helper(member, bot, true))
                        continue;

                    // Do not make bots your master if they are not a real player.
                    if (!IsRealPlayer(member) && !bot->InBattleGround())
                        continue;

                    if (bot->InBattleGround())
                        continue;

                    newMaster = member;
                    break;
                }

        // Issue #1014: never hand a hire to a bot, nor to anyone while its hirer
        // is only between maps.
        // A commanded bot never takes a real player of its group as master (see `commanded`).
        if (newMaster && (!master || master != newMaster) && bot != newMaster &&
            !(commanded && IsRealPlayer(newMaster)) &&
            TortoiseBots::HireAllowsMasterAdoption(hired, hirerBetweenMaps, IsRealPlayer(newMaster)))
        {
            bool masterChanged = true;
            if (TortoiseBots::BotManager::Instance().IsBot(bot->GetObjectGuid()))
            {
                masterChanged = TortoiseBots::BotManager::Instance().BindBotMaster(
                    bot->GetObjectGuid(), newMaster->GetObjectGuid());
                if (!masterChanged)
                    sLog.outError("TortoiseBots: durable master adoption failed for bot %s; leaving its current master unchanged",
                        bot->GetName());
            }
            else
                SetMaster(newMaster);

            if (masterChanged)
            {
                master = newMaster;
                // Bot members were skipped above, so a changed master here is a
                // real player taking ownership: halt autonomous loops now. Full
                // reset purges travel/grind targets; StopMoving cancels the
                // current path so the bot tethers to its owner immediately.
                Reset(true);
                bot->StopMoving();
                ResetStrategies();

                if (sRandomBotFacade.IsFreeBot(bot))
                {
                    std::string defaultMovementStrategy = GetDefaultMovementStrategy();
                    ChangeStrategy("+" + defaultMovementStrategy, BotState::BOT_STATE_NON_COMBAT);
                }

                if (GetMaster() == GetGroupMaster())
                {
                    // Issue #1108: the announcement in chat is ambient, the
                    // whisper below answers the new master and stays.
                    if (!HasStrategy("silent", BotState::BOT_STATE_NON_COMBAT) && master && sPlayerbotAIConfig.enableBroadcasts)
                        bot->TextEmote("is following " + std::string(master->GetName()) + ".");
                }
                else
                {
                    TellPlayer(master, BOT_TEXT("hello"));
                }
            }
        }
    }
}

    // fix bots in BG not having proper strategies
    if (bot->InBattleGround() && !HasStrategy("battleground", BotState::BOT_STATE_NON_COMBAT))
        ResetStrategies();

    if (master && master->IsInWorld())
	{

        // Mirror the master sitting only out of combat. Live H11 (Patchwerk):
        // the player sat after eating, so every fire mage within 10 yd was
        // re-seated each tick, the cast path stood it up and deferred the
        // cast - Fireball failed ~350 times a minute per mage.
        if (master->GetStandState() == UNIT_STAND_STATE_SIT && aiInternalUpdateDelay < 1000 && !bot->IsInCombat())
        {
            if (!sServerFacade.isMoving(bot) && sServerFacade.getDistance2d(bot, master) < 10.0f)
                bot->SetStandState(UNIT_STAND_STATE_SIT);
        }
        else if (aiInternalUpdateDelay < 1000)
            bot->SetStandState(UNIT_STAND_STATE_STAND);

        // Issue #644: following bots often miss the dungeon entrance, and a
        // bot cannot path to a master on another map, so it would wait outside
        // forever (first entry, or after reviving outside from a wipe).
        // Bring it in like the summon on leaving a dungeon brings it out.
        if (group && group == master->GetGroup() && master->GetMap()->IsDungeon() &&
            bot->GetMapId() != master->GetMapId() && HasActivePlayerMaster() &&
            HasStrategy("follow", BotState::BOT_STATE_NON_COMBAT) &&
            time(nullptr) >= m_dungeonCatchUpAt)
        {
            m_dungeonCatchUpAt = time(nullptr) + 20;
            if (TortoiseBots::PlayerConvenience::Instance().RequestSummon(master, bot))
                sLog.outString("TortoiseBots: bringing bot %s into the dungeon after master %s",
                    bot->GetName(), master->GetName());
        }

        if (!group && sRandomBotFacade.IsFreeBot(bot) && !IsRealPlayer())
        {
            if (TortoiseBots::BotManager::Instance().IsBot(bot->GetObjectGuid()) &&
                !TortoiseBots::BotManager::Instance().ClearBotMaster(bot->GetObjectGuid()))
                sLog.outError("TortoiseBots: failed to clear durable master for %s after leaving its group",
                    bot->GetName());
        }
	}
	else if ((aiInternalUpdateDelay < 1000) && bot->GetStandState() == UNIT_STAND_STATE_SIT) bot->SetStandState(UNIT_STAND_STATE_STAND);


    if (bot->IsTaxiFlying())
    {
        explicitActivityOverride = previousActivityOverride;
        return;
    }

    explicitActivityOverride = previousActivityOverride;
}

void PlayerbotAI::ReInitCurrentEngine()
{
    InterruptSpell();
    currentEngine->Init();
}

void PlayerbotAI::ChangeStrategy(const std::string& names, BotState type)
{
    // The wait window is the hold's own guard, so it must not outlive it:
    // whichever path drops "wait for attack" - a bot's own release, a party
    // release, or a player's ".bot co -wait for attack" - frees the anchor hold
    // with it. One rule here beats repeating the release at every caller; a
    // hold left without its guard would keep the bot on stay for good.
    if (names.find("-wait for attack") != std::string::npos)
        ReleasePullHold();

    if(type == BotState::BOT_STATE_ALL)
    {
        for (uint8 i = 0; i < (uint8)BotState::BOT_STATE_ALL; i++)
        {
            Engine* engine = engines[i];
            if (engine)
            {
                engine->ChangeStrategy(names);
            }
        }
    }
    else
    {
        Engine* engine = engines[(uint8)type];
        if (engine)
        {
            engine->ChangeStrategy(names);
        }
    }

    // "+", "-" and the "~" toggle alike: read back what the engines hold now.
    if (names.find("wait for attack") != std::string::npos)
        SyncPullWindow(type);
}

void PlayerbotAI::PrintStrategies(Player* requester, BotState type)
{
    if (type == BotState::BOT_STATE_ALL)
    {
        for (uint8 i = 0; i < (uint8)BotState::BOT_STATE_ALL; i++)
        {
            Engine* engine = engines[i];
            if (engine)
            {
                engine->PrintStrategies(requester, BotStateToString(BotState(i)));
            }
        }
    }
    else
    {
        Engine* engine = engines[(uint8)type];
        if (engine)
        {
            engine->PrintStrategies(requester, BotStateToString(type));
        }
    }
}

void PlayerbotAI::ClearStrategies(BotState type)
{
    if (type == BotState::BOT_STATE_ALL)
    {
        for (uint8 i = 0; i < (uint8)BotState::BOT_STATE_ALL; i++)
        {
            Engine* engine = engines[i];
            if (engine)
            {
                engine->removeAllStrategies();
            }
        }
    }
    else
    {
        Engine* engine = engines[(uint8)type];
        if (engine)
        {
            engine->removeAllStrategies();
        }
    }
}

std::list<std::string_view> PlayerbotAI::GetStrategies(BotState type)
{
    // Can't get all strategies for all engines
    if (type != BotState::BOT_STATE_ALL)
    {
        Engine* engine = engines[(uint8)type];
        if (engine)
        {
            return engine->GetStrategies();
        }
    }

    return std::list<std::string_view>();
}

bool PlayerbotAI::CanDoSpecificAction(const std::string& name, bool isUseful, bool isPossible)
{
    for (uint8 i = 0; i < (uint8)BotState::BOT_STATE_ALL; i++)
    {
        Engine* engine = engines[i];
        if(engine)
        {
            if(engine->CanExecuteAction(name, isUseful, isPossible))
            {
                return true;
            }
        }
    }

    return false;
}

bool PlayerbotAI::DoSpecificAction(const std::string& name, Event event, bool silent)
{
    Player* requester = event.getOwner();
    for (uint8 i = 0 ; i < (uint8)BotState::BOT_STATE_ALL; i++)
    {
        Engine* engine = engines[i];
        if(engine)
        {
            ActionResult res = engine->ExecuteAction(name, event);
            switch (res)
            {
                case ACTION_RESULT_OK:
                {
                    if (!silent)
                    {
                        PlaySound(TEXTEMOTE_NOD);
                    }

                    return true;
                }

                case ACTION_RESULT_IMPOSSIBLE:
                {
                    if (!silent)
                    {
                        std::ostringstream out;
                        out << name << ": impossible";
                        TellError(requester, out.str());
                        PlaySound(TEXTEMOTE_NO);
                    }

                    return false;
                }

                case ACTION_RESULT_USELESS:
                {
                    if (!silent)
                    {
                        std::ostringstream out;
                        out << name << ": useless";
                        TellError(requester, out.str());
                        PlaySound(TEXTEMOTE_NO);
                    }

                    return false;
                }

                case ACTION_RESULT_FAILED:
                {
                    if (!silent)
                    {
                        std::ostringstream out;
                        out << name << ": failed";
                        TellError(requester, out.str());
                    }

                    return false;
                }

                default:
                {
                    if (!silent)
                    {
                        std::ostringstream out;
                        out << name << ": unknown action";
                        TellError(requester, out.str());
                    }

                    return false;
                }
            }
        }
        else
        {
            if (!silent)
            {
                std::ostringstream out;
                out << name << ": engine not ready";
                TellError(requester, out.str());
            }
        }
    }

    return false;
}

bool PlayerbotAI::PlaySound(uint32 emote)
{
    // The pinned core does not load the donor EmotesTextSound table. A text
    // emote packet is still a real client/core contract and lets the client
    // select the race/gender animation and associated sound from its DBC.
    return PlayEmote(emote);
}

bool PlayerbotAI::PlayEmote(uint32 emote)
{
    // A bot between two maps has no map: TeleportTo onto another map takes the player off
    // its map at once and the new one arrives with the worldport ack on a later tick. The
    // text-emote handler reads GetMap(), which asserts on a mapless player and takes the
    // whole world server down. Seen with a dead dungeon-crew bot: "find corpse" -> "spirit
    // healer" -> DoSpecificAction("repop") teleported the ghost from the instance to its
    // graveyard, returned OK, and the confirming nod right after it was the crash
    // (Object.cpp:2046 "Assertion in GetMap failed: m_currMap", backtrace PlayEmote <-
    // DoSpecificAction <- SpiritHealerAction). No map, no emote.
    if (!bot->IsInWorld() || bot->IsBeingTeleported() || !bot->FindMap())
        return false;

    WorldPacket data(SMSG_TEXT_EMOTE);
    data << (TextEmotes)emote;
    data << urand(0, EmoteAction::GetNumberOfEmoteVariants((TextEmotes)emote, bot->GetRace(), bot->GetGender()) - 1);
    data << ((master && (sServerFacade.getDistance2d(bot, master) < 30.0f) && urand(0, 1)) ? master->getObjectGuid() : (bot->GetSelectionGuid() && urand(0, 1)) ? bot->GetSelectionGuid() : ObjectGuid());
    bot->GetSession()->HandleTextEmoteOpcode(data);

    return true;
}

bool PlayerbotAI::ContainsStrategy(StrategyType type)
{
    for (uint8 i = 0 ; i < (uint8)BotState::BOT_STATE_ALL; i++)
    {
        if(engines[i])
        {
            if (engines[i]->ContainsStrategy(type))
            {
                return true;
            }
        }
    }

    return false;
}

bool PlayerbotAI::HasStrategy(const std::string& name, BotState type)
{
    // Can't check the strategy for all engines at once
    if(type != BotState::BOT_STATE_ALL)
    {
        const uint8 typeIndex = (uint8)type;
        if (engines[typeIndex])
        {
            return engines[typeIndex]->HasStrategy(name);
        }
    }

    return false;
}

void PlayerbotAI::ResetStrategies(bool autoLoad)
{
    // The wipe below bypasses ChangeStrategy, so it would leave a pull's
    // window running with no "pull hold expired" left to end it and the
    // pull's delay in "wait for attack time". End it first: the player's
    // delay comes back and the anchor goes (PullJoinWindowPolicy.h).
    if (HasPullWindow())
        EndPullWindow("reset");

    for (uint8 i = 0; i < (uint8)BotState::BOT_STATE_ALL; i++)
    {
        engines[i]->initMode = true;
        engines[i]->removeAllStrategies();
    }

    AiFactory::AddDefaultCombatStrategies(bot, this, engines[(uint8)BotState::BOT_STATE_COMBAT]);
    AiFactory::AddDefaultNonCombatStrategies(bot, this, engines[(uint8)BotState::BOT_STATE_NON_COMBAT]);
    AiFactory::AddDefaultDeadStrategies(bot, this, engines[(uint8)BotState::BOT_STATE_DEAD]);
    AiFactory::AddDefaultReactionStrategies(bot, this, reactionEngine);
    if (autoLoad && HasPlayerRelation()) sPlayerbotDbStore.Load(this);

    // A role handed out by the dungeon finder wins over both the talent spec
    // and the stored strategy set - hence applied last. addStrategy() drops the
    // sibling spec strategy by itself and does nothing for classes that cannot
    // fill the role, so a mage stays a mage.
    if (m_forcedRole)
    {
        char const* roleStrategy = m_forcedRole == 1 ? "tank" : (m_forcedRole == 2 ? "heal" : "dps");
        for (BotState state : { BotState::BOT_STATE_COMBAT, BotState::BOT_STATE_NON_COMBAT })
        {
            Engine* engine = engines[(uint8)state];
            // Priests heal from two specs and "heal" aliases Holy, which
            // would drop a Discipline priest's own spec as a sibling. A
            // Discipline priest is already a healer, so it keeps its spec.
            if (m_forcedRole == 2 && engine->HasStrategy("discipline"))
                continue;
            engine->addStrategy(roleStrategy);
        }
        sLog.outBasic("LFT: %s strategies rebuilt as %s", bot->GetName(), roleStrategy);
    }

    // A player's companion leaves every rebuild with a movement mode: a stored
    // snapshot saved without one would otherwise park it after each rebuild
    // until the player whispers "follow" (ai::NeedsDefaultMovementMode).
    if (ai::NeedsDefaultMovementMode(bot->InBattleGround(), HasActivePlayerMaster(), HasActiveMovementStrategy()))
        SetMovementStrategy(GetDefaultMovementStrategy());

    for (uint8 i = 0; i < (uint8)BotState::BOT_STATE_ALL; i++)
    {
        engines[i]->initMode = false;
        engines[i]->Init();
    }
}

bool PlayerbotAI::IsRanged(Player* player, bool inGroup)
{
    PlayerbotAI* botAi = PlayerbotAIStorage::Instance().GetAI(player);
    if (botAi)
    {
        if (botAi->ContainsStrategy(STRATEGY_TYPE_RANGED))
            return true;
        if (botAi->ContainsStrategy(STRATEGY_TYPE_MELEE))
            return false;
    }

    switch (player->GetClass())
    {
    case CLASS_PALADIN:
    case CLASS_WARRIOR:
    case CLASS_ROGUE:
        return false;
    case CLASS_DRUID:
        return !HasAnyAuraOf(player, "cat form", "bear form", "dire bear form", NULL);
    }
    return true;
}

bool PlayerbotAI::IsMelee(Player* player, bool inGroup)
{
    return !IsRanged(player, inGroup);
}

bool PlayerbotAI::IsTank(Player* player, bool inGroup)
{
    uint8 forcedRole = PlayerbotAIStorage::Instance().GetPlayerForcedRole(player->GetObjectGuid());
    if (forcedRole != static_cast<uint8>(BOT_ROLE_NONE))
        return forcedRole == static_cast<uint8>(BOT_ROLE_TANK);

    PlayerbotAI* botAi = PlayerbotAIStorage::Instance().GetAI(player);
    if (botAi)
    {
        if (botAi->GetForcedRole() == static_cast<uint8>(BOT_ROLE_TANK))
            return true;
        if (botAi->ContainsStrategy(STRATEGY_TYPE_TANK))
            return true;
        // An explicit forced role wins over the talent read below for owned
        // companions: a Feral ordered to DPS must not report as a tank.
        if (botAi->GetForcedRole() != static_cast<uint8>(BOT_ROLE_NONE) && botAi->HasActivePlayerMaster())
            return false;
    }

    BotRoles botRoles = AiFactory::GetPlayerRoles(player);
    return (botRoles & BOT_ROLE_TANK) != 0;
}

bool PlayerbotAI::IsHeal(Player* player, bool inGroup)
{
    uint8 forcedRole = PlayerbotAIStorage::Instance().GetPlayerForcedRole(player->GetObjectGuid());
    if (forcedRole != static_cast<uint8>(BOT_ROLE_NONE))
        return forcedRole == static_cast<uint8>(BOT_ROLE_HEALER);

    PlayerbotAI* botAi = PlayerbotAIStorage::Instance().GetAI(player);
    if (botAi)
    {
        if (botAi->GetForcedRole() == static_cast<uint8>(BOT_ROLE_HEALER))
            return true;
        if (botAi->ContainsStrategy(STRATEGY_TYPE_HEAL))
            return true;
        if (botAi->GetForcedRole() != static_cast<uint8>(BOT_ROLE_NONE) && botAi->HasActivePlayerMaster())
            return false;
    }

    BotRoles botRoles = AiFactory::GetPlayerRoles(player);
    return (botRoles & BOT_ROLE_HEALER) != 0;
}

namespace MaNGOS
{

    class UnitByGuidInRangeCheck
    {
    public:
        UnitByGuidInRangeCheck(WorldObject const* obj, ObjectGuid guid, float range) : i_obj(obj), i_range(range), i_guid(guid) {}
        WorldObject const& GetFocusObject() const { return *i_obj; }
        bool operator()(Unit* u)
        {
            return u->getObjectGuid() == i_guid && i_obj->IsWithinDistInMap(u, i_range);
        }
    private:
        WorldObject const* i_obj;
        float i_range;
        ObjectGuid i_guid;
    };

    class GameObjectByGuidInRangeCheck
    {
    public:
        GameObjectByGuidInRangeCheck(WorldObject const* obj, ObjectGuid guid, float range) : i_obj(obj), i_range(range), i_guid(guid) {}
        WorldObject const& GetFocusObject() const { return *i_obj; }
        bool operator()(GameObject* u)
        {
            if (u && i_obj->IsWithinDistInMap(u, i_range) && sServerFacade.isSpawned(u) && u->GetGOInfo() && u->getObjectGuid() == i_guid)
                return true;

            return false;
        }
    private:
        WorldObject const* i_obj;
        float i_range;
        ObjectGuid i_guid;
    };

};


Unit* PlayerbotAI::GetUnit(ObjectGuid guid)
{
    if (!guid)
        return NULL;

    Map* map = bot->GetMap();
    if (!map)
        return NULL;

    return sObjectAccessor.GetUnit(*bot, guid);
}

Unit* PlayerbotAI::GetUnit(CreatureDataPair const* creatureDataPair)
{
    if (!creatureDataPair)
        return NULL;

    ObjectGuid guid(HIGHGUID_UNIT, creatureDataPair->second.creature_id[0], creatureDataPair->first);

    if (!guid)
        return NULL;

    Map* map = sMapMgr.FindMap(creatureDataPair->second.position.mapId);

    if (!map)
        return NULL;

    return map->GetUnit(guid);
}


Creature* PlayerbotAI::GetCreature(ObjectGuid guid) const
{
    if (!guid)
        return NULL;

    Map* map = bot->GetMap();
    if (!map)
        return NULL;

    return map->GetCreature(guid);
}

Creature* PlayerbotAI::GetAnyTypeCreature(ObjectGuid guid) const
{
    if (!guid)
        return NULL;

    Map* map = bot->GetMap();
    if (!map)
        return NULL;

    return map->GetAnyTypeCreature(guid);
}

GameObject* PlayerbotAI::GetGameObject(ObjectGuid guid)
{
    if (!guid)
        return NULL;

    Map* map = bot->GetMap();
    if (!map)
        return NULL;

    return map->GetGameObject(guid);
}

GameObject* PlayerbotAI::GetGameObject(GameObjectDataPair const* gameObjectDataPair)
{
    if (!gameObjectDataPair)
        return NULL;

    ObjectGuid guid(HIGHGUID_GAMEOBJECT, gameObjectDataPair->second.id, gameObjectDataPair->first);

    if (!guid)
        return NULL;

    Map* map = sMapMgr.FindMap(gameObjectDataPair->second.position.mapId);

    if (!map)
        return NULL;

    return map->GetGameObject(guid);
}

WorldObject* PlayerbotAI::GetWorldObject(ObjectGuid guid)
{
    if (!guid)
        return NULL;

    Map* map = bot->GetMap();
    if (!map)
        return NULL;

    return map->GetWorldObject(guid);
}

std::vector<Player*> PlayerbotAI::GetPlayersInGroup()
{
    std::vector<Player*> members;

    Group* group = bot->GetGroup();

    if (!group)
        return members;

    for (Player* member : LiveGroupMembers(group))
    {

        if (PlayerbotAIStorage::Instance().GetAI(member) && !PlayerbotAIStorage::Instance().GetAI(member)->IsRealPlayer())
            continue;

        members.push_back(member);
    }

    return members;
}

const AreaTableEntry* PlayerbotAI::GetCurrentArea()
{
    return GetAreaEntryByAreaID(sServerFacade.GetAreaId(bot));
}

const AreaTableEntry* PlayerbotAI::GetCurrentZone()
{
    return GetAreaEntryByAreaID(sTerrainMgr.getZoneId(bot->GetMapId(), bot->getPositionX(), bot->getPositionY(), bot->getPositionZ()));
}

/*
* @return localized area_name
*/
std::string PlayerbotAI::GetLocalizedAreaName(const AreaTableEntry* entry)
{
    // Penqle's area_name is single char* (no locale array).
    return entry && entry->Name ? std::string(entry->Name) : std::string();
}

bool PlayerbotAI::IsInCapitalCity()
{
    AreaTableEntry const* current_area = GetAreaEntryByAreaID(sServerFacade.GetAreaId(bot));
    if (!current_area)
    {
        return false;
    }
    return current_area->Flags & AREA_FLAG_CAPITAL;
}

void PlayerbotAI::DurabilityLoss(Item* item, double percent)
{
    if (item)
    {
        const uint32 pCurrDurability = item->GetUInt32Value(ITEM_FIELD_DURABILITY);
        const uint32 pMaxDurability = item->GetUInt32Value(ITEM_FIELD_MAXDURABILITY);
        if (pMaxDurability)
        {
            if (!HasCheat(BotCheatMask::repair))
            {
                // Break item
                uint32 pDurabilityLoss = std::max(uint32(pMaxDurability * percent), 1U);
                bot->DurabilityPointsLoss(item, pDurabilityLoss);
            }
            else if (pCurrDurability < pMaxDurability)
            {
                // Repair if broken
                bot->DurabilityRepair(item->GetPos(), false, 0.0f);
            }
        }
    }
}

bool IsAlliance(uint8 race)
{
    // The core owns race faction data, including Tortoise's Goblin/High Elf
    // rows. Do not duplicate the classic eight-race table here.
    return Player::TeamForRace(race) == ALLIANCE;
}

uint32 PlayerbotAI::GetFixedBotNumber(BotTypeNumber typeNumber, uint32 maxNum, float cyclePerMin, bool ignoreGuid)
{
    uint8 seedNumber = uint8(typeNumber);
    std::mt19937 rng(seedNumber);
    uint32 randseed = rng();                                       //Seed random number
    uint32 randnum = randseed;                                     //Semi-random.

    if (!ignoreGuid)
        randnum += bot->GetGUIDLow();                              //but fixed number for each bot.

    if (cyclePerMin > 0)
    {
        uint32 cycle = floor(WorldTimer::getMSTime() / (1000));    //Semi-random number adds 1 each second.
        cycle = cycle * cyclePerMin / 60;                          //Cycles cyclePerMin per minute.
        randnum += cycle;                                          //Make the random number cylce.
    }
    randnum = (randnum % (maxNum+1));                              //Loops the randomnumber at maxNum. Bassically removes all the numbers above 99.
    return randnum;                                                //Now we have a number unique for each bot between 0 and maxNum that increases by cyclePerMin.
}

GrouperType PlayerbotAI::GetGrouperType()
{
    AiObjectContext* context = GetAiObjectContext();
    int32 grouperOverride = AI_VALUE2(int32, "manual saved int", "grouper override");

    if (grouperOverride >= 0)
        return GrouperType(grouperOverride);

    uint32 maxGroupType = sPlayerbotAIConfig.randomBotRaidNearby ? 100 : 95;
    uint32 grouperNumber = GetFixedBotNumber(BotTypeNumber::GROUPER_TYPE_NUMBER, maxGroupType, 0);

    //20% solo
    //50% member
    //20% leader
    //10% raider

    if (grouperNumber < 20 && !HasRealPlayerMaster())
        return GrouperType::SOLO;
    if (grouperNumber < 75)
        return GrouperType::MEMBER;
    if (grouperNumber < 80 || bot->GetLevel() < 3)
        return GrouperType::LEADER_2;
    if (grouperNumber < 85 || bot->GetLevel() < 5)
        return GrouperType::LEADER_3;
    if (grouperNumber < 90 || bot->GetLevel() < 7)
        return GrouperType::LEADER_4;
    if (grouperNumber <= 95 || bot->GetLevel() < 9)
        return GrouperType::LEADER_5;
    if (grouperNumber <= 97)
        return GrouperType::RAIDER_20;
   return GrouperType::RAIDER_MAX;
}

GuilderType PlayerbotAI::GetGuilderType()
{
    AiObjectContext* context = GetAiObjectContext();
    int32 guilderOverride = AI_VALUE2(int32, "manual saved int", "guilder override");

    if (guilderOverride >= 0)
        return GuilderType(guilderOverride);

    uint32 grouperNumber = GetFixedBotNumber(BotTypeNumber::GUILDER_TYPE_NUMBER, 100, 0);

    if (grouperNumber < 20 && !HasRealPlayerMaster())
        return GuilderType::SOLO;
    if (grouperNumber < 30)
        return GuilderType::TINY;
    if (grouperNumber < 40)
        return GuilderType::SMALL;
    if (grouperNumber < 60)
        return GuilderType::MEDIUM;
    if (grouperNumber < 80)
        return GuilderType::LARGE;

    return GuilderType::MASSIVE;
}

uint32 PlayerbotAI::GetMaxPreferedGuildSize()
{
    uint32 maxSize = (uint8)GetGuilderType();

    if (!bot->GetGuildId()) return maxSize;

    Guild* guild =sGuildMgr.GetGuildById(bot->GetGuildId());

    uint32 maxRank = guild->GetRanksSize();

    MemberSlot* botMember = guild->GetMemberSlot(bot->getObjectGuid());

    if (!botMember->RankId) return maxSize;

    uint32 memberMod = (botMember->RankId * 20) / maxRank;

    return (maxSize * (100 - memberMod)) / 100;
}

// Real (network) players in the world, rebuilt once per world loop. The
// donor's HasPlayerNearby walks its real-player registry; GetPlayers() here
// holds the headless random bots instead, so every call scanned the whole
// pool (2000 FindPlayer lookups, ~11% of bot AI time) and treated any
// nearby bot as a watching player.
static std::vector<ObjectGuid> const& RealPlayerGuids()
{
    static std::vector<ObjectGuid> guids;
    static uint32 builtAtLoop = 0;
    static bool built = false;
    uint32 const loop = World::m_worldLoopCounter;
    if (built && builtAtLoop == loop)
        return guids;
    built = true;
    builtAtLoop = loop;
    guids.clear();
    for (auto const& entry : sWorld.GetAllSessions())
    {
        WorldSession* session = entry.second;
        if (session && session->HasNetworkTransport())
            if (Player* player = session->GetPlayer())
                guids.push_back(player->GetObjectGuid());
    }
    return guids;
}

bool PlayerbotAI::HasPlayerNearby(WorldPosition pos, float range)
{
    if (!range)
        range = pos.getVisibilityDistance();

    float sqRange = range * range;
    bool nearPlayer = false;
    for (ObjectGuid const& guid : RealPlayerGuids())
    {
        Player* player = sObjectAccessor.FindPlayer(guid);
        if (!player || !player->IsInWorld())
            continue;

        if (!player->IsGameMaster() || player->IsGMVisible())
        {
            if (player->GetMapId() != bot->GetMapId())
                continue;

            if (pos.sqDistance(WorldPosition(player)) < sqRange)
                nearPlayer = true;

            // if player is far check farsight/cinematic camera
            Camera& viewPoint = player->GetCamera();
            WorldObject* viewObj = viewPoint.GetBody();
            if (viewObj && viewObj != player && viewObj->IsInWorld())
            {
                if (pos.sqDistance(WorldPosition(viewObj)) < sqRange)
                    nearPlayer = true;
            }
        }
    }

    return nearPlayer;
}

bool PlayerbotAI::HasPlayerNearby(float range)
{
    return HasPlayerNearby(bot, range);
}

bool PlayerbotAI::AllowActivity(ActivityType activityType, bool checkNow)
{
    if (explicitActivityOverride)
        return true;

    if (!allowActiveCheckTimer[activityType])
        allowActiveCheckTimer[activityType] = time(NULL);

    if (!checkNow && time(NULL) < (allowActiveCheckTimer[activityType] + 5))
        return allowActive[activityType];

    bool allowed = AllowActive(activityType);
    allowActive[activityType] = allowed;
    allowActiveCheckTimer[activityType] = time(NULL);
    return allowed;
}

bool PlayerbotAI::HasCheat(BotCheatMask mask) const
{
    if (((uint32)mask & (uint32)cheatMask) != 0)
        return true;

    if (sRandomBotFacade.IsRandomBot(bot))
    {
        if (((uint32)mask & sPlayerbotAIConfig.rndBotCheatMask) != 0)
            return true;
    }
    else
    {
        if (((uint32)mask & sPlayerbotAIConfig.botCheatMask) != 0)
            return true;
        // Issue #872: the item cheat (ammo/reagents) applies to autonomous
        // alts as for the pool (B15). The cheat mask is never set per-bot,
        // so without this an alt would read the Bank list (BotCheats:
        // repair,breath) and lose the refill/ration/kit paths the pool gets
        // from the random list. Scoped to the item cheat only: gold/health/
        // mana/cooldown cheats stay off, so repair stays paid (B10).
        if (mask == BotCheatMask::item &&
            TortoiseBots::AltBotService::Instance().IsAutonomous(bot->GetObjectGuid()))
            return true;
    }

    return false;
}

bool PlayerbotAI::IsOpposing(Player* player)
{
    return IsOpposing(player->GetRace(), bot->GetRace());
}

bool PlayerbotAI::IsOpposing(uint8 race1, uint8 race2)
{
    return Player::TeamForRace(race1) != Player::TeamForRace(race2);
}

void PlayerbotAI::RemoveShapeshift()
{
    RemoveAura("bear form");
    RemoveAura("dire bear form");
    RemoveAura("moonkin form");
    RemoveAura("travel form");
    RemoveAura("cat form");
    RemoveAura("aquatic form");
    RemoveAura("ghost wolf");
    RemoveAura("tree of life form");
}

uint32 PlayerbotAI::GetEquipGearScore(Player* player, bool withBags, bool withBank)
{
    std::vector<uint32> gearScore(EQUIPMENT_SLOT_END);
    uint32 twoHandScore = 0;

    for (uint8 i = EQUIPMENT_SLOT_START; i < EQUIPMENT_SLOT_END; ++i)
    {
        if (Item* item = player->GetItemByPos(INVENTORY_SLOT_BAG_0, i))
            _fillGearScoreData(player, item, &gearScore, twoHandScore);
    }

    if (withBags)
    {
        // check inventory
        for (int i = INVENTORY_SLOT_ITEM_START; i < INVENTORY_SLOT_ITEM_END; ++i)
        {
            if (Item* item = player->GetItemByPos(INVENTORY_SLOT_BAG_0, i))
                _fillGearScoreData(player, item, &gearScore, twoHandScore);
        }

        // check bags
        for (int i = INVENTORY_SLOT_BAG_START; i < INVENTORY_SLOT_BAG_END; ++i)
        {
            if (Bag* pBag = (Bag*)player->GetItemByPos(INVENTORY_SLOT_BAG_0, i))
            {
                for (uint32 j = 0; j < pBag->GetBagSize(); ++j)
                {
                    if (Item* item2 = pBag->GetItemByPos(j))
                        _fillGearScoreData(player, item2, &gearScore, twoHandScore);
                }
            }
        }
    }

    if (withBank)
    {
        for (uint8 i = BANK_SLOT_ITEM_START; i < BANK_SLOT_ITEM_END; ++i)
        {
            if (Item* item = player->GetItemByPos(INVENTORY_SLOT_BAG_0, i))
                _fillGearScoreData(player, item, &gearScore, twoHandScore);
        }

        for (uint8 i = BANK_SLOT_BAG_START; i < BANK_SLOT_BAG_END; ++i)
        {
            if (Item* item = player->GetItemByPos(INVENTORY_SLOT_BAG_0, i))
            {
                if (item->IsBag())
                {
                    Bag* bag = (Bag*)item;
                    for (uint8 j = 0; j < bag->GetBagSize(); ++j)
                    {
                        if (Item* item2 = bag->GetItemByPos(j))
                            _fillGearScoreData(player, item2, &gearScore, twoHandScore);
                    }
                }
            }
        }
    }

    uint8 count = EQUIPMENT_SLOT_END - 2;   // ignore body and tabard slots
    uint32 sum = 0;

    // check if 2h hand is higher level than main hand + off hand
    if (gearScore[EQUIPMENT_SLOT_MAINHAND] + gearScore[EQUIPMENT_SLOT_OFFHAND] < twoHandScore * 2)
    {
        gearScore[EQUIPMENT_SLOT_OFFHAND] = 0;  // off hand is ignored in calculations if 2h weapon has higher score
        --count;
        gearScore[EQUIPMENT_SLOT_MAINHAND] = twoHandScore;
    }

    for (uint8 i = EQUIPMENT_SLOT_START; i < EQUIPMENT_SLOT_END; ++i)
    {
       sum += gearScore[i];
    }

    if (count)
    {
        uint32 res = uint32(sum / count);
        return res;
    }
    else
        return 0;
}

uint32 PlayerbotAI::GetEquipStatsValue(Player* player)
{
    uint32 statsValue = 0;
    uint32 specId = sRandomItemMgr.GetPlayerSpecId(player);

    for (uint8 i = EQUIPMENT_SLOT_START; i < EQUIPMENT_SLOT_END; ++i)
    {
        if (Item* item = player->GetItemByPos(INVENTORY_SLOT_BAG_0, i))
            statsValue += sRandomItemMgr.GetStatWeight(item->GetProto()->ItemId, specId);
    }

    return statsValue;
}

void PlayerbotAI::_fillGearScoreData(Player *player, Item* item, std::vector<uint32>* gearScore, uint32& twoHandScore)
{
    if (!item)
        return;

    if (player->CanUseItem(item->GetProto()) != EQUIP_ERR_OK)
        return;

    uint8 type   = item->GetProto()->InventoryType;
    uint32 level = item->GetProto()->ItemLevel;

    switch (type)
    {
        case INVTYPE_2HWEAPON:
            twoHandScore = std::max(twoHandScore, level);
            break;
        case INVTYPE_WEAPON:
        case INVTYPE_WEAPONMAINHAND:
            (*gearScore)[SLOT_MAIN_HAND] = std::max((*gearScore)[SLOT_MAIN_HAND], level);
            break;
        case INVTYPE_SHIELD:
        case INVTYPE_WEAPONOFFHAND:
        case INVTYPE_HOLDABLE:
            (*gearScore)[EQUIPMENT_SLOT_OFFHAND] = std::max((*gearScore)[EQUIPMENT_SLOT_OFFHAND], level);
            break;
        case INVTYPE_THROWN:
        case INVTYPE_RANGEDRIGHT:
        case INVTYPE_RANGED:
        case INVTYPE_QUIVER:
        case INVTYPE_RELIC:
            (*gearScore)[EQUIPMENT_SLOT_RANGED] = std::max((*gearScore)[EQUIPMENT_SLOT_RANGED], level);
            break;
        case INVTYPE_HEAD:
            (*gearScore)[EQUIPMENT_SLOT_HEAD] = std::max((*gearScore)[EQUIPMENT_SLOT_HEAD], level);
            break;
        case INVTYPE_NECK:
            (*gearScore)[EQUIPMENT_SLOT_NECK] = std::max((*gearScore)[EQUIPMENT_SLOT_NECK], level);
            break;
        case INVTYPE_SHOULDERS:
            (*gearScore)[EQUIPMENT_SLOT_SHOULDERS] = std::max((*gearScore)[EQUIPMENT_SLOT_SHOULDERS], level);
            break;
        case INVTYPE_BODY:
            (*gearScore)[EQUIPMENT_SLOT_BODY] = std::max((*gearScore)[EQUIPMENT_SLOT_BODY], level);
            break;
        case INVTYPE_CHEST:
        case INVTYPE_ROBE:
            (*gearScore)[EQUIPMENT_SLOT_CHEST] = std::max((*gearScore)[EQUIPMENT_SLOT_CHEST], level);
            break;
        case INVTYPE_WAIST:
            (*gearScore)[EQUIPMENT_SLOT_WAIST] = std::max((*gearScore)[EQUIPMENT_SLOT_WAIST], level);
            break;
        case INVTYPE_LEGS:
            (*gearScore)[EQUIPMENT_SLOT_LEGS] = std::max((*gearScore)[EQUIPMENT_SLOT_LEGS], level);
            break;
        case INVTYPE_FEET:
            (*gearScore)[EQUIPMENT_SLOT_FEET] = std::max((*gearScore)[EQUIPMENT_SLOT_FEET], level);
            break;
        case INVTYPE_WRISTS:
            (*gearScore)[EQUIPMENT_SLOT_WRISTS] = std::max((*gearScore)[EQUIPMENT_SLOT_WRISTS], level);
            break;
        case INVTYPE_HANDS:
            (*gearScore)[EQUIPMENT_SLOT_HANDS] = std::max((*gearScore)[EQUIPMENT_SLOT_HANDS], level);
            break;
        // equipped gear score check uses both rings and trinkets for calculation, assume that for bags/banks it is the same
        // with keeping second highest score at second slot
        case INVTYPE_FINGER:
        {
            if ((*gearScore)[EQUIPMENT_SLOT_FINGER1] < level)
            {
                (*gearScore)[EQUIPMENT_SLOT_FINGER2] = (*gearScore)[EQUIPMENT_SLOT_FINGER1];
                (*gearScore)[EQUIPMENT_SLOT_FINGER1] = level;
            }
            else if ((*gearScore)[EQUIPMENT_SLOT_FINGER2] < level)
                (*gearScore)[EQUIPMENT_SLOT_FINGER2] = level;
            break;
        }
        case INVTYPE_TRINKET:
        {
            if ((*gearScore)[EQUIPMENT_SLOT_TRINKET1] < level)
            {
                (*gearScore)[EQUIPMENT_SLOT_TRINKET2] = (*gearScore)[EQUIPMENT_SLOT_TRINKET1];
                (*gearScore)[EQUIPMENT_SLOT_TRINKET1] = level;
            }
            else if ((*gearScore)[EQUIPMENT_SLOT_TRINKET2] < level)
                (*gearScore)[EQUIPMENT_SLOT_TRINKET2] = level;
            break;
        }
        case INVTYPE_CLOAK:
            (*gearScore)[EQUIPMENT_SLOT_BACK] = std::max((*gearScore)[EQUIPMENT_SLOT_BACK], level);
            break;
        default:
            break;
    }
}

std::string PlayerbotAI::BotStateToString(BotState state)
{
    switch (state)
    {
        case BotState::BOT_STATE_COMBAT: return "Combat";
        case BotState::BOT_STATE_NON_COMBAT: return "Non Combat";
        case BotState::BOT_STATE_DEAD: return "Dead";
        case BotState::BOT_STATE_REACTION: return "Reaction";
        default: return "";
    }
}

std::string PlayerbotAI::GetDefaultMovementStrategy()
{
    // Player master -> follow
    if (HasActivePlayerMaster())
        return "follow";

    // Bot/no master -> wander
    return "wander";
}

bool PlayerbotAI::HasActiveMovementStrategy()
{
    for (char const* strategy : ai::kMovementModeStrategies)
    {
        if (HasStrategy(strategy, BotState::BOT_STATE_NON_COMBAT) ||
            HasStrategy(strategy, BotState::BOT_STATE_REACTION))
            return true;
    }

    return false;
}

void PlayerbotAI::SetMovementStrategy(const std::string& movement)
{
    // An explicit movement order supersedes a pull's anchor hold. Dropping the
    // marker here is what lets the release paths tell our anchor stay from a
    // stay the player asked for: a player-placed stay must survive the hold's
    // release. Callers that place the hold write the marker after calling this.
    if (aiObjectContext)
        aiObjectContext->GetValue<PositionMap&>("position")->Get().erase("pull hold");

    // Keep the mature chat-shortcut semantics in one place. Follow, wander,
    // and stay are the mutually exclusive owner-controlled modes. Guard and
    // free intentionally retain their mature relationships with passive/stay.
    if (movement == "follow")
    {
        ChangeStrategy("+follow,-passive,-stay,-wander", BotState::BOT_STATE_REACTION);
        ChangeStrategy("+follow,-passive,-stay,-wander", BotState::BOT_STATE_NON_COMBAT);
        ChangeStrategy("-stay,-wander,-guard", BotState::BOT_STATE_COMBAT);
        if (HasStrategy("passive", BotState::BOT_STATE_COMBAT))
            ChangeStrategy("-passive,-follow", BotState::BOT_STATE_COMBAT);
    }
    else if (movement == "wander")
    {
        ChangeStrategy("+wander,-follow,-stay,-passive", BotState::BOT_STATE_REACTION);
        ChangeStrategy("+wander,-follow,-stay,-guard,-passive", BotState::BOT_STATE_NON_COMBAT);
        ChangeStrategy("-follow,-guard,-stay,-passive", BotState::BOT_STATE_COMBAT);
    }
    else if (movement == "stay")
    {
        ChangeStrategy("+stay,-follow,-wander,-passive", BotState::BOT_STATE_REACTION);
        ChangeStrategy("+stay,-follow,-wander,-passive", BotState::BOT_STATE_NON_COMBAT);
        ChangeStrategy("+stay,-follow,-wander,-passive", BotState::BOT_STATE_COMBAT);
    }
    else if (movement == "guard")
    {
        ChangeStrategy("+guard,-follow,-wander,-passive", BotState::BOT_STATE_REACTION);
        ChangeStrategy("+guard,-follow,-wander,-passive", BotState::BOT_STATE_NON_COMBAT);
        ChangeStrategy("+guard,-follow,-wander,-passive", BotState::BOT_STATE_COMBAT);
    }
    else if (movement == "free")
    {
        ChangeStrategy("+free,-passive", BotState::BOT_STATE_REACTION);
        ChangeStrategy("+free,-passive", BotState::BOT_STATE_NON_COMBAT);
        ChangeStrategy("+free,-passive", BotState::BOT_STATE_COMBAT);
    }
    else if (movement == "passive")
    {
        ChangeStrategy("+passive", BotState::BOT_STATE_REACTION);
        ChangeStrategy("+passive", BotState::BOT_STATE_NON_COMBAT);
        ChangeStrategy("+passive", BotState::BOT_STATE_COMBAT);
    }
}

void PlayerbotAI::EnsureDefaultMovementStrategy(Player* requester)
{
    SetMovementStrategy(GetDefaultMovementStrategy());

    if (requester)
    {
        TellPlayerNoFacing(
            requester,
            "Welcome back!",
            PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL,
            false
        );
    }
}

void PlayerbotAI::ReleasePullHold()
{
    if (!aiObjectContext)
        return;

    // "pull hold" is the marker that the stay in posMap["stay"] is the anchor a
    // pull parked this bot on. Its absence means either no hold was placed or a
    // later movement order took it over (SetMovementStrategy drops the marker),
    // so a stay the player asked for is never cleared here. The hold has exactly
    // one reason - the pull that placed it - and the caller decides that it is
    // over: the fight ended, the bot died, or the wait window was cut short.
    PositionMap& posMap = aiObjectContext->GetValue<PositionMap&>("position")->Get();
    if (!posMap["pull hold"].isSet())
        return;

    posMap.erase("pull hold");
    if (posMap["stay"].isSet())
    {
        posMap.erase("stay");
        EnsureDefaultMovementStrategy();
    }
}

void PlayerbotAI::StartPullWindow(uint8 windowSeconds, bool leadKeyed, uint8 minSeconds, bool fightStart)
{
    if (!aiObjectContext)
        return;

    // A restarted or chained window must not log the previous pull's reason.
    SetPullReleaseReason(nullptr);

    auto* waitTime = aiObjectContext->GetValue<uint8>("wait for attack time");
    uint8 const previousSeconds = waitTime->Get();

    // Only the engines without "wait for attack" get the pull's copy: one the
    // player put there (in any engine) stays theirs and outlives the window.
    // Added before the window (re)starts, so the add is not read as someone
    // else's change (SyncPullWindow).
    unsigned added = 0;
    for (uint8 i = 0; i < (uint8)BotState::BOT_STATE_ALL; i++)
    {
        BotState const state = static_cast<BotState>(i);
        if (HasStrategy("wait for attack", state))
            continue;
        ChangeStrategy("+wait for attack", state);
        added |= ai::PullJoinWindow::EngineBit(i);
    }

    waitTime->Set(windowSeconds);
    pullWindow_.Start(time(0), added, previousSeconds, leadKeyed, minSeconds, fightStart);
}

void PlayerbotAI::SetPlayerWaitForAttackTime(uint8 seconds)
{
    if (pullWindow_.DeferPlayerWaitSeconds(seconds) || !aiObjectContext)
        return;
    aiObjectContext->GetValue<uint8>("wait for attack time")->Set(seconds);
}

void PlayerbotAI::SyncPullWindow(BotState type)
{
    if (!pullWindow_.Running())
        return;

    unsigned touched = 0;
    if (type == BotState::BOT_STATE_ALL)
    {
        for (uint8 i = 0; i < (uint8)BotState::BOT_STATE_ALL; i++)
            touched |= ai::PullJoinWindow::EngineBit(i);
    }
    else
        touched = ai::PullJoinWindow::EngineBit(static_cast<unsigned>(type));
    pullWindow_.Forget(touched);

    // The combat engine's copy is the gate itself: once someone removed it
    // (an attack or flee order, a player's `co -wait for attack` or toggle),
    // the hold is over. The player's delay goes back as the window ends.
    if (!HasStrategy("wait for attack", BotState::BOT_STATE_COMBAT))
        EndPullWindow("order");
}

void PlayerbotAI::EndPullWindow(const char* reason)
{
    bool const running = pullWindow_.Running();
    ai::PullJoinWindow const window = pullWindow_;
    // Cleared first, so the removals below are not read as someone else's
    // change to the strategy (SyncPullWindow).
    pullWindow_.Clear();
    // The single release line: only a running window logs, exactly once, and
    // the stored reason dies with it so two consecutive windows never share
    // one. The trigger names the rule (tank-first/hit/tank-low/tank-dead/
    // cap/expired); direct callers name the path (order/reset/reload).
    if (running && bot)
    {
        std::string const stored = PullReleaseReason();
        std::string const logged = reason && *reason ? reason : (stored.empty() ? "order" : stored);
        sLog.outBasic("[BOT] pull-release %s reason=%s kind=%s elapsed=%u", bot->GetName(), logged.c_str(),
            window.fightStart ? "fight-start" : (window.leadKeyed ? "pull" : "chat"), window.start ? uint32(time(0) - window.start) : 0);
    }
    SetPullReleaseReason(nullptr);

    for (uint8 i = 0; i < (uint8)BotState::BOT_STATE_ALL; i++)
    {
        BotState const state = static_cast<BotState>(i);
        if (window.Added(i) && HasStrategy("wait for attack", state))
            ChangeStrategy("-wait for attack", state);
    }

    // The pull's delay never replaces the player's own setting.
    if (window.Running() && aiObjectContext)
        aiObjectContext->GetValue<uint8>("wait for attack time")->Set(static_cast<uint8>(window.savedWaitSeconds));

    // The anchor goes with the window; a no-op when it is already released
    // (combat end, death, a movement order) or when none was placed.
    ReleasePullHold();
    // Member-owned pet stance: a pet the hold parked passive comes back even
    // when the tank's pull-end loop can no longer reach this bot (left the
    // group, logged out, died). Every path that outlives the window funnels
    // through here (expiry, strategy reset, reload).
    RestorePullHoldPet();
}

void PlayerbotAI::ParkPetForPullHold()
{
    if (!bot)
        return;
    Pet* pet = bot->GetPet();
    if (!pet || pet->GetReactState() == REACT_PASSIVE)
        return;
    // Save once: a re-hold while already parked must not overwrite the
    // pre-hold stance with REACT_PASSIVE.
    if (pullHoldPetStance_ == kNoSavedStance)
        pullHoldPetStance_ = static_cast<int>(pet->GetReactState());
    pet->SetReactState(REACT_PASSIVE);
}

void PlayerbotAI::RestorePullHoldPet()
{
    if (pullHoldPetStance_ == kNoSavedStance)
        return;
    ReactStates stance = static_cast<ReactStates>(pullHoldPetStance_);
    pullHoldPetStance_ = kNoSavedStance;
    if (!bot)
        return;
    Pet* pet = bot->GetPet();
    if (!pet)
        return;
    // Only undo our own park: a stance the player (or another order) set
    // meanwhile is theirs, not the hold's.
    if (pet->GetReactState() != REACT_PASSIVE)
        return;
    pet->SetReactState(stance);
}

size_t PlayerbotAI::DropQueuedMovementOrders()
{
    size_t const dropped = TortoiseBots::DropQueuedMovementOrders(chatCommands,
        [](ChatCommandHolder& holder) { return holder.GetCommand(); });
    // HandleCommands may already have turned a queued order into a pending
    // trigger that the engine has not processed yet: clear that too.
    if (aiObjectContext)
    {
        size_t count = 0;
        char const* const* orders = TortoiseBots::MovementChatOrders(count);
        for (size_t i = 0; i < count; ++i)
            if (Trigger* trigger = aiObjectContext->GetTrigger(orders[i]))
                trigger->Reset();
    }
    return dropped;
}

void PlayerbotAI::EndStayForOrder()
{
    // A flee parks the bot in passive follow; an order that sends it at a
    // target is the owner calling it back into the fight, so the passive
    // hold ends here too. Dropped first: "passive" counts as a movement
    // mode for the check below, and a fleeing bot given no mode back would
    // otherwise keep it through the fight (the PassiveMultiplier vetoes its
    // whole rotation) and stand wherever the fight ends afterwards.
    ChangeStrategy("-passive", BotState::BOT_STATE_NON_COMBAT);
    ChangeStrategy("-passive", BotState::BOT_STATE_COMBAT);
    // Stay is kept in all three engines by the central movement setter;
    // remove its reaction copy as well or it can survive the order and be
    // mistaken for an active hold during the next transition.
    ChangeStrategy("-stay", BotState::BOT_STATE_REACTION);
    ChangeStrategy("-stay", BotState::BOT_STATE_NON_COMBAT);
    // The broken stay was the bot's only movement mode: give it the default
    // back or it stands wherever the fight ends until the player whispers
    // "follow". Added directly rather than through SetMovementStrategy, which
    // would drop the "pull hold" marker the pull release still reads.
    if (!HasActiveMovementStrategy())
    {
        std::string const movement = "+" + GetDefaultMovementStrategy();
        ChangeStrategy(movement, BotState::BOT_STATE_REACTION);
        ChangeStrategy(movement, BotState::BOT_STATE_NON_COMBAT);
    }
    ChangeStrategy("-stay,-follow", BotState::BOT_STATE_COMBAT);
}

std::string PlayerbotAI::HandleRemoteCommand(std::string command)
{
    if (command == "state")
    {
        switch (currentState)
        {
        case BotState::BOT_STATE_COMBAT:
            return "combat";
        case BotState::BOT_STATE_DEAD:
            return "dead";
        case BotState::BOT_STATE_NON_COMBAT:
            return "non-combat";
        default:
            return "unknown";
        }
    }
    else if (command == "position")
    {
        std::ostringstream out; out << bot->getPositionX() << " " << bot->getPositionY() << " " << bot->getPositionZ() << " " << bot->GetMapId() << " " << bot->getOrientation();
        uint32 area = sServerFacade.GetAreaId(bot);
        if (const AreaTableEntry* areaEntry = GetAreaEntryByAreaID(area))
        {
            if (AreaTableEntry const* zoneEntry = areaEntry->ZoneId ? GetAreaEntryByAreaID(areaEntry->ZoneId) : areaEntry)
                out << " |" << zoneEntry->Name << "|";
        }
        return out.str();
    }
    else if (command == "tpos")
    {
        Unit* target = *GetAiObjectContext()->GetValue<Unit*>("current target");
        if (!target) {
            return "";
        }

        std::ostringstream out; out << target->getPositionX() << " " << target->getPositionY() << " " << target->getPositionZ() << " " << target->GetMapId() << " " << target->getOrientation();
        return out.str();
    }
    else if (command == "target")
    {
        Unit* target = *GetAiObjectContext()->GetValue<Unit*>("current target");
        if (!target) {
            return "";
        }

        return target->GetName();
    }
    else if (command == "hp")
    {
        int pct = (int)((static_cast<float> (bot->GetHealth()) / bot->GetMaxHealth()) * 100);
        std::ostringstream out; out << pct << "%";

        Unit* target = *GetAiObjectContext()->GetValue<Unit*>("current target");
        if (!target) {
            return out.str();
        }

        pct = (int)((static_cast<float> (target->GetHealth()) / target->GetMaxHealth()) * 100);
        out << " / " << pct << "%";
        return out.str();
    }
    else if (command == "combat")
    {
        std::ostringstream out;

        bool unitFlagInCombat = bot->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_IN_COMBAT);
        out << "UNIT_FLAG_IN_COMBAT: " << (unitFlagInCombat ? "SET" : "clear");

        out << ", IsInCombat(): " << (bot->IsInCombat() ? "true" : "false");

        out << ", CMaNGOS attackers: " << bot->GetAttackers().size();

        Unit* victim = bot->GetVictim();
        out << ", victim: " << (victim ? victim->GetName() : "none");

        out << " | BotAI: current target: ";
        Unit* aiTarget = *GetAiObjectContext()->GetValue<Unit*>("current target");
        if (aiTarget)
        {
            out << aiTarget->GetName() << " (" << aiTarget->getObjectGuid().GetCounter() << ")";
            bool isInvalid = GetAiObjectContext()->GetValue<bool>("invalid target", "current target")->Get();
            out << ", invalid: " << (isInvalid ? "YES" : "no");
        }
        else
        {
            out << "none";
        }

        bool hasAttackers = GetAiObjectContext()->GetValue<bool>("has attackers")->Get();
        out << ", has attackers: " << (hasAttackers ? "true" : "false");

        out << " | Selection: " << bot->GetSelectionGuid().GetCounter();

        return out.str();
    }
    else if (command == "strategy")
    {
        return currentEngine->ListStrategies();
    }
    else if (command == "action")
    {
        return currentEngine->GetLastAction();
    }
    else if (command == "values")
    {
        return GetAiObjectContext()->FormatValues();
    }
    else if (command == "travel")
    {
        std::ostringstream out;

        TravelTarget* target = GetAiObjectContext()->GetValue<TravelTarget*>("travel target")->Get();

        if (target->GetGroupmember() && target->GetGroupmember().GetPlayer())
            out << target->GetGroupmember().GetPlayer()->GetName() << "'s ";

        if (target->GetDestination()) {
            out << "Target: " << target->GetDestination()->GetTitle();
        }

        if (target->GetStatus() != TravelStatus::TRAVEL_STATUS_NONE)
        {
            out << "\nStatus: ";
            if (target->GetStatus() == TravelStatus::TRAVEL_STATUS_READY)
                out << "ready";
            else if (target->GetStatus() == TravelStatus::TRAVEL_STATUS_PREPARE)
                out << "preparing";
            else if (target->GetStatus() == TravelStatus::TRAVEL_STATUS_TRAVEL)
                out << (target->IsForced() ? "forced traveling" : "traveling");
            else if (target->GetStatus() == TravelStatus::TRAVEL_STATUS_WORK)
                out << "working";
            else if (target->GetStatus() == TravelStatus::TRAVEL_STATUS_COOLDOWN)
                out << "cooldown";
            else if (target->GetStatus() == TravelStatus::TRAVEL_STATUS_EXPIRED)
                out << "expired";

            if (target->GetStatus() != TravelStatus::TRAVEL_STATUS_EXPIRED)
                out << " [for " << (target->GetTimeLeft() / 1000) << "s]";

            if (target->GetRetryCount(true) || target->GetRetryCount(false))
                out << "(retry " << target->GetRetryCount(true) << "/" << target->GetRetryCount(false) << ")";
        }

        return out.str();
    }
    else if (command == "traveldetail")
    {
        std::ostringstream out;

        TravelTarget* target = GetAiObjectContext()->GetValue<TravelTarget*>("travel target")->Get();

        if (target->GetGroupmember() && target->GetGroupmember().GetPlayer())
            out << target->GetGroupmember().GetPlayer()->GetName() << "'s ";

        if (target->GetDestination()) {
            out << target->GetDestination()->GetShortName() << " travel target";

            out << "\nTarget: " << target->GetDestination()->GetTitle();

            out << "\nDistance " << round(target->GetDestination()->DistanceTo(bot)) << "y";

            if (*target->getPosition())
            {
                out << "\nLocation: " << target->getPosition()->GetAreaName();
                out << " (" << round(target->getPosition()->distance(bot)) << "y)";
            }
        }

        if (target->GetStatus() != TravelStatus::TRAVEL_STATUS_NONE)
        {
            out << "\nStatus: ";
            if (target->GetStatus() == TravelStatus::TRAVEL_STATUS_READY)
                out << "ready";
            else if (target->GetStatus() == TravelStatus::TRAVEL_STATUS_PREPARE)
                out << "preparing";
            else if (target->GetStatus() == TravelStatus::TRAVEL_STATUS_TRAVEL)
                out << (target->IsForced() ? "forced traveling" : "traveling");
            else if (target->GetStatus() == TravelStatus::TRAVEL_STATUS_WORK)
                out << "working";
            else if (target->GetStatus() == TravelStatus::TRAVEL_STATUS_COOLDOWN)
                out << "cooldown";
            else if (target->GetStatus() == TravelStatus::TRAVEL_STATUS_EXPIRED)
                out << "expired";

            if (target->GetStatus() != TravelStatus::TRAVEL_STATUS_EXPIRED)
                out << " [for " << (target->GetTimeLeft() / 1000) << "s]";

            if (target->GetRetryCount(true) || target->GetRetryCount(false))
                out << "(retry " << target->GetRetryCount(true) << "/" << target->GetRetryCount(false) << ")";

            if (target->GetConditions().size())
            {
                out << "\nConditions: ";

                for (auto& condition : target->GetConditions())
                {
                    AiObjectContext* context = GetAiObjectContext();
                    out << condition;
                    if (AI_VALUE(bool, condition))
                        out << " (true)";
                    else
                        out << " (false)";

                    if (condition != target->GetConditions().back())
                        out << ", ";
                }
            }
        }

        return out.str();
    }
    else if (command.find("budget") == 0)
    {
        std::string sub;

        if (command.size() > 7)
            sub = command.substr(7);

        std::ostringstream out;

        AiObjectContext* context = GetAiObjectContext();

        if (sub.empty())
        {
            out << "Current money: " << ChatHelper::formatMoney(bot->GetMoney()) << " free to use:" << ChatHelper::formatMoney(AI_VALUE2(uint32, "free money for", (uint32)NeedMoneyFor::anything)) << "\n";
            out << "Purpose: Needed | Available\n";
        }

        for (uint32 i = 1; i < (uint32)NeedMoneyFor::anything; i++)
        {
            NeedMoneyFor needMoneyFor = NeedMoneyFor(i);

            std::unordered_map<NeedMoneyFor, std::string> needStrMap =
            { { NeedMoneyFor::none, "nothing"},
              { NeedMoneyFor::repair, "repair"},
              { NeedMoneyFor::ammo, "ammo"},
              { NeedMoneyFor::spells, "spells"},
              { NeedMoneyFor::travel, "travel"},
              { NeedMoneyFor::consumables, "consumables"},
              { NeedMoneyFor::gear, "gear"},
              { NeedMoneyFor::guild, "guild"},
              { NeedMoneyFor::tradeskill, "tradeskill"},
              { NeedMoneyFor::skilltraining, "skilltraining"},
              { NeedMoneyFor::ah, "ah"},
              { NeedMoneyFor::mount, "mount"},
              { NeedMoneyFor::anything, "anything"}};

            if (!sub.empty() && needStrMap.at(needMoneyFor).find(sub) == std::string::npos)
                continue;

            out <<  needStrMap.at(needMoneyFor);

            out << ": " << ChatHelper::formatMoney(AI_VALUE2(uint32, "money needed for", i)) << " | " << ChatHelper::formatMoney(AI_VALUE2(uint32, "free money for", i));

            if (i != (uint32)NeedMoneyFor::mount)
                out << "\n";
        }

        return out.str();
    }
    std::ostringstream out; out << "invalid command: " << command;
    return out.str();
}

bool PlayerbotAI::HasSkill(SkillType skill)
{
    return bot->HasSkill(skill) && bot->GetSkillValue(skill) > 0;
}

float PlayerbotAI::GetRange(std::string type)
{
    float val = 0;

    if (type == "follow" && bot->GetGroup() && bot->GetGroup()->isRaidGroup())
        type = "followraid";

    // In dungeons and raids the follow leash stays tight (raid distance)
    // so bots trail the master instead of cutting corners into side packs.
    if (type == "follow" && bot->GetMap() && (bot->GetMap()->IsDungeon() || bot->GetMap()->IsRaid()))
        type = "followraid";

    if (aiObjectContext) val = aiObjectContext->GetValue<float>("range", type)->Get();
    if (abs(val) >= 0.1f) return val;

    if (type == "spell") return sPlayerbotAIConfig.spellDistance;
    if (type == "shoot") return sPlayerbotAIConfig.shootDistance;
    if (type == "flee") return sPlayerbotAIConfig.fleeDistance;
    if (type == "heal") return sPlayerbotAIConfig.healDistance;
    if (type == "follow") return sPlayerbotAIConfig.followDistance;
    if (type == "guard") return sPlayerbotAIConfig.sightDistance;
    if (type == "followraid") return sPlayerbotAIConfig.raidFollowDistance;
    if (type == "wandermin") return sPlayerbotAIConfig.wanderMinDistance;
    if (type == "wandermax") return sPlayerbotAIConfig.wanderMaxDistance;
    if (type == "attack") return 0;
    return 0;
}

//Copy from reputation GetFactionReaction
ReputationRank PlayerbotAI::GetFactionReaction(FactionTemplateEntry const* thisTemplate, FactionTemplateEntry const* otherTemplate)
{
    MANGOS_ASSERT(thisTemplate)
        MANGOS_ASSERT(otherTemplate)

        // Original logic begins

        if (otherTemplate->ourMask & thisTemplate->hostileMask)
            return REP_HOSTILE;

    if (thisTemplate->enemyFaction[0] && otherTemplate->faction)
    {
        for (unsigned int i : thisTemplate->enemyFaction)
        {
            if (i == otherTemplate->faction)
                return REP_HOSTILE;
        }
    }

    if (otherTemplate->ourMask & thisTemplate->friendlyMask)
        return REP_FRIENDLY;

    if (thisTemplate->friendFaction[0] && otherTemplate->faction)
    {
        for (unsigned int i : thisTemplate->friendFaction)
        {
            if (i == otherTemplate->faction)
                return REP_FRIENDLY;
        }
    }

    if (thisTemplate->ourMask & otherTemplate->friendlyMask)
        return REP_FRIENDLY;

    if (otherTemplate->friendFaction[0] && thisTemplate->faction)
    {
        for (unsigned int i : otherTemplate->friendFaction)
        {
            if (i == thisTemplate->faction)
                return REP_FRIENDLY;
        }
    }
    return REP_NEUTRAL;
}

bool PlayerbotAI::AddAura(Unit* unit, uint32 spellId)
{
    // number or [name] Shift-click form |color|Hspell:spell_id|h[name]|h|r or Htalent form

    SpellEntry const* spellInfo = sSpellTemplate.LookupEntry<SpellEntry>(spellId);
    if (!spellInfo)
        return false;

    if (!IsSpellAppliesAura(spellInfo, (1 << EFFECT_INDEX_0) | (1 << EFFECT_INDEX_1) | (1 << EFFECT_INDEX_2)) &&
        !IsSpellHaveEffect(spellInfo, SPELL_EFFECT_PERSISTENT_AREA_AURA))
    {
        return false;
    }

    SpellAuraHolder* holder = CreateSpellAuraHolder(spellInfo, unit, unit, unit);

    for (uint32 i = 0; i < MAX_EFFECT_INDEX; ++i)
    {
        uint8 eff = spellInfo->Effect[i];
        if (eff >= MAX_SPELL_EFFECTS)
            continue;
        if (IsAreaAuraEffect(eff) ||
            eff == SPELL_EFFECT_APPLY_AURA ||
            eff == SPELL_EFFECT_PERSISTENT_AREA_AURA)
        {
            int32 basePoints = spellInfo->CalculateSimpleValue(SpellEffectIndex(i));
            int32 damage = basePoints;
            Aura* aur = CreateAura(spellInfo, SpellEffectIndex(i), &damage, holder, unit);
            holder->AddAura(aur, SpellEffectIndex(i));
        }
    }
    if (!unit->AddSpellAuraHolder(holder))
        holder = nullptr;

    return true;
}

std::list<Unit*> PlayerbotAI::GetAllHostileUnitsAroundWO(WorldObject* wo, float distanceAround)
{
    std::list<Unit*> hostileUnits;
    // Search around the world object `wo` (e.g. a lootable corpse) but judge hostility from
    // the BOT's perspective. The 2-arg ctor used the object itself as the friendly-reference,
    // so for a creature corpse it asked "is the dead mob friendly to u?" (flagging neutral
    // critters and even friendly guards as hostile), and for a non-Unit object (GameObject)
    // it set the reference to nullptr and crashed. Passing `bot` fixes both.
    MaNGOS::AnyUnfriendlyUnitInObjectRangeCheck u_check(wo, bot, distanceAround);
    MaNGOS::UnitListSearcher<MaNGOS::AnyUnfriendlyUnitInObjectRangeCheck> searcher(hostileUnits, u_check);
    Cell::VisitAllObjects(wo, searcher, distanceAround);

    // AnyUnfriendlyUnitInObjectRangeCheck uses !IsFriendlyTo (reaction < REP_FRIENDLY), which
    // includes neutral critters and wolves. Strip them out — only units the bot is truly hostile
    // to (reaction <= REP_HOSTILE) are real threats that should block looting.
    hostileUnits.remove_if([this](Unit* u) { return !bot->IsHostileTo(u); });

    return hostileUnits;
}

std::list<Unit*> PlayerbotAI::GetAllHostileNPCNonPetUnitsAroundWO(WorldObject* wo, float distanceAround)
{
    std::list<Unit*> hostileUnitsNonPlayers;
    for (auto hostileUnit : GetAllHostileUnitsAroundWO(wo, distanceAround))
    {
        if (!hostileUnit->IsPlayer() && !hostileUnit->IsCorpse())
        {
            if (hostileUnit->IsCreature())
            {
                Creature* creature = GetCreature(hostileUnit->getObjectGuid());
                if (!creature || (creature && creature->IsPet()))
                {
                    continue;
                }
            }

            hostileUnitsNonPlayers.push_back(hostileUnit);
        }
    }

    return hostileUnitsNonPlayers;
}

bool PlayerbotAI::CanMove()
{
    if (sServerFacade.IsFrozen(bot))
    {
        return false;
    }
    if (sServerFacade.IsInRoots(bot))
    {
        return false;
    }
    if (sServerFacade.IsFeared(bot))
    {
        return false;
    }
    if (sServerFacade.IsCharmed(bot))
    {
        return false;
    }
    if (bot->HasUnitState(UNIT_STAT_STUNNED))
    {
        return false;
    }
    if (bot->HasAuraType(SPELL_AURA_SPIRIT_OF_REDEMPTION))
    {
        return false;
    }
    if (bot->IsPolymorphed())
    {
        return false;
    }
    if (bot->IsTaxiFlying())
    {
        return false;
    }
    if (sServerFacade.UnitIsDead(bot) && !bot->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_GHOST))
    {
        return false;
    }
    if (bot->IsBeingTeleported())
    {
        return false;
    }
    if (bot->HasUnitState(UNIT_STAT_CAN_NOT_REACT_OR_LOST_CONTROL))
    {
        return false;
    }
    if (IsJumping())
    {
        return false;
    }
    if (bot->IsFalling())
    {
        return false;
    }

    MotionMaster& mm = *bot->GetMotionMaster();
    MovementGeneratorType currentMotion = mm.GetCurrentMovementGeneratorType();

#ifdef CMANGOS
    if (currentMotion == TAXI_MOTION_TYPE)
    {
        return false;
    }
#endif
#ifdef MANGOS
    if (currentMotion == FLIGHT_MOTION_TYPE)
    {
        return false;
    }
#endif
    return true;
}

bool PlayerbotAI::StopForObjectCast()
{
    if (!sServerFacade.isMoving(bot) && bot->movespline->Finalized())
        return false;
    StopMoving();
    return true;
}

void PlayerbotAI::StopMoving()
{
    if (bot->IsTaxiFlying())
        return;

    if (bot->IsBeingTeleportedFar())
        return;

    if (bot->GetTransport())
        bot->m_movementInfo.SetMovementFlags(MOVEFLAG_ONTRANSPORT);
    else
        bot->m_movementInfo.SetMovementFlags(MOVEFLAG_NONE);

    bot->StopMoving();
    MovementInfo mInfo = bot->m_movementInfo;
    float x, y, z;
    bot->GetPosition(x, y, z);
    float o = bot->GetOrientation();
    mInfo.ChangePosition(x, y, z, o);
    WorldPacket data(MSG_MOVE_STOP);
    data << mInfo;
    bot->GetSession()->HandleMovementOpcodes(data);

    if (bot->GetMotionMaster()->GetCurrentMovementGeneratorType())
    {
        bot->GetMotionMaster()->Clear(false, true);
        bot->GetMotionMaster()->MoveIdle();
    }
}

bool PlayerbotAI::IsInRealGuild()
{
    if (!bot->GetGuildId())
        return false;

    Guild* guild = sGuildMgr.GetGuildById(bot->GetGuildId());
    if (!guild)
        return false;

    uint32 leaderAccount = sObjectMgr.GetPlayerAccountIdByGUID(guild->GetLeaderGuid());
    if (!leaderAccount)
        return false;

    return !sPlayerbotAIConfig.IsInRandomAccountList(leaderAccount);
}

bool PlayerbotAI::PlayAttackEmote(float chanceMultiplier)
{
    auto group = bot->GetGroup();
    if (group) chanceMultiplier /= (group->GetMembersCount() - 1);
    if ((float)urand(0, 10000) * chanceMultiplier < sPlayerbotAIConfig.attackEmoteChance * 10000.0f && bot->IsInCombat())
    {
        std::vector<uint32> sounds;
        sounds.push_back(TEXTEMOTE_OPENFIRE);
        sounds.push_back(305);
        sounds.push_back(307);
        PlaySound(sounds[urand(0, sounds.size() - 1)]);
        return true;
    }

    return false;
}

void PlayerbotAI::QueuePacket(WorldPacket& pkt)
{
    // Penqle WorldPacket has deleted copy-assign; copy-construct + move-assign.
    std::unique_ptr<WorldPacket> packet(new WorldPacket(pkt));
    bot->GetSession()->QueuePacket(packet.release());
}

float PlayerbotAI::GetLevelFloat() const
{
    float level = bot->GetLevel();

    uint32 nextLevelXp = bot->GetUInt32Value(PLAYER_NEXT_LEVEL_XP);

    if (!nextLevelXp)
        return level;

    uint32 xp = bot->GetUInt32Value(PLAYER_XP);

    while (xp > nextLevelXp)
    {
        level++;
        xp -= nextLevelXp;

        nextLevelXp = sObjectMgr.GetXPForLevel(level);
    }

    level += float(xp) / float(nextLevelXp);

    return level;
}
