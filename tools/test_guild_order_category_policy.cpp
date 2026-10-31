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
    // A fish that mobs also drop (Raw Slitherskin Mackerel 6303) still
    // browses as fish: the fish token beats drop.
    CHECK(ClassifyOrderCategory("fish,drop", false) == OrderItemCategory::Fish);
    // Trade Goods cloth beats every source token: Wool Cloth 2592 drops from
    // mobs and skins from Sheep 1933, but it browses as cloth so cat=cloth
    // finds it and cat=leather does not.
    CHECK(ClassifyOrderCategory("drop", true) == OrderItemCategory::Cloth);
    CHECK(ClassifyOrderCategory("craft,ah", true) == OrderItemCategory::Cloth);
    CHECK(ClassifyOrderCategory("drop,skin", true) == OrderItemCategory::Cloth);
    CHECK(ClassifyOrderCategory("drop", false) == OrderItemCategory::Drop);
    CHECK(ClassifyOrderCategory("craft,ah", false) == OrderItemCategory::Other);
    CHECK(ClassifyOrderCategory("-", false) == OrderItemCategory::Other);
    // A skinned hide without a cloth name stays leather.
    CHECK(ClassifyOrderCategory("skin,drop", false) == OrderItemCategory::Leather);

    // Trade-goods cloth: the 1.12 cloth names (Linen to Mooncloth, bolts
    // included) end in "cloth"; class 7 only, so armor never matches.
    CHECK(IsTradeCloth(7, "linen cloth"));
    CHECK(IsTradeCloth(7, "bolt of runecloth"));
    CHECK(IsTradeCloth(7, "runecloth"));
    CHECK(IsTradeCloth(7, "mooncloth"));
    CHECK(!IsTradeCloth(7, "copper ore"));
    CHECK(IsTradeCloth(7, "cloth"));
    CHECK(!IsTradeCloth(4, "tattered cloth vest"));
    CHECK(!IsTradeCloth(7, ""));

    // cat=<kind> sits anywhere in the text and strips out of it.
    {
        OrderItemCategory cat = OrderItemCategory::Other;
        std::string query = "cat=cloth linen";
        CHECK(StripCategoryArg(query, cat) == CategoryArgParse::Ok);
        CHECK(cat == OrderItemCategory::Cloth && query == "linen");
    }
    {
        OrderItemCategory cat = OrderItemCategory::Other;
        std::string query = "linen cat=cloth";
        CHECK(StripCategoryArg(query, cat) == CategoryArgParse::Ok);
        CHECK(cat == OrderItemCategory::Cloth && query == "linen");
    }
    {
        OrderItemCategory cat = OrderItemCategory::Other;
        std::string query = "linen cat= cloth";
        CHECK(StripCategoryArg(query, cat) == CategoryArgParse::Ok);
        CHECK(cat == OrderItemCategory::Cloth && query == "linen");
    }
    {
        OrderItemCategory cat = OrderItemCategory::Other;
        std::string query = "CAT=Ore iron";
        CHECK(StripCategoryArg(query, cat) == CategoryArgParse::Ok);
        CHECK(cat == OrderItemCategory::Ore && query == "iron");
    }
    {
        OrderItemCategory cat = OrderItemCategory::Other;
        std::string query = "cat=ore";
        CHECK(StripCategoryArg(query, cat) == CategoryArgParse::Ok);
        CHECK(cat == OrderItemCategory::Ore && query.empty());
    }
    {
        OrderItemCategory cat = OrderItemCategory::Other;
        std::string query = "cat=stone linen";
        CHECK(StripCategoryArg(query, cat) == CategoryArgParse::Bad);
        CHECK(query == "cat=stone linen");
    }
    {
        OrderItemCategory cat = OrderItemCategory::Other;
        std::string query = "linen";
        CHECK(StripCategoryArg(query, cat) == CategoryArgParse::None);
        CHECK(query == "linen");
    }
    if (failures == 0)
        std::printf("test_guild_order_category_policy ok\n");
    return failures ? 1 : 0;
}
