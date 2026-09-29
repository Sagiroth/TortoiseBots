#pragma once

#include <cstdint>

namespace TortoiseBots
{

// Which bots the per-tick AI pass treats as "player-owned": updated first, on
// every tick, and never cut by the random-pool budget (BotManager::UpdateBots).
//
// The rule is about real ownership, not about bookkeeping flags:
//   - a live master with a network session (a real player) always wins. That is
//     what makes hired companions (HireProvisionService) and adopted party bots
//     player bots while their player is online, and pool bots again once that
//     player is gone;
//   - otherwise the character's account decides: characters on the owner's own
//     account are player bots, characters on a registered RNDBOT pool account
//     (tortoise_bots_pool_account) are pool bots.
//
// record.random, masterGuid alone and the PlayerMaster lease are deliberately
// NOT ownership signals here: a pool bot can carry random=false, and a
// bot-only group hands its members a bot master plus a PlayerMaster lease
// (PlayerbotAI::GetGroupMaster -> BotManager::BindBotMaster), which is how
// ~40% of a 500-bot pool was once misclassified as player-owned.
struct PlayerBotClassificationInputs
{
    // Character account is a registered random-pool account. Unknown counts as
    // pool: an unvalidated registry must never promote a pool bot.
    bool accountIsPool = true;
    // Master is online with a network session (not headless, not a bot).
    bool hasLiveRealPlayerMaster = false;
};

inline bool IsPlayerOwnedBot(PlayerBotClassificationInputs const& inputs)
{
    return inputs.hasLiveRealPlayerMaster || !inputs.accountIsPool;
}

// The managed-account registry answers "is this account a registered RNDBOT
// pool account". An unvalidated registry must never promote a pool bot into
// the unbudgeted player pass, so it counts as pool. Once the registry is
// validated, an account it does not know is not a pool account: that is the
// owner's own account, and its bots are player bots.
inline bool AccountIsPool(bool registryValidated, bool accountRegistered)
{
    return !registryValidated || accountRegistered;
}

} // namespace TortoiseBots
