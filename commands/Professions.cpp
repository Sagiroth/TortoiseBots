// pi-lens-ignore-file: all
#include "BotCommandHandlers.h"
#include "CommandShared.h"
#include "BotCommandContext.h"

#include "../runtime/BotManager.h"
#include "../runtime/GuildOrderCraftPolicy.h"
#include "../runtime/GuildOrderStore.h"
#include "../runtime/PlayerbotAIStorage.h"
#include "../runtime/policy/provision/ProfessionSnapshotPolicy.h"
#include "../ai/playerbot/GuildOrderEnchants.h"

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

#include <algorithm>
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

void FillAbilityRow(AbilityRow& row, SkillLineAbilityEntry const* ability, Player* bot)
{
    row.skillId = ability->skillId;
    row.spellId = ability->spellId;
    row.reqSkillValue = ability->req_skill_value;
    row.minValue = ability->min_value;
    row.maxValue = ability->max_value;
    row.known = bot->HasSpell(ability->spellId);
    row.passive = IsPassiveSpell(ability->spellId);
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
        if (!skill || !IsProfessionSkill(uint32_t(skill->categoryId), skill->id))
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
                cached = recipeItemCache.emplace(row.spellId, ai::GuildOrderRecipeItemFor(row.spellId)).first;
            recipeItem = cached->second;
            uint32_t trainerSkill = 0;
            bool const fromTrainer = ai::GuildOrderTaughtByAnyTrainer(row.spellId, trainerSkill);
            ItemPrototype const* recipeProto = recipeItem ? sObjectMgr.GetItemPrototype(recipeItem) : nullptr;
            uint32_t const itemSkill = recipeProto ? recipeProto->RequiredSkillRank : 0;
            row.reqSkillValue = GuildOrders::EnchantReqSkillCombine(fromTrainer, trainerSkill,
                recipeProto != nullptr, itemSkill);
            if (!IsMissingRecipe(row, skillMax[skillId], fromTrainer, recipeItem))
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
