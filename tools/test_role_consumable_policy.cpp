#include "../runtime/ConsumableProvisionClock.h"
#include "../runtime/RoleConsumablePolicy.h"
#include <cassert>
#include <iostream>
#include <set>
using namespace TortoiseBots;
int main()
{
    using C = ConsumableCategory;
    for (auto c : {C::Health, C::Mana, C::Elixir, C::Scroll, C::Flask, C::Food})
    {
        assert(!ConsumableUnlocked(c, false, false, false, false));
        assert(!ConsumableUnlocked(c, false, false, true, true));
        assert(ConsumableUnlocked(c, true, false, false, false));
    }
    assert(ConsumableUnlocked(C::Health, false, true, false, false));
    assert(ConsumableUnlocked(C::Mana, false, true, false, false));
    assert(!ConsumableUnlocked(C::Scroll, false, true, false, true));
    assert(ConsumableUnlocked(C::Elixir, false, true, true, false));
    assert(ConsumableUnlocked(C::Food, false, true, true, false));
    assert(!ConsumableUnlocked(C::Flask, false, true, true, false));
    assert(ConsumableUnlocked(C::Flask, false, true, true, true));
    assert(ConsumableTopUp(C::Flask, 0, 10) == 2);
    assert(ConsumableTopUp(C::Flask, 1, 10) == 1);
    assert(ConsumableTopUp(C::Flask, 2, 10) == 0);
    assert(ConsumableTopUp(C::Scroll, 2, 20) == 3);
    assert(ConsumableTopUp(C::Elixir, 6, 10) == 0);
    assert(ConsumableTopUp(C::Health, 0, 5) == 5);
    assert(ConsumableTopUp(C::Mana, 0, 0) == 0);
    assert(ConsumableTopUp(C::Health, UINT32_MAX, 10) == 0);
    assert(NeedsConsumablePotion(C::Health, true, 1, 10, true));
    assert(NeedsConsumablePotion(C::Health, true, 29, 10, true));
    assert(!NeedsConsumablePotion(C::Health, true, 30, 10, true));
    assert(!NeedsConsumablePotion(C::Health, false, 1, 1, true));
    assert(NeedsConsumablePotion(C::Mana, true, 90, 29, true));
    assert(!NeedsConsumablePotion(C::Mana, true, 90, 0, false));
    assert(NeedsConsumablePotion(C::Mana, true, 90, 30, true));
    assert(NeedsConsumablePotion(C::Mana, true, 90, 64, true));
    assert(!NeedsConsumablePotion(C::Mana, true, 90, 65, true));
    ConsumableProvisionClock clock;
    assert(clock.Tick(true, 1)); // first safe login/hire update
    clock.remaining = 300000;
    assert(!clock.Tick(true, 299999));
    assert(clock.Tick(true, 1));
    clock.remaining = 300000;
    assert(!clock.Tick(false, 1));
    assert(clock.Tick(true, 1)); // resurrection must not wait five minutes
    clock.remaining = 5000;
    assert(!clock.Tick(true, 4999));
    assert(clock.Tick(true, UINT32_MAX)); // no unsigned timer wrap
    std::set<unsigned> ids;
    for (auto const& e : RoleConsumables) assert(ids.insert(e.item).second);
    auto first = [](C category, unsigned role) {
        for (auto const& e : RoleConsumables)
            if (e.category == category && (e.roles & role)) return e.item;
        return 0u;
    };
    assert(first(C::Flask, Tank) == 13510);
    assert(first(C::Flask, Physical) == 13510);
    assert(first(C::Flask, Healer) == 13511);
    assert(first(C::Flask, Caster) == 13512);
    assert(first(C::Elixir, Tank) == 13445);
    assert(first(C::Elixir, Healer) == 13447);
    assert(first(C::Elixir, Caster) == 13454);
    assert(first(C::Elixir, Physical) == 13452);
    std::cout << "role consumable policy: OK\n";
}
