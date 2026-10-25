#include "playerbot/playerbot.h"
#include "RaidTankHealing.h"
#include "playerbot/GroupMembers.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/strategy/AiObjectContext.h"
#include <algorithm>
#include <cctype>

using namespace ai;

bool ai::IsPatchwerkHealingAssignment(Player* player)
{
    if (!player || !player->IsAlive() || !player->IsInWorld() ||
        player->GetMapId() != 533 || !player->IsInCombat())
        return false;
    PlayerbotAI* peer = PlayerbotAIStorage::Instance().GetAI(player);
    Unit* target = player->GetVictim();
    return peer && target && target->IsAlive() && target->GetEntry() == 16028 &&
        (peer->HasStrategy("patchwerk main", BotState::BOT_STATE_COMBAT) ||
         peer->HasStrategy("patchwerk soak", BotState::BOT_STATE_COMBAT));
}

bool RaidTankHealingValue::Calculate()
{
    Group* group = bot->GetGroup();
    if (!group || !group->IsRaidGroup() || !bot->GetMap()->IsRaid() || !ai->IsHeal(bot))
        return false;
    // Independent of patient selection: LOS, range, focus and a heal in flight
    // must not authorize damage while a raid tank is still below the safe line.
    for (Player* member : LiveGroupMembers(group))
        if (member->IsAlive() && member->IsInWorld() && member->IsInMap(bot) &&
            member->IsInCombat() && sServerFacade.IsFriendlyTo(bot, member) &&
            (ai->IsTank(member) || IsPatchwerkHealingAssignment(member)) &&
            member->GetHealthPercent() < sPlayerbotAIConfig.almostFullHealth)
            return true;
    return false;
}

bool ai::RaidHealerShouldHoldDamage(PlayerbotAI* ai)
{
    return ai->GetAiObjectContext()->GetValue<bool>("raid tank needs healing")->Get();
}

bool ai::IsRaidHealerFiller(std::string name)
{
    std::transform(name.begin(), name.end(), name.begin(),
        [](unsigned char c) { return std::tolower(c); });
    return name == "smite" || name == "holy fire" || name == "shadow word: pain" ||
        name == "shoot" || name == "mind blast" || name == "starshards";
}

bool ai::StopUnsafeRaidHealerFiller(PlayerbotAI* ai)
{
    Player* bot = ai->GetBot();
    Spell* cast = bot->GetCurrentSpell(CURRENT_GENERIC_SPELL);
    Spell* wand = bot->GetCurrentSpell(CURRENT_AUTOREPEAT_SPELL);
    bool const offensiveCast = cast && cast->m_spellInfo &&
        cast->getState() == SPELL_STATE_PREPARING && IsRaidHealerFiller(cast->m_spellInfo->SpellName[0]);
    if ((!offensiveCast && !wand) || !RaidHealerShouldHoldDamage(ai))
        return false;
    if (offensiveCast)
        bot->InterruptSpell(CURRENT_GENERIC_SPELL);
    if (wand)
        bot->InterruptSpell(CURRENT_AUTOREPEAT_SPELL);
    return true;
}
