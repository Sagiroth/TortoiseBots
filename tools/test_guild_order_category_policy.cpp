// Standalone test for runtime/GuildOrderCategoryPolicy.h: the guild-order
// search item kinds (TBM:ORDER_ITEM field 12).
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra -pedantic tools/test_guild_order_category_policy.cpp -o /tmp/test_guild_order_category_policy
//   /tmp/test_guild_order_category_policy
#include "../runtime/GuildOrderCategoryPolicy.h"

#include <cstdio>
#include <string>

using namespace TortoiseBots::GuildOrders;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { ++failures; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

int main()
{
    for (uint8_t i = 0; i <= uint8_t(OrderItemCategory::Other); ++i)
    {
        OrderItemCategory back = OrderItemCategory::Other;
        CHECK(ParseCategory(CategoryToken(OrderItemCategory(i)), back) && back == OrderItemCategory(i));
    }
    OrderItemCategory parsed = OrderItemCategory::Other;
    CHECK(!ParseCategory("stone", parsed));
    CHECK(!ParseCategory("Drop", parsed));
    CHECK(!ParseCategory("", parsed));

    CHECK(std::string(CategoryToken(OrderItemCategory::Herb)) == "herb");
    CHECK(std::string(CategoryToken(OrderItemCategory::Ore)) == "ore");
    CHECK(std::string(CategoryToken(OrderItemCategory::Leather)) == "leather");
    CHECK(std::string(CategoryToken(OrderItemCategory::Fish)) == "fish");
    CHECK(std::string(CategoryToken(OrderItemCategory::Cloth)) == "cloth");
    CHECK(std::string(CategoryToken(OrderItemCategory::Drop)) == "drop");
    CHECK(std::string(CategoryToken(OrderItemCategory::Other)) == "other");

    CHECK(SourcesTokenHas("drop,skin", "drop"));
    CHECK(SourcesTokenHas("drop,skin", "skin"));
    CHECK(!SourcesTokenHas("drop,skin", "herb"));
    CHECK(!SourcesTokenHas("drop,skinner", "skin"));
    CHECK(!SourcesTokenHas("", "drop"));
    CHECK(SourcesTokenHas("-", "drop") == false);

    // Planner sources win over everything else.
    CHECK(ClassifyOrderCategory("herb,drop", false) == OrderItemCategory::Herb);
    CHECK(ClassifyOrderCategory("ore,drop", false) == OrderItemCategory::Ore);
    CHECK(ClassifyOrderCategory("skin,drop", false) == OrderItemCategory::Leather);
    CHECK(ClassifyOrderCategory("fish", false) == OrderItemCategory::Fish);
    // Trade Goods cloth only when no gather source claims the item.
    CHECK(ClassifyOrderCategory("drop", true) == OrderItemCategory::Cloth);
    CHECK(ClassifyOrderCategory("craft,ah", true) == OrderItemCategory::Cloth);
    CHECK(ClassifyOrderCategory("drop", false) == OrderItemCategory::Drop);
    CHECK(ClassifyOrderCategory("craft,ah", false) == OrderItemCategory::Other);
    CHECK(ClassifyOrderCategory("-", false) == OrderItemCategory::Other);
    // Skinning beats cloth: a skinned hide is leather even when filed as cloth.
    CHECK(ClassifyOrderCategory("skin,drop", true) == OrderItemCategory::Leather);

    if (failures == 0)
        std::printf("test_guild_order_category_policy ok\n");
    return failures ? 1 : 0;
}
