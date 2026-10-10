
#include "playerbot/playerbot.h"
#include "TrainerValues.h"
#include "SharedValueContext.h"
#include "playerbot/PlayerbotHelpMgr.h"
#include "playerbot/strategy/values/TradeTrainerPolicy.h"
#include "../../../../runtime/ProfessionGrantPolicy.h"

using namespace ai;


trainableSpellMap* TrainableSpellMapValue::Calculate()
{
    trainableSpellMap* spellMap = new trainableSpellMap;

    //           template, trainer
    std::unordered_map <uint32, std::vector<CreatureInfo const*>> trainerTemplateIds;

    //Select all trainer lists and their trainers.
    for (uint32 id = 0; id < sCreatureStorage.GetMaxEntry(); ++id)
    {
        CreatureInfo const* creatureInfo = sCreatureStorage.LookupEntry<CreatureInfo>(id);
        if (!creatureInfo)
            continue;

        if (!creatureInfo->trainer_type && !creatureInfo->trainer_class)
            continue;

        if(creatureInfo->trainer_id)
            trainerTemplateIds[creatureInfo->trainer_id].push_back(creatureInfo);
        else
            trainerTemplateIds[id].push_back(creatureInfo);
    }

    for (auto& [templateOrEntryId, trainers] : trainerTemplateIds)
    {
        TrainerSpellData const* trainer_spells = sObjectMgr.GetNpcTrainerTemplateSpells(templateOrEntryId);
        if (!trainer_spells)
            trainer_spells = sObjectMgr.GetNpcTrainerSpells(templateOrEntryId);

        if (!trainer_spells)
            continue;

        CreatureInfo const* firstTrainer = trainers.front();

        TrainerType trainerType = (TrainerType)firstTrainer->trainer_type;

        uint32 spellRequirement = 0;
        if (trainerType == TRAINER_TYPE_CLASS || trainerType == TRAINER_TYPE_PETS)
            spellRequirement = firstTrainer->trainer_class;
        else if (trainerType == TRAINER_TYPE_MOUNTS)
            spellRequirement = firstTrainer->trainer_race;

        for (auto& [id, trainerSpell] : trainer_spells->spellList)
        {
            const TrainerSpell* sameTrainerSpell = &trainerSpell;
            for (auto& [otherTrainerSpell, trainers] : (*spellMap)[trainerType][spellRequirement])
            {
                if (otherTrainerSpell->spell != trainerSpell.spell)
                    continue;

                if (otherTrainerSpell->spellCost != trainerSpell.spellCost)
                    continue;

                if (otherTrainerSpell->reqSkill != trainerSpell.reqSkill)
                    continue;

                if (otherTrainerSpell->reqSkillValue != trainerSpell.reqSkillValue)
                    continue;

                if (otherTrainerSpell->reqLevel != trainerSpell.reqLevel)
                    continue;

                sameTrainerSpell = otherTrainerSpell;
                break;
            }

            if (trainerType == TRAINER_TYPE_TRADESKILLS)
            {
                if (trainerSpell.reqSkill)
                    spellRequirement = trainerSpell.reqSkill;
                else
                {
                    // exist, already checked at loading
                    SpellEntry const* spell = sSpellTemplate.LookupEntry<SpellEntry>(trainerSpell.spell);

                    spellRequirement = spell->EffectMiscValue[1];
                }
            }

            for (auto& trainer : trainers)
                (*spellMap)[trainerType][spellRequirement][sameTrainerSpell].push_back(trainer->entry);
        }
    }

    return spellMap;
}

TrainerStateSignature TrainerStateSignature::Of(Player* bot)
{
    TrainerStateSignature s;
    s.level = bot->GetLevel();
    s.knownSpells = static_cast<uint32>(bot->GetSpellMap().size());
    s.freeProfessionPoints = bot->GetFreePrimaryProfessionPoints();
    // Skill value and bonus fields of every slot (Player.cpp PLAYER_SKILL_INDEX
    // layout: id, value, bonus per slot): any skill-up changes the sum.
    for (uint32 slot = 0; slot < PLAYER_MAX_SKILLS; ++slot)
        s.skills += uint64(bot->GetUInt32Value(PLAYER_SKILL_INFO_1_1 + slot * 3 + 1)) +
            bot->GetUInt32Value(PLAYER_SKILL_INFO_1_1 + slot * 3 + 2);
    return s;
}

