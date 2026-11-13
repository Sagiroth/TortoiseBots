#include "../ai/playerbot/strategy/paladin/PaladinBlessingOrderPolicy.h"
#include <cstdio>
#include <cstdlib>

#define CHECK(cond) do { if (!(cond)) { std::printf("FAILED: %s (line %d)\n", #cond, __LINE__); std::exit(1); } } while (0)

int main()
{
    CHECK(ai::BlessingOrderClassIndex("warrior") == 0);
    CHECK(ai::BlessingOrderClassIndex("druid") == 8);
    CHECK(ai::BlessingOrderClassIndex("deathknight") == -1);
    CHECK(ai::BlessingOrderClassId(ai::BlessingOrderClassIndex("shaman")) == 7);
    CHECK(ai::BlessingOrderClassIndexById(11) == 8);
    CHECK(ai::BlessingOrderClassIndexById(6) == -1);
    for (int i = 0; i < ai::kBlessingOrderClassCount; ++i)
        CHECK(ai::BlessingOrderClassIndexById(ai::BlessingOrderClassId(i)) == i);

    CHECK(ai::BlessingOrderSpell("kings") == "blessing of kings");
    CHECK(ai::BlessingOrderSpell("auto").empty());
    CHECK(ai::BlessingOrderSpell("fury").empty());
    CHECK(ai::IsBlessingOrderWord("auto"));
    CHECK(ai::IsBlessingOrderWord("light"));
    CHECK(!ai::IsBlessingOrderWord(""));
    CHECK(!ai::IsBlessingOrderWord("none"));
    CHECK(ai::BlessingOrderKey(1) == "blessing order paladin");
    CHECK(ai::BlessingOrderPlayersKey() == "blessing order players");

    CHECK(ai::BlessingOrderNamesEqual("Aegnor", "aegnor"));
    CHECK(!ai::BlessingOrderNamesEqual("", ""));

    std::string players;
    players = ai::SetBlessingPlayerOrder(players, "Aegnor", "kings");
    CHECK(players == "Aegnor:kings");
    players = ai::SetBlessingPlayerOrder(players, "Bryn", "salvation");
    CHECK(players == "Aegnor:kings+Bryn:salvation");
    // Case-insensitive update replaces existing entry.
    players = ai::SetBlessingPlayerOrder(players, "aegnor", "wisdom");
    CHECK(players == "Bryn:salvation+aegnor:wisdom");

    // Precedence: player > class > auto.
    CHECK(ai::ResolveBlessingOrderSpell(players, "AEGNOR", "might") == "blessing of wisdom");
    CHECK(ai::ResolveBlessingOrderSpell(players, "Other", "might") == "blessing of might");
    CHECK(ai::ResolveBlessingOrderSpell(players, "Other", "auto").empty());
    CHECK(ai::ResolveBlessingOrderSpell(players, "Other", "").empty());

    // "auto" removes the player exception and falls back to class/auto.
    players = ai::SetBlessingPlayerOrder(players, "AEGNOR", "auto");
    CHECK(players == "Bryn:salvation");
    CHECK(ai::ResolveBlessingOrderSpell(players, "Aegnor", "might") == "blessing of might");

    // Malformed / unknown entries are ignored by parser.
    auto parsed = ai::ParseBlessingPlayerOrders("bad+Aegnor:auto+:kings+Bryn:LIGHT+Cirdan:fury");
    CHECK(parsed.size() == 1);
    CHECK(parsed[0].name == "Bryn");
    CHECK(parsed[0].word == "light");

    // Cap of 20 drops the oldest entry.
    std::string capped;
    for (int i = 0; i < 22; ++i)
        capped = ai::SetBlessingPlayerOrder(capped, "Player" + std::to_string(i), "kings");
    auto cappedList = ai::ParseBlessingPlayerOrders(capped);
    CHECK(cappedList.size() == ai::kBlessingOrderPlayerCap);
    CHECK(cappedList.front().name == "Player2");
    CHECK(cappedList.back().name == "Player21");
    CHECK(ai::FindBlessingPlayerOrder(capped, "Player0").empty());
    CHECK(ai::FindBlessingPlayerOrder(capped, "Player1").empty());
    CHECK(ai::FindBlessingPlayerOrder(capped, "Player2") == "kings");

    std::printf("All blessing order policy checks PASSED!\n");
    return 0;
}
