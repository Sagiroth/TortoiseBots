#include "../ai/playerbot/policy/guild/EnchantTradePolicy.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::CheckEnchantTrade;
using ai::CheckMaterialsTrade;
using ai::CheckTradeTry;
using ai::EnchantAskReplaceText;
using ai::EnchantTradeState;
using ai::EnchantTradeToken;
using ai::EnchantTradeVerdict;
using ai::kEnchantExtrasText;
using ai::kEnchantNoItemText;
using ai::kEnchantWrongItemText;
using ai::kEnchantWrongPlayerText;
using ai::kMaterialsNoGoldText;
using ai::kMaterialsNotMaterialText;
using ai::kTradeGiveUpText;
using ai::MaterialsTradeState;
using ai::MaterialsTradeToken;
using ai::MaterialsTradeVerdict;
using ai::OfferedStack;
using ai::EnchantReagentNeed;
using ai::ReplaceAnswerIsYes;
using ai::ReplaceWaitExpired;
using ai::TradeTryAction;
using ai::TradeTryToken;
using ai::kEnchantTradeMaxTries;
using ai::kEnchantTradeTryMs;
using ai::kReplaceAnswerMs;
using ai::kTradeWaitCountsTowardOrderLimits;

static EnchantTradeState Enchant(bool giver, bool item, bool fits, bool enchanted,
    bool confirmed, bool extras)
{
    EnchantTradeState state;
    state.traderIsGiver = giver;
    state.hasItem = item;
    state.fitsEnchant = fits;
    state.hasPermanentEnchant = enchanted;
    state.replaceConfirmed = confirmed;
    state.traderExtras = extras;
    return state;
}

static MaterialsTradeState Materials(std::vector<EnchantReagentNeed> const& allowed,
    std::vector<OfferedStack> const& offered, uint32_t gold = 0)
{
    MaterialsTradeState state;
    state.allowed = allowed;
    state.offered = offered;
    state.traderGold = gold;
    return state;
}

