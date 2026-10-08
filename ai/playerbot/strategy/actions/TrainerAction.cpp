
#include "playerbot/playerbot.h"
#include "TrainerAction.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/strategy/values/BudgetValues.h"
#include "playerbot/SpellRankPolicy.h"
#include "playerbot/TravelMgr.h"

using namespace ai;

void TrainerAction::Learn(uint32 cost, ObjectGuid trainerGuid, uint32 spellId, TrainerSpell const* tSpell, std::ostringstream& msg)
{
    if (sPlayerbotAIConfig.autoTrainSpells != "free" &&  !ai->HasCheat(BotCheatMask::gold))
    {
        if (AI_VALUE2(uint32, "free money for", (uint32)NeedMoneyFor::spells) < cost)
        {
            if (cost < visitCheapestUnaffordable)
                visitCheapestUnaffordable = cost;
            msg << " - too expensive";
            return;
        }

        bot->ModifyMoney(-int32(cost));
    }

    SpellEntry const* proto = sServerFacade.LookupSpellInfo(tSpell->spell);
    if (!proto)
        return;

    bool learned = false;
    for (int j = 0; j < 3; ++j)
    {
        if (proto->Effect[j] == SPELL_EFFECT_LEARN_SPELL && proto->EffectTriggerSpell[j])
        {
            // Issue #381: the visit loop only lists green rows, but a stale
            // cached row can still teach above the bot's level; the taught
            // spell's own level decides (shared rule in
            // ai/playerbot/SpellRankPolicy.h).
            SpellEntry const* taughtInfo = sServerFacade.LookupSpellInfo(proto->EffectTriggerSpell[j]);
            if (taughtInfo && !ai::SpellRankTeachableNow(bot->GetLevel(), taughtInfo->spellLevel,
                    taughtInfo->spellLevel != 0, true))
                continue;
            bot->LearnSpell(proto->EffectTriggerSpell[j], false);
            learned = true;
        }
    }
    // A rank refused by the level gate above leaves nothing learned: casting
    // the teaching spell instead would re-teach the refused rank through the
    // core, so count the visit without casting.
    if (!learned)
        return;

    ++visitLearned;

    sPlayerbotAIConfig.logEvent(ai, "TrainerAction", proto->SpellName[0], std::to_string(proto->Id));

    msg << " - learned";
}

