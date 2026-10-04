// pi-lens-ignore-file: clang:pp_file_not_found,clang:unknown_typename,clang:use_of_undeclared_identifier,clang:unknown_type_name,clang:undeclared_var_use,clang:incomplete_member_access
#include "WorldBuffAuraAdapter.h"
#include "../runtime/WorldBuffPolicy.h"
#include "../ai/playerbot/playerbot.h"

#include "ScriptObjects.h"
#include "Player.h"
#include "Creature.h"
#include "Spells/SpellAuras.h"

namespace TortoiseBots
{

WorldBuffAuraAdapter::WorldBuffAuraAdapter()
    : UnitScript("tortoisebots_worldbuff_auras", { UNITHOOK_ON_AURA_APPLY })
{
}

void WorldBuffAuraAdapter::OnAuraApply(Unit* unit, Aura* aura)
{
    if (!unit || !aura)
        return;
    if (!sPlayerbotAIConfig.worldBuffsEnabled)
        return;
    uint32_t spellId = aura->GetId();
    if (!IsAuraUnlockSpell(spellId))
        return;
    if (!unit->IsPlayer())
        return;
    Player* player = unit->ToPlayer();
    if (!player)
        return;
    bool horde = player->GetTeam() == HORDE;
    uint32_t questId = AuraUnlockQuest(spellId, horde);
    if (questId == 0)
        return;
    // Recruiter-cast auras (purchases) never unlock: otherwise one buyer
    // would unlock the buff for the whole group straight from the till.
    // Chronoboon-style restores with no live caster carry no "real way"
    // evidence either, so they need a live non-recruiter caster too.
    Unit* caster = aura->GetCaster();
    if (!caster)
        return;
    if (Creature* casterCreature = caster->ToCreature())
    {
        if (IsHireRecruiterEntry(casterCreature->GetEntry()))
            return;
    }
    // Only the receiver progresses, and only while the quest is incomplete
    // in the log (pool bots never hold it: banned in QuestLogPolicy).
    // GetQuestStatus is the public log check; AreaExploredOrEventHappens
    // itself no-ops for quests not in the log.
    if (player->GetQuestStatus(questId) != QUEST_STATUS_INCOMPLETE)
        return;
    if (player->GetQuestRewardStatus(questId))
        return;
    player->AreaExploredOrEventHappens(questId);
}

} // namespace TortoiseBots
