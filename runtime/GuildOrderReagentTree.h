#pragma once

// Whole-tree base-reagent totals for a craft order. The supply ladder needs
// the total count of every material the bot cannot make, not one branch's
// share: a base reagent can sit under two different intermediates at
// different depths (Goblin Rocket Boots: Mageweave Cloth under Unstable
// Trigger and under Bolt of Mageweave), and stock held for one branch must
// not answer another branch's need. This header walks the tree once,
// expanding each craftable item by its guaranteed yield and subtracting
// held stock at every level.
//
// Header-only and free of engine types on purpose: the recipe and the stock
// arrive as callbacks, so the rule is exercised standalone in
// tools/test_guild_order_reagent_tree_policy.cpp.
#include <cstddef>
#include <cstdint>
#include <utility>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "policy/provision/CraftOrderPolicy.h"

namespace TortoiseBots
{
namespace GuildOrders
{

struct RecipeReagent
{
    uint32_t itemId = 0;
    uint32_t perCast = 0;
};

// What one cast of a known recipe consumes. yieldPerCast is the guaranteed
// minimum the cast creates, so the walk never plans on a lucky roll.
struct Recipe
{
    uint32_t yieldPerCast = 1;
    std::vector<RecipeReagent> reagents;
    std::vector<uint32_t> tools;
};

struct ReagentNeed
{
    uint32_t itemId = 0;
    uint32_t count = 0;
};

// The same base reagent counted three ways, because the supply ladder asks
// three different questions of it: how much the whole tree needs, how much
// the bags leave short, and how much the bags and the mailbox together leave
// short. The mailbox leg brings in what the bags do not cover, while a bank,
// auction or farm leg must cover only what the mail cannot.
struct ReagentTotals
{
    uint32_t itemId = 0;
    uint32_t required = 0;
    uint32_t bagShort = 0;
    uint32_t extShort = 0;
};

// The mailbox stock a walk may spend. The ladder drops the mail leg for an
// item it could not fetch from the mailbox for ten minutes; that mail is not
// a source, so letting the walk subtract it makes the shortfall collapse to
// zero while the ladder itself asks for the whole branch.
inline uint32_t SpentMail(uint32_t inMail, bool mailLegSkipped)
{
    return mailLegSkipped ? 0 : inMail;
}

// The count the ladder sources for one base reagent. The whole-tree walk
// spends each stock once across every branch, so its total can come out below
// the shortfall of the branch the ladder stands on; that branch is still
// short, so the larger of the two is the count.
inline uint32_t WholeTreeCount(uint32_t treeCount, uint32_t branchShortfall)
{
    return treeCount > branchShortfall ? treeCount : branchShortfall;
}

// Eight levels cover every vanilla craft chain (ore -> bar -> tube -> casing
// -> item) and the deep chains a sub-crafted kit adds on top; past it the
// walk treats the item as a leaf instead of trusting a recipe cycle to
// terminate. The supply ladder stops at the same depth so both views of a
// chain agree on where it ends.
inline constexpr uint8_t kReagentTreeMaxDepth = 8;

namespace detail
{

template <typename RecipeFor, typename Held>
class ReagentWalker
{
public:
    ReagentWalker(RecipeFor& recipeFor, Held& held)
        : m_recipeFor(recipeFor), m_held(held)
    {
    }

    // subtractHeld is off for the root: callers hand in the count that is
    // still missing after their own stock check.
    void Walk(uint32_t itemId, uint32_t need, uint8_t depth, bool subtractHeld)
    {
        if (!itemId || !need)
            return;
        if (subtractHeld)
        {
            uint32_t& left = Stock(itemId);
            uint32_t taken = left < need ? left : need;
            left -= taken;
            need -= taken;
            if (!need)
                return;
        }

        Recipe recipe;
        if (depth < kReagentTreeMaxDepth && !InPath(itemId) && m_recipeFor(itemId, recipe))
        {
            uint32_t casts = TortoiseBots::CraftOrderPolicy::CastsForRemaining(need, recipe.yieldPerCast);
            m_path.push_back(itemId);
            for (RecipeReagent const& reagent : recipe.reagents)
                Walk(reagent.itemId, Scaled(reagent.perCast, casts), uint8_t(depth + 1), true);
            // A recipe tool is held once and reused by every level that lists
            // it: demand it once, and the stock ledger keeps it quiet when
            // the bags already hold one.
            for (uint32_t tool : recipe.tools)
                if (m_tools.insert(tool).second)
                    Walk(tool, 1, uint8_t(depth + 1), true);
            m_path.pop_back();
            return;
        }

        Emit(itemId, need);
    }

