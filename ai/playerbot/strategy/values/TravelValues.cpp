#include "playerbot/playerbot.h"
#include <algorithm>
#include "TravelValues.h"
#include "playerbot/TravelMgr.h"
#include "playerbot/LocalPickPolicy.h"
#include "MaintenanceValues.h"
#include "QuestValues.h"
#include "SharedValueContext.h"
#include "BudgetValues.h"
#include "GuildValues.h"
#include "Guild/GuildMgr.h"

using namespace ai;

EntryGuidps EntryGuidpsValue::Calculate()
{
    EntryGuidps guidps;

    for (auto& creatureDataPair : WorldPosition().GetCreaturesNear())
    {
        AsyncGuidPosition aGuidP(creatureDataPair);
        if (aGuidP.isValid() && !aGuidP.IsEventUnspawned())
        {
            aGuidP.FetchArea();
            guidps[aGuidP.GetEntry()].push_back(aGuidP);
        }
    }

    for (auto& goDataPair : WorldPosition().GetGameObjectsNear())
    {
        AsyncGuidPosition aGuidP(goDataPair);
        if (aGuidP.isValid() && !aGuidP.IsEventUnspawned())
        {
            aGuidP.FetchArea();
            guidps[-((int32)aGuidP.GetEntry())].push_back(aGuidP);
        }
    }

    return guidps;
}

// The gathering skill a game object's lock requires (herbalism / mining). Creatures no longer
// carry a gather purpose: a skinnable spawn point is a live mob, not a corpse, so those
// destinations were removed (see the map builder and NeedTravelPurposeValue).
static uint32 GameObjectLockSkill(GameObjectInfo const* gInfo)
{
    if (!gInfo)
        return SKILL_NONE;

    LockEntry const* lockInfo = sLockStore.LookupEntry(gInfo->GetLockId());
    if (!lockInfo)
        return SKILL_NONE;

    for (int i = 0; i < 8; ++i)
    {
        if (lockInfo->Type[i] != LOCK_KEY_SKILL)
            continue;

        if (uint32 skillId = SkillByLockType(LockType(lockInfo->Index[i])))
            return skillId;
    }

    return SKILL_NONE;
}