std::vector<TrainerSpell const*> TrainableSpellsValue::Calculate()
{
    TrainerStateSignature const now = TrainerStateSignature::Of(bot);
    if (calculated && now == signature)
        return value;
    calculated = true;
    signature = now;

    std::vector<TrainerSpell const*> trainableSpells;

    int8 qualifierType = getQualifier().empty() ? -1 : stoi(getQualifier());

    trainableSpellMap* spellMap = GAI_VALUE(trainableSpellMap*, "trainable spell map");

    for (auto& [trainerType, spellReqList] : *spellMap)
    {
        if (trainerType >= 0 && trainerType != qualifierType)
            continue;

        for (auto& [requirement, trainerSpellList] : spellReqList)
        {
            if (trainerType == TRAINER_TYPE_CLASS && requirement != bot->GetClass())
                continue;
            if (trainerType == TRAINER_TYPE_MOUNTS && requirement != bot->GetRace())
                continue;

            for (auto& [trainerSpell, trainers] : trainerSpellList)
            {
                TrainerSpellState state = bot->GetTrainerSpellState(trainerSpell);
                if (state != TRAINER_SPELL_GREEN)
                    continue;

                // Skip initial profession training below the pool grant level:
                // pool bots earn their primary pair at PRIMARY_PROFESSION_MIN_LEVEL
                // (5), so rank-1 profession spells become trainable from the same
                // level instead of 10.
                if (!TortoiseBots::IsRankOneProfessionTrainable(bot->GetLevel()) && sSpellMgr.IsProfessionSpell(trainerSpell->spell) && sSpellMgr.GetSpellRank(trainerSpell->spell) == 1)
                    continue;

                trainableSpells.push_back(trainerSpell);
            }
        }
    }

    return trainableSpells;
}

std::string TrainableSpellsValue::Format()
{
    std::vector<std::string> vec;
    for (auto t : value) {
        SpellEntry const* spell = sServerFacade.LookupSpellInfo(t->spell);
        if (!spell)
            continue;
        vec.push_back(chat->formatSpell(spell));
    }

    return sPlayerbotHelpMgr.makeList(vec, "[<part>]");
}

std::vector<int32> AvailableTrainersValue::Calculate()
{
    TrainerStateSignature const now = TrainerStateSignature::Of(bot);
    if (calculated && now == signature)
        return value;
    calculated = true;
    signature = now;

    std::vector<TrainerSpell const*> trainableSpells = AI_VALUE2(std::vector<TrainerSpell const*>, "trainable spells", getQualifier());;
    std::vector<int32> retTrainers;

    int8 qualifierType = getQualifier().empty() ? -1 : stoi(getQualifier());

    trainableSpellMap* spellMap = GAI_VALUE(trainableSpellMap*, "trainable spell map");

    for (auto& [trainerType, spellReqList] : *spellMap)
    {
        if (trainerType >= 0 && trainerType != qualifierType)
            continue;

        for (auto& [requirement, trainerSpellList] : spellReqList)
        {
            if (trainerType == TRAINER_TYPE_CLASS && requirement != bot->GetClass())
                continue;
            if (trainerType == TRAINER_TYPE_MOUNTS && requirement != bot->GetRace())
                continue;

            for (auto& [trainerSpell, trainers] : trainerSpellList)
            {
                if (std::find(trainableSpells.begin(), trainableSpells.end(), trainerSpell) == trainableSpells.end())
                    continue;

                // Trade trainers teach the bot's OWN craft only: for TRADESKILLS
                // the map key is the spell's skill id, so a herbalism trainer
                // never qualifies a miner. Without this every green rank-1 in
                // the world qualified every trade trainer, and level-5 bots
                // walked to whichever craft was nearest, learning nothing.
                if (trainerType == TRAINER_TYPE_TRADESKILLS &&
                    !TradeSpellJustifiesTrip(requirement, bot->HasSkill((uint16)requirement),
                        sSpellMgr.IsPrimaryProfessionFirstRankSpell(trainerSpell->spell),
                        bot->GetFreePrimaryProfessionPoints() > 0))
                    continue;

                for (auto& trainer : trainers)
                {
                    if(std::find(retTrainers.begin(), retTrainers.end(), trainer) == retTrainers.end())
                        retTrainers.push_back(trainer);
                }
            }
        }
    }

    return retTrainers;
}

uint32 TrainCostValue::Calculate()
{
    uint32 TotalCost = 0;

    for (auto& spells : AI_VALUE2(std::vector<TrainerSpell const*>, "trainable spells", getQualifier()))
        TotalCost += spells->spellCost;

    return TotalCost;
}
