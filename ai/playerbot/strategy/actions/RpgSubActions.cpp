
#include "playerbot/playerbot.h"
#include "playerbot/TaxiRouteGuard.h"
#include "RpgSubActions.h"
#include "ChooseRpgTargetAction.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/strategy/values/PossibleRpgTargetsValue.h"
#include "playerbot/strategy/values/Formations.h"
#include "playerbot/strategy/values/SharedValueContext.h"
#include "EmoteAction.h"
#include "GossipDef.h"
#include "GuildCreateActions.h"
#include "SocialMgr.h"
#include "playerbot/TravelMgr.h"
#include "playerbot/RandomBotFacade.h"
#include "playerbot/strategy/values/MaintenanceValues.h"


using namespace ai;

void RpgHelper::BeforeExecute()
{
    bot->SetSelectionGuid(guidP());

    setFacingTo(guidP());

    setFacing(guidP());
}

void RpgHelper::AfterExecute(bool doDelay, bool waitForGroup, std::string nextAction)
{
    uint32 goToDifferentTargetChance = 50;
    if (ai->HasRealPlayerMaster() || bot->GetGroup())
        goToDifferentTargetChance = 30;

    if (nextAction == "rpg" && urand(0, 100) < goToDifferentTargetChance)
        nextAction = "rpg cancel";

    SET_AI_VALUE(std::string, "next rpg action", nextAction);

    if(doDelay)
        setDelay(waitForGroup);
}

void RpgHelper::setFacingTo(GuidPosition guidPosition)
{
    if (!sPlayerbotAIConfig.turnInRpg)
        return;

    //sServerFacade.SetFacingTo(bot, guidPosition.GetWorldObject());
    MotionMaster& mm = *bot->GetMotionMaster();
    bot->SetFacingTo(bot->GetAngle(guidPosition.GetWorldObject(bot->GetInstanceId())));
    bot->m_movementInfo.RemoveMovementFlag(MovementFlags(MOVEFLAG_SPLINE_ENABLED | MOVEFLAG_FORWARD));
}

void RpgHelper::setFacing(GuidPosition guidPosition)
{
    if (!sPlayerbotAIConfig.turnInRpg)
        return;

    if (!guidPosition.IsUnit())
        return;

    if (guidPosition.IsPlayer())
        return;


    Unit* unit = guidPosition.GetUnit(bot->GetInstanceId());

    if (unit->IsMoving())
        return;

    MotionMaster& mm = *unit->GetMotionMaster();
    unit->SetFacingTo(unit->GetAngle(bot));
    unit->m_movementInfo.RemoveMovementFlag(MovementFlags(MOVEFLAG_SPLINE_ENABLED | MOVEFLAG_FORWARD));
}

void RpgHelper::resetFacing(GuidPosition guidPosition)
{
    if (!sPlayerbotAIConfig.turnInRpg)
        return;

    if (!guidPosition.IsCreature())
        return;

    Creature* unit = guidPosition.GetCreature(bot->GetInstanceId());

    if (unit->IsMoving())
        return;

    CreatureData const* data = guidPosition.GetCreatureData();

    if (data)
    {
        unit->SetFacingTo(data->position.o);
    }
}

void RpgHelper::setDelay(bool waitForGroup)
{
    // A pool bot (no master, no group) that still holds an active travel target is
    // only idling between the steps of an errand it has not finished: give it the
    // short pause so it goes back to its destination instead of standing at an NPC
    // for the full rpgDelay per step. Live rosters had 100+ bots parked in
    // "rpg gossip talk" for minutes with an unfinished travel target, i.e. standing
    // with a destination and getting nowhere - what the observability layer reports
    // as "stalled". A pool bot with no travel target at all takes the short pause
    // too: with nothing to walk, a new errand (grind above all) should win the
    // next tick, not ten seconds of gossip per step (issue #393). Only a pool bot
    // parked at an NPC it actually needs - a rewardable hand-in, an acceptable
    // quest, a needed vendor or trainer sale/learn - keeps the full roleplay pause.
    bool const poolSolo = !ai->HasRealPlayerMaster() && !bot->GetGroup() && sRandomBotFacade.IsFreeBot(bot);
    if (poolSolo && !NearbyServiceTarget(ai))
    {
        ai->SetActionDuration(sPlayerbotAIConfig.rpgDelay / 5);
        return;
    }

    bool const hasPendingTravel = AI_VALUE(TravelTarget*, "travel target")->IsActive();

    if ((!ai->HasRealPlayerMaster() && !bot->GetGroup() && !hasPendingTravel) || (bot->GetGroup() && bot->GetGroup()->IsLeader(bot->getObjectGuid()) && waitForGroup))
        ai->SetActionDuration(sPlayerbotAIConfig.rpgDelay);
    else
        ai->SetActionDuration(sPlayerbotAIConfig.rpgDelay / 5);
}