EntryTravelPurposeMap EntryTravelPurposeMapValue::Calculate()
{
    EntryQuestRelationMap const& relationMap = GAI_VALUE_REF(EntryQuestRelationMap, "entry quest relation");
    EntryGuidps const& guidpMap = GAI_VALUE_REF(EntryGuidps, "entry guidps");

    EntryTravelPurposeMap entryPurposeMap;

    std::vector<NPCFlags> allowedNpcFlags;

    allowedNpcFlags.push_back(UNIT_NPC_FLAG_INNKEEPER);
    allowedNpcFlags.push_back(UNIT_NPC_FLAG_GOSSIP);
    allowedNpcFlags.push_back(UNIT_NPC_FLAG_QUESTGIVER);
    allowedNpcFlags.push_back(UNIT_NPC_FLAG_FLIGHTMASTER);
    allowedNpcFlags.push_back(UNIT_NPC_FLAG_BANKER);
    allowedNpcFlags.push_back(UNIT_NPC_FLAG_AUCTIONEER);
    allowedNpcFlags.push_back(UNIT_NPC_FLAG_STABLEMASTER);
    allowedNpcFlags.push_back(UNIT_NPC_FLAG_PETITIONER);
    allowedNpcFlags.push_back(UNIT_NPC_FLAG_TABARDDESIGNER);

    allowedNpcFlags.push_back(UNIT_NPC_FLAG_TRAINER);
    allowedNpcFlags.push_back(UNIT_NPC_FLAG_VENDOR);
    allowedNpcFlags.push_back(UNIT_NPC_FLAG_REPAIR);

    std::unordered_map<NPCFlags, TravelDestinationPurpose> npcPurposeMap =
    {
        { UNIT_NPC_FLAG_REPAIR, TravelDestinationPurpose::Repair },
        { UNIT_NPC_FLAG_VENDOR, TravelDestinationPurpose::Vendor },
        { UNIT_NPC_FLAG_TRAINER, TravelDestinationPurpose::Trainer },
        { UNIT_NPC_FLAG_AUCTIONEER, TravelDestinationPurpose::AH }
    };

    for (uint32 entry = 0; entry < sCreatureStorage.GetMaxEntry(); ++entry)
    {
        CreatureInfo const* cInfo = sCreatureStorage.LookupEntry<CreatureInfo>(entry);

        if (!cInfo)
            continue;

        if (cInfo->flags_extra & CREATURE_EXTRA_FLAG_INVISIBLE)
            continue;

        DestinationPurose purpose = 0;

        if(relationMap.find(entry) != relationMap.end())
            for(auto& [questId, questFlag] : relationMap.at(entry))
                purpose |= questFlag;

        for (auto& flag : allowedNpcFlags)
        {
            if ((cInfo->npc_flags & flag) != 0)
            {
                purpose |= (uint32)TravelDestinationPurpose::GenericRpg;
                break;
            }
        }

        for (auto& [flag, flagPurpose] : npcPurposeMap)
        {
            if ((cInfo->npc_flags & flag) != 0)
            {
                // #404: a flagged-but-stockless NPC (entry 1650) is not a vendor destination: the
                // trip would sell nothing and each hello prints a core "empty trading item list"
                // error. Stock = npc_vendor rows or a non-empty vendor template. Quest/other
                // purposes above are untouched, so the NPC stays reachable for its quest.
                if (flagPurpose == TravelDestinationPurpose::Vendor &&
                    !sObjectMgr.GetNpcVendorItemList(entry) &&
                    (!cInfo->vendor_id || !sObjectMgr.GetNpcVendorTemplateItemList(cInfo->vendor_id)))
                    continue;
                purpose |= (uint32)flagPurpose;
            }
        }

        // Coinless creatures are grind destinations too: wildlife (wolves, boars,
        // spiders, scorpids, bears) carries no copper at all - it pays in XP, grey
        // vendor loot and skins - and is the bread-and-butter hunt of every level.
        // Only critters stay out (zero XP, not worth a walk). The per-bot rule that
        // owns this purpose (GrindTravelDestination::IsPossible) keeps owned and
        // hired bots on the old copper-only set; the map itself is team- and
        // bot-blind, so it has to offer both.
        if (cInfo->gold_min > 0 || cInfo->type != CREATURE_TYPE_CRITTER)
            purpose |= (uint32)TravelDestinationPurpose::Grind;

        if (cInfo->rank == CREATURE_ELITE_ELITE || cInfo->rank == CREATURE_ELITE_RAREELITE || cInfo->rank == CREATURE_ELITE_WORLDBOSS || cInfo->rank == CREATURE_ELITE_RARE)
        {
            if (cInfo->rank == 1)
            {
                auto it = guidpMap.find(entry);
                if (it != guidpMap.end() && it->second.size() == 1)
                    purpose |= (uint32)TravelDestinationPurpose::Boss;
            }
            else
                purpose |= (uint32)TravelDestinationPurpose::Boss;
        }

        // No gather purpose for creatures: a skinnable spawn point is a *live* mob, so the
        // destination could only ever deliver a grind, never a corpse to skin (skinning is a
        // corpse of the bot's own kill). Until now every skinnable entry got a GatherSkinning
        // destination whose points are static spawn coordinates - the source of the 600 yd-plus
        // skin errands (see NeedTravelPurposeValue).

        if (purpose > 0)
            entryPurposeMap[entry] = purpose;
    }

    for (uint32 entry = 0; entry < sGOStorage.GetMaxEntry(); ++entry)
    {
        GameObjectInfo const* gInfo = sObjectMgr.GetGameObjectInfo(entry);

        if (!gInfo)
            continue;

        // Penqle's GameObjectInfo has no ExtraFlags (cmangos uses it for invisible
        // markers; the bot is checking a creature flag here — likely a bot-side
        // bug). Skip the invisibility check; harmless until a host hook lands.

        uint32 purpose = 0;

        DestinationEntry goEntry = entry * -1;

        if (relationMap.find(goEntry) != relationMap.end())
            for (auto& [questId, questFlag] : relationMap.at(goEntry))
                purpose |= questFlag;

        std::vector<GameobjectTypes> allowedGoTypes;

        allowedGoTypes.push_back(GAMEOBJECT_TYPE_MAILBOX);

        for (auto type : allowedGoTypes)
        {
            if (gInfo->type == type)
            {
                purpose |= (uint32)TravelDestinationPurpose::Mail;
                break;
            }
        }

        if (uint32 skillId = GameObjectLockSkill(gInfo))
        {
            if (skillId == SKILL_MINING)
                purpose |= (uint32)TravelDestinationPurpose::GatherMining;
            else if (skillId == SKILL_HERBALISM)
                purpose |= (uint32)TravelDestinationPurpose::GatherHerbalism;
        }

        if (purpose > 0)
            entryPurposeMap[goEntry] = purpose;
    }

    return entryPurposeMap;
}