    std::vector<ReagentNeed> Take() { return std::move(m_needs); }

private:
    uint32_t& Stock(uint32_t itemId)
    {
        auto found = m_stock.find(itemId);
        if (found == m_stock.end())
            found = m_stock.emplace(itemId, m_held(itemId)).first;
        return found->second;
    }

    bool InPath(uint32_t itemId) const
    {
        for (uint32_t onPath : m_path)
            if (onPath == itemId)
                return true;
        return false;
    }

    static uint32_t Scaled(uint32_t perCast, uint32_t casts)
    {
        uint64_t total = uint64_t(perCast) * casts;
        return total > UINT32_MAX ? UINT32_MAX : uint32_t(total);
    }

    void Emit(uint32_t itemId, uint32_t count)
    {
        auto found = m_index.find(itemId);
        if (found != m_index.end())
        {
            m_needs[found->second].count += count;
            return;
        }
        m_index.emplace(itemId, m_needs.size());
        m_needs.push_back(ReagentNeed{ itemId, count });
    }

    RecipeFor& m_recipeFor;
    Held& m_held;
    std::unordered_map<uint32_t, uint32_t> m_stock;
    std::unordered_map<uint32_t, std::size_t> m_index;
    std::unordered_set<uint32_t> m_tools;
    std::vector<ReagentNeed> m_needs;
    std::vector<uint32_t> m_path;
};

} // namespace detail

// Base reagents (and missing leaves) still to be sourced for `count` more of
// itemId, one row per item in first-encounter order. recipeFor fills a
// Recipe and returns false for an item the bot cannot make; held returns the
// stock still in bags and mail.
template <typename RecipeFor, typename Held>
inline std::vector<ReagentNeed> CollectBaseReagents(uint32_t itemId, uint32_t count,
    RecipeFor recipeFor, Held held)
{
    detail::ReagentWalker<RecipeFor, Held> walker(recipeFor, held);
    walker.Walk(itemId, count, 0, false);
    return walker.Take();
}

// Base reagents for `count` more of itemId with all three counts filled, one
// row per item in first-encounter order. Each aggregate gets its own walk:
// held stock changes how many casts the tree needs, so the walks cannot share
// one ledger. bags and mail return the stock in each place; the mailbox walk
// also spends the bags, exactly as the ladder does.
template <typename RecipeFor, typename Bags, typename Mail>
inline std::vector<ReagentTotals> CollectReagentTotals(uint32_t itemId, uint32_t count,
    RecipeFor recipeFor, Bags bags, Mail mail)
{
    std::unordered_map<uint32_t, std::size_t> index;
    std::vector<ReagentTotals> totals;
    auto absorb = [&index, &totals](std::vector<ReagentNeed> const& needs, uint32_t ReagentTotals::*field)
    {
        for (ReagentNeed const& need : needs)
        {
            auto found = index.find(need.itemId);
            if (found == index.end())
            {
                found = index.emplace(need.itemId, totals.size()).first;
                totals.push_back(ReagentTotals{ need.itemId, 0, 0, 0 });
            }
            totals[found->second].*field = need.count;
        }
    };
    auto walk = [&recipeFor, &bags, &mail, itemId, count](bool useBags, bool useMail)
    {
        auto held = [&bags, &mail, useBags, useMail](uint32_t heldId) -> uint32_t
        {
            uint64_t total = (useBags ? uint64_t(bags(heldId)) : 0) +
                (useMail ? uint64_t(mail(heldId)) : 0);
            return total > UINT32_MAX ? UINT32_MAX : uint32_t(total);
        };
        detail::ReagentWalker<RecipeFor, decltype(held)> walker(recipeFor, held);
        walker.Walk(itemId, count, 0, false);
        return walker.Take();
    };
    absorb(walk(false, false), &ReagentTotals::required);
    absorb(walk(true, false), &ReagentTotals::bagShort);
    absorb(walk(true, true), &ReagentTotals::extShort);
    return totals;
}

} // namespace GuildOrders
} // namespace TortoiseBots