bool RpgEmoteAction::Execute(Event& event)
{
    rpg->BeforeExecute();

    Unit* unit = rpg->guidP().GetUnit(bot->GetInstanceId());

    uint32 type;

    if (unit && unit->GetEntry() == 620)
    {
        type = TEXTEMOTE_CHICKEN;
        WorldPacket data(SMSG_TEXT_EMOTE);
        data << type;
        data << 1;
        data << rpg->guidP();
        bot->GetSession()->HandleTextEmoteOpcode(data);
    }
    else
        type = TalkAction::GetRandomEmote(rpg->guidP().GetUnit(bot->GetInstanceId()));

    WorldPacket p1;
    p1 << rpg->guid();
    bot->GetSession()->HandleGossipHelloOpcode(p1);

    bot->HandleEmoteCommand(type);

    if (type != TEXTEMOTE_CHICKEN)
        rpg->AfterExecute();
    else if(unit && !bot->GetNPCIfCanInteractWith(rpg->guidP(), UNIT_NPC_FLAG_QUESTGIVER) && AI_VALUE(TravelTarget*, "travel target")->GetEntry() == 620)
        rpg->AfterExecute(true,false, "rpg emote");

    DoDelay();

    return true;
}

bool RpgCancelAction::Execute(Event& event)
{
    rpg->OnCancel();

    if (!urand(0,3) || AI_VALUE(GuidPosition, "rpg target").GetEntry() != AI_VALUE(TravelTarget*, "travel target")->GetEntry() || AI_VALUE(TravelTarget*, "travel target")->GetStatus() != TravelStatus::TRAVEL_STATUS_WORK) //1 out of 4 to ignore current travel target after cancel.
        AI_VALUE(std::set<ObjectGuid>&, "ignore rpg target").insert(AI_VALUE(GuidPosition, "rpg target"));

    RESET_AI_VALUE(GuidPosition, "rpg target"); rpg->AfterExecute(false, false, ""); DoDelay();

    return true;
};

