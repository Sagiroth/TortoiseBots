#pragma once

// pi-lens-ignore: clang:pp_file_not_found
#include "ScriptObjects.h"

namespace TortoiseBots
{

// Module-owned CreatureScript adapter binding the <Mercenary Hire> recruiter
// NPCs (script_name tortoise_mercenary_hire) to the gossip wizard. Lives in
// the native module: zero core changes, core builds with bots off.
class HireRecruiterAdapter final : public CreatureScript
{
public:
    HireRecruiterAdapter();

    bool OnGossipHello(Player* player, Creature* creature) override;
    bool OnGossipSelect(Player* player, Creature* creature, uint32 sender, uint32 action) override;
    uint32 GetDialogStatus(Player* player, Creature* creature) override;
};

} // namespace TortoiseBots