bool TrainerAction::Iterate(Player* requester, Creature* creature, TrainerSpellAction action, SpellIds& spells)
{
    bool hasHeader = false;
    bool hasTrainable = false;

    TrainerSpellData const* cSpells = creature->GetTrainerSpells();
    TrainerSpellData const* tSpells = creature->GetTrainerTemplateSpells();
    float fDiscountMod =  bot->GetReputationPriceDiscount(creature);
    uint32 totalCost = 0;

    TrainerSpellMap trainer_spells;
    if (cSpells)
        trainer_spells.insert(cSpells->spellList.begin(), cSpells->spellList.end());
    if (tSpells)
        trainer_spells.insert(tSpells->spellList.begin(), tSpells->spellList.end());

    for (TrainerSpellMap::const_iterator itr =  trainer_spells.begin(); itr !=  trainer_spells.end(); ++itr)
    {
        TrainerSpell const* tSpell = &itr->second;

        if (!tSpell)
            continue;

        uint32 reqLevel = 0;

        reqLevel = tSpell->reqLevel;
        TrainerSpellState state = bot->GetTrainerSpellState(tSpell);
        if (state != TRAINER_SPELL_GREEN)
            continue;

        hasTrainable = true;
        uint32 spellId = tSpell->spell;
        const SpellEntry *const pSpellInfo =  sServerFacade.LookupSpellInfo(spellId);
        if (!pSpellInfo)
            continue;


        if (!spells.empty() && spells.find(tSpell->spell) == spells.end())
            continue;

        uint32 cost = uint32(floor(tSpell->spellCost *  fDiscountMod));
        totalCost += cost;

        std::ostringstream out;
        out << chat->formatSpell(pSpellInfo) << chat->formatMoney(cost);

        if (action)
            (this->*action)(cost, creature->getObjectGuid(), itr->first, tSpell, out);

        if (!hasHeader)
        {
            TellHeader(requester, creature);
            hasHeader = true;
        }
        ai->TellPlayer(requester, out, PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
    }

    if(hasHeader)
        TellFooter(requester, totalCost);
    else if (!ai->GetMaster() || sServerFacade.getDistance2d(bot, ai->GetMaster()) < sPlayerbotAIConfig.reactDistance || ai->HasStrategy("debug", BotState::BOT_STATE_NON_COMBAT))
        ai->TellPlayerNoFacing(requester, "No spells can be learned from this trainer");

    return hasTrainable;
}

bool TrainerAction::Execute(Event& event)
{
    Player* requester = event.GetOwner() ? event.GetOwner() : GetMaster();
    std::string text = event.GetParam();
    Creature* creature = nullptr;

    if (event.GetSource() == "rpg action")
    {
        ObjectGuid guid = event.getObject();
        creature = ai->GetCreature(guid);
    }
    else
    {
        if (requester)
            creature = ai->GetCreature(requester->GetSelectionGuid());
        else
            return false;
    }

#ifdef MANGOS
    if (!creature || !creature->IsTrainer())
#endif
#ifdef CMANGOS
    if (!creature || !creature->IsTrainer())
#endif
        return false;

    if (!creature->IsTrainerOf(bot, false))
    {
        if (!ai->GetMaster() || sServerFacade.getDistance2d(bot, ai->GetMaster()) < sPlayerbotAIConfig.reactDistance || ai->HasStrategy("debug", BotState::BOT_STATE_NON_COMBAT))
            ai->TellPlayerNoFacing(requester, "This trainer cannot teach me");
        return false;
    }

    // check present spell in trainer spell list
    TrainerSpellData const* cSpells = creature->GetTrainerSpells();
    TrainerSpellData const* tSpells = creature->GetTrainerTemplateSpells();
    if (!cSpells && !tSpells)
    {
        if (!ai->GetMaster() || sServerFacade.getDistance2d(bot, ai->GetMaster()) < sPlayerbotAIConfig.reactDistance || ai->HasStrategy("debug", BotState::BOT_STATE_NON_COMBAT))
            ai->TellPlayerNoFacing(requester, "No spells can be learned from this trainer");
        return false;
    }

    uint32 spell = chat->parseSpell(text);
    SpellIds spells;
    if (spell)
        spells.insert(spell);

    if (text.find("learn") != std::string::npos || sRandomBotFacade.IsFreeBot(bot) || (sPlayerbotAIConfig.autoTrainSpells != "no" && (creature->GetCreatureInfo()->trainer_type != TRAINER_TYPE_TRADESKILLS || !ai->HasActivePlayerMaster()))) //Todo rewrite to only exclude start primary profession skills and make config dependent.
    {
        visitCheapestUnaffordable = UINT32_MAX;
        visitLearned = 0;
        bool const hasTrainable = Iterate(requester, creature, &TrainerAction::Learn, spells);
        if (hasTrainable)
            context->ClearValues("item usage"); //Bot might be able to use new items.

        // A visit that actually bought something ends the "one trip at a time"
        // window (ShouldTravelNamedValue): the next rank the bot can afford - a
        // later ding, or coins it looted on the way - is trainable right away
        // instead of waiting out the ten minutes. A visit that bought nothing
        // leaves the window alone; the fruitless-visit park below covers that case.
        if (visitLearned > 0)
            RESET_AI_VALUE2(time_t, "manual time", "trainer trip since");

        // A class-trainer visit that achieved nothing cannot be finished by standing
        // there. That is every visit that learned no spell and left the bot with
        // nothing learnable and affordable: either this trainer has nothing green to
        // teach it at all (hasTrainable false - and then nothing was learned by
        // definition), or the ranks it offers were all too expensive
        // (visitCheapestUnaffordable). Meanwhile "should travel named::trainer class"
        // reads the whole trainable-spell map, so the cheapest green rank anywhere
        // still fits the budget and the need stays true - the bot re-requested a
        // trainer roughly every 20 s forever. Measured on the stage-3 pool at level 5:
        // 930 trainer-class picks against 395 grind picks, 105 of 263 level-5 bots
        // with no grind target at all, and the quartile that travelled to trainers
        // most (8.6 picks/h) needed 32.6 min for 5->6 against 23.8 min for the one
        // that stayed in the field. Blacklist the purpose for 10 minutes with the same
        // mechanism ChooseTravelTargetAction::Execute uses when a search yields no
        // usable destination; RequestTravelTargetAction::isUseful reads the same key.
        //
        // The park is timed, not flag-based: readers of the trainer park
        // (ShouldTravelNamedValue, the nearby-trainer service) consult this
        // timestamp, so it holds for its full ten minutes whatever else the bot picks
        // up in the meantime. It ends early only when what made the visit fruitless
        // changed: a level-up (AutoLearnSpellAction) always, and - for the
        // nothing-affordable case - a purse that now covers the cheapest rank
        // (ShouldTravelNamedValue). The reason is recorded with the park, so a bot
        // that walked to a trainer which teaches it nothing is not sent back there the
        // moment it loots a copper.
        bool const nothingLearnable = !hasTrainable || visitCheapestUnaffordable != UINT32_MAX;
        bool const fruitless = visitLearned == 0 && nothingLearnable;

        if (fruitless && spells.empty() &&
            creature->GetCreatureInfo()->trainer_type == TRAINER_TYPE_CLASS)
        {
            std::string const purposeKey = "trainer class";
            if (visitCheapestUnaffordable != UINT32_MAX &&
                AI_VALUE2(time_t, "manual time", "no travel purpose until::" + purposeKey) <= time(0))
                sPlayerbotAIConfig.logEvent(ai, "TrainerNoMoney",
                    std::to_string(visitCheapestUnaffordable),
                    std::to_string(AI_VALUE2(uint32, "free money for", (uint32)NeedMoneyFor::spells)));

            SET_AI_VALUE2(bool, "manual bool", "trainer park needs money", visitCheapestUnaffordable != UINT32_MAX);
            SET_AI_VALUE2(bool, "no active travel destinations", purposeKey, true);
            SET_AI_VALUE2(time_t, "manual time", "no travel purpose until::" + purposeKey, time(0) + 10 * MINUTE);
        }

        // The visit is the whole errand: a trip to this trainer is done once it has
        // taught what it can. Left alone the target sat in its work state for the full
        // five-minute expiry, and a pool bot with nothing else to do stood at the
        // trainer for those five minutes (93 of 643 frozen bots at 2000, all trained).
        TravelTarget* target = AI_VALUE(TravelTarget*, "travel target");
        if (!ai->HasActivePlayerMaster() && target->GetStatus() == TravelStatus::TRAVEL_STATUS_WORK &&
            target->GetEntry() == (int32)creature->GetEntry())
        {
            sTravelMgr.SetNullTravelTarget(target);
            RESET_AI_VALUE(bool, "travel target active");
        }
    }
    else
        Iterate(requester, creature, NULL, spells);

    return true;
}

void TrainerAction::TellHeader(Player* requester, Creature* creature)
{
    std::ostringstream out; out << "--- Can learn from " << creature->GetName() << " ---";
    ai->TellPlayer(requester, out, PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
}

void TrainerAction::TellFooter(Player* requester, uint32 totalCost)
{
    if (totalCost)
    {
        std::ostringstream out; out << "Total cost: " << chat->formatMoney(totalCost);
        ai->TellPlayer(requester, out, PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
    }
}