bool RpgTaxiAction::Execute(Event& event)
{
    rpg->BeforeExecute();

    GuidPosition guidP = rpg->guidP();

    ai->Unmount();

    uint32 node = sObjectMgr.GetNearestTaxiNode(guidP.getX(), guidP.getY(), guidP.getZ(), guidP.GetMapId(), bot->GetTeam());

    std::vector<uint32> nodes;
    for (uint32 i = 0; i < sTaxiPathStore.GetNumRows(); ++i)
    {
        TaxiPathEntry const* entry = sTaxiPathStore.LookupEntry(i);
        if (entry && entry->from == node && (bot->GetTaxi().IsTaximaskNodeKnown(entry->to) || bot->IsTaxiCheater()))
        {
            // Only destinations usable by the bot's own faction. Previously
            // the sole check was whether the flight point is KNOWN - but with
            // "AllFlightPaths = 1" (or isTaxiCheater) every node counts as
            // known, including the enemy's. The randomly drawn destination
            // therefore flew bots straight into enemy towns, where they landed
            // at the flight master (observed live: Alliance bots in The
            // Crossroads and Orgrimmar, Horde bots in Stormwind).
            // A flight point is only usable by a faction if it has a mount
            // entry for it - the same check the core does in
            // ObjectMgr::GetNearestTaxiNode.
            TaxiNodesEntry const* toNode = sObjectMgr.GetTaxiNodeEntry(entry->to);
            if (!toNode || !toNode->MountCreatureID[bot->GetTeam() == ALLIANCE ? 1 : 0])
                continue;

            nodes.push_back(i);
        }
    }

    if (nodes.empty())
    {
        sLog.outError("Bot %s - No flight paths available", bot->GetName());
        return false;
    }

    uint32 path = nodes[urand(0, nodes.size() - 1)];
    uint32 money = bot->GetMoney();
    bot->SetMoney(money + 100000);

    TaxiPathEntry const* entry = sTaxiPathStore.LookupEntry(path);
    if (!entry)
    {
        bot->SetMoney(money);
        return false;
    }

    TaxiNodesEntry const* nodeFrom = sTaxiNodesStore.LookupEntry(entry->from);
    TaxiNodesEntry const* nodeTo = sTaxiNodesStore.LookupEntry(entry->to);
    if (!nodeFrom || !nodeTo)
    {
        bot->SetMoney(money);
        return false;
    }

    Creature* flightMaster = bot->GetNPCIfCanInteractWith(guidP, UNIT_NPC_FLAG_FLIGHTMASTER);
    if (!flightMaster)
    {
        sLog.outError("Bot %s cannot talk to flightmaster (%zu location available)", bot->GetName(), nodes.size());
        bot->SetMoney(money);
        return false;
    }
    if (!ai::BotTaxiRouteAllowed({ entry->from, entry->to }) || !bot->ActivateTaxiPathTo({ entry->from, entry->to }, flightMaster, 0))
    {
        sLog.outError("Bot %s cannot fly %u (%zu location available)", bot->GetName(), path, nodes.size());
        bot->SetMoney(money);
        return false;
    }
    ai::BotInstantFlightFinish(bot, { entry->from, entry->to });

    sLog.outDetail("Bot #%d <%s> is flying from %s to %s (%zu location available)", bot->GetGUIDLow(), bot->GetName(), nodeFrom->name[LOCALE_enUS], nodeTo->name[LOCALE_enUS], nodes.size());
    bot->SetMoney(money);

    rpg->AfterExecute();

    DoDelay();

    return true;
}

bool RpgDiscoverAction::Execute(Event& event)
{
    rpg->BeforeExecute();

    GuidPosition guidP = rpg->guidP();

    uint32 node = sObjectMgr.GetNearestTaxiNode(guidP.getX(), guidP.getY(), guidP.getZ(), guidP.GetMapId(), bot->GetTeam());

    if (!node)
        return false;

    Creature* flightMaster = bot->GetNPCIfCanInteractWith(guidP, UNIT_NPC_FLAG_FLIGHTMASTER);

    if (!flightMaster)
        return false;

    rpg->AfterExecute(true, true);

    DoDelay();

    return bot->GetSession()->SendLearnNewTaxiNode(flightMaster);
}

bool RpgHealAction::Execute(Event& event)
{
    bool retVal = false;

    rpg->BeforeExecute();

    switch (bot->GetClass())
    {
    case CLASS_PRIEST:
        retVal = ai->DoSpecificAction("lesser heal on party", Event(), true);
        break;
    case CLASS_DRUID:
        retVal=ai->DoSpecificAction("healing touch on party", Event(), true);
        break;
    case CLASS_PALADIN:
        retVal=ai->DoSpecificAction("holy light on party", Event(), true);
        break;
    case CLASS_SHAMAN:
        retVal=ai->DoSpecificAction("healing wave on party", Event(), true);
        break;
    }

    rpg->AfterExecute(true, false);

    DoDelay();

    return retVal;
}

bool RpgUseAction::isUseful()
{
    GuidPosition guidP = rpg->guidP();

    switch (guidP.GetEntry())
    {
        case 190767: {

            //Do not get in cart if miner is moving some other bot. (This is a core bug, minecart will head to other more distant miner if it exists).
            Creature* creature = nullptr;
            MaNGOS::NearestCreatureEntryWithLiveStateInObjectRangeCheck creature_check(*bot, 28841, true, 500.0f);
            MaNGOS::CreatureLastSearcher<MaNGOS::NearestCreatureEntryWithLiveStateInObjectRangeCheck> searcher(creature, creature_check);
            Cell::VisitGridObjects(bot, searcher, 500.0f);

            if (!creature || guidP.distance(creature) > 300.0f)
                return true;

            return false;
        }
    }

    return true;
}

