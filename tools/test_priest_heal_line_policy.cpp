// Priest Heal-line gate: a priest that trained Heal never casts Lesser
// Heal. Decision lives in ai/playerbot/policy/class/PriestHealLinePolicy.h.
//
// Build: g++ -std=c++17 -Wall -Wextra tools/test_priest_heal_line_policy.cpp -o /tmp/test_priest_heal_line

#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <set>

// Minimal Player stand-in: the policy header only calls HasSpell on it.
// Defined before the include so the inline wrapper sees a complete type.
class Player
{
public:
    explicit Player(std::set<std::uint32_t> const& spells) : spells(spells) {}
    bool HasSpell(std::uint32_t spellId) const { return spells.count(spellId) > 0; }

private:
    std::set<std::uint32_t> spells;
};

#include "../ai/playerbot/policy/class/PriestHealLinePolicy.h"

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::PriestHealLineKnown;
using ai::PriestKnowsHealLine;

static std::function<bool(std::uint32_t)> HasSpells(std::set<std::uint32_t> const& spells)
{
    return [&spells](std::uint32_t id) { return spells.count(id) > 0; };
}

int main()
{
    std::cout << "Starting TortoiseBots priest-heal-line policy tests...\n";

    // Lesser Heal ranks only (2050/2052/2053): no Heal line.
    CHECK(!PriestHealLineKnown(HasSpells({2050, 2052, 2053})));
    // Heal rank 1 alone is enough.
    CHECK(PriestHealLineKnown(HasSpells({2054})));
    // Top Heal rank alone is enough.
    CHECK(PriestHealLineKnown(HasSpells({6064})));
    // A mid Heal rank next to a Lesser Heal rank still counts.
    CHECK(PriestHealLineKnown(HasSpells({2053, 6063})));
    // Nothing known: no Heal line.
    CHECK(!PriestHealLineKnown(HasSpells({})));

    // The Player wrapper reads the same ranks off the spellbook.
    Player noHeal({});
    CHECK(!PriestKnowsHealLine(&noHeal));
    Player lowPriest({2050, 2052, 2053});
    CHECK(!PriestKnowsHealLine(&lowPriest));
    Player healPriest({2050, 2052, 2053, 2054});
    CHECK(PriestKnowsHealLine(&healPriest));

    std::cout << "All priest-heal-line policy tests passed.\n";
    return 0;
}
