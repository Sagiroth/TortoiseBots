// Standalone test for runtime/GuildOrderCraftPolicy.h: the shared
// "can this recipe be ordered as Craft" rule (RECIPE order field, order
// give, craft search verdict).
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra tools/test_craft_orderable_policy.cpp -o /tmp/test_craft_orderable_policy
//   /tmp/test_craft_orderable_policy

#include "../runtime/GuildOrderCraftPolicy.h"

#include <cstdio>
#include <string>

using namespace TortoiseBots::GuildOrders;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { ++failures; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

static CraftOrderableInputs Orderable()
{
    CraftOrderableInputs inputs;
    inputs.known = true;
    inputs.createsItem = true;
    inputs.itemOrderable = true;
    inputs.spellRecoveryMs = 0;
    return inputs;
}

int main()
{
    // A normal known recipe is orderable.
    CHECK(CraftOrderableVerdict(Orderable()) == OrderReason::None);
    CHECK(std::string(CraftOrderToken(OrderReason::None)) == "ok");

    // Enchants (no made item) are not orderable yet.
    CraftOrderableInputs enchant = Orderable();
    enchant.createsItem = false;
    CHECK(CraftOrderableVerdict(enchant) == OrderReason::Enchant);
    CHECK(std::string(CraftOrderToken(OrderReason::Enchant)) == "enchant");

    // An unknown recipe keeps its own reason and text.
    CraftOrderableInputs unknown = Orderable();
    unknown.known = false;
    CHECK(CraftOrderableVerdict(unknown) == OrderReason::NoRecipe);
    CHECK(std::string(CraftOrderRefusal(OrderReason::NoRecipe)) == "The bot does not know that recipe.");

    // A BoP/quest/timed made item cannot be handed over.
    CraftOrderableInputs bound = Orderable();
    bound.itemOrderable = false;
    CHECK(CraftOrderableVerdict(bound) == OrderReason::Bound);
    CHECK(std::string(CraftOrderToken(OrderReason::Bound)) == "bound");

    // An item that needs a profession to use (engineering goggles).
    CraftOrderableInputs needSkill = Orderable();
    needSkill.itemNeedsProfession = true;
    CHECK(CraftOrderableVerdict(needSkill) == OrderReason::NeedsProfession);
    CHECK(std::string(CraftOrderToken(OrderReason::NeedsProfession)) == "needsprofession");

    // A recipe with a real cooldown (transmute): above the normal GCD.
    CraftOrderableInputs cooldown = Orderable();
    cooldown.spellRecoveryMs = 20 * 3600 * 1000;
    CHECK(CraftOrderableVerdict(cooldown) == OrderReason::Cooldown);
    CHECK(std::string(CraftOrderToken(OrderReason::Cooldown)) == "cooldown");

    // A normal cast-time GCD is fine.
    CraftOrderableInputs gcd = Orderable();
    gcd.spellRecoveryMs = 1500;
    CHECK(CraftOrderableVerdict(gcd) == OrderReason::None);

    // Precedence: after the known check, enchant runs first, then bound, then skill, then cooldown.
    CraftOrderableInputs multi = Orderable();
    multi.createsItem = false;
    multi.itemOrderable = false;
    multi.itemNeedsProfession = true;
    multi.spellRecoveryMs = 7200000;
    CHECK(CraftOrderableVerdict(multi) == OrderReason::Enchant);

    CraftOrderableInputs boundSkill = Orderable();
    boundSkill.itemOrderable = false;
    boundSkill.itemNeedsProfession = true;
    CHECK(CraftOrderableVerdict(boundSkill) == OrderReason::Bound);

    // Refusal lines are plain and say what to do instead where it matters.
    CHECK(std::string(CraftOrderRefusal(OrderReason::Enchant)) == "Enchants cannot be ordered yet.");
    CHECK(std::string(CraftOrderRefusal(OrderReason::Bound)) ==
        "That item is bound when picked up, so the bot cannot hand it over.");
    CHECK(std::string(CraftOrderRefusal(OrderReason::NeedsProfession)) == "That item needs a profession to use.");
    CHECK(std::string(CraftOrderRefusal(OrderReason::Cooldown)) ==
        "That recipe has a long cooldown, so the bot cannot make it on order.");
    CHECK(std::string(CraftOrderRefusal(OrderReason::NoRecipe)).find('|') == std::string::npos);

    // Fits the existing contract: token round-trips, states stay safe for old addons.
    for (uint8_t i = 0; i <= uint8_t(OrderReason::Cooldown); ++i)
    {
        OrderReason back = OrderReason::None;
        CHECK(ParseReason(ReasonToken(OrderReason(i)), back) && back == OrderReason(i));
    }
    CHECK(StateForReason(OrderReason::Enchant) == OrderState::Cant);
    CHECK(StateForReason(OrderReason::Bound) == OrderState::Cant);
    CHECK(StateForReason(OrderReason::NeedsProfession) == OrderState::Cant);
    CHECK(StateForReason(OrderReason::Cooldown) == OrderState::Cant);

    if (failures == 0)
        std::printf("test_craft_orderable_policy ok\n");
    return failures != 0;
}