bool RpgTradeUsefulAction::IsTradingItem(uint32 entry)
{
    TradeData* trade = bot->GetTradeData();

    if (!trade)
        return false;

    for (uint8 i = 0; i < TRADE_SLOT_TRADED_COUNT; i++)
    {
        Item* tradeItem = trade->GetItem(TradeSlots(i));

        if (tradeItem && tradeItem->GetEntry() == entry)
            return true;;
    }

    return false;
}

bool RpgTradeUsefulAction::Execute(Event& event)
{
    rpg->BeforeExecute();

    GuidPosition guidP = AI_VALUE(GuidPosition, "rpg target");

    Player* player = guidP.GetPlayer();

    if (!player)
        return false;

    std::list<Item*> items = AI_VALUE(std::list<Item*>, "items useful to give");

    if (items.empty())
        return false;

    Item* item = items.front();

    std::ostringstream param;

    param << chat->formatWorldobject(player);
    param << " ";
    param << chat->formatItem(item);

    if (ai->IsRealPlayer() && !bot->GetTradeData()) //Start the trade from the other side to open the window
    {
        WorldPacket packet(CMSG_INITIATE_TRADE);
        packet << bot->getObjectGuid();
        player->GetSession()->HandleInitiateTradeOpcode(packet);
    }

    if (!IsTradingItem(item->GetEntry()))
        ai->DoSpecificAction("trade", Event("rpg action", param.str().c_str()), true);

    bool isTrading = bot->GetTradeData();

    if (isTrading)
    {
        if (IsTradingItem(item->GetEntry())) //Did we manage to add the item to the trade?
        {
            // Pool bots trade with each other constantly; the line names a bot
            // nobody is watching. Only a real trade partner gets told.
            if (isRealPlayer_Helper(player) && bot->GetGroup() && bot->GetGroup()->IsMember(guidP))
                ai->TellPlayerNoFacing(GetMaster(), "You can use this " + chat->formatItem(item) + " better than me, " + player->GetName() + ".", PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);

            if (!urand(0, 4) || items.size() < 2) //Complete the trade if we have no more items to trade.
            {
                //bot->Say("End trade with" + chat->formatWorldobject(player), (bot->GetTeam() == ALLIANCE ? LANG_COMMON : LANG_ORCISH));
                WorldPacket p;
                uint32 status = TRADE_STATUS_TRADE_ACCEPT;
                p << status;
                bot->GetSession()->HandleAcceptTradeOpcode(p);
            }
        }
        //else
        //   bot->Say("Start trade with" + chat->formatWorldobject(player), (bot->GetTeam() == ALLIANCE ? LANG_COMMON : LANG_ORCISH));

        ai->SetActionDuration(sPlayerbotAIConfig.rpgDelay);
    }

    rpg->AfterExecute(isTrading, true, isTrading ? "rpg trade useful" : "rpg");

    DoDelay();

    return isTrading;
}

