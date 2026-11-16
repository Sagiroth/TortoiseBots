#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_set>
#include <vector>

namespace TortoiseBots
{

// Dedicated offline seller account and per-faction character identities for the
// AH bot. Characters are created only when Update() runs, never log in, and
// are excluded from all random-pool, hire, alt-bot, and cleanup paths.
class AhBotSellerAccount
{
public:
    static AhBotSellerAccount& Instance();

    void Update(uint32_t diff);              // ensure account + sellers exist, repair if deleted; cheap when healthy
    bool IsReady() const;                    // account + >=1 seller per faction exist
    uint32_t PickSellerGuid(uint32_t auctionHouseId) const; // 1 -> Alliance seller, 6 -> Horde, 7 -> any; 0 if none
    uint32_t AccountId() const;              // 0 until created
    bool IsSellerGuid(uint32_t guidLow) const;
    bool IsSellerAccount(uint32_t accountId) const;
    std::vector<uint32_t> SellerGuids() const;
    uint32_t PurgeSellerMail(uint32_t maxMails); // delete up to N mails (+ attached item_instance rows) to sellers
    std::string GetStatus() const;

private:
    AhBotSellerAccount() = default;
    ~AhBotSellerAccount() = default;

    void EnsureLoadedLocked() const;
    void RunRepairCheck();
    bool CreateSellerCharacter(uint32_t accountId, uint8_t factionIndex, uint32_t& outGuid);

    mutable std::mutex m_mutex;
    mutable bool m_dbLoaded = false;
    mutable uint32_t m_accountId = 0;
    mutable std::string m_accountName;
    mutable std::vector<uint32_t> m_allianceGuids;
    mutable std::vector<uint32_t> m_hordeGuids;
    mutable std::unordered_set<uint32_t> m_sellerGuidSet;
    mutable std::unordered_set<uint32_t> m_knownSellerAccounts;

    bool m_initialCheckDone = false;
    bool m_passwordRotatedThisRun = false;
    bool m_invalidNameLogged = false;
    bool m_pvpTeamsViolationLogged = false;
    bool m_charLimitLogged = false;
    bool m_pendingAccountResolve = false;
    std::string m_pendingAccountName;
    uint32_t m_pendingRetryMs = 0;
    uint32_t m_pendingElapsedMs = 0;
    uint32_t m_repairElapsedMs = 0;
    uint32_t m_backoffMs = 0;
};

} // namespace TortoiseBots
