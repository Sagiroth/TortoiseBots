#include "playerbot/playerbot.h"
#include "RoleConsumableAction.h"
#include "../../../../runtime/RoleConsumableService.h"

using namespace TortoiseBots;
namespace ai
{
bool RoleConsumableAction::isUseful()
{
    return RoleConsumablesManaged(ai) && bot->IsAlive() && bot->IsInWorld() &&
        !bot->IsTaxiFlying() && !bot->IsMounted() && !bot->IsBeingTeleported() &&
        !bot->InBattleGround() && !bot->IsShapeShifted() && !bot->HasStealthAura() &&
        !bot->HasInvisibilityAura() && !bot->GetTradeData() && !bot->IsNonMeleeSpellCasted(false);
}
bool RoleConsumableAction::Execute(Event&)
{
    if (!isUseful()) return false;
    Player* master = ai->GetMaster();
    if (!master->IsInWorld() || master->GetMap() != bot->GetMap() ||
        !bot->IsWithinDistInMap(master, sPlayerbotAIConfig.sightDistance)) return false;
    unsigned const role = RoleConsumableRole(ai);
    for (auto category : {ConsumableCategory::Health, ConsumableCategory::Mana,
        ConsumableCategory::Flask, ConsumableCategory::Elixir, ConsumableCategory::Scroll, ConsumableCategory::Food})
    {
        if (!RoleConsumableUnlocked(ai, category)) continue;
        bool const potion = category == ConsumableCategory::Health || category == ConsumableCategory::Mana;
        if (potion)
        {
            if (!NeedsConsumablePotion(category, bot->IsInCombat(), bot->GetHealthPercent(),
                bot->GetPowerPercent(POWER_MANA), bot->GetMaxPower(POWER_MANA) != 0)) continue;
        }
        else if (bot->IsInCombat() || !HasConsumableBuff(master, category) || HasConsumableBuff(bot, category)) continue;
        for (auto const& entry : RoleConsumables)
        {
            if (entry.category != category || !(entry.roles & role)) continue;
            Item* item = FindRoleConsumable(bot, entry.item);
            if (!item || bot->CanUseItem(item->GetProto()) != EQUIP_ERR_OK) continue;
            auto const* info = sSpellTemplate.LookupEntry<SpellEntry>(item->GetProto()->Spells[0].SpellId);
            if (!info) continue;
            // Native validation and native item-use packet: item cheats never
            // substitute a free spell, bypass cooldowns or avoid consumption.
            Spell check(bot, info, false);
            check.SetCastItem(item);
            check.m_targets.setUnitTarget(bot);
            SpellCastResult result = check.CheckCast(true);
            if (result != SPELL_CAST_OK && result != SPELL_FAILED_NOT_STANDING) continue;
            if (category == ConsumableCategory::Food) ai->StopMoving();
            ai->ImbueItem(item, bot);
            SetDuration(2000);
            return true;
        }
    }
    return false;
}
}
