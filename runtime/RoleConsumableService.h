#pragma once
#include "RoleConsumablePolicy.h"
#include "ConsumableProvisionClock.h"
class Player;
class PlayerbotAI;
class Item;
namespace TortoiseBots
{
bool RoleConsumablesEnabled();
bool RoleConsumablesManaged(PlayerbotAI* ai);
bool RoleConsumableUnlocked(PlayerbotAI* ai, ConsumableCategory category);
unsigned RoleConsumableRole(PlayerbotAI* ai);
Item* FindRoleConsumable(Player* bot, std::uint32_t entry);
bool HasConsumableBuff(Player* player, ConsumableCategory category);
void TopUpRoleConsumables(PlayerbotAI* ai);
void UpdateRoleConsumableProvision(PlayerbotAI* ai, std::uint32_t elapsed, ConsumableProvisionClock& clock);
}