bool RpgEnchantAction::Execute(Event& event)
{
    rpg->BeforeExecute();

    GuidPosition guidP = AI_VALUE(GuidPosition, "rpg target");

    Player* player = guidP.GetPlayer();

    if (!player)
        return false;

    if (!ai->IsSafe(player))
        return false;

    std::list<Item*> items = AI_VALUE(std::list<Item*>, "items useful to enchant");

    if (items.empty())
        return false;

    std::vector<uint32> enchantSpells = AI_VALUE(std::vector<uint32>, "enchant spells");

    //Needs more logic to pick best enchant to apply instead of a random one that's bette than current.
    std::shuffle(enchantSpells.begin(), enchantSpells.end(), *GetRandomGenerator());

    for (auto& spellId : enchantSpells)
    {
        Item* item = PAI_VALUE2(Item*, "item for spell", spellId);

        if (std::find(items.begin(), items.end(), item) == items.end())
            continue;

        std::ostringstream param;

        param << chat->formatWorldobject(bot);
        param << " ";
        param << chat->formatItem(item);

        ai->TellDebug(ai->GetMaster(), "enchanting" + param.str(), "debug rpg");

        if (isRealPlayer_Helper(player) && !player->GetTradeData()) //Start the trade from the other side to open the window
        {
            ai->TellDebug(ai->GetMaster(), "open trade window", "debug rpg");
            WorldPacket packet(CMSG_INITIATE_TRADE);
            packet << player->getObjectGuid();
            bot->GetSession()->HandleInitiateTradeOpcode(packet);
        }

        if (!player->GetTradeData() || !player->GetTradeData()->HasItem(item->getObjectGuid()))
        {
            ai->TellDebug(ai->GetMaster(), "starting trade", "debug rpg");
            PlayerbotAIStorage::Instance().GetAI(player)->DoSpecificAction("trade", Event("rpg action", param.str().c_str()), true);
        }

        bool isTrading = bot->GetTradeData();

        if (isTrading)
        {
            if (player->GetTradeData()->HasItem(item->getObjectGuid())) //Did we manage to add the item to the trade?
            {
                uint32 duration;
                Unit* target = nullptr;

                ai->TellDebug(ai->GetMaster(), "set enchant spell", "debug rpg");
                bool didCast = ai->CastSpell(spellId, target, item, true, &duration);

                if (didCast)
                {
                    ai->TellDebug(ai->GetMaster(), "accept trade", "debug rpg");
                    // Same as the trade line above: bot-to-bot enchants stay silent.
                    if (isRealPlayer_Helper(player) && bot->GetGroup() && bot->GetGroup()->IsMember(guidP))
                        ai->TellPlayerNoFacing(GetMaster(), "Let me enchant this " + chat->formatItem(item) + " with " + chat->formatSpell(spellId) + " for you " + player->GetName() + ".", PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);

                    WorldPacket p;
                    uint32 status = TRADE_STATUS_TRADE_ACCEPT;
                    p << status;
                    bot->GetSession()->HandleAcceptTradeOpcode(p);
                }
            }

            ai->SetActionDuration(sPlayerbotAIConfig.rpgDelay);
        }

        rpg->AfterExecute(isTrading, true, isTrading ? "rpg enchant" : "rpg");

        DoDelay();

        return isTrading;
    }

    return false;
}

bool RpgDuelAction::isUseful()
{
    // do not offer duel in non pvp areas
    if (sPlayerbotAIConfig.IsInPvpProhibitedZone(sServerFacade.GetAreaId(bot)))
        return false;

    // Players can only fight a duel with each other outside (=not inside dungeons and not in capital cities)
    AreaTableEntry const* casterAreaEntry = GetAreaEntryByAreaID(sServerFacade.GetAreaId(bot));
    if (casterAreaEntry && !(casterAreaEntry->Flags & AREA_FLAG_DUEL))
    {
        // Dueling isn't allowed here
        return false;
    }

    return true;
}

bool RpgDuelAction::Execute(Event& event)
{
    GuidPosition guidP = AI_VALUE(GuidPosition, "rpg target");

    Player* player = guidP.GetPlayer();

    if (!player)
        return false;

    return ai->DoSpecificAction("cast", Event("rpg action", chat->formatWorldobject(player) + " 7266"), true);
}

bool RpgItemAction::Execute(Event& event)
{
    Player* requester = event.GetOwner() ? event.GetOwner() : GetMaster();
    GuidPosition guidP = AI_VALUE(GuidPosition, "rpg target"), objectGuidP;

    if (sServerFacade.isMoving(bot))
    {
        ai->StopMoving();
        rpg->AfterExecute(true, false, "rpg item");
        return true;
    }

    GameObject* gameObjectTarget = nullptr;
    Unit* unitTarget = nullptr;
    if (guidP.IsUnit())
    {
        unitTarget = guidP.GetUnit(bot->GetInstanceId());
    }
    else if (guidP.IsGameObject())
    {
        gameObjectTarget = guidP.GetGameObject(bot->GetInstanceId());
    }

    std::list<Item*> questItems = AI_VALUE2(std::list<Item*>, "inventory items", "quest");

    bool used = false;
    for (Item* item : questItems)
    {
        if (AI_VALUE2(bool, "can use item on", Qualified::MultiQualify({ std::to_string(item->GetProto()->ItemId),guidP.to_string() }, ",")))
        {
            if (gameObjectTarget)
            {
                used = UseItem(requester, item->GetEntry(), gameObjectTarget);
            }
            else
            {
                used = UseItem(requester, item->GetEntry(), unitTarget);
            }
        }
    }

    if (used && !GetDuration())
    {
        SetDuration(sPlayerbotAIConfig.globalCoolDown);
    }

    return used;
}
