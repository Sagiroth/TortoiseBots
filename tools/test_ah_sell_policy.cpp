// Standalone policy test: bots never post unlimited vendor stock or blacklisted items.
// Covers runtime/policy/ah/AhSellPolicy.h (the pure rules ItemUsageValue and
// AhMarketService call into) plus the blacklist default in PlayerbotAIConfig /
// the shipped template (both must be "18256").
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra tools/test_ah_sell_policy.cpp -o /tmp/test_ah_sell_policy && /tmp/test_ah_sell_policy

#include "../runtime/policy/ah/AhSellPolicy.h"
#include "../runtime/policy/ah/AhSyntheticMarketPolicy.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_set>

static int checks = 0;
#define CHECK(cond) do { \
    ++checks; \
    if (!(cond)) { \
        std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        std::exit(1); \
    } \
} while (0)

static std::string TemplateDefault(char const* key)
{
    std::ifstream in("ai/playerbot/aiplayerbot.conf.dist.in");
    CHECK(in.good());
    std::string line;
    while (std::getline(in, line))
    {
        if (line.rfind(key, 0) != 0)
            continue;
        std::string value = line.substr(std::string(key).size());
        std::istringstream ss(value);
        std::string token;
        std::getline(ss, token, '#');
        size_t b = token.find_first_not_of(" \t=");
        size_t e = token.find_last_not_of(" \t");
        CHECK(b != std::string::npos);
        return token.substr(b, e - b + 1);
    }
    return "";
}

static std::string CodeDefault()
{
    std::ifstream in("ai/playerbot/PlayerbotAIConfig.cpp");
    CHECK(in.good());
    std::string line;
    while (std::getline(in, line))
    {
        size_t pos = line.find("\"AiPlayerbot.AhSellBlacklist\"");
        if (pos == std::string::npos)
            continue;
        size_t q1 = line.find('"', pos + 30);
        CHECK(q1 != std::string::npos);
        size_t q2 = line.find('"', q1 + 1);
        CHECK(q2 != std::string::npos);
        return line.substr(q1 + 1, q2 - q1 - 1);
    }
    return "";
}

int main()
{
    // Blacklist membership is exact: listed ids are blocked, others pass.
    std::unordered_set<uint32_t> blacklist = { ai::kAhSellBlacklistDefault };
    CHECK(ai::AhItemSellBlacklisted(18256, blacklist));
    CHECK(!ai::AhItemSellBlacklisted(18257, blacklist));
    CHECK(!ai::AhItemSellBlacklisted(18256, std::unordered_set<uint32_t>()));

    // Unlimited vendor stock: sold by a vendor with no limited maxcount.
    CHECK(ai::AhUnlimitedVendorStock(true, false));
    CHECK(!ai::AhUnlimitedVendorStock(true, true));
    CHECK(!ai::AhUnlimitedVendorStock(false, false));
    CHECK(!ai::AhUnlimitedVendorStock(false, true));

    // Synthetic supply rejects unlimited vendor stock even when eligible.
    CHECK(!ai::AhItemEligible(false, false, true, 1, 4, 0, 60, 55, 60, 0, 0, true));
    CHECK(ai::AhItemEligible(false, false, true, 1, 4, 0, 60, 55, 60, 0, 0, false));
    CHECK(!ai::AhItemEligible(false, true, true, 1, 4, 0, 60, 55, 60, 0, 0, true));

    // The owner-editable default ships the vial everywhere it is read.
    CHECK(TemplateDefault("AiPlayerbot.AhSellBlacklist") == "18256");
    CHECK(CodeDefault() == "18256");

    std::cout << "OK " << checks << " checks\n";
    return 0;
}
