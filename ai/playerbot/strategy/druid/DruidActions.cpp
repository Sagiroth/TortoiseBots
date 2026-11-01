#include "playerbot/playerbot.h"
#include "playerbot/GroupMembers.h"
#include "DruidActions.h"

using namespace ai;

bool CastCasterFormAction::Execute(Event& event)
{
    ai->RemoveShapeshift();
    return true;
}

Unit* ai::FindRejuvenationSpreadTarget(PlayerbotAI* ai, Player* bot)
{
    if (!ai || !bot)
        return nullptr;
    Group* group = bot->GetGroup();
    if (!group)
        return nullptr;
    float maxRange = 0.0f;
    if (!ai->GetSpellRange("rejuvenation", &maxRange))
        return nullptr;
    Unit* best = nullptr;
    float bestHealth = (float)sPlayerbotAIConfig.almostFullHealth;
    for (Player* member : LiveGroupMembers(group))
    {
        if (!member || member == bot || !sServerFacade.IsAlive(member))
            continue;
        if (member->GetMapId() != bot->GetMapId() || member->GetVisibility() == VISIBILITY_OFF)
            continue;
        if (member->GetHealthPercent() >= sPlayerbotAIConfig.almostFullHealth)
            continue;
        if (ai->HasMyAura("rejuvenation", member))
            continue;
        if (bot->GetDistance(member) > maxRange) // 3D, combat reach removed: the cast's own range test
            continue;
        if (!bot->IsWithinLOSInMap(member))
            continue;
        if (!best || member->GetHealthPercent() < bestHealth)
        {
            best = member;
            bestHealth = (float)member->GetHealthPercent();
        }
    }
    return best;
}

Unit* CastRejuvenationSpreadAction::GetTarget()
{
    return FindRejuvenationSpreadTarget(ai, bot);
}

bool CastRejuvenationSpreadAction::isUseful()
{
    Unit* target = GetTarget();
    if (!target)
        return false;
    // The almost-full tier is a mana-gated top-up tier (HealthTriggers): no
    // healer HoTs below medium mana, and never double-blanket one target.
    if (AI_VALUE2(uint8, "mana", "self target") < sPlayerbotAIConfig.mediumMana)
        return false;
    return CastHealingSpellAction::isUseful();
}
