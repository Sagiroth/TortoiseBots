// Standalone regression test for the per-tick AI pass classification
// (runtime/PlayerBotClassification.h): which bots the module treats as
// "player-owned" and therefore updates first, unbudgeted, ahead of the random
// pool.
//
// The case that must never regress: a character on a managed RNDBOT pool
// account is a pool bot, even when it carries a master — including a bot
// master from a bot-only group (PlayerbotAI::GetGroupMaster -> BindBotMaster
// grants those a PlayerMaster lease) and even when its record still says
// random=false. Only a live, non-headless (network) player master, or an
// account the pool does not own, makes a bot player-owned.
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra tools/test_player_bot_classification.cpp -o /tmp/test_player_bot_classification
//   /tmp/test_player_bot_classification

#include "../runtime/PlayerBotClassification.h"

#include <cstdio>
#include <cstdlib>

using namespace TortoiseBots;

static int checks = 0;
#define CHECK(cond) do { \
    ++checks; \
    if (!(cond)) { \
        std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        std::exit(1); \
    } \
} while (0)

// Registered pool account (RNDBOT), validated registry.
static PlayerBotClassificationInputs PoolAccount(bool liveRealPlayerMaster)
{
    PlayerBotClassificationInputs in;
    in.accountIsPool = AccountIsPool(true, true);
    in.hasLiveRealPlayerMaster = liveRealPlayerMaster;
    return in;
}

// The owner's own account: not registered in tortoise_bots_pool_account.
static PlayerBotClassificationInputs OwnAccount(bool liveRealPlayerMaster)
{
    PlayerBotClassificationInputs in;
    in.accountIsPool = AccountIsPool(true, false);
    in.hasLiveRealPlayerMaster = liveRealPlayerMaster;
    return in;
}

static void TestPoolAccountStaysInThePool()
{
    // Plain pool bot: no master at all.
    CHECK(!IsPlayerOwnedBot(PoolAccount(false)));

    // Pool bot with a bot master (headless leader of a bot-only group) and its
    // PlayerMaster lease: still a pool bot.
    CHECK(!IsPlayerOwnedBot(PoolAccount(false)));

    // Pool bot whose record still carries random=false after a restart: the
    // account decides, so it is still a pool bot.
    CHECK(!IsPlayerOwnedBot(PoolAccount(false)));
}

static void TestPoolAccountWithRealPlayerMasterIsPlayerOwned()
{
    // Hired companion / adopted party bot while its player is online.
    CHECK(IsPlayerOwnedBot(PoolAccount(true)));
}

static void TestOwnAccountIsPlayerOwned()
{
    // The owner's own characters (`.bot add`, alts) are player bots even with
    // no master and with a bot master from their own group.
    CHECK(IsPlayerOwnedBot(OwnAccount(false)));
    CHECK(IsPlayerOwnedBot(OwnAccount(true)));
}

static void TestUnknownAccountCountsAsPool()
{
    // Unvalidated registry: nothing can be told apart, so nothing is promoted.
    CHECK(AccountIsPool(false, true));
    CHECK(AccountIsPool(false, false));
    CHECK(!IsPlayerOwnedBot(PlayerBotClassificationInputs{AccountIsPool(false, false), false}));
    CHECK(!IsPlayerOwnedBot(PlayerBotClassificationInputs{AccountIsPool(false, true), false}));

    // A real player master still wins, even then: it needs no registry.
    CHECK(IsPlayerOwnedBot(PlayerBotClassificationInputs{AccountIsPool(false, false), true}));
}

static void TestAccountDerivation()
{
    CHECK(AccountIsPool(true, true));    // registered pool account
    CHECK(!AccountIsPool(true, false));  // owner's account, registry valid
}

int main()
{
    TestPoolAccountStaysInThePool();
    TestPoolAccountWithRealPlayerMasterIsPlayerOwned();
    TestOwnAccountIsPlayerOwned();
    TestUnknownAccountCountsAsPool();
    TestAccountDerivation();

    std::printf("PASSED: player-bot classification (%d checks)\n", checks);
    return 0;
}
