#include "playerbot/playerbot.h"
#include "PoolMoneyTopUpAction.h"
#include "playerbot/strategy/values/BudgetValues.h"
#include "../../../../runtime/ClaimLifecycle.h"
#include "../../../../runtime/HireLifecycle.h"
#include "../../../../runtime/PoolMoneyTopUpPolicy.h"

using namespace ai;

// The last level a bot was topped up at lives in memory only (facade value),
// never in the DB.
static char const* const kPoolMoneyTopUpLevelKey = "poolMoneyTopUpLevel";

bool PoolMoneyTopUpAction::isUseful()
{
    if (!TortoiseBots::PoolMoneyTopUpDue(bot->GetLevel(), sRandomBotFacade.GetValue(bot, kPoolMoneyTopUpLevelKey)))
        return false;

    return TortoiseBots::PoolMoneyTopUpEligible(sPlayerbotAIConfig.poolMoneyTopUp,
        sRandomBotFacade.IsRandomBot(bot),
        TortoiseBots::HireLifecycle::Instance().IsHired(bot->GetObjectGuid()),
        TortoiseBots::ClaimLifecycle::Instance().IsClaimed(bot->GetGUIDLow()),
        ai->HasRealPlayerMaster(),
        ai->HasCheat(BotCheatMask::gold));
}

bool PoolMoneyTopUpAction::Execute(Event& event)
{
    uint32 const level = bot->GetLevel();
    // One look per level, also when the purse already covers the need.
    sRandomBotFacade.SetValue(bot, kPoolMoneyTopUpLevelKey, level);

    // The budget values cache for 60 s; the ding just changed the trainable
    // ranks (and the level-scaled reserves), so read them fresh. Once per
    // level, so the recalculation is not a per-tick cost.
    RESET_AI_VALUE2(uint32, "train cost", (uint32)TRAINER_TYPE_CLASS);
    for (NeedMoneyFor need : { NeedMoneyFor::repair, NeedMoneyFor::ammo, NeedMoneyFor::ah, NeedMoneyFor::guild, NeedMoneyFor::spells })
        RESET_AI_VALUE2(uint32, "money needed for", (uint32)need);
    RESET_AI_VALUE2(uint32, "total money needed for", (uint32)NeedMoneyFor::spells);

    // Next trainer visit with the reserves the trip gate keeps aside first
    // (repair - zero under the repair cheat -, ammo, AH deposit, guild),
    // capped per level.
    uint32 const need = AI_VALUE2(uint32, "total money needed for", (uint32)NeedMoneyFor::spells);
    uint32 const topUpFloor = TortoiseBots::PoolMoneyTopUpFloor(need, level);
    uint32 const money = bot->GetMoney();
    uint32 const amount = TortoiseBots::PoolMoneyTopUpAmount(money, topUpFloor);
    if (!amount)
        return false;

    bot->SetMoney(money + amount);
    sPlayerbotAIConfig.logEvent(ai, "PoolMoneyTopUp", std::to_string(amount), std::to_string(topUpFloor));
    return true;
}
