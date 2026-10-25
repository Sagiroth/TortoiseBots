#include "playerbot/playerbot.h"
#include "CthunDungeonActions.h"
#include "playerbot/RaidDpsExclusionPolicy.h"
#include "playerbot/strategy/values/RtiTargetValue.h"

bool ai::IsCthunCarapaceTargetExcluded(PlayerbotAI* ai, Unit* target)
{
    Player* bot = ai->GetBot();
    // Carapace reduces damage by 99%; it is not an immunity mechanic.
    // Only the body with its shell active needs this opt-in DPS guard.
    if (bot->GetMapId() != 531 || !target || target->GetEntry() != 15727 ||
        !bot->IsInWorld() || bot->IsBeingTeleported() || !bot->IsAlive() || !bot->IsInCombat() || !target->IsAlive() ||
        !target->IsInMap(bot) || !target->IsInCombat() || !target->HasAura(26156))
        return false;

    bool const enabled = ai->HasStrategy("cthun carapace", BotState::BOT_STATE_COMBAT);
    bool const tank = ai->IsTank(bot);
    if (!enabled || tank)
        return false;

    AiObjectContext* context = ai->GetAiObjectContext();
    bool ordered = AI_VALUE(ObjectGuid, "explicit attack target") == target->GetObjectGuid();
    if (Group* group = bot->GetGroup())
    {
        int const mark = RtiTargetValue::GetRtiIndex(AI_VALUE(std::string, "rti"));
        if (mark >= 0 && ObjectGuid(group->GetTargetIcon(mark)) == target->GetObjectGuid())
            ordered = true;
    }
    return RaidDpsTargetExcluded(enabled, true, tank, ordered, true, target->IsInCombat());
}