bool NeedTravelPurposeValue::Calculate()
{
    TravelDestinationPurpose purpose = TravelDestinationPurpose(stoi(getQualifier()));

    // Gather errands have two hard limits, applied before the per-purpose skill check below.
    //
    // Skinning is never a travel purpose. A GatherSkinning destination is a static spawn point of
    // a *live* skinnable creature, and a bot can only skin a corpse it looted itself - so the
    // errand can never pay off, while live it was the largest death cause of the beginner pool
    // (215 of 676 deaths in a 43-minute stage-2 window: level 1-5 bots walking into level 5-6
    // mobs for a spawn 600+ yd away). Skinning now happens where it belongs, on the bot's own
    // kill, right after that kill is looted (LootObjectStack + LootAction). The donor,
    // mod-playerbots, has no gather travel purpose at all.
    if (purpose == TravelDestinationPurpose::GatherSkinning)
        return false;

    // Herb and ore trips start at level 10. Below that every errand is a walk through mobs the
    // bot cannot fight (live: 147 herb/mining errand deaths, all with a killer above the bot's
    // level), and the nodes it passes on its way are picked up by the walk-past scan anyway.
    if ((purpose == TravelDestinationPurpose::GatherMining || purpose == TravelDestinationPurpose::GatherHerbalism) &&
        bot->GetLevel() < 10)
        return false;

    const std::map<TravelDestinationPurpose, SkillType> gatheringSkills =
    { {TravelDestinationPurpose::GatherFishing, SKILL_FISHING}
        , {TravelDestinationPurpose::GatherMining, SKILL_MINING}
        , {TravelDestinationPurpose::GatherHerbalism, SKILL_HERBALISM}
    };

    SkillType skill;

    switch (purpose)
    {
    case TravelDestinationPurpose::Repair:
        if (AI_VALUE2(bool, "group or", "should repair,can repair,following party"))
            return true;
        if (AI_VALUE2(bool, "has strategy", "free") && AI_VALUE(bool, "should repair") && AI_VALUE(bool, "can repair"))
            return true;
        break;
    case TravelDestinationPurpose::Vendor:
        //Real need only (issue #379 follow-up): the `should sell` && `can sell`
        //pair this replaces was true for every pool bot the moment it held food
        //or drink, because the random-bot item cheat skipped the consumable
        //decision and classified rations as vendor trash - live cycle 4 walked
        //2,300 vendor errands in 65 minutes (1,286 of the 4,394 sale rows were the
        //bot's own food and drink) and BuyAction never fired. A journey now needs
        //stock a vendor pays for and that is worth the walk, a durability below
        //the repair threshold, or an empty food/drink bag the bot can pay to
        //refill - plus the bag-pressure valve below for a bot whose bags are full
        //in the field. Both sides read the same ten-minute fruitless-errand park.
        return VendorTripNeeded(ai) || BagPressureVendorTrip(ai);
    case TravelDestinationPurpose::AH:
        if (AI_VALUE2(bool, "group or", "should ah sell,can ah sell,following party"))
            return true;
        if (AI_VALUE2(bool, "has strategy", "free") && AI_VALUE(bool, "should ah sell") && AI_VALUE(bool, "can ah sell"))
            return true;
        // Organic buyer demand (issue #405 rework, review 2): a masterless
        // pool bot holding spendable gold walks to its auction house on its
        // own feet and bids on arrival (no buyer teleport). Phase gate first:
        // it is the cheap clock check, so the purse/position work below only
        // runs in the open slice (review finding 7).
        if (!AI_VALUE2(bool, "has strategy", "free"))
            break;
        if (!ai::BuyerTripPhaseOpen(ai->GetFixedBotNumber(BotTypeNumber::RPG_PHASE_NUMBER, ai::kBuyerTripPhaseMax, 1)))
            break;
        if (ai::AhBuyerTripNeeded(ai))
            return true;
        break;
    case TravelDestinationPurpose::GatherFishing:
        if (!AI_VALUE2(bool, "has strategy", "tfish"))
            return false;
    case TravelDestinationPurpose::GatherMining:
    case TravelDestinationPurpose::GatherHerbalism:
        skill = gatheringSkills.at(purpose);
        if (bot->GetSkillValue(skill) >= std::min(bot->GetSkillMax(skill), bot->GetSkillMaxForLevel(bot)))
            return false;

        // Empty-search probe, same shape as the trainer class/trade probe
        // below: skill headroom alone raised the need while no node in the
        // window would be accepted, so the bot requested, searched, parked
        // a minute and re-asked (live: 414 empty GatherMining + 107 empty
        // GatherHerbalism searches in 23 min, mostly "0:empty"). Refuse
        // when no destination in the request window is active. Same window
        // the purpose request searches (RequestTravelTargetAction), same
        // entry source (all entries) and same acceptance (IsActive): a
        // picked node is always inside the window it was picked from, so
        // this stays true while walking its own trip.
        {
            PlayerTravelInfo const info(bot);
            DestinationList const nodes = sTravelMgr.GetDestinations(info,
                (uint32)purpose, {}, true, 10000.0f);
            if (std::none_of(nodes.begin(), nodes.end(),
                    [&](TravelDestination* node) { return node->IsActive(bot, info); }))
                return false;
        }

        return true;
    case TravelDestinationPurpose::Boss:
        return AI_VALUE(bool, "can fight boss");
    case TravelDestinationPurpose::Mail:
        return AI_VALUE(bool, "can get mail") && AI_VALUE(bool, "should get mail");
    case TravelDestinationPurpose::Explore:
        return ai->HasStrategy("explore", BotState::BOT_STATE_NON_COMBAT);
    case TravelDestinationPurpose::GenericRpg:
    {
        uint32 rpgPhase = ai->GetFixedBotNumber(BotTypeNumber::RPG_PHASE_NUMBER, 60, 1);

        if (rpgPhase < 15) //Only last 45 minutes of the hour allow generic rpg.
            return false;

        return !AI_VALUE2(bool, "manual bool", "is travel refresh");
    }
    case TravelDestinationPurpose::Grind:
    {
        // Beginners never take the GenericRpg walk this staggers against
        // (RpgTravelDestination::IsPossible blocks every non-vendor RPG errand
        // below level 5), so the last 15 minutes of each hour leave a level 1-4
        // pool bot with quests parked and no errand at all (issue #393). Let
        // them grind around the clock instead of idling at an NPC.
        bool const beginner = bot->GetLevel() < 5 && sRandomBotFacade.IsRandomBot(bot) && !ai->HasRealPlayerMaster();
        if (!beginner)
        {
            uint32 rpgPhase = ai->GetFixedBotNumber(BotTypeNumber::RPG_PHASE_NUMBER, 60, 1);

            if (rpgPhase > 45) //Only first 45 minutes of the hour allow generic grind.
                return false;
        }

        return !AI_VALUE2(bool, "manual bool", "is travel refresh");
    }
    default:
        return false;
    }

    return false;
}

