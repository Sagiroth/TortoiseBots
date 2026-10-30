// Standalone test for runtime/GuildOrderPoolPolicy.h: the guild-order pool
// residency rules (logout skip, order-first login).
// No core, no DB: feeds the policy plain order state and pick predicates.
#include "../runtime/GuildOrderPoolPolicy.h"

#include <cstdio>

using namespace TortoiseBots::GuildOrders;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { ++failures; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

static OrderRecord Unfinished()
{
    OrderRecord record;
    record.state = OrderState::Working;
    return record;
}

int main()
{
    // An unfinished order keeps its bot online; finished orders and no order log out.
    CHECK(KeepOrderBotOnline(Unfinished()));
    OrderRecord working = Unfinished();
    working.state = OrderState::Delivering;
    CHECK(KeepOrderBotOnline(working));
    OrderRecord done;
    done.state = OrderState::Done;
    CHECK(!KeepOrderBotOnline(done));
    OrderRecord mailed;
    mailed.state = OrderState::Mailed;
    CHECK(!KeepOrderBotOnline(mailed));
    OrderRecord noskill;
    noskill.state = OrderState::NoSkill;
    CHECK(!KeepOrderBotOnline(noskill));
    OrderRecord cant;
    cant.state = OrderState::Cant;
    CHECK(!KeepOrderBotOnline(cant));
    CHECK(!KeepOrderBotOnline(std::nullopt));

    // Order-first logins take scan order, bounded by the free slots and queue.
    bool unfinished[] = {true, false, true, true};
    auto has = [&](size_t i) { return unfinished[i]; };
    OrderLoginPick first = PickOrderBotsFirst(4, has, 8, 10, 5);
    CHECK(first.indexes.size() == 2 && first.indexes[0] == 0 && first.indexes[1] == 2);
    CHECK(first.online == 10);

    // The pool cap wins over the queue budget: one free slot, one login.
    OrderLoginPick capped = PickOrderBotsFirst(4, has, 9, 10, 5);
    CHECK(capped.indexes.size() == 1 && capped.indexes[0] == 0);
    CHECK(capped.online == 10);

    // The queue budget wins below the cap: one queue slot, one login.
    OrderLoginPick queued = PickOrderBotsFirst(4, has, 5, 10, 1);
    CHECK(queued.indexes.size() == 1 && queued.indexes[0] == 0);
    CHECK(queued.online == 6);

    // Full pool and empty queue log in nobody.
    CHECK(PickOrderBotsFirst(4, has, 10, 10, 5).indexes.empty());
    CHECK(PickOrderBotsFirst(4, has, 5, 10, 0).indexes.empty());
    CHECK(PickOrderBotsFirst(0, has, 5, 10, 5).indexes.empty());

    if (failures == 0)
        std::printf("test_guild_order_pool_policy ok\n");
    return failures ? 1 : 0;
}
