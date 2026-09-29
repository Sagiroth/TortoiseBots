#pragma once

// Hard safety rule for deleting a dismissed hired companion: a hire is
// temporary and its character is wiped when the hire ends, but deleting a
// character is irreversible, so the decision is a pure function of durable
// facts and is unit-tested on its own (tools/test_hire_deletion_policy.cpp).
//
// Facts, all revalidated immediately before the deletion:
//  - hireLedgerRow: a row in tortoise_bots_hire. It is written exactly once
//    per hire, by HireLifecycle::Claim(), for a character the module itself
//    created. A player's character never gets one, and neither does a pool
//    character the module did not create through Hire().
//  - accountIsRegisteredPool: the character's account is listed in
//    tortoise_bots_pool_account, the managed pool registry. A player account
//    is never registered there.
//  - accountMatchesLedger: the character database still reports the account
//    the ledger recorded. A character that moved accounts in between is never
//    deleted.
//
// Anything else is refused and logged: the character stays.

namespace TortoiseBots
{

enum class HireDeletionDecision
{
    Allowed,
    RefusedNoHireLedger,      // not created by Hire(): a player character or an untouched pool bot
    RefusedAccountNotManaged, // account is not a registered managed pool account
    RefusedAccountMismatch,   // characters.account differs from the ledger row
};

inline HireDeletionDecision DecideHireDeletion(bool hireLedgerRow, bool accountIsRegisteredPool,
    bool accountMatchesLedger)
{
    if (!hireLedgerRow)
        return HireDeletionDecision::RefusedNoHireLedger;
    if (!accountIsRegisteredPool)
        return HireDeletionDecision::RefusedAccountNotManaged;
    if (!accountMatchesLedger)
        return HireDeletionDecision::RefusedAccountMismatch;
    return HireDeletionDecision::Allowed;
}

inline char const* HireDeletionDecisionName(HireDeletionDecision decision)
{
    switch (decision)
    {
        case HireDeletionDecision::Allowed: return "allowed";
        case HireDeletionDecision::RefusedNoHireLedger: return "no hire ledger row";
        case HireDeletionDecision::RefusedAccountNotManaged: return "account is not a managed pool account";
        case HireDeletionDecision::RefusedAccountMismatch: return "account differs from the ledger row";
    }
    return "unknown";
}

} // namespace TortoiseBots