bool ShouldTravelNamedValue::Calculate()
{
    std::string name = getQualifier();

    WorldPosition botPos(bot);

    if (name == "city")
    {
        if (bot->GetLevel() <= 5)
            return false;

        if (!botPos.isOverworld())
            return false;

        uint32 rpgPhase = ai->GetFixedBotNumber(BotTypeNumber::RPG_PHASE_NUMBER, ai::kBuyerTripPhaseMax, 1);

        // A city trip without capital business is a 3000 yd walk to nowhere
        // (live: 1614 "city" picks, 917 move failures, 520 drops against no
        // buyer/seller need of its own). Gate it on the same needs the
        // capital serves: stock to post at the AH, or an affordable AH
        // purchase. Trainer/vendor/mail trips carry their own need gates;
        // the battlemaster leg is gone (bots never queue on their own).
        // Only buyer leg is phase-gated to avoid checking AhBuyerTripNeeded continuously.
        bool shouldAhSell = AI_VALUE(bool, "should ah sell");
        if (!shouldAhSell && !(ai::BuyerTripPhaseOpen(rpgPhase) && ai::AhBuyerTripNeeded(ai)))
            return false;

        if (rpgPhase > 20) //Only first 20 minutes of the hour allow generic city pvp without reason.
            return false;

        if (AI_VALUE2(bool, "manual bool", "is travel refresh"))
            return false;

        return true;
    }
    else if (name == "pvp")
    {
        if (bot->GetLevel() <= 50)
            return false;

        int32 rpgStyle = AI_VALUE2(int32, "manual saved int", "rpg style override");

        if (rpgStyle < 0)
            rpgStyle = ai->GetFixedBotNumber(BotTypeNumber::RPG_STYLE_NUMBER, 100);

        if (rpgStyle > 10) //Only 10% of the bots like to go to world-pvp.
            return false;

        uint32 rpgPhase = ai->GetFixedBotNumber(BotTypeNumber::RPG_PHASE_NUMBER, 60, 1);

        if (rpgPhase > 15) //Only first 15 minutes of the hour allow world pvp.
            return false;

        if (!botPos.isOverworld())
            return false;

        return true;
    }
    else if (name == "guild meeting")
    {
        if (!bot->GetGuildId())
            return false;

        Guild* guild = sGuildMgr.GetGuildById(bot->GetGuildId());
        if (!guild)
            return false;

        std::string motd = guild->GetMOTD();
        if (motd.empty())
            return false;

        // Parse guild MOTD for the meeting time.
        // Meeting: <location> <start time> <end time>
        auto pos = motd.find("Meeting:");
        if (pos == std::string::npos)
            return false;

        std::string body = motd.substr(pos + 8);
        std::vector<std::string> tokens;
        { std::istringstream iss(body); std::string t; while (iss >> t) tokens.push_back(t); }
        if (tokens.size() < 3)
            return false;

        auto parseTime = [](const std::string& tok, int& h, int& m) -> bool {
            auto colon = tok.find(':');
            if (colon == std::string::npos) return false;
            h = std::stoi(tok.substr(0, colon));
            std::string rest = tok.substr(colon + 1);
            std::string digits, suffix;
            for (char c : rest) { if (std::isdigit(c)) digits += c; else suffix += (char)toupper(c); }
            m = std::stoi(digits);
            if (h < 0 || h > 23 || m < 0 || m > 59) return false;
            if (suffix == "PM" && h != 12) h += 12;
            if (suffix == "AM" && h == 12) h = 0;
            return true;
        };

        int sh, sm, eh, em;
        if (!parseTime(tokens[tokens.size() - 2], sh, sm)) return false;
        if (!parseTime(tokens[tokens.size() - 1], eh, em)) return false;

        time_t now = time(nullptr);
        tm local = *localtime(&now);
        tm startTm = local; startTm.tm_hour = sh; startTm.tm_min = sm; startTm.tm_sec = 0;
        tm endTm = local;   endTm.tm_hour = eh;   endTm.tm_min = em;   endTm.tm_sec = 0;
        time_t start = mktime(&startTm);
        time_t end = mktime(&endTm);
        if (end < start) end += 24 * 3600;

        return (now >= start - 30 * 60) && (now <= end);
    }
    else if (name == "guild order")
    {
        return AI_VALUE(bool, "has guild travel order");
    }
    else if (name == "reagent vendor")
    {
        return AI_VALUE(bool, "needs profession reagents");
    }
    else if (name == "mount")
    {
        if (AI_VALUE(bool, "can buy mount"))
            return true;

        return false;
    }
    else if (name.find("trainer") == 0)
    {
        // Park pre-gate (perf): a parked trainer need stays parked until the
        // purse covers the cheapest rank (money park) or a level-up clears it
        // (AutoLearnSpellAction). The full probe below walks the trainer map
        // and the destination window; skip it while the park timestamp the
        // tail of this branch reads is still in the future and the purse
        // cannot have lifted it. Same verdict, no destination walk: a money
        // park with no new money still refuses, a teaching park still refuses.
        // The tail re-reads the timestamp, so a park that expired (or was
        // cleared by a ding) since the last visit falls through to the probe.
        if (AI_VALUE2(time_t, "manual time", "no travel purpose until::" + name) > time(0) &&
            !AI_VALUE2(bool, "manual bool", "trainer park needs money"))
            return false;

        if (ai->HasRealPlayerMaster())
            return false;
        TrainerType trainerType = TRAINER_TYPE_CLASS;
        NeedMoneyFor budgetType = NeedMoneyFor::spells;

        if (name == "trainer mount")
        {
            trainerType = TRAINER_TYPE_MOUNTS;
            budgetType = NeedMoneyFor::mount; //Only train mounts when you can actually buy mount
        }
        if (name == "trainer trade")
        {
            trainerType = TRAINER_TYPE_TRADESKILLS;
            budgetType = NeedMoneyFor::skilltraining;
        }
        if (name == "trainer pet")
        {
            trainerType = TRAINER_TYPE_PETS;
            budgetType = NeedMoneyFor::anything;
        }

        if (AI_VALUE2(uint32, "train cost", trainerType) == 0) //Has nothing to train
            return false;

        // Class and trade trips need a trainer of the bot's OWN class/craft in
        // reach: "train cost" counts every green rank in the world, so without
        // this a bot with nothing learnable nearby still raised the trigger,
        // requested, and parked for a minute, every minute (live: ~1000 empty
        // + ~400 rejected "trainer class" searches in 20 min, ~33 wasted trade
        // picks/h at level 5). The search uses the same window. The need
        // doubles as the in-flight trip's stored condition, so this must stay
        // true while walking: a picked trainer is always inside the window it
        // was picked from, and learning only ever removes entries, which is
        // exactly when the trip should end.
        if (name == "trainer class" || name == "trainer trade")
        {
            std::vector<int32> entries =
                AI_VALUE2(std::vector<int32>, "available trainers", (uint32)trainerType);
            if (entries.empty())
                return false;

            bool const masterless = sRandomBotFacade.IsRandomBot(bot) && !ai->HasRealPlayerMaster();
            // Same window the request searches (ChooseTravelTargetAction).
            float const window = name == "trainer class" ?
                ClassTrainerRequestMaxDistance(masterless, bot->GetLevel(),
                    AI_VALUE2(std::vector<TrainerSpell const*>, "trainable spells", (uint32)TRAINER_TYPE_CLASS).size(), 10000.0f) :
                CampRequestMaxDistance(masterless, bot->GetLevel(), 10000.0f);
            // Only trainers the pick would accept: possible (level, outgrown
            // town) and active (not hostile, not just visited). Counting every
            // listed trainer raised the need for an enemy-faction or outgrown
            // trainer nearby, and the search then refused them all and parked
            // a minute, every minute (1836 "inactive" refusals in 20 min).
            PlayerTravelInfo const info(bot);
            DestinationList const trainers = sTravelMgr.GetDestinations(info,
                (uint32)TravelDestinationPurpose::Trainer, entries, true, window);
            if (std::none_of(trainers.begin(), trainers.end(),
                    [&](TravelDestination* trainer) { return trainer->IsActive(bot, info); }))
                return false;
        }

        // Partial-purse rule: travel when at least the cheapest trainable spell
        // fits the free-money budget. The old "has all money for" check demanded
        // the full batch price up front, so bots never visited until they could
        // buy everything. TrainerAction still skips individual spells that are
        // too expensive once there.
        uint32 minSpellCost = UINT32_MAX;
        for (TrainerSpell const* trainable : AI_VALUE2(std::vector<TrainerSpell const*>, "trainable spells", trainerType))
            if (trainable && trainable->spellCost < minSpellCost)
                minSpellCost = trainable->spellCost;

        if (minSpellCost == UINT32_MAX)
            return false;

        uint32 const freeMoney = AI_VALUE2(uint32, "free money for", (uint32)budgetType);
        bool const canAfford = freeMoney >= minSpellCost;

        // One trainer journey at a time is enforced request-side
        // (RequestNamedTravelTargetAction::isAllowed): this value doubles as the
        // in-flight trip's stored condition, and the 10-minute window here
        // turned every new trainer trip to cooldown on its first step.

        // A fruitless visit parks the trainer (TrainerAction). The park is a
        // cooling-off for the training need that visit found, not a ban, and it must
        // not outlive it. A park set because nothing was affordable ends the moment
        // the purse covers the cheapest rank - that is exactly the change that makes
        // the visit worth repeating - while a park set because the trainer had nothing
        // to teach is held: money is not what made it fruitless, so the bot is not sent
        // back there the moment it loots. A level-up lifts either kind
        // (AutoLearnSpellAction, the ding handler that already expires the travel
        // target). The timestamp is what every reader of the park consults, so
        // clearing it unblocks the nearby-trainer service as well; the
        // "no active travel destinations" flag alone would not, it is cleared by the
        // next successful pick of any purpose while the timestamp survives. While the
        // park holds it also keeps the need from pinning a bot in a capital: the
        // leave-outgrown-zone rule waits for trainer needs.
        std::string const parkKey = "no travel purpose until::" + name;

        if (AI_VALUE2(time_t, "manual time", parkKey) > time(0))
        {
            bool const moneyPark = AI_VALUE2(bool, "manual bool", "trainer park needs money");
            if (!canAfford || !moneyPark)
                return false;

            RESET_AI_VALUE2(time_t, "manual time", parkKey);
        }

        return canAfford;
    }

    return false;
}

