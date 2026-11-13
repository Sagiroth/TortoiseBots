#include "../runtime/policy/infra/TimedStrategyReapplyPolicy.h"

#include <cstdlib>
#include <iostream>
#include <cstdint>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using TortoiseBots::TimedReapplyInputs;
using TortoiseBots::TimedReapplyHoldsRole;
using TortoiseBots::TimedReapplyMember;
using TortoiseBots::TimedReapplyMemberIsRealPlayer;

namespace
{
    bool Holds(uint8_t forcedRole, bool inGroup, bool groupHasRealPlayer = false)
    {
        return TimedReapplyHoldsRole({forcedRole, inGroup, groupHasRealPlayer});
    }

    bool RealPlayer(TimedReapplyMember member)
    {
        return TimedReapplyMemberIsRealPlayer(member);
    }

    // The two facts the facade's scan can fill for an offline slot.
    TimedReapplyMember Offline(bool accountKnown, bool botAccount)
    {
        TimedReapplyMember member;
        member.hasAccount = accountKnown;
        member.botAccount = botAccount;
        return member;
    }

    // The one fact it fills for a live slot.
    TimedReapplyMember Online(bool hasClient)
    {
        TimedReapplyMember member;
        member.online = true;
        member.hasClient = hasClient;
        return member;
    }
}

int main()
{
    std::cout << "Starting TortoiseBots timed strategy re-apply tests...\n";

    // (1) The issue #1605 incident: the LFT fill handed a pool priest the
    // healer role (forcedRole 2) and she sits in the real player's dungeon
    // group. The timed re-apply must not run: its "+dps" is the priest's
    // shadow alias and would drop the heal kit mid-run.
    CHECK(Holds(1, true, true)); // tank fill in a player's group
    CHECK(Holds(2, true, true)); // healer fill in a player's group - the reported case
    CHECK(Holds(4, true, true)); // damage fill in a player's group
    std::cout << "  [PASS] grouped bot holding an LFT role keeps its kit\n";

    // (2) Same bot, same role, no group: the pool roulette applies as before
    // (the role only counts while a group is there to honour it).
    CHECK(!Holds(1, false));
    CHECK(!Holds(2, false));
    CHECK(!Holds(4, false));
    std::cout << "  [PASS] solo bot with a role still re-rolls\n";

    // (3) A grouped free bot with no forced role keeps today's behaviour:
    // the timed re-apply is what gives it its pool kit.
    CHECK(!Holds(0, true, false));
    std::cout << "  [PASS] grouped bot without a role still re-rolls\n";

    // (4) Neither condition: unchanged.
    CHECK(!Holds(0, false, false));
    std::cout << "  [PASS] masterless unforced bot still re-rolls\n";

    // (5) A natural-role pool healer the fill put in a player's group: the
    // fill assigns no forced role when the natural kit matches the slot, so
    // only the party hold stops the re-apply. Without it the roulette ran on
    // exactly this bot (#1605 round 1, Codex HIGH).
    CHECK(Holds(0, true, true));
    std::cout << "  [PASS] natural-role healer in a player's group is not re-rolled\n";

    // (6) A pool bot in an all-bot group is still the pool's: the timed
    // re-apply must keep running for it.
    CHECK(!Holds(0, true, false));
    std::cout << "  [PASS] pool bot in an all-bot group is still re-rolled\n";

    // (7) Round 2: the human is offline for the whole dungeon. The slot keeps
    // the guid, so the scan still finds the party's human through the
    // character's account and the pool healer is held exactly as above.
    CHECK(Holds(0, true, true));
    std::cout << "  [PASS] pool healer in a group whose human is offline is not re-rolled\n";

    // (8) Member rule, online slot: the transport decides. A network client
    // is a real player; a headless bot is not, even while it plays.
    CHECK(RealPlayer(Online(true)));
    CHECK(!RealPlayer(Online(false)));
    std::cout << "  [PASS] online member is judged by its network client\n";

    // (9) Member rule, offline slot: the account decides. A human's account
    // is a real player's party no matter who is logged in; a module bot
    // account is not, so an all-bot group whose bot logged out mid-dungeon
    // stays the pool's.
    CHECK(RealPlayer(Offline(true, false)));
    CHECK(!RealPlayer(Offline(true, true)));
    std::cout << "  [PASS] offline member is judged by its account\n";

    // (10) An offline slot the character cache cannot resolve has no account
    // flag to read and is not counted (a slot guid always resolves in
    // practice; this keeps the rule total).
    CHECK(!RealPlayer(Offline(false, false)));
    CHECK(!RealPlayer(Offline(false, true)));
    std::cout << "  [PASS] unresolvable offline member is not counted\n";

    // (11) The bot itself never counts, whatever its transport or account.
    TimedReapplyMember self = Online(true);
    self.self = true;
    CHECK(!RealPlayer(self));
    self = Offline(true, false);
    self.self = true;
    CHECK(!RealPlayer(self));
    std::cout << "  [PASS] the bot's own slot never counts\n";

    std::cout << "TortoiseBots timed strategy re-apply tests passed.\n";
    return 0;
}
