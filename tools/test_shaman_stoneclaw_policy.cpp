#include "../ai/playerbot/strategy/shaman/ShamanStoneclawPolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::StoneclawPanicShouldDrop;

int main()
{
    std::cout << "Starting shaman stoneclaw panic policy tests...\n";

    // Not low health: never drops, even solo with a manual pick.
    CHECK(StoneclawPanicShouldDrop(false, false, false, false) == false);
    CHECK(StoneclawPanicShouldDrop(false, false, true, false) == false);
    CHECK(StoneclawPanicShouldDrop(false, true, false, false) == false);
    std::cout << "  [PASS] healthy bots never drop the panic totem\n";

    // Solo + low health: the panic drop fires (donor behaviour).
    CHECK(StoneclawPanicShouldDrop(true, false, false, false) == true);
    std::cout << "  [PASS] solo low-health bots drop stoneclaw\n";

    // Grouped + low health: the earth slot stays with the spec totem.
    CHECK(StoneclawPanicShouldDrop(true, true, false, false) == false);
    std::cout << "  [PASS] grouped bots keep the spec earth totem\n";

    // Manual `totem earth stoneclaw` order wins everywhere: the player
    // picked Stoneclaw for this fight, so the panic drop follows it.
    CHECK(StoneclawPanicShouldDrop(true, true, true, false) == true);
    CHECK(StoneclawPanicShouldDrop(true, false, true, false) == true);
    std::cout << "  [PASS] manual stoneclaw orders keep precedence\n";

    // Any OTHER explicit earth order wins over the panic drop, solo or
    // grouped: the player picked that totem for this fight.
    CHECK(StoneclawPanicShouldDrop(true, false, false, true) == false);
    CHECK(StoneclawPanicShouldDrop(true, true, false, true) == false);
    CHECK(StoneclawPanicShouldDrop(true, false, true, true) == false);
    CHECK(StoneclawPanicShouldDrop(false, false, false, true) == false);
    std::cout << "  [PASS] other manual earth orders veto the panic drop\n";

    std::cout << "All shaman stoneclaw panic policy checks PASSED!\n";
    return 0;
}
