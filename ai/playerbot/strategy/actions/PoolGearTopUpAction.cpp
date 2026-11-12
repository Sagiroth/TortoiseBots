#include "playerbot/playerbot.h"
#include "PoolGearTopUpAction.h"
#include "playerbot/PlayerbotFactory.h"
#include "playerbot/ServerFacade.h"
#include "../../../../runtime/ClaimLifecycle.h"
#include "../../../../runtime/HireLifecycle.h"
#include "../../../../runtime/PoolGearTopUpPolicy.h"

using namespace ai;

// The last level a bot's gear was topped up at lives in memory only (facade
// value), never in the DB.
static char const* const kPoolGearTopUpLevelKey = "poolGearTopUpLevel";

bool PoolGearTopUpAction::isUseful()
{
    if (!TortoiseBots::PoolGearTopUpEligible(sPlayerbotAIConfig.poolGearTopUpOnLevel,
            sRandomBotFacade.IsRandomBot(bot),
            TortoiseBots::HireLifecycle::Instance().IsHired(bot->GetObjectGuid()),
            TortoiseBots::ClaimLifecycle::Instance().IsClaimed(bot->GetGUIDLow()),
            ai->HasRealPlayerMaster()))
        return false;

    return TortoiseBots::PoolGearTopUpDue(bot->GetLevel(), sRandomBotFacade.GetValue(bot, kPoolGearTopUpLevelKey));
}

bool PoolGearTopUpAction::Execute(Event& event)
{
    // Worn gear is never changed in combat (#336, see EquipUpgradesAction):
    // a ding on the killing blow leaves the level open and the next
    // out-of-combat "equipment audit" picks it up.
    if (!sServerFacade.IsAlive(bot) || sServerFacade.IsInCombat(bot))
        return false;

    uint32 const level = bot->GetLevel();
    // Mark first: one attempt per level, also when no slot found a candidate,
    // so a slot the item cache cannot fill is not re-searched every audit.
    sRandomBotFacade.SetValue(bot, kPoolGearTopUpLevelKey, level);

    PlayerbotFactory factory(bot, level);
    uint32 const changed = factory.TopUpPoolGear();
    if (changed)
        sPlayerbotAIConfig.logEvent(ai, "PoolGearTopUp", std::to_string(level), std::to_string(changed));

    return changed > 0;
}
