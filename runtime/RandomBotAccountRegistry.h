#pragma once

#include "PoolResetPolicy.h"

#include <cstdint>
#include <ctime>
#include <string>
#include <unordered_set>
#include <vector>

namespace TortoiseBots
{

// Why an account entered the registry. Audit information only: no behaviour
// branches on the source.
enum class RegistrationSource
{
    AutoCreate,
    Hire,
    Adopted,
};

char const* RegistrationSourceName(RegistrationSource source);

enum class RegisterResult
{
    Success,           // row written and read back
    AlreadyRegistered, // idempotent: same account, same username
    InvalidInput,      // empty id/username
    DatabaseError,     // write failed, read-back failed, or conflicting row
};

enum class RegistryLoadResult
{
    Loaded,  // at least one validated account
    Empty,   // registry table read fine and holds no rows
    Failed,  // a query failed or an account no longer validates
};

struct PoolAccount
{
    uint32_t accountId = 0;
    std::string username;                    // as stored at registration
    RegistrationSource source = RegistrationSource::AutoCreate;
};

// The authoritative list of login accounts the module owns as random-pool
// infrastructure (issue #265). Every random-pool operation crosses this seam:
// candidate discovery, hire account selection, auto-create targets, reset
// target selection, and `IsInRandomAccountList`.
//
// An account that is not registered here is never treated as module-owned, no
// matter how closely its username resembles the configured prefix.
class RandomBotAccountRegistry
{
public:
    static RandomBotAccountRegistry& Instance();

    // Startup: read the registry and validate every row against the login
    // database (account exists, username still matches). Never auto-enrolls
    // prefix-matching accounts; that requires explicit adoption.
    RegistryLoadResult LoadValidatedAccounts();

    // True when the last load completed (Loaded or Empty). Destructive callers
    // must refuse to run while this is false.
    bool IsValidated() const { return m_validated; }
    std::string const& LastError() const { return m_lastError; }

    // Record a module-created account before any character is created on it.
    // Writes the row and reads it back; the caller must not use the account
    // when this does not return Success or AlreadyRegistered.
    RegisterResult RegisterCreatedAccount(uint32_t accountId, std::string const& username, RegistrationSource source);

    bool IsRegistered(uint32_t accountId) const { return m_ids.count(accountId) != 0; }
    std::vector<PoolAccount> const& Accounts() const { return m_accounts; }
    std::vector<uint32_t> const& AccountIds() const { return m_accountIds; }
    size_t AccountCount() const { return m_accounts.size(); }

    // "1,2,3" for the registered account IDs, empty when none. Shared by the
    // reset and by reporting so the two can never build a different account
    // scope.
    std::string AccountIdList() const;

    // Live count of characters on the registered accounts. Returns false when
    // the count could not be read; callers must never treat that as zero.
    bool CountManagedCharacters(uint32_t& out, std::string& error) const;

    void Clear();

    // ---- Legacy adoption (server-console only; the caller checks permission) ----
    struct LegacyAccount
    {
        uint32_t accountId = 0;
        std::string username;
        uint32_t characterCount = 0;
        bool alreadyRegistered = false;
    };

    struct AdoptionPreview
    {
        bool ok = false;
        std::string error;
        std::string prefix;                     // the configured prefix that matched
        std::vector<LegacyAccount> accounts;    // every prefix match, registered or not
        uint32_t totalCharacters = 0;
        uint32_t pendingAccounts = 0;           // matches that still need adoption
        std::string challenge;
    };

    struct AdoptionConfirm
    {
        bool ok = false;
        std::string error;
        uint32_t adopted = 0;
        uint32_t alreadyRegistered = 0;
    };

    // Read-only discovery of every account whose username matches the
    // configured prefix. No writes.
    AdoptionPreview PreviewLegacyAdoption();

    // Registers the previewed accounts (no character is touched). Requires an
    // unexpired challenge and an identical rediscovered set.
    AdoptionConfirm ConfirmLegacyAdoption(std::string const& challenge, time_t now);

    // Accounts matching the configured prefix that are not registered yet.
    // Used for the startup warning; read-only.
    std::vector<std::string> UnregisteredPrefixMatches();

private:
    RandomBotAccountRegistry() = default;
    ~RandomBotAccountRegistry() = default;

    bool LoadAccountsFromDatabase(std::vector<PoolAccount>& out, std::string& error) const;
    bool ValidateAgainstLoginDatabase(PoolAccount const& account, std::string& error) const;
    // COUNT(*) cannot return an empty row set, so a null result is a real
    // query failure: this is how the registry tells "no accounts" apart from
    // "the database did not answer".
    bool QueryCount(char const* sql, uint32_t& out) const;
    // ok=false means discovery could not be completed; the match list is then
    // incomplete and must not be used or confirmed.
    bool DiscoverPrefixMatches(std::vector<LegacyAccount>& out, std::string& error) const;

    std::vector<PoolAccount> m_accounts;
    std::vector<uint32_t> m_accountIds;
    std::unordered_set<uint32_t> m_ids;
    bool m_validated = false;
    std::string m_lastError;

    struct PendingAdoption
    {
        bool valid = false;
        std::vector<std::pair<uint32_t, std::string>> accounts;
        std::string challenge;
        time_t expiresAt = 0;
    };
    PendingAdoption m_pendingAdoption;
};

} // namespace TortoiseBots
