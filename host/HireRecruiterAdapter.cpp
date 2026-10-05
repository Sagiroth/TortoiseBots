// pi-lens-ignore-file: clang:pp_file_not_found,clang:unknown_typename,clang:use_of_undeclared_identifier,clang:unknown_type_name,clang:undeclared_var_use,clang:incomplete_member_access
#include "HireRecruiterAdapter.h"
#include "HireRecruiterScript.h"
#include "QuestDef.h"

class Player;
class Creature;

namespace TortoiseBots
{

HireRecruiterAdapter::HireRecruiterAdapter() : CreatureScript("tortoise_mercenary_hire")
{
}

bool HireRecruiterAdapter::OnGossipHello(Player* player, Creature* creature)
{
    return HireRecruiterScript::OnHello(player, creature);
}

bool HireRecruiterAdapter::OnGossipSelect(Player* player, Creature* creature, uint32 sender, uint32 action)
{
    return HireRecruiterScript::OnSelect(player, creature, sender, action);
}

// The base CreatureScript answers 0 (no icon), which hides the quest marks of
// the capital recruiters' world-buff quests (issue #492). UNDEFINED hands the
// "!"/"?" decision back to the core's normal quest-giver status.
uint32 HireRecruiterAdapter::GetDialogStatus(Player* /*player*/, Creature* /*creature*/)
{
    return DIALOG_STATUS_UNDEFINED;
}

} // namespace TortoiseBots
