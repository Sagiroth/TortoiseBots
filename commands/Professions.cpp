// pi-lens-ignore-file: all
#include "BotCommandHandlers.h"
#include "CommandShared.h"
#include "BotCommandContext.h"

#include "../runtime/BotManager.h"
#include "../runtime/GuildOrderCraftPolicy.h"
#include "../runtime/GuildOrderStore.h"
#include "../runtime/PlayerbotAIStorage.h"
#include "../runtime/policy/provision/ProfessionSnapshotPolicy.h"

// pi-lens-ignore: clang:pp_file_not_found
#include "Chat.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "ObjectAccessor.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "ObjectMgr.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "Player.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "Database/DBCStores.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "Spells/SpellMgr.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "playerbot/PlayerbotAI.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace TortoiseBots {
namespace BotCommands {
namespace {

using TortoiseBots::ProfessionSnapshotPolicy::AbilityRow;

// One scan of the ability table per profession the bot holds: the rows the
// bot knows become RECIPE lines, the rows it does not know feed the missing
// check. Indexed by skill at startup (ability count, not skill x ability).
std::unordered_map<uint32_t, std::vector<SkillLineAbilityEntry const*>> const& AbilityBySkill()
{
    static std::unordered_map<uint32_t, std::vector<SkillLineAbilityEntry const*>> index;
    static bool built = false;
    if (!built)
    {
        for (uint32 i = 0; i < sSkillLineAbilityStore.GetNumRows(); ++i)
        {
            SkillLineAbilityEntry const* ability = sSkillLineAbilityStore.LookupEntry(i);
            if (!ability || !ability->spellId || !ability->skillId)
                continue;
            index[ability->skillId].push_back(ability);
        }
        built = true;
    }
    return index;
}

// Spell teaching a recipe item teaches: first lowest-entry item whose use
// spell learns that spell. Trained-from-trainer rows have no item and stay 0.
uint32_t RecipeItemFor(uint32_t spellId)
{
    uint32_t found = 0;
    for (uint32 id = 0; id < sItemStorage.GetMaxEntry(); ++id)
    {
        ItemPrototype const* proto = sItemStorage.LookupEntry<ItemPrototype>(id);
        if (!proto || proto->Class != ITEM_CLASS_RECIPE)
            continue;
        for (uint32 e = 0; e < MAX_ITEM_PROTO_SPELLS; ++e)
        {
            if (!proto->Spells[e].SpellId)
                continue;
            SpellEntry const* use = sSpellMgr.GetSpellEntry(proto->Spells[e].SpellId);
            if (!use)
                continue;
            for (uint32 f = 0; f < MAX_EFFECT_INDEX; ++f)
            {
                if (use->Effect[f] != SPELL_EFFECT_LEARN_SPELL || !use->EffectTriggerSpell[f])
                    continue;
                if (use->EffectTriggerSpell[f] == spellId && (!found || id < found))
                    found = id;
            }
        }
    }
    return found;
}

bool TaughtByTrainer(uint32_t spellId, uint32_t trainerType,
    TrainerSpellData const* spells, Player* bot)
{
    if (!spells || !bot)
        return false;
    if (trainerType != TRAINER_TYPE_TRADESKILLS)
        return false;
    for (auto const& entry : spells->spellList)
    {
        TrainerSpell const* tSpell = &entry.second;
        if (!tSpell || !tSpell->spell)
            continue;
        SpellEntry const* teaching = sSpellMgr.GetSpellEntry(tSpell->spell);
        if (!teaching)
            continue;
        for (uint32 e = 0; e < MAX_EFFECT_INDEX; ++e)
        {
            if (teaching->Effect[e] != SPELL_EFFECT_LEARN_SPELL ||
                teaching->EffectTriggerSpell[e] != spellId)
                continue;
            if (bot->GetTrainerSpellState(tSpell) != TRAINER_SPELL_RED)
                return true;
            return false;
        }
        if (tSpell->spell == spellId && bot->GetTrainerSpellState(tSpell) != TRAINER_SPELL_RED)
            return true;
    }
    return false;
}

// Any trade-skill trainer anywhere teaching this spell (the bot visits the
// trainer to learn it; reachability is the travel layer's job, not this
// listing's). Cached per run like the factory's trainer sweeps.
bool TaughtByAnyTrainer(uint32_t spellId, Player* bot)
{
    struct TrainerCache
    {
        uint32_t trainerType = 0;
        TrainerSpellData const* spells = nullptr;
    };
    static std::vector<TrainerCache> tradeskillTrainers;
    static bool cached = false;
    if (!cached)
    {
        for (uint32 id = 0; id < sCreatureStorage.GetMaxEntry(); ++id)
        {
            CreatureInfo const* co = sCreatureStorage.LookupEntry<CreatureInfo>(id);
            if (!co || co->trainer_type != TRAINER_TYPE_TRADESKILLS)
                continue;
            uint32 trainerId = co->trainer_id ? co->trainer_id : co->entry;
            TrainerSpellData const* spells = sObjectMgr.GetNpcTrainerTemplateSpells(trainerId);
            if (!spells)
                spells = sObjectMgr.GetNpcTrainerSpells(trainerId);
            if (!spells)
                continue;
            bool known = false;
            for (TrainerCache const& cachedTrainer : tradeskillTrainers)
                if (cachedTrainer.spells == spells)
                    known = true;
            if (!known)
                tradeskillTrainers.push_back({ co->trainer_type, spells });
        }
        cached = true;
    }
    for (TrainerCache const& entry : tradeskillTrainers)
        if (TaughtByTrainer(spellId, entry.trainerType, entry.spells, bot))
            return true;
    return false;
}

void FillAbilityRow(AbilityRow& row, SkillLineAbilityEntry const* ability, Player* bot)
{
    row.skillId = ability->skillId;
    row.spellId = ability->spellId;
    row.reqSkillValue = ability->req_skill_value;
    row.minValue = ability->min_value;
    row.maxValue = ability->max_value;
    row.known = bot->HasSpell(ability->spellId);
    row.passive = IsPassiveSpell(ability->spellId);
    row.learnOnGetSkill = ability->learnOnGetSkill != 0;
    SpellEntry const* spell = sSpellMgr.GetSpellEntry(ability->spellId);
    if (!spell)
        return;
    row.effect0 = spell->Effect[EFFECT_INDEX_0];
    row.isSkillStep = spell->Effect[EFFECT_INDEX_1] == SPELL_EFFECT_SKILL_STEP;
    for (uint32 e = 0; e < MAX_EFFECT_INDEX; ++e)
    {
        if (spell->Effect[e] == SPELL_EFFECT_CREATE_ITEM && spell->EffectItemType[e])
        {
            row.createsItem = true;
            if (!row.createdItemId)
                row.createdItemId = spell->EffectItemType[e];
        }
        if (spell->Effect[e] == SPELL_EFFECT_ENCHANT_ITEM ||
            spell->Effect[e] == SPELL_EFFECT_ENCHANT_ITEM_TEMPORARY)
            row.isEnchant = true;
    }
}

// TBM:PROF_BEGIN|<bot>
// TBM:PROF|<skillId>|<value>|<max>
// TBM:RECIPE|<skillId>|<spellId>|<itemId or 0>|<orange>|<yellow>|<green>|<grey>|<order>
// TBM:RECIPE_MISSING|<skillId>|<spellId>|<itemId or 0>|<reqSkill>|<recipeItemId or 0>
// TBM:PROF_END|<bot>|<recipeCount>|<missingCount>
// <order> is ok when the recipe may be ordered as Craft, else the reason
// token (shared with the order give refusal and the search verdict).
std::string CraftOrderField(AbilityRow const& row)
{
    GuildOrders::CraftOrderableInputs inputs;
    inputs.known = row.known;
    inputs.createsItem = row.createsItem;
    if (ItemPrototype const* proto = sObjectMgr.GetItemPrototype(row.createdItemId))
    {
        inputs.itemOrderable = GuildOrders::IsOrderableItem(proto);
        inputs.itemNeedsProfession = proto->RequiredSkill != 0;
    }
    if (SpellEntry const* spell = sSpellMgr.GetSpellEntry(row.spellId))
        inputs.spellRecoveryMs = spell->GetRecoveryTime();
    return GuildOrders::CraftOrderToken(GuildOrders::CraftOrderableVerdict(inputs));
}
void SendProfessionSnapshot(ChatHandler* handler, Player* bot)
{
    using namespace TortoiseBots::ProfessionSnapshotPolicy;
    std::string const name = ProtocolSafe(bot->GetName());
    handler->PSendSysMessage("%s", FormatBegin(name).c_str());

    std::unordered_set<uint32_t> professionSkills;
    std::unordered_map<uint32_t, uint32_t> skillMax;
    for (uint32 i = 1; i < sSkillLineStore.GetNumRows(); ++i)
    {
        SkillLineEntry const* skill = sSkillLineStore.LookupEntry(i);
        if (!skill || !IsProfessionCategory(uint32_t(skill->categoryId)))
            continue;
        if (!bot->HasSkill(skill->id))
            continue;
        professionSkills.insert(skill->id);
        handler->PSendSysMessage("%s",
            FormatProfession({ skill->id, bot->GetSkillValue(skill->id), bot->GetSkillMax(skill->id) }).c_str());
        skillMax[skill->id] = bot->GetSkillMax(skill->id);
    }

    std::unordered_map<uint32_t, uint32_t> recipeItemCache;
    size_t recipeCount = 0;
    std::vector<MissingRow> missing;
    std::unordered_map<uint32_t, std::vector<SkillLineAbilityEntry const*>> const& bySkill = AbilityBySkill();
    for (uint32_t skillId : professionSkills)
    {
        auto found = bySkill.find(skillId);
        if (found == bySkill.end())
            continue;
        for (SkillLineAbilityEntry const* ability : found->second)
        {
            AbilityRow row;
            FillAbilityRow(row, ability, bot);
            if (IsKnownRecipe(row))
            {
                handler->PSendSysMessage("%s", FormatRecipe(row, CraftOrderField(row)).c_str());
                ++recipeCount;
                continue;
            }
            uint32_t recipeItem = 0;
            auto cached = recipeItemCache.find(row.spellId);
            if (cached == recipeItemCache.end())
                cached = recipeItemCache.emplace(row.spellId, RecipeItemFor(row.spellId)).first;
            recipeItem = cached->second;
            if (!IsMissingRecipe(row, skillMax[skillId],
                TaughtByAnyTrainer(row.spellId, bot), recipeItem))
                continue;
            missing.push_back({ row.skillId, row.spellId, row.createdItemId,
                row.reqSkillValue, recipeItem });
        }
    }
    for (MissingRow const& row : missing)
        handler->PSendSysMessage("%s", FormatMissing(row).c_str());
    handler->PSendSysMessage("%s", FormatEnd(name, recipeCount, missing.size()).c_str());
}

} // namespace

bool HandleProfessions(ChatHandler* handler, char const* args)
{
    Player* requester = Requester(handler);
    Player* bot = nullptr;
    BotRecord* record = nullptr;
    std::string name;
    if (!requester || !ResolveOwnedBot(handler, args, bot, record, name) ||
        !PlayerbotAIStorage::Instance().GetAI(bot))
    {
        SendActionError(handler, "professions", "no-bot", "Usage: .bot professions <online bot you control>");
        return true;
    }
    SendProfessionSnapshot(handler, bot);
    return true;
}

} // namespace BotCommands
} // namespace TortoiseBots
