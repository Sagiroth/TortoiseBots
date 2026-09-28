
#include "playerbot/playerbot.h"
#include "TrainerAction.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/strategy/values/BudgetValues.h"

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
            bot->LearnSpell(proto->EffectTriggerSpell[j], false);
            learned = true;
        }
    }
    if (!learned)
        ai->CastSpell(tSpell->spell, bot);

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

        // A class-trainer visit that found trainable spells but learned none of
        // them because every one was too expensive cannot be finished by
        // standing there: "should travel named::trainer class" stays true (the
        // cheapest green spell anywhere still fits the budget), so the bot
        // re-requested the same trainer roughly every 20 s forever. Blacklist
        // the purpose for 10 minutes with the same mechanism
        // ChooseTravelTargetAction::Execute uses when a search yields no usable
        // destination; RequestTravelTargetAction::isUseful reads the same key.
        // Like the other parks it is a cooling-off: any successful pick of
        // another purpose clears it early (setNewTarget clears all blacklists).
        if (hasTrainable && visitLearned == 0 && visitCheapestUnaffordable != UINT32_MAX && spells.empty() &&
            creature->GetCreatureInfo()->trainer_type == TRAINER_TYPE_CLASS)
        {
            std::string const purposeKey = "trainer class";
            if (AI_VALUE2(time_t, "manual time", "no travel purpose until::" + purposeKey) <= time(0))
                sPlayerbotAIConfig.logEvent(ai, "TrainerNoMoney",
                    std::to_string(visitCheapestUnaffordable),
                    std::to_string(AI_VALUE2(uint32, "free money for", (uint32)NeedMoneyFor::spells)));

            SET_AI_VALUE2(bool, "no active travel destinations", purposeKey, true);
            SET_AI_VALUE2(time_t, "manual time", "no travel purpose until::" + purposeKey, time(0) + 10 * MINUTE);
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
