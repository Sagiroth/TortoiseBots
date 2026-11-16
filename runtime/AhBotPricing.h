#pragma once

#include <cstdint>
#include "runtime/policy/ah/AhBotPricingPolicy.h"

struct ItemPrototype;

namespace TortoiseBots
{
struct AhBotListingPrice
{
    uint32_t bid = 0;
    uint32_t buyout = 0;
};

class AhBotPricing
{
public:
    static AhBotPricing& Instance();

    void Reload();
    uint32_t StackSizeFor(ItemPrototype const* proto) const;
    AhBotListingPrice PriceFor(ItemPrototype const* proto, uint32_t count) const;
    uint64_t BuyerFairValuePerUnit(ItemPrototype const* proto) const;

private:
    AhBotPricing() = default;
    ~AhBotPricing() = default;

    void EnsureLoaded() const;

    mutable bool m_loaded = false;
    mutable ai::AhBotPricingConfig m_config;
};
} // namespace TortoiseBots