namespace outgrown_yield
{
// A finished quest the bot can actually hand in: complete, unrewarded,
// rewardable right now, not elite/dungeon unless the bot can fight bosses,
// and the rpg-quest strategy is on (quest travel needs it). Anything else
// would pin the leave rule forever behind an unturnable quest.
bool HasHandInAbleQuest(PlayerbotAI* ai, Player* bot)
{
    if (!ai->HasStrategy("rpg quest", BotState::BOT_STATE_NON_COMBAT))
        return false;
    bool const canFightBoss = ai->GetAiObjectContext()->GetValue<bool>("can fight boss")->Get();
    for (auto& [questId, questStatus] : bot->getQuestStatusMap())
    {
        if (questStatus.m_rewarded || questStatus.m_status != QUEST_STATUS_COMPLETE)
            continue;
        Quest const* quest = sObjectMgr.GetQuestTemplate(questId);
        if (!quest)
            continue;
        if ((quest->GetType() == QUEST_TYPE_ELITE || quest->GetType() == QUEST_TYPE_DUNGEON) && !canFightBoss)
            continue;
        if (!bot->CanRewardQuest(quest, false))
            continue;
        return true;
    }
    return false;
}

} // namespace

bool ShouldLeaveOutgrownZoneValue::Calculate()
{
    if (!sPlayerbotAIConfig.leaveOutgrownZones)
        return false;

    if (!sRandomBotFacade.IsRandomBot(bot) || ai->HasRealPlayerMaster())
        return false;

    if (!bot->IsAlive())
        return false;

    if (bot->GetLevel() < 10)
        return false;

    // No IsActive early-out here: this value doubles as the travel condition
    // stored on the Grind target it requests, and bailing while a target is
    // active drops that target to cooldown on the first status check after it
    // is picked. Re-fire is already gated - request actions only run while no
    // travel target is active (RequestTravelTargetAction::isUseful,
    // TravelActionMultiplier) - so a true value while traveling is harmless.

    // Pending business first, everywhere - but only business the bot can
    // actually do, and only for a bounded time. An unbounded yield deadlocks:
    // an unhand-in-able quest (elite/dungeon solo, cross-continent turn-in,
    // no rpg-quest strategy) or an unreachable local vendor would pin the bot
    // in the outgrown zone forever. So: quests yield only while hand-in-able
    // (rewardable now, not elite/dungeon unless the bot can fight bosses, rpg
    // quest strategy on), vendor/repair yield only while a permitted vendor
    // exists (not skipped by the outgrown service gate - i.e. same zone but
    // fitting, or a capital), and after ~10 min outgrown the bot leaves
    // anyway. All reads are 2-tick cached AI values plus a bounded quest-log
    // walk - no world scan, no DB.
    //
    // Zone level, not sub-area: sub-areas scatter ±4 around their zone
    // (task E: Galwurth fired 3x at 10.59 in a low Durotar sub-area while
    // Durotar's zone level is 8, i.e. not outgrown at 10). The destination
    // floor below is a zone-level test, so the trigger must be one too or
    // the two disagree on what "outgrown" means.
    AreaTableEntry const* botArea = WorldPosition(bot).GetArea();
    uint32 botZoneId = botArea ? (botArea->ZoneId ? botArea->ZoneId : botArea->Id) : 0;
    int32 zoneLevel = 0;
    bool const zoneKnown = botZoneId && sTravelMgr.TryGetValidatedAreaLevel(botZoneId, zoneLevel) && zoneLevel > 0;
    bool const outgrown = zoneKnown && zoneLevel + 5 < (int32)bot->GetLevel();
    bool leaveAnyway = false;
    {
        if (outgrown)
        {
            time_t outgrownSince = AI_VALUE2(time_t, "manual time", "outgrown since");
            if (!outgrownSince)
                SET_AI_VALUE2(time_t, "manual time", "outgrown since", time(0));
            else if (time(0) - outgrownSince > 10 * MINUTE)
                leaveAnyway = true;
        }
        else
            SET_AI_VALUE2(time_t, "manual time", "outgrown since", (time_t)0);
    }
    if (!leaveAnyway)
    {
        // Outside capitals only hand-in-able quests hold the bot; selling and
        // repairs happen at the next town that fits its level (the service
        // gate skips low-zone vendors), so waiting for them here would deadlock.
        if (outgrown_yield::HasHandInAbleQuest(ai, bot))
            return false;
    }

    // Capitals are service stops, not places to stay: a level 10+ bot idling
    // in a capital with no pending capital service need should leave.
    // Same priority-guard reasoning as above, extended to the capital-only
    // services (trainers, mount vendor, mailbox).
    if (WorldPosition(bot).HasAreaFlag(AREA_FLAG_CAPITAL))
    {
        if (AI_VALUE2(bool, "should travel named", "trainer class"))
            return false;
        if (AI_VALUE2(bool, "should travel named", "trainer mount"))
            return false;
        if (AI_VALUE2(bool, "should travel named", "trainer trade"))
            return false;
        if (AI_VALUE2(bool, "should travel named", "mount"))
            return false;
        if (AI_VALUE2(bool, "need travel purpose", std::to_string((uint32)TravelDestinationPurpose::AH)))
            return false;
        if (AI_VALUE2(bool, "need travel purpose", std::to_string((uint32)TravelDestinationPurpose::Mail)))
            return false;
        if (AI_VALUE2(bool, "need travel purpose", std::to_string((uint32)TravelDestinationPurpose::Vendor)))
            return false;
        if (AI_VALUE2(bool, "need travel purpose", std::to_string((uint32)TravelDestinationPurpose::Repair)))
            return false;
        return true;
    }

    // Fail closed: unknown area levels never trigger the rule (zoneKnown
    // above covers the unresolvable/unknown case).
    if (!zoneKnown)
        return false;

    return outgrown;
}

