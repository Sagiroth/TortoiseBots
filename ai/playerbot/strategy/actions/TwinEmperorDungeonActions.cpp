#include "playerbot/playerbot.h"
#include "TwinEmperorDungeonActions.h"
#include "playerbot/RaidDpsExclusionPolicy.h"
#include "playerbot/strategy/values/RtiTargetValue.h"

bool ai::IsTwinPhysicalDpsTargetExcluded(PlayerbotAI* ai, Unit* target)
{
    Player* bot = ai->GetBot();
    // Vek'lor is permanently immune to normal damage. Reject unrelated units
    // before strategy/group lookups; no nearby scan or boss search is needed.
    if (bot->GetMapId() != 531 || !target || target->GetEntry() != 15276 ||
        !bot->IsAlive() || !bot->IsInCombat() || !target->IsAlive() ||
        !target->IsInMap(bot) || !target->IsInCombat())
        return false;

    bool const enabled = ai->HasStrategy("twins physical dps", BotState::BOT_STATE_COMBAT);
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
