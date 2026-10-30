#include "RandomBotAccountRegistry.h"

#include "runtime/policy/infra/PoolResetPolicy.h"
#include "../host/ModuleLog.h"
#include "../ai/playerbot/PlayerbotAIConfig.h"

#include "AccountMgr.h"
#include "Database/DatabaseEnv.h"
#include "Log.h"

#include <algorithm>
#include <cstdio>

namespace TortoiseBots
{

namespace
{
// A confirmation challenge is short-lived on purpose: the operator is expected
// to run preview and confirm back to back.
constexpr time_t kAdoptionChallengeLifetimeSec = 300;

char const* SourceName(RegistrationSource source)
{
    switch (source)
    {
        case RegistrationSource::Hire:   return "hire";
        case RegistrationSource::Adopted: return "adopted";
        case RegistrationSource::AutoCreate:
        default:                          return "auto-create";
    }
}
} // namespace

char const* RegistrationSourceName(RegistrationSource source)
{
    return SourceName(source);
}

RandomBotAccountRegistry& RandomBotAccountRegistry::Instance()
{
    static RandomBotAccountRegistry instance;
    return instance;
}

void RandomBotAccountRegistry::Clear()
{
    m_accounts.clear();
    m_accountIds.clear();
    m_ids.clear();
    m_validated = false;
    m_lastError.clear();
    m_pendingAdoption = PendingAdoption();
}

bool RandomBotAccountRegistry::QueryCount(char const* sql, uint32_t& out) const
{
    std::unique_ptr<QueryResult> result(CharacterDatabase.PQuery(sql));
    if (!result)
        return false;
    out = result->Fetch()[0].GetUInt32();
    return true;
}

bool RandomBotAccountRegistry::LoadAccountsFromDatabase(std::vector<PoolAccount>& out, std::string& error) const
{
    uint32_t total = 0;
    if (!QueryCount("SELECT COUNT(*) FROM `tortoise_bots_pool_account`", total))
    {
        error = "managed-account registry table could not be read";
        return false;
    }
    if (!total)
        return true;

    std::unique_ptr<QueryResult> rows(CharacterDatabase.PQuery(
        "SELECT `account_id`, `username_at_registration`, `registration_source` "
        "FROM `tortoise_bots_pool_account` ORDER BY `account_id`"));
    if (!rows)
    {
        error = "managed-account registry rows could not be read";
        return false;
    }

    do
    {
        Field* fields = rows->Fetch();
        PoolAccount account;
        account.accountId = fields[0].GetUInt32();
        account.username = fields[1].GetCppString();
        std::string source = fields[2].GetCppString();
        if (source == "hire")
            account.source = RegistrationSource::Hire;
        else if (source == "adopted")
            account.source = RegistrationSource::Adopted;
        else
            account.source = RegistrationSource::AutoCreate;

        if (!account.accountId || account.username.empty())
        {
            error = "managed-account registry holds an invalid row (empty account id or username)";
            return false;
        }
        out.push_back(std::move(account));
    } while (rows->NextRow());

    return true;
}

bool RandomBotAccountRegistry::ValidateAgainstLoginDatabase(PoolAccount const& account, std::string& error) const
{
    std::unique_ptr<QueryResult> row(LoginDatabase.PQuery(
        "SELECT `username` FROM `account` WHERE `id` = '%u'", account.accountId));
    if (!row)
    {
        error = "registered pool account " + std::to_string(account.accountId) +
            " is missing from the login database (or the login database did not answer)";
        return false;
    }

    std::string stored = row->Fetch()[0].GetCppString();
    if (!AccountUsernameEquals(stored, account.username))
    {
        error = "registered pool account " + std::to_string(account.accountId) +
            " was renamed (registered as '" + NormalizeAccountUsername(account.username) +
            "', login database now says '" + NormalizeAccountUsername(stored) + "')";
        return false;
    }
    return true;
}

RegistryLoadResult RandomBotAccountRegistry::LoadValidatedAccounts()
{
    m_accounts.clear();
    m_accountIds.clear();
    m_ids.clear();
    m_validated = false;
    m_lastError.clear();

    std::vector<PoolAccount> loaded;
    std::string error;
    if (!LoadAccountsFromDatabase(loaded, error))
    {
        m_lastError = error;
        sLog.outError("TortoiseBots: managed-account registry load failed: %s", error.c_str());
        return RegistryLoadResult::Failed;
    }

    for (PoolAccount const& account : loaded)
    {
        if (!ValidateAgainstLoginDatabase(account, error))
        {
            m_lastError = error;
            m_accounts.clear();
            m_accountIds.clear();
            m_ids.clear();
            sLog.outError("TortoiseBots: managed-account registry validation failed: %s", error.c_str());
            return RegistryLoadResult::Failed;
        }
        m_ids.insert(account.accountId);
        m_accountIds.push_back(account.accountId);
        m_accounts.push_back(account);
    }

    m_validated = true;
    if (m_accounts.empty())
        return RegistryLoadResult::Empty;
    return RegistryLoadResult::Loaded;
}

std::string RandomBotAccountRegistry::AccountIdList() const
{
    std::string list;
    for (size_t i = 0; i < m_accountIds.size(); ++i)
    {
        if (i)
            list += ",";
        list += std::to_string(m_accountIds[i]);
    }
    return list;
}

bool RandomBotAccountRegistry::CountManagedCharacters(uint32_t& out, std::string& error) const
{
    out = 0;
    if (!m_validated)
    {
        // An unvalidated registry has no account scope at all: reporting zero
        // would claim the pool is empty when the truth is unknown.
        error = m_lastError.empty() ? "the managed-account registry is not validated" : m_lastError;
        return false;
    }
    if (m_accountIds.empty())
        return true;

    std::string accounts = AccountIdList();
    std::unique_ptr<QueryResult> count(CharacterDatabase.PQuery(
        "SELECT COUNT(*) FROM `characters` WHERE `deleteDate` IS NULL AND `account` IN (%s) "
        "AND `guid` NOT IN (SELECT `bot_guid` FROM `tortoise_bots_claimed`)", accounts.c_str()));
    if (!count)
    {
        error = "managed character count could not be read";
        return false;
    }
    out = count->Fetch()[0].GetUInt32();
    return true;
}

RegisterResult RandomBotAccountRegistry::RegisterCreatedAccount(uint32_t accountId, std::string const& username,
    RegistrationSource source)
{
    if (!accountId || username.empty())
        return RegisterResult::InvalidInput;

    std::string normalized = NormalizeAccountUsername(username);
    if (normalized.empty())
        return RegisterResult::InvalidInput;

    // Read the current row first: re-registering the same account is idempotent
    // and must not be reported as a database error.
    std::unique_ptr<QueryResult> existing(CharacterDatabase.PQuery(
        "SELECT `username_at_registration` FROM `tortoise_bots_pool_account` WHERE `account_id` = '%u'",
        accountId));
    if (existing)
    {
        std::string registered = existing->Fetch()[0].GetCppString();
        if (AccountUsernameEquals(registered, normalized))
        {
            if (!IsRegistered(accountId))
            {
                // The process cache is behind the table (fresh row from another
                // path); keep them consistent without rewriting the row.
                m_ids.insert(accountId);
                m_accountIds.push_back(accountId);
                PoolAccount account;
                account.accountId = accountId;
                account.username = registered;
                account.source = source;
                m_accounts.push_back(account);
            }
            return RegisterResult::AlreadyRegistered;
        }
        sLog.outError("TortoiseBots: pool account %u is registered as '%s' but was used as '%s'; refusing to re-register",
            accountId, NormalizeAccountUsername(registered).c_str(), normalized.c_str());
        return RegisterResult::DatabaseError;
    }

    // Direct (synchronous) execute: the character database queues plain
    // PExecute writes on its async worker, and the read-back below must not
    // race that queue. Registration is a single row and never in a hot loop.
    if (!CharacterDatabase.DirectPExecute(
            "INSERT INTO `tortoise_bots_pool_account` "
            "(`account_id`, `username_at_registration`, `registration_source`) "
            "VALUES ('%u', '%s', '%s')",
            accountId, normalized.c_str(), SourceName(source)))
    {
        sLog.outError("TortoiseBots: could not register pool account %u ('%s')", accountId, normalized.c_str());
        return RegisterResult::DatabaseError;
    }

    // Read the row back before the caller creates anything on the account: a
    // registry entry that cannot be read is not a registry entry.
    std::unique_ptr<QueryResult> written(CharacterDatabase.PQuery(
        "SELECT `username_at_registration` FROM `tortoise_bots_pool_account` WHERE `account_id` = '%u'",
        accountId));
    if (!written)
    {
        sLog.outError("TortoiseBots: pool account %u ('%s') was written but could not be read back",
            accountId, normalized.c_str());
        return RegisterResult::DatabaseError;
    }
    std::string stored = written->Fetch()[0].GetCppString();
    if (!AccountUsernameEquals(stored, normalized))
    {
        sLog.outError("TortoiseBots: pool account %u read back as '%s' instead of '%s'",
            accountId, NormalizeAccountUsername(stored).c_str(), normalized.c_str());
        return RegisterResult::DatabaseError;
    }

    m_ids.insert(accountId);
    m_accountIds.push_back(accountId);
    PoolAccount account;
    account.accountId = accountId;
    account.username = stored;
    account.source = source;
    m_accounts.push_back(account);
    TB_LOG_DETAIL("TortoiseBots: registered pool account %s (%u) as %s",
        stored.c_str(), accountId, SourceName(source));
    return RegisterResult::Success;
}

bool RandomBotAccountRegistry::DiscoverPrefixMatches(std::vector<LegacyAccount>& out, std::string& error) const
{
    out.clear();

    std::string prefix = sPlayerbotAIConfig.randomBotAccountPrefix;
    if (prefix.empty())
        prefix = "RNDBOT";

    // The prefix is matched in code, not by a SQL LIKE: the configured value is
    // never interpolated into a pattern, and no wildcard in it can widen the
    // result set.
    std::unique_ptr<QueryResult> probe(LoginDatabase.PQuery("SELECT COUNT(*) FROM `account`"));
    uint32_t accountTotal = 0;
    if (probe)
        accountTotal = probe->Fetch()[0].GetUInt32();
    switch (VerdictForAccountScanProbe(probe != nullptr, accountTotal))
    {
        case AccountScanVerdict::Failed:
            error = "the login database did not answer the account scan";
            return false;
        case AccountScanVerdict::Empty:
            return true;
        case AccountScanVerdict::Populate:
            break;
    }

    std::unique_ptr<QueryResult> rows(LoginDatabase.PQuery("SELECT `id`, `username` FROM `account` ORDER BY `id`"));
    if (!rows)
    {
        error = "the login database did not answer the account scan";
        return false;
    }

    do
    {
        Field* fields = rows->Fetch();
        LegacyAccount account;
        account.accountId = fields[0].GetUInt32();
        account.username = fields[1].GetCppString();
        if (!account.accountId || !AccountUsernameHasPrefix(account.username, prefix))
            continue;
        out.push_back(std::move(account));
    } while (rows->NextRow());

    for (LegacyAccount& account : out)
    {
        std::unique_ptr<QueryResult> count(CharacterDatabase.PQuery(
            "SELECT COUNT(*) FROM `characters` WHERE `account` = '%u' AND `deleteDate` IS NULL",
            account.accountId));
        if (!count)
        {
            // An unknown character count is not zero characters: the preview
            // would understate what adoption enrolls.
            error = "the character count for account " + NormalizeAccountUsername(account.username) +
                " (" + std::to_string(account.accountId) + ") could not be read";
            out.clear();
            return false;
        }
        account.characterCount = count->Fetch()[0].GetUInt32();
        account.alreadyRegistered = IsRegistered(account.accountId);
    }

    return true;
}

std::vector<std::string> RandomBotAccountRegistry::UnregisteredPrefixMatches()
{
    std::vector<std::string> result;
    std::string error;
    std::vector<LegacyAccount> matches;
    if (!DiscoverPrefixMatches(matches, error))
    {
        sLog.outError("TortoiseBots: could not list prefix-matching accounts: %s", error.c_str());
        return result;
    }
    for (LegacyAccount const& account : matches)
        if (!account.alreadyRegistered)
            result.push_back(account.username);
    return result;
}

RandomBotAccountRegistry::AdoptionPreview RandomBotAccountRegistry::PreviewLegacyAdoption()
{
    AdoptionPreview preview;
    preview.prefix = sPlayerbotAIConfig.randomBotAccountPrefix;
    if (preview.prefix.empty())
        preview.prefix = "RNDBOT";

    // Any failure clears a previously pending challenge: a stale challenge must
    // never confirm a set the operator has not just reviewed.
    m_pendingAdoption = PendingAdoption();

    std::string error;
    if (!DiscoverPrefixMatches(preview.accounts, error))
    {
        preview.error = error;
        return preview;
    }

    std::vector<std::pair<uint32_t, std::string>> identity;
    identity.reserve(preview.accounts.size());
    for (LegacyAccount const& account : preview.accounts)
    {
        identity.emplace_back(account.accountId, account.username);
        preview.totalCharacters += account.characterCount;
        if (!account.alreadyRegistered)
            ++preview.pendingAccounts;
    }

    if (!identity.empty())
    {
        m_pendingAdoption.valid = true;
        m_pendingAdoption.accounts = identity;
        m_pendingAdoption.challenge = AdoptionChallenge(identity);
        m_pendingAdoption.expiresAt = time(nullptr) + kAdoptionChallengeLifetimeSec;
        preview.challenge = m_pendingAdoption.challenge;
    }

    preview.ok = true;
    return preview;
}

RandomBotAccountRegistry::AdoptionConfirm RandomBotAccountRegistry::ConfirmLegacyAdoption(std::string const& challenge, time_t now)
{
    AdoptionConfirm confirm;

    if (!m_pendingAdoption.valid)
    {
        confirm.error = "no adoption preview is pending; run 'bot pool adopt preview' first";
        return confirm;
    }
    if (now > m_pendingAdoption.expiresAt)
    {
        m_pendingAdoption = PendingAdoption();
        confirm.error = "the adoption challenge expired; run 'bot pool adopt preview' again";
        return confirm;
    }
    if (!AdoptionChallengeMatches(m_pendingAdoption.challenge, challenge))
    {
        confirm.error = "the adoption challenge does not match the pending preview";
        return confirm;
    }

    std::string error;
    std::vector<LegacyAccount> rediscovered;
    if (!DiscoverPrefixMatches(rediscovered, error))
    {
        confirm.error = error;
        return confirm;
    }

    std::vector<std::pair<uint32_t, std::string>> identity;
    identity.reserve(rediscovered.size());
    for (LegacyAccount const& account : rediscovered)
        identity.emplace_back(account.accountId, account.username);

    if (identity.size() != m_pendingAdoption.accounts.size())
    {
        confirm.error = "the matching account set changed since the preview; run 'bot pool adopt preview' again";
        return confirm;
    }
    for (size_t i = 0; i < identity.size(); ++i)
    {
        if (identity[i].first != m_pendingAdoption.accounts[i].first ||
            !AccountUsernameEquals(identity[i].second, m_pendingAdoption.accounts[i].second))
        {
            confirm.error = "the matching account set changed since the preview; run 'bot pool adopt preview' again";
            return confirm;
        }
    }

    for (LegacyAccount const& account : rediscovered)
    {
        if (account.alreadyRegistered)
        {
            ++confirm.alreadyRegistered;
            continue;
        }

        RegisterResult result = RegisterCreatedAccount(account.accountId, account.username, RegistrationSource::Adopted);
        if (result == RegisterResult::Success)
        {
            ++confirm.adopted;
            continue;
        }
        if (result == RegisterResult::AlreadyRegistered)
        {
            ++confirm.alreadyRegistered;
            continue;
        }

        confirm.error = "could not register account " + NormalizeAccountUsername(account.username) +
            " (" + std::to_string(account.accountId) + "); no characters were changed";
        return confirm;
    }

    m_pendingAdoption = PendingAdoption();
    confirm.ok = true;
    return confirm;
}

} // namespace TortoiseBots
