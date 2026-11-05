
#include "playerbot/strategy/ItemVisitors.h"
#include "playerbot/playerbot.h"
#include "CastCustomSpellAction.h"

#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/ServerFacade.h"
#include "CheckMountStateAction.h"
#include "MailAction.h"
#include "Mail/Mail.h"
#include "../../../../runtime/GuildOrderDisenchantPolicy.h"
#include "../../../../runtime/GuildOrderStore.h"

#include <unordered_map>
#include <vector>

using namespace ai;

int FindLastSeparator(std::string text, std::string sep)
{
    int pos = text.rfind(sep);
    if (pos == std::string::npos) return pos;

    int lastLinkBegin = text.rfind("|H");
    int lastLinkEnd = text.find("|h|r", lastLinkBegin + 1);
    if (pos >= lastLinkBegin && pos <= lastLinkEnd)
        pos = text.find_last_of(sep, lastLinkBegin);

    return pos;
}

static inline void ltrim(std::string& s)
{
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch)
    {
        return !std::isspace(ch);
    }));
}

bool CastCustomSpellAction::Execute(Event& event)
{
    Unit* target = nullptr;
    std::string text = getQualifier();

    if(text.empty() || text == "travel")
        text = event.GetParam();

    Player* requester = event.GetOwner() ? event.GetOwner() : GetMaster();

    // Process summon request
    if (CastSummonPlayer(requester, text))
    {
        return true;
    }

    // Grab the first game object or unit from the parameters as target.
    GameObject* gameObjectTarget = nullptr;
    std::list<ObjectGuid> gos = chat->parseGameobjects(text);
    if (!gos.empty())
    {
        for (auto go : gos)
        {
            if (go.IsGameObject())
            {
                gameObjectTarget = ai->GetGameObject(go);

                if (gameObjectTarget)
                {
                    chat->eraseAllSubStr(text, chat->formatWorldobject(gameObjectTarget));
                    break;
                }
            }
            else if (go.IsUnit())
            {
                target = ai->GetUnit(go);

                if (target)
                {
                    chat->eraseAllSubStr(text, chat->formatWorldobject(target));
                    break;
                }
            }
        }

        ltrim(text);
    }

    if (!target)
    {
        if (requester && requester->GetSelectionGuid())
        {
            target = ai->GetUnit(requester->GetSelectionGuid());
        }
    }

    if (!target)
        target = GetTarget();

    if (!target)
        target = bot;

    if (!requester) //Use self as requester for permissions.
        requester = bot;

    Item* itemTarget = nullptr;
    int pos = FindLastSeparator(text, " ");
    int castCount = 1;
    if (pos != std::string::npos)
    {
        std::string param = text.substr(pos + 1);
        std::list<Item*> items = ai->InventoryParseItems(param, IterateItemsMask::ITERATE_ITEMS_IN_BAGS);
        if (!items.empty()) itemTarget = *items.begin();
        else
        {
            castCount = atoi(param.c_str());
            if (castCount > 0)
                text = text.substr(0, pos);
        }
    }

    uint32 spell = AI_VALUE2(uint32, "spell id", text);

    if (!spell)
    {
        std::map<std::string, std::string> args;
        args["%spell"] = text;
        ai->TellPlayerNoFacing(requester, BOT_TEXT2("cast_spell_command_error_unknown_spell", args));
        return false;
    }

    SpellEntry const* pSpellInfo = sServerFacade.LookupSpellInfo(spell);
    if (!pSpellInfo)
    {
        std::map<std::string, std::string> args;
        args["%spell"] = text;
        ai->TellPlayerNoFacing(requester, BOT_TEXT2("cast_spell_command_error_unknown_spell", args));
        return false;
    }

    // Don't use totem items for totem spells (except enchanting bars)
    if (pSpellInfo->Totem[0] > 0 &&
        pSpellInfo->Totem[0] != 5060 &&
        pSpellInfo->Totem[0] != 6218 &&
        pSpellInfo->Totem[0] != 6339 &&
        pSpellInfo->Totem[0] != 11130 &&
        pSpellInfo->Totem[0] != 11145 &&
        pSpellInfo->Totem[0] != 16207 &&
        pSpellInfo->Totem[0] != 22461 &&
        pSpellInfo->Totem[0] != 22462 &&
        pSpellInfo->Totem[0] != 22463)
    {
        itemTarget = nullptr;
    }

    if ((pSpellInfo->Targets & TARGET_FLAG_ITEM) || (pSpellInfo->Targets & TARGET_FLAG_SELF))
        target = bot;

    WorldObject* woTarget = nullptr;
    if (gameObjectTarget)
        woTarget = gameObjectTarget;
    else
        woTarget = target;

    if (woTarget != bot && !sServerFacade.isInFront(bot, woTarget, sPlayerbotAIConfig.sightDistance, CAST_ANGLE_IN_FRONT))
    {
        sServerFacade.SetFacingTo(bot, woTarget);
        SetDuration(sPlayerbotAIConfig.globalCoolDown);
        std::ostringstream msg;
        msg << "cast " << text;
        ai->HandleCommand(CHAT_MSG_WHISPER, msg.str(), event.GetOwner() ? *event.GetOwner() : *bot);
        return true;
    }

    if (bot->IsMoving())
    {
        ai->StopMoving();
    }

    if (AI_VALUE2(uint32, "current mount speed", "self target"))
    {
        if (bot->IsFlying() && WorldPosition(bot).currentHeight() > 10.0f)
            return false;

        ai->Unmount();
    }

    ai->RemoveShapeshift();

    std::ostringstream replyStr;
    std::map<std::string, std::string> replyArgs;
    if (!pSpellInfo->EffectItemType[0])
    {
        replyStr << BOT_TEXT("cast_spell_command_spell");

        replyArgs["%spell"] = ChatHelper::formatSpell(pSpellInfo);
    }
    else
    {
        replyStr << BOT_TEXT("cast_spell_command_craft");

        uint32 newItemId = pSpellInfo->EffectItemType[0];

        if (!newItemId)
            replyArgs["%spell"] = ChatHelper::formatSpell(pSpellInfo);
        else
        {

            ItemPrototype const* proto = sObjectMgr.GetItemPrototype(newItemId);
            replyArgs["%spell"] = ChatHelper::formatItem(proto);
        }
    }

    if (bot->GetTrader())
    {
        replyStr << " " << BOT_TEXT("command_target_trade");
    }
    else if (itemTarget)
    {
        replyStr << " " << BOT_TEXT("command_target_item");
        replyArgs["%item"] = chat->formatItem(itemTarget);
    }
    else if (gameObjectTarget)
    {
        replyStr << " " << BOT_TEXT("command_target_item");
        replyArgs["%item"] = chat->formatGameobject(gameObjectTarget);
    }
    else if (pSpellInfo->EffectItemType[0])
    {
        replyStr << "";
    }
    else if (target == bot)
    {
        replyStr << " " << BOT_TEXT("command_target_self");
    }
    else
    {
        replyStr << " " << BOT_TEXT("command_target_unit");
        replyArgs["%unit"] = target->GetName();
    }

    SpellCastResult checkResult;
    // An item in bags authorizes its own spell (potions, bandages, poisons are
    // never in the spellbook). The engine CheckCast still validates level,
    // cooldown, health and everything else.
    const bool canCast = gameObjectTarget ? ai->CanCastSpell(spell, gameObjectTarget, 0, true, false, false, false, &checkResult) : ai->CanCastSpell(spell, target, 0, !itemTarget, itemTarget, false, false, false, &checkResult);
    if (!bot->GetTrader() && !canCast)
    {
        // "Try again in a moment" is not information: the trigger refires on
        // its own (chain-crafting handstitched goods hits GCD constantly), so
        // retry quietly instead of announcing a failure nobody can act on.
        if (checkResult == SPELL_FAILED_NOT_READY || checkResult == SPELL_FAILED_ITEM_NOT_READY ||
            checkResult == SPELL_FAILED_SPELL_IN_PROGRESS)
        {
            SetDuration(sPlayerbotAIConfig.globalCoolDown);
            return false;
        }
        std::map<std::string, std::string> args;
        args["%spell"] = replyArgs["%spell"];
        std::string failReason = GetSpellCastResultString(checkResult);
        if (failReason.empty())
            return false;
        args["%fail_reason"] = failReason;
        ai->TellPlayerNoFacing(requester, BOT_TEXT2("cast_spell_command_error_reason", args));
        return false;
    }

    MotionMaster& mm = *bot->GetMotionMaster();
    uint32 spellDuration = sPlayerbotAIConfig.globalCoolDown;

    bool result = gameObjectTarget ? ai->CastSpell(spell, gameObjectTarget, itemTarget, true, &spellDuration) : ai->CastSpell(spell, target, itemTarget, true, &spellDuration);
    if (result)
    {
        SetDuration(spellDuration);

        if (castCount > 1)
        {
            std::ostringstream cmd;
            cmd << castString(target) << " " << text << " " << (castCount - 1);
            ai->HandleCommand(CHAT_MSG_WHISPER, cmd.str(), *requester);

            replyStr << " " << BOT_TEXT("cast_spell_command_amount");
            replyArgs["%amount"] = std::to_string(castCount - 1);
        }

        ai->TellPlayerNoFacing(requester, BOT_TEXT2(replyStr.str(), replyArgs), PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
    }
    else
    {
        std::map<std::string, std::string> args;
        args["%spell"] = replyArgs["%spell"];
        ai->TellPlayerNoFacing(requester, BOT_TEXT2("cast_spell_command_error_failed", args));
    }

    return result;
}

bool CastCustomSpellAction::CastSummonPlayer(Player* requester, std::string command)
{
    if (bot->GetClass() == CLASS_WARLOCK)
    {
        if (command.find("summon") != std::string::npos)
        {
            // Don't summon player when trying to summon warlock pet
            if (command.find("imp") != std::string::npos ||
                command.find("voidwalker") != std::string::npos ||
                command.find("succubus") != std::string::npos ||
                command.find("felhunter") != std::string::npos ||
                command.find("felsteed") != std::string::npos ||
                command.find("dreadsteed") != std::string::npos)
            {
                return false;
            }

            if (!ai->IsStateActive(BotState::BOT_STATE_COMBAT))
            {
                // Get target from command parameters
                uint8 membersAroundSummoner = 0;
                Player* target = nullptr;
                const std::string summonString = "summon ";
                const int pos = command.find(summonString);
                if (pos != std::string::npos)
                {
                    // Get player name
                    std::string playerName = command.substr(summonString.size());

                    const Group* group = bot->GetGroup();
                    if (group && !playerName.empty())
                    {
                        const Group::MemberSlotList& groupSlot = group->GetMemberSlots();
                        for (Group::member_citerator itr = groupSlot.begin(); itr != groupSlot.end(); itr++)
                        {
                            Player* member = sObjectMgr.GetPlayer(itr->guid);
                            if (member)
                            {
                                if (member->GetName() == playerName)
                                {
                                    target = member;
                                }

                                if (member->GetDistance(bot) <= sPlayerbotAIConfig.reactDistance)
                                {
                                    membersAroundSummoner++;
                                }
                            }
                        }
                    }
                }
                else
                {
                    // Get target from requester target
                    if (requester && requester->GetSelectionGuid())
                    {
                        const ObjectGuid& targetGuid = requester->GetSelectionGuid();
                        if (targetGuid.IsPlayer())
                        {
                            const Group* group = bot->GetGroup();
                            if (group)
                            {
                                const Group::MemberSlotList& groupSlot = group->GetMemberSlots();
                                for (Group::member_citerator itr = groupSlot.begin(); itr != groupSlot.end(); itr++)
                                {
                                    Player* member = sObjectMgr.GetPlayer(itr->guid);
                                    if (member)
                                    {
                                        if (itr->guid == targetGuid)
                                        {
                                            target = member;
                                        }

                                        if (ai->IsSafe(member) && member->GetDistance(bot) <= sPlayerbotAIConfig.reactDistance)
                                        {
                                            membersAroundSummoner++;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                if (target)
                {
                    if (membersAroundSummoner >= 3)
                    {
                        if (isRealPlayer_Helper(target))
                        {
                            float x, y, z;
                            bot->GetPosition(x, y, z);
                            target->SetSummonPoint(bot->GetMapId(), x, y, z);

                            WorldPacket data(SMSG_SUMMON_REQUEST, 8 + 4 + 4);
                            data << bot->getObjectGuid();
                            data << uint32(bot->getZoneId());
                            data << uint32(MAX_PLAYER_SUMMON_DELAY * IN_MILLISECONDS);
                            target->GetSession()->SendPacket(&data);
                        }
                        else
                        {
                            target->TeleportTo(bot->GetMapId(), bot->getPositionX(), bot->getPositionY(), bot->getPositionZ(), bot->getOrientation());
                            if (isRealPlayer_Helper(target))
                                target->SendHeartBeat();
                        }

                        std::ostringstream msg;
                        msg << "Summoning " << target->GetName();

                        std::map<std::string, std::string> args;
                        args["%target"] = target->GetName();
                        ai->TellPlayerNoFacing(requester, BOT_TEXT2("cast_spell_command_summon", args), PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
                        SetDuration(sPlayerbotAIConfig.globalCoolDown);
                        return true;
                    }
                    else
                    {
                        ai->TellPlayerNoFacing(requester, BOT_TEXT("cast_spell_command_summon_error_members"));
                    }
                }
                else
                {
                    ai->TellPlayerNoFacing(requester, BOT_TEXT("cast_spell_command_summon_error_target"));
                }
            }
            else
            {
                ai->TellPlayerNoFacing(requester, BOT_TEXT("cast_spell_command_summon_error_combat"));
            }
        }
    }

    return false;
}

bool CastRandomSpellAction::Execute(Event& event)
{
    Player* requester = event.GetOwner() ? event.GetOwner() : GetMaster();
    std::list<std::pair<uint32, std::string>> spellMap = GetSpellList();

    Unit* target = nullptr;
    GameObject* got = nullptr;

    std::string name = event.GetParam();
    if (name.empty())
    {
        name = getName();
    }

    std::list<ObjectGuid> wos = chat->parseGameobjects(name);

    for (auto wo : wos)
    {
        target = ai->GetUnit(wo);
        got = ai->GetGameObject(wo);

        if (got || target)
        {
            break;
        }
    }

    if (!got && !target && bot->GetSelectionGuid())
    {
        target = ai->GetUnit(bot->GetSelectionGuid());
        got = ai->GetGameObject(bot->GetSelectionGuid());
    }

    if (!got && !target)
    {
        if (requester && requester->GetSelectionGuid())
        {
            target = ai->GetUnit(requester->GetSelectionGuid());
        }
    }

    if (!got && !target)
    {
        target = bot;
    }

    std::vector<std::pair<uint32, std::pair<uint32, WorldObject*>>> spellList;
    for (auto & spell : spellMap)
    {
        uint32 spellId = spell.first;

        const SpellEntry* pSpellInfo = sServerFacade.LookupSpellInfo(spellId);
        if (!pSpellInfo)
        {
            continue;
        }

        if (!AcceptSpell(pSpellInfo))
        {
            continue;
        }

        if (bot->HasSpell(spellId))
        {
            uint32 spellPriority = GetSpellPriority(pSpellInfo);

            if (!spellPriority)
            {
                continue;
            }

            if (target && ai->CanCastSpell(spellId, target, 0))
            {
                spellList.push_back(std::make_pair(spellId, std::make_pair(spellPriority, target)));
            }

            if (target && ai->CanCastSpell(spellId, target->getPositionX(), target->getPositionY(), target->getPositionZ(), 0))
            {
                spellList.push_back(std::make_pair(spellId, std::make_pair(spellPriority, target)));
            }

            if (got && ai->CanCastSpell(spellId, got->getPositionX(), got->getPositionY(), got->getPositionZ(), 0))
            {
                spellList.push_back(std::make_pair(spellId, std::make_pair(spellPriority, got)));
            }

            Item* item = nullptr;

            if (pSpellInfo->Targets & TARGET_FLAG_ITEM)
                item = AI_VALUE2(Item*, "item for spell", pSpellInfo->Id);

            if (ai->CanCastSpell(spellId, bot, 0, true, item))
            {
                spellList.push_back(std::make_pair(spellId, std::make_pair(spellPriority, bot)));
            }
        }
    }

    if (spellList.empty())
    {
        return false;
    }

    bool isCast = false;

    bool allTheSame = true;

    for(auto& spell : spellList)
    {
        if (spell.first != spellList[0].first)
        {
            allTheSame = false;
            break;
        }
    }

    if (!allTheSame)
        std::sort(spellList.begin(), spellList.end(), [](std::pair<uint32, std::pair<uint32, WorldObject*>> i, std::pair<uint32, std::pair<uint32, WorldObject*>> j) {return i.second.first > j.second.first; });

    uint32 rndBound = spellList.size() - 1;

    if(!allTheSame)
        rndBound = std::min(rndBound/4, (uint32)10);

    for (uint32 i = 0; i < 5; i++)
    {
        uint32 rnd = urand(0, rndBound);

        auto spell = spellList[rnd];

        uint32 spellId = spell.first;
        WorldObject* wo = spell.second.second;

        return castSpell(spellId, wo, requester);
    }

    return false;
}

bool CastRandomSpellAction::castSpell(uint32 spellId, WorldObject* wo, Player* requester)
{
    bool executed = false;
    uint32 spellDuration = sPlayerbotAIConfig.globalCoolDown;

    SpellEntry const* pSpellInfo = sServerFacade.LookupSpellInfo(spellId);

    Item* spellItem = AI_VALUE2(Item*, "item for spell", spellId);

    if (spellItem)
    {
        if (ai->CastSpell(spellId, bot, spellItem, false, &spellDuration))
        {
            ai->TellPlayer(requester, "Casting " + ChatHelper::formatSpell(pSpellInfo) + " on " + ChatHelper::formatItem(spellItem), PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
            executed = true;
        }
    }

    if (!executed && wo)
    {
        if (pSpellInfo->Targets & TARGET_FLAG_DEST_LOCATION)
        {
            if (ai->CastSpell(spellId, wo->getPositionX(), wo->getPositionY(), wo->getPositionZ(), nullptr, false, &spellDuration))
            {
                ai->TellPlayer(requester, "Casting " + ChatHelper::formatSpell(pSpellInfo) + " near " + ChatHelper::formatWorldobject(wo), PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
                executed = true;
            }
        }
        else if (wo->getObjectGuid().IsUnit())
        {
            if (ai->CastSpell(spellId, (Unit*)wo, nullptr, false, &spellDuration))
            {
                if(wo != bot)
                    ai->TellPlayer(requester, "Casting " + ChatHelper::formatSpell(pSpellInfo) + " on " + ChatHelper::formatWorldobject(wo), PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
                else
                    ai->TellPlayer(requester, "Casting " + ChatHelper::formatSpell(pSpellInfo), PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
                executed = true;
            }
        }
        else if (wo->getObjectGuid().IsGameObject())
        {
            if (ai->CastSpell(spellId, (GameObject*)wo, nullptr, false, &spellDuration))
            {
                ai->TellPlayer(requester, "Casting " + ChatHelper::formatSpell(pSpellInfo) + " on " + ChatHelper::formatWorldobject(wo), PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
                executed = true;
            }
        }
        else
        {
            if (ai->CastSpell(spellId, wo->getPositionX(), wo->getPositionY(), wo->getPositionZ(), nullptr, false, &spellDuration))
            {
                ai->TellPlayer(requester, "Casting " + ChatHelper::formatSpell(pSpellInfo) + " near " + ChatHelper::formatWorldobject(wo), PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
                executed = true;
            }
        }
    }

    if (!executed)
    {
        if (ai->CastSpell(spellId, bot, nullptr, false, &spellDuration))
        {
            ai->TellPlayer(requester, "Casting " + ChatHelper::formatSpell(pSpellInfo), PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
            executed = true;
        }
    }

    if (executed)
    {
        SetDuration(spellDuration);
    }
    return executed;
}

bool CraftRandomItemAction::Execute(Event& event)
{
    std::vector<uint32> spellIds = AI_VALUE(std::vector<uint32>, "craft spells");
    std::shuffle(spellIds.begin(), spellIds.end(),*GetRandomGenerator());

    std::list<ObjectGuid> wos = chat->parseGameobjects(event.GetParam());
    WorldObject* wot = nullptr;

    for (auto wo : wos)
    {
        wot = ai->GetGameObject(wo);

        if (wot)
            break;
    }

    if (!wot)
        wot = bot;

    for (uint32 spellId : spellIds)
    {
        if (!AI_VALUE2(bool, "can craft spell", spellId))
            continue;

        if (!AI_VALUE2(bool, "should craft spell", spellId))
            continue;

        // Generic skilling casts must not eat order stock: the order's own
        // craft path (guild craft order) owns those reagents.
        const SpellEntry* pSpellCastInfo = sServerFacade.LookupSpellInfo(spellId);
        if (bot->GetGuildId() && pSpellCastInfo)
        {
            bool eatsOrderStock = false;
            for (uint8 i = 0; i < MAX_SPELL_REAGENTS; ++i)
                if (pSpellCastInfo->ReagentCount[i] && pSpellCastInfo->Reagent[i] &&
                    GuildOrderConsumesItem(bot, uint32(pSpellCastInfo->Reagent[i])))
                {
                    eatsOrderStock = true;
                    break;
                }
            if (eatsOrderStock)
                continue;
        }
        const SpellEntry* pSpellInfo = pSpellCastInfo;
        if (!pSpellInfo)
            continue;

        if (pSpellInfo->RequiresSpellFocus)
        {
            if (!GuidPosition(wot).IsGameObject())
                continue;

            if (GuidPosition(wot).GetGameObjectInfo()->type != GAMEOBJECT_TYPE_SPELL_FOCUS)
                continue;

            if (GuidPosition(wot).GetGameObjectInfo()->spellFocus.focusId != pSpellInfo->RequiresSpellFocus)
                continue;
        }
        else if(wot != bot)
        {
            wot = nullptr;
        }

        uint32 castCount = AI_VALUE2(uint32, "has reagents for", spellId);

        uint32 newItemId = pSpellInfo->EffectItemType[0];

        if (!newItemId)
            continue;

        ItemPrototype const* proto = sObjectMgr.GetItemPrototype(newItemId);

        if (!proto)
            continue;

        if (castCount > proto->GetMaxStackSize())
            castCount = proto->GetMaxStackSize();

        std::ostringstream cmd;
        cmd << "castnc ";

        if (((wot && sServerFacade.isInFront(bot, wot, sPlayerbotAIConfig.sightDistance, CAST_ANGLE_IN_FRONT))))
        {
            cmd << chat->formatWorldobject(wot) << " ";
        }

        cmd << spellId << " " << castCount;

        ai->HandleCommand(CHAT_MSG_WHISPER, cmd.str(), *bot);
        SetDuration(1.0f); //Spel was not cast yet so no delay is needed.
        return true;

    }

    return false;
}

bool DisenchantRandomItemAction::Execute(Event& event)
{
    Player* requester = event.GetOwner() ? event.GetOwner() : GetMaster();
    std::list<uint32> items = AI_VALUE2(std::list<uint32>, "inventory item ids", "usage " + std::to_string((uint8)ItemUsage::ITEM_USAGE_DISENCHANT));

    items.reverse();

    if (bot->IsMoving())
    {
        ai->StopMoving();
    }

    if (bot->IsMounted())
    {
        return false;
    }

    for (auto& item: items)
    {
        ItemPrototype const* proto = sObjectMgr.GetItemPrototype(item);

        if (!proto)
            continue;

        if (!proto->DisenchantID)
            continue;


        // don't touch rare+ items if with real player/guild
        if ((ai->HasRealPlayerMaster() || ai->IsInRealGuild()) && proto->Quality > ITEM_QUALITY_UNCOMMON)
        {
            continue;
        }

        // Order stock is never disenchanted: the keep set already shields
        // it from the usage list, this covers the id path. Queued service
        // items wait for their own cast the same way.
        if (bot->GetGuildId() && GuildOrderConsumesItem(bot, item))
            continue;
        if (bot->GetGuildId() && TortoiseBots::GuildOrders::DisenchantRowsHoldItem(
                TortoiseBots::GuildOrders::DisenchantQueued(bot->GetGUIDLow()), item))
            continue;

        ItemQualifier itemQualifier(item);

        Event disenchantEvent = Event("disenchant random item", "13262 " + chat->formatQItem(item));
        bool didCast = CastCustomSpellAction::Execute(disenchantEvent);

        if(didCast)
        {
            ai->TellPlayer(requester, "Disenchanting " + chat->formatItem(itemQualifier), PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
        }

        return didCast;
    }

    return false;
};

namespace
{
// The bot's work list, topped up from its persisted rows: the mail intake
// and a restart only write rows, the list picks them up here.
std::vector<TortoiseBots::GuildOrders::DisenchantServiceItem>& SyncedServiceQueue(Player* bot)
{
    auto& queue = TortoiseBots::GuildOrders::DisenchantServiceQueue(bot->GetGUIDLow());
    for (TortoiseBots::GuildOrders::DisenchantQueueEntry const& row :
        TortoiseBots::GuildOrders::DisenchantQueued(bot->GetGUIDLow()))
    {
        bool known = false;
        for (auto const& queued : queue)
            known = known || queued.itemGuidLow == row.itemGuidLow;
        if (known)
            continue;
        TortoiseBots::GuildOrders::DisenchantServiceItem item;
        item.senderLow = row.senderGuidLow;
        item.itemGuidLow = row.itemGuidLow;
        item.itemId = row.itemId;
        item.itemName = row.itemName;
        item.pendingResults = row.pendingResults;
        item.results = TortoiseBots::GuildOrders::ParseDisenchantResults(row.results);
        queue.push_back(item);
    }
    return queue;
}
} // namespace

bool GuildDisenchantServiceAction::isUseful()
{
    if (!bot->GetGuildId())
        return false;
    bool hasWork = !SyncedServiceQueue(bot).empty();
    // Moving or mounted does not block: the cast stops and dismounts the bot.
    return TortoiseBots::GuildOrders::DisenchantCastReady(bot->IsInCombat(), false,
        false, bot->IsNonMeleeSpellCasted(false), bot->GetTrader() != nullptr, hasWork);
}

// Mails one stack back to its sender (the live bag stack: the intake moved
// it there). No gold taken, nothing of the bot's own added; one item per
// mail on the core side. False when the stack is gone, so the stranded row
// drops instead of looping.
namespace ai
{
namespace
{
bool MailServiceStackBack(Player* bot, uint32_t senderLow, Item* item, std::string const& subject,
    std::string const& reason)
{
    if (!bot || !item)
        return false;
    Player* sender = sObjectMgr.GetPlayer(ObjectGuid(HIGHGUID_PLAYER, senderLow));
    ObjectGuid receiverGuid = sender ? sender->getObjectGuid() : ObjectGuid(HIGHGUID_PLAYER, senderLow);
    std::ostringstream body;
    body << "Hello,\n\n";
    if (reason.empty())
        body << "Here are the materials from your disenchant request.\n\n";
    else
        body << "I could not disenchant this (" << item->GetProto()->Name1 << "): " << reason << ".\n\n";
    body << bot->GetName() << '\n';
    bot->MoveItemFromInventory(item->GetBagSlot(), item->GetSlot(), true);
    item->DeleteFromInventoryDB();
    item->SetOwnerGuid(receiverGuid);
    if (item->GetState() != ITEM_NEW)
        item->FSetState(ITEM_CHANGED);
    item->SaveToDB();
    MailDraft draft(subject, body.str());
    draft.AddItem(item);
    draft.SendMailTo(MailReceiver(sender, receiverGuid), MailSender(bot));
    return true;
}

// Mails exactly what one cast made back to the member: one item per mail on
// the core side, the subject naming the disenchanted item. Counts shrink
// live off the bag stacks as each piece is mailed, so an interrupted batch
// keeps the remainder queued for the next mailbox visit.
void MailServiceResultsBack(Player* bot, TortoiseBots::GuildOrders::DisenchantServiceItem& queued)
{
    for (TortoiseBots::GuildOrders::DisenchantServiceResult& result : queued.results)
    {
        while (result.count > 0)
        {
            // Exactly what this cast made, never the bot's own stock: prefer
            // a whole stack that fits, else split one down to the remainder
            // with a core split packet so the mail holds only service loot.
            Item* stack = nullptr;
            Item* splitter = nullptr;
            for (uint8_t slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END && !stack && !splitter; ++slot)
            {
                Item* candidate = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
                if (!candidate || candidate->GetEntry() != result.itemId)
                    continue;
                if (candidate->GetCount() <= result.count)
                    stack = candidate;
                else if (!splitter)
                    splitter = candidate;
            }
            for (uint8_t bag = INVENTORY_SLOT_BAG_START; bag < INVENTORY_SLOT_BAG_END && !stack && !splitter; ++bag)
            {
                Bag* pack = (Bag*)bot->GetItemByPos(INVENTORY_SLOT_BAG_0, bag);
                if (!pack)
                    continue;
                for (uint32_t slot = 0; slot < pack->GetBagSize() && !stack && !splitter; ++slot)
                {
                    Item* candidate = pack->GetItemByPos(uint8_t(slot));
                    if (!candidate || candidate->GetEntry() != result.itemId)
                        continue;
                    if (candidate->GetCount() <= result.count)
                        stack = candidate;
                    else if (!splitter)
                        splitter = candidate;
                }
            }
            if (!stack && splitter)
            {
                ItemPosCountVec dest;
                uint32_t splitCount = splitter->GetCount() - result.count;
                if (bot->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, result.itemId, splitCount) == EQUIP_ERR_OK &&
                    !dest.empty())
                {
                    uint32_t before = bot->GetItemCount(result.itemId);
                    bot->SplitItem((splitter->GetBagSlot() << 8) | splitter->GetSlot(),
                        dest.front().pos, splitCount);
                    if (bot->GetItemCount(result.itemId) == before)
                        stack = splitter;
                }
            }
            if (!stack)
                break;
            uint32_t mailed = stack->GetCount();
            std::string subject =
                TortoiseBots::GuildOrders::DisenchantServiceMailSubject(queued.itemName);
            if (!MailServiceStackBack(bot, queued.senderLow, stack, subject, ""))
                break;
            result.count = mailed >= result.count ? 0 : result.count - mailed;
        }
    }
} // namespace
} // namespace ai

bool GuildDisenchantServiceAction::Execute(Event& event)
{
    (void)event;
    auto& queue = SyncedServiceQueue(bot);
    if (queue.empty())
        return false;
    // The cast leaves its results in a loot window on the item for the bot
    // itself: the collect tick below takes it there, so a cast never
    // reports success before its loot exists. Pending results ride the
    // queue head until they are mailed back.
    TortoiseBots::GuildOrders::DisenchantServiceItem& head = queue.front();
    if (head.pendingResults)
    {
        if (head.collectLootOpen)
        {
            ObjectGuid lootGuid = bot->GetLootGuid();
            Loot* loot = sLootMgr.GetLoot(bot);
            if (lootGuid.IsEmpty() || !loot)
            {
                // Still casting: wait. Cast broke off with the item still in
                // the bags: try it again instead of dropping the row.
                if (bot->IsNonMeleeSpellCasted(false))
                    return true;
                if (bot->GetItemByGuid(ObjectGuid(HIGHGUID_ITEM, head.itemGuidLow)))
                {
                    head.collectLootOpen = false;
                    head.pendingResults = false;
                    head.bagBefore.clear();
                    return false;
                }
            }
            if (!lootGuid.IsEmpty() && loot)
            {
                for (uint8_t i = 0; i < MAX_NR_LOOT_ITEMS; ++i)
                {
                    LootItem* lootItem = loot->LootItemInSlot(i, bot->GetGUIDLow());
                    if (!lootItem || lootItem->is_looted)
                        continue;
                    WorldPacket packet(CMSG_AUTOSTORE_LOOT_ITEM, 1);
                    packet << i;
                    bot->GetSession()->HandleAutostoreLootItemOpcode(packet);
                }
                WorldPacket release(CMSG_LOOT_RELEASE, 8);
                release << lootGuid;
                bot->GetSession()->HandleLootReleaseOpcode(release);
            }
            // The bot's own loot handling may have emptied the window first:
            // the results are what the bags gained since the cast either way.
            head.results.clear();
            std::unordered_map<uint32_t, uint32_t> after;
            for (uint8_t slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
                if (Item* held = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
                    after[held->GetEntry()] += held->GetCount();
            for (uint8_t bag = INVENTORY_SLOT_BAG_START; bag < INVENTORY_SLOT_BAG_END; ++bag)
            {
                Bag* pack = (Bag*)bot->GetItemByPos(INVENTORY_SLOT_BAG_0, bag);
                if (!pack)
                    continue;
                for (uint32_t slot = 0; slot < pack->GetBagSize(); ++slot)
                    if (Item* held = pack->GetItemByPos(uint8_t(slot)))
                        after[held->GetEntry()] += held->GetCount();
            }
            // Items mailed in for a later cast are not this cast's results.
            std::vector<TortoiseBots::GuildOrders::DisenchantQueueEntry> const queuedRows =
                TortoiseBots::GuildOrders::DisenchantQueued(bot->GetGUIDLow());
            for (auto const& counted : after)
            {
                bool queuedItem = false;
                for (auto const& row : queuedRows)
                    queuedItem = queuedItem || row.itemId == counted.first;
                if (queuedItem)
                    continue;
                uint32_t had = 0;
                auto found = head.bagBefore.find(counted.first);
                if (found != head.bagBefore.end())
                    had = found->second;
                if (counted.second > had)
                    head.results.push_back({counted.first, counted.second - had});
            }
            head.bagBefore.clear();
            {
                TortoiseBots::GuildOrders::DisenchantQueueEntry row;
                row.botGuidLow = bot->GetGUIDLow();
                row.senderGuidLow = head.senderLow;
                row.itemGuidLow = head.itemGuidLow;
                row.itemId = head.itemId;
                row.itemName = head.itemName;
                row.pendingResults = true;
                row.results = TortoiseBots::GuildOrders::FormatDisenchantResults(head.results);
                TortoiseBots::GuildOrders::SaveDisenchantRow(row);
            }
            head.collectLootOpen = false;
        }
        // Server-side mail, like the refusals: the bot need not stand at a mailbox.
        MailServiceResultsBack(bot, head);
        bool leftover = false;
        for (TortoiseBots::GuildOrders::DisenchantServiceResult const& result : head.results)
            if (result.count > 0)
                leftover = true;
        if (!leftover)
        {
            TortoiseBots::GuildOrders::DeleteDisenchantRow(bot->GetGUIDLow(), head.itemGuidLow);
            queue.erase(queue.begin());
        }
        else
        {
            TortoiseBots::GuildOrders::DisenchantQueueEntry row;
            row.botGuidLow = bot->GetGUIDLow();
            row.senderGuidLow = head.senderLow;
            row.itemGuidLow = head.itemGuidLow;
            row.itemId = head.itemId;
            row.itemName = head.itemName;
            row.pendingResults = true;
            row.results = TortoiseBots::GuildOrders::FormatDisenchantResults(head.results);
            TortoiseBots::GuildOrders::SaveDisenchantRow(row);
        }
        return true;
    }
    TortoiseBots::GuildOrders::DisenchantServiceItem queued = head;
    Item* item = bot->GetItemByGuid(ObjectGuid(HIGHGUID_ITEM, queued.itemGuidLow));
    if (!item || item->GetEntry() != queued.itemId)
    {
        TortoiseBots::GuildOrders::DeleteDisenchantRow(bot->GetGUIDLow(), queued.itemGuidLow);
        queue.erase(queue.begin());
        return false;
    }
    ItemPrototype const* proto = item->GetProto();
    if (!proto)
    {
        TortoiseBots::GuildOrders::DeleteDisenchantRow(bot->GetGUIDLow(), queued.itemGuidLow);
        queue.erase(queue.begin());
        return false;
    }
    // A bot without Enchanting mails everything back `noskill`: the member
    // learns which bot to ask, and nothing of theirs is eaten.
    if (!ai->HasSkill(SKILL_ENCHANTING))
    {
        MailServiceStackBack(bot, queued.senderLow, item, "Disenchant: noskill",
            TortoiseBots::GuildOrders::DisenchantVerdictToken(
                TortoiseBots::GuildOrders::DisenchantVerdict::NoSkill));
        TortoiseBots::GuildOrders::DeleteDisenchantRow(bot->GetGUIDLow(), queued.itemGuidLow);
        queue.erase(queue.begin());
        return true;
    }
    bool disenchantable = proto->DisenchantID != 0 &&
        (proto->Class == ITEM_CLASS_WEAPON || proto->Class == ITEM_CLASS_ARMOR);
    bool isEpic = proto->Quality >= ITEM_QUALITY_EPIC;
    uint32_t skill = bot->GetSkillValue(SKILL_ENCHANTING);
    TortoiseBots::GuildOrders::DisenchantVerdict verdict =
        TortoiseBots::GuildOrders::ClassifyDisenchantItem(true, disenchantable,
            proto->ItemLevel, proto->RequiredLevel, skill, isEpic);
    if (verdict != TortoiseBots::GuildOrders::DisenchantVerdict::Ok)
    {
        std::string token = TortoiseBots::GuildOrders::DisenchantVerdictToken(verdict);
        MailServiceStackBack(bot, queued.senderLow, item, "Disenchant: " + token, token);
        TortoiseBots::GuildOrders::DeleteDisenchantRow(bot->GetGUIDLow(), queued.itemGuidLow);
        queue.erase(queue.begin());
        return true;
    }
    std::string sourceName = proto->Name1;
    head.bagBefore.clear();
    for (uint8_t slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
        if (Item* held = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
            head.bagBefore[held->GetEntry()] += held->GetCount();
    for (uint8_t bag = INVENTORY_SLOT_BAG_START; bag < INVENTORY_SLOT_BAG_END; ++bag)
    {
        Bag* pack = (Bag*)bot->GetItemByPos(INVENTORY_SLOT_BAG_0, bag);
        if (!pack)
            continue;
        for (uint32_t slot = 0; slot < pack->GetBagSize(); ++slot)
            if (Item* held = pack->GetItemByPos(uint8_t(slot)))
                head.bagBefore[held->GetEntry()] += held->GetCount();
    }
    ItemQualifier itemQualifier(queued.itemId);
    // Cast on the exact queued stack, not by a chat link: a link with a
    // space in the item name never parsed to a bag item.
    if (bot->IsMoving())
        ai->StopMoving();
    if (bot->IsMounted())
        ai->Unmount();
    // The client's own cast with the stack as the item target; PlayerbotAI's
    // CastSpell would pass it as the casting item and the core refuses that.
    WorldPacket cast(CMSG_CAST_SPELL, 4 + 2 + 9);
    cast << uint32(13262);
    cast << uint16(TARGET_FLAG_ITEM);
    cast << item->GetObjectGuid().WriteAsPacked();
    bot->GetSession()->HandleCastSpellOpcode(cast);
    bool didCast = bot->IsNonMeleeSpellCasted(false);
    // Hold the bot still for the cast: the next action would walk off and
    // interrupt it.
    if (didCast)
        if (SpellEntry const* disenchant = sServerFacade.LookupSpellInfo(13262))
            SetDuration(GetSpellCastTime(disenchant) + 500);
    if (!didCast)
    {
        head.bagBefore.clear();
        return false;
    }
    // The cast only started: the results arrive in the loot window on a
    // later tick, so mark the head pending and let the collect pass above
    // finish it instead of reading the bags too early.
    head.collectLootOpen = true;
    head.itemName = sourceName;
    head.pendingResults = true;
    {
        TortoiseBots::GuildOrders::DisenchantQueueEntry row;
        row.botGuidLow = bot->GetGUIDLow();
        row.senderGuidLow = head.senderLow;
        row.itemGuidLow = head.itemGuidLow;
        row.itemId = head.itemId;
        row.itemName = head.itemName;
        row.pendingResults = true;
        TortoiseBots::GuildOrders::SaveDisenchantRow(row);
    }
    Player* requester = event.GetOwner() ? event.GetOwner() : GetMaster();
    ai->TellPlayer(requester, "Disenchanting " + chat->formatItem(itemQualifier),
        PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
    return true;
}
} // namespace ai
