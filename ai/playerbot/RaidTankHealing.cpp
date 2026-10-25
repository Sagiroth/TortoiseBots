#include "playerbot/playerbot.h"
#include "RaidTankHealing.h"
#include "playerbot/GroupMembers.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/strategy/AiObjectContext.h"
#include "playerbot/strategy/actions/GenericSpellActions.h"
#include "playerbot/strategy/actions/PatchwerkSoakerActions.h"
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
    if (!peer || (!peer->HasStrategy("patchwerk main", BotState::BOT_STATE_COMBAT) &&
        !peer->HasStrategy("patchwerk soak", BotState::BOT_STATE_COMBAT)))
        return false;
    if (target && target->IsAlive() && target->GetEntry() == 16028)
        return true;
    // A pulled boss can already be hitting the assigned tank before Attack()
    // establishes the tank's own victim (or while the tank cannot attack).
    for (Unit* attacker : player->GetAttackers())
        if (attacker->IsAlive() && attacker->GetEntry() == 16028)
            return true;
    return false;
}

Unit* ai::RaidHealingAnchor(PlayerbotAI* ai)
{
    Player* bot = ai->GetBot();
    Group* group = bot->GetGroup();
    if (!group || !group->IsRaidGroup() || !bot->GetMap()->IsRaid() || !ai->IsHeal(bot) ||
        ai->HasStrategy("focus heal targets", BotState::BOT_STATE_COMBAT))
        return nullptr;
    for (Player* member : LiveGroupMembers(group))
    {
        if (member == bot || !member->IsInMap(bot) || !sServerFacade.IsFriendlyTo(bot, member) ||
            !IsPatchwerkHealingAssignment(member))
            continue;
        PlayerbotAI* peer = PlayerbotAIStorage::Instance().GetAI(member);
        if (peer->HasStrategy("patchwerk main", BotState::BOT_STATE_COMBAT))
            return member;
    }
    return nullptr;
}

bool RaidTankHealingValue::Calculate()
{
    Group* group = bot->GetGroup();
    if (!group || !group->IsRaidGroup() || !bot->GetMap()->IsRaid() || !ai->IsHeal(bot))
        return false;
    // Get into healing range during the opening, even before the first hit.
    // Reach retains the player's stay order and all existing movement checks.
    Unit* anchor = RaidHealingAnchor(ai);
    if (anchor && bot->GetDistance(anchor) > ai->GetRange("heal"))
        return true;
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

bool ai::IsRaidHealerMaintenance(std::string name)
{
    std::transform(name.begin(), name.end(), name.begin(),
        [](unsigned char c) { return std::tolower(c); });
    for (std::string const suffix : {" on party", " on tank"})
        if (name.size() >= suffix.size() && name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0)
            name.resize(name.size() - suffix.size());
    return name == "resurrection" || name == "power word: fortitude" ||
        name == "prayer of fortitude" || name == "divine spirit" || name == "prayer of spirit" ||
        name == "shadow protection" || name == "prayer of shadow protection" ||
        name == "inner fire" || name == "fear ward";
}

bool ai::StopUnsafeRaidHealerFiller(PlayerbotAI* ai)
{
    Player* bot = ai->GetBot();
    Spell* cast = bot->GetCurrentSpell(CURRENT_GENERIC_SPELL);
    Spell* wand = bot->GetCurrentSpell(CURRENT_AUTOREPEAT_SPELL);
    bool const offensiveCast = cast && cast->m_spellInfo &&
        cast->getState() == SPELL_STATE_PREPARING && (IsRaidHealerFiller(cast->m_spellInfo->SpellName[0]) ||
            IsRaidHealerMaintenance(cast->m_spellInfo->SpellName[0]));
    if ((!offensiveCast && !wand) || !RaidHealerShouldHoldDamage(ai))
        return false;
    if (offensiveCast)
        bot->InterruptSpell(CURRENT_GENERIC_SPELL);
    if (wand)
        bot->InterruptSpell(CURRENT_AUTOREPEAT_SPELL);
    return true;
}

// Explicit chat actions and this bot's assigned CC mark remain authoritative.
bool ai::SuppressAutomaticRaidShackle(PlayerbotAI* ai, Unit* target, bool explicitRequest)
{
    Player* bot = ai->GetBot();
    if (explicitRequest || !bot->GetMap()->IsRaid())
        return false;
    AiObjectContext* context = ai->GetAiObjectContext();
    Unit* mark = AI_VALUE(Unit*, "rti cc target");
    if (target && target == mark)
        return false;
    Unit* boss = AI_VALUE(Unit*, "boss target");
    return boss && boss->IsAlive() && boss->IsInCombat() && boss->IsInMap(bot);
}

Unit* PatchwerkSoakerShieldValue::Calculate()
{
    Group* group = bot->GetGroup();
    if (!group || !group->IsRaidGroup() || bot->GetMapId() != 533 || !ai->IsHeal(bot) ||
        ai->HasStrategy("focus heal targets", BotState::BOT_STATE_COMBAT))
        return nullptr;
    // An instant preventive shield must not displace an already critical heal.
    Unit* patient = AI_VALUE(Unit*, "party member to heal");
    if (patient && patient->GetHealthPercent() < sPlayerbotAIConfig.criticalHealth)
        return nullptr;
    float range = 0.0f;
    if (!ai->GetSpellRange("power word: shield", &range))
        return nullptr;
    Player* best = nullptr;
    for (Player* member : LiveGroupMembers(group))
    {
        if (!member->IsAlive() || !member->IsInWorld() || !member->IsInMap(bot) ||
            !sServerFacade.IsFriendlyTo(bot, member) || !bot->IsWithinDistInMap(member, range) ||
            !sServerFacade.IsWithinLOSInMap(bot, member))
            continue;
        PlayerbotAI* peer = PlayerbotAIStorage::Instance().GetAI(member);
        // Reuse the engagement value: group contact, not injury or the soaker's
        // own first melee swing, opens the shielding window during approach.
        if (!peer || !GetPatchwerkSoakBoss(peer) || ai->HasAura(6788, member) ||
            ai->HasAura("power word: shield", member) ||
            BuffClaimRegistry::IsTargetClaimedByOther(bot, member, "power word: shield"))
            continue;
        if (!best || member->GetHealth() > best->GetHealth() ||
            (member->GetHealth() == best->GetHealth() && member->GetObjectGuid() < best->GetObjectGuid()))
            best = member;
    }
    return best;
}