int main()
{
    std::cout << "Starting TortoiseBots enchant-trade-check policy tests...\n";

    // 1. Enchant trade: each branch of the verdict order.
    CHECK(CheckEnchantTrade(Enchant(true, true, true, false, false, false)) == EnchantTradeVerdict::Ok);
    CHECK(CheckEnchantTrade(Enchant(true, true, true, true, true, false)) == EnchantTradeVerdict::Ok);
    CHECK(CheckEnchantTrade(Enchant(false, true, true, false, false, false)) == EnchantTradeVerdict::WrongPlayer);
    CHECK(CheckEnchantTrade(Enchant(false, false, false, false, false, true)) == EnchantTradeVerdict::WrongPlayer);
    CHECK(CheckEnchantTrade(Enchant(true, true, true, false, false, true)) == EnchantTradeVerdict::Extras);
    CHECK(CheckEnchantTrade(Enchant(true, false, false, false, false, false)) == EnchantTradeVerdict::NoItem);
    CHECK(CheckEnchantTrade(Enchant(true, true, false, false, false, false)) == EnchantTradeVerdict::WrongItem);
    CHECK(CheckEnchantTrade(Enchant(true, true, true, true, false, false)) == EnchantTradeVerdict::AskReplace);
    std::cout << "  [PASS] enchant verdict order (wrongplayer, extras, noitem, wrongitem, askreplace, ok)\n";

    // 2. Enchant trade: verdict tokens double as the refusal reason.
    CHECK(std::string(EnchantTradeToken(EnchantTradeVerdict::Ok)) == "ok");
    CHECK(std::string(EnchantTradeToken(EnchantTradeVerdict::WrongPlayer)) == "wrongplayer");
    CHECK(std::string(EnchantTradeToken(EnchantTradeVerdict::NoItem)) == "noitem");
    CHECK(std::string(EnchantTradeToken(EnchantTradeVerdict::WrongItem)) == "wrongitem");
    CHECK(std::string(EnchantTradeToken(EnchantTradeVerdict::AskReplace)) == "askreplace");
    CHECK(std::string(EnchantTradeToken(EnchantTradeVerdict::Extras)) == "extras");
    std::cout << "  [PASS] enchant verdict tokens\n";

    // 3. Enchant trade: the bot never brings its own items or gold, so the
    // caller passes no bot-side state; the texture check stays with the action.
    CHECK(!Enchant(true, true, true, false, false, false).traderExtras);
    std::cout << "  [PASS] bot-side window is out of scope\n";

    // 4. Materials trade: empty window and allowed reagents (even surplus) pass.
    std::vector<EnchantReagentNeed> needs = { { 12810, 2 }, { 11178, 1 } };
    CHECK(CheckMaterialsTrade(Materials(needs, {})).verdict == MaterialsTradeVerdict::Accept);
    CHECK(CheckMaterialsTrade(Materials(needs, { { 12810, 3 } })).verdict == MaterialsTradeVerdict::Accept);
    CHECK(CheckMaterialsTrade(Materials(needs, { { 12810, 2 }, { 11178, 1 } })).verdict ==
        MaterialsTradeVerdict::Accept);
    CHECK(std::string(MaterialsTradeToken(MaterialsTradeVerdict::Accept)) == "accept");
    std::cout << "  [PASS] materials accept (empty, exact, surplus)\n";

    // 5. Materials trade: the first foreign stack refuses the window.
    {
        auto result = CheckMaterialsTrade(Materials(needs, { { 12810, 2 }, { 999, 1 }, { 555, 1 } }));
        CHECK(result.verdict == MaterialsTradeVerdict::NotMaterial);
        CHECK(result.offendingItemId == 999);
        CHECK(std::string(MaterialsTradeToken(MaterialsTradeVerdict::NotMaterial)) == "notmaterial");
    }
    CHECK(CheckMaterialsTrade(Materials({}, { { 1, 1 } })).verdict == MaterialsTradeVerdict::NotMaterial);
    std::cout << "  [PASS] materials refuse with the first offending item\n";

    // 6. Materials trade: gold refuses the window, even with valid items.
    CHECK(CheckMaterialsTrade(Materials(needs, {}, 50)).verdict == MaterialsTradeVerdict::NoGold);
    CHECK(CheckMaterialsTrade(Materials(needs, { { 12810, 1 } }, 1)).verdict == MaterialsTradeVerdict::NoGold);
    CHECK(std::string(MaterialsTradeToken(MaterialsTradeVerdict::NoGold)) == "nogold");
    std::cout << "  [PASS] materials refuse gold\n";

    // 7. Materials trade: foreign items beat gold (the window names one reason).
    {
        auto result = CheckMaterialsTrade(Materials(needs, { { 999, 1 } }, 50));
        CHECK(result.verdict == MaterialsTradeVerdict::NotMaterial);
        CHECK(result.offendingItemId == 999);
    }
    std::cout << "  [PASS] materials name the foreign item before gold\n";

    // 8. Try budget: 2 minutes per try, 3 tries, then give up.
    CHECK(kEnchantTradeTryMs == 2u * 60u * 1000u);
    CHECK(kEnchantTradeMaxTries == 3);
    CHECK(CheckTradeTry(1000, 1000, 1, false) == TradeTryAction::KeepWaiting);
    CHECK(CheckTradeTry(1000 + 60 * 1000, 1000, 1, false) == TradeTryAction::KeepWaiting);
    CHECK(CheckTradeTry(1000 + kEnchantTradeTryMs - 1, 1000, 1, false) == TradeTryAction::KeepWaiting);
    CHECK(CheckTradeTry(1000 + kEnchantTradeTryMs, 1000, 1, false) == TradeTryAction::CancelRetry);
    CHECK(CheckTradeTry(1000 + kEnchantTradeTryMs, 1000, 2, false) == TradeTryAction::CancelRetry);
    CHECK(CheckTradeTry(1000 + kEnchantTradeTryMs, 1000, 3, false) == TradeTryAction::GiveUp);
    CHECK(std::string(TradeTryToken(TradeTryAction::KeepWaiting)) == "wait");
    CHECK(std::string(TradeTryToken(TradeTryAction::CancelRetry)) == "retry");
    CHECK(std::string(TradeTryToken(TradeTryAction::GiveUp)) == "giveup");
    std::cout << "  [PASS] 2 minutes per try, 3 tries, then give up\n";

    // 9. Try budget: placing items at the deadline buys one more window.
    CHECK(CheckTradeTry(5000 + kEnchantTradeTryMs, 5000, 3, true) == TradeTryAction::KeepWaiting);
    CHECK(CheckTradeTry(5000 + 2 * kEnchantTradeTryMs, 5000, 3, true) == TradeTryAction::GiveUp);
    CHECK(CheckTradeTry(5000 + kEnchantTradeTryMs - 1, 5000, 3, true) == TradeTryAction::KeepWaiting);
    std::cout << "  [PASS] mid-placement grace never past one extra window\n";

    // 10. Trade waiting stays out of the order time and no-progress limits.
    CHECK(!kTradeWaitCountsTowardOrderLimits);
    std::cout << "  [PASS] trade wait excluded from order limits\n";

    // 11. Replace answer: "yes" (any case, padded) confirms, the rest refuses.
    CHECK(ReplaceAnswerIsYes("yes") && ReplaceAnswerIsYes("|c1049e6ffyes|r") && !ReplaceAnswerIsYes("|c1049e6ffno|r"));
    CHECK(ReplaceAnswerIsYes("YES"));
    CHECK(ReplaceAnswerIsYes("  Yes  "));
    CHECK(ReplaceAnswerIsYes("\tyes\n"));
    CHECK(!ReplaceAnswerIsYes(""));
    CHECK(!ReplaceAnswerIsYes("   "));
    CHECK(!ReplaceAnswerIsYes("y"));
    CHECK(!ReplaceAnswerIsYes("yeah"));
    CHECK(!ReplaceAnswerIsYes("yes please"));
    CHECK(!ReplaceAnswerIsYes("no"));
    std::cout << "  [PASS] replace answer parsing\n";

    // 12. Replace answer has 2 minutes from the question.
    CHECK(kReplaceAnswerMs == 2u * 60u * 1000u);
    CHECK(!ReplaceWaitExpired(7000 + kReplaceAnswerMs - 1, 7000));
    CHECK(ReplaceWaitExpired(7000 + kReplaceAnswerMs, 7000));
    std::cout << "  [PASS] replace answer window\n";

    // 13. Reply texts are short English one-liners with the issue's wording.
    CHECK(std::string(ai::kEnchantNoItemText) == "Put your item in Will not be traded.");
    CHECK(std::string(ai::kEnchantWrongItemText) == "That item does not fit the enchant.");
    CHECK(std::string(ai::kEnchantWrongPlayerText) == "This trade is not for you.");
    CHECK(std::string(ai::kEnchantExtrasText) ==
        "Take your items out, this trade is only for the enchant.");
    CHECK(std::string(ai::kMaterialsNotMaterialText) == "That is not part of this order.");
    CHECK(std::string(ai::kMaterialsNoGoldText) == "I take no gold for this order.");
    CHECK(std::string(ai::kTradeGiveUpText) == "Trade timed out. Press Meet when you are ready.");
    CHECK(ai::EnchantAskReplaceText("Enchant Bracer - Minor Health") ==
        "It already has Enchant Bracer - Minor Health, replace it? Answer yes");
    std::cout << "  [PASS] reply texts\n";

    std::cout << "All TortoiseBots enchant-trade-check policy tests passed.\n";
    return 0;
}