bool TravelTargetActiveValue::Calculate()
{
    return AI_VALUE(TravelTarget*, "travel target")->IsActive();
};

bool TravelTargetReadyValue::Calculate()
{
    return AI_VALUE(TravelTarget*, "leader travel target")->GetStatus() == TravelStatus::TRAVEL_STATUS_READY;
};

bool TravelTargetTravelingValue::Calculate()
{
    return AI_VALUE(TravelTarget*, "leader travel target")->GetStatus() == TravelStatus::TRAVEL_STATUS_TRAVEL;
};

bool TravelTargetWorkingValue::Calculate()
{
    return AI_VALUE(TravelTarget*, "leader travel target")->GetStatus() == TravelStatus::TRAVEL_STATUS_WORK;
};

bool QuestStageActiveValue::Calculate()
{
    uint32 questId = getMultiQualifierInt(getQualifier(), 0, ",");
    TravelDestinationPurpose purpose = (TravelDestinationPurpose)getMultiQualifierInt(getQualifier(), 1, ",");

    uint32 objective = 0;

    switch (purpose)
    {
    case TravelDestinationPurpose::QuestGiver:
        if (bot->HasQuest(questId))
            return false;
        break;
    case TravelDestinationPurpose::QuestTaker:
        // CanCompleteQuest answers "could this quest still flip to complete", and
        // the core returns false the moment it has - "not allow re-complete quest".
        // Asking it here threw away exactly the quests that were ready to hand in,
        // leaving only the brief window where the objectives are met but the status
        // has not been updated yet. Bots that missed that window carried the quest
        // forever: ten characters followed from level 1 accumulated 25 completed
        // quests between them without a single QuestTravelToTaker event.
        if (bot->GetQuestRewardStatus(questId))
            return false;
        if (bot->GetQuestStatus(questId) != QUEST_STATUS_COMPLETE && !bot->CanCompleteQuest(questId))
            return false;
        break;
    case TravelDestinationPurpose::QuestObjective1:
        objective = 1;
        break;
    case TravelDestinationPurpose::QuestObjective2:
        objective = 2;
        break;
    case TravelDestinationPurpose::QuestObjective3:
        objective = 3;
        break;
    case TravelDestinationPurpose::QuestObjective4:
        objective = 4;
        break;
    }

    if(objective)
        if (!AI_VALUE2(bool, "need quest objective", "{" + std::to_string(questId) + "," + std::to_string(objective - 1) + "}"))
            return false;

    return true;
}
