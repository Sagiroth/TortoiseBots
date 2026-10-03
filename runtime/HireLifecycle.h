#pragma once

// pi-lens-ignore: clang:pp_file_not_found
#include "ObjectGuid.h"

#include <cstdint>
#include <ctime>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class Player;
class Group;

namespace TortoiseBots
{

// Issue #192: hired-companion lifecycle. Hired companions are temporary:
// they live while their master is online plus the disconnect grace period,
// and when the hire ends the character is deleted instead of returning to the
// roaming pool. Tracks active hires, runs the master-disconnect grace timer,
// and drives the asynchronous deletion of dismissed companions. Driven from
// BotManager's world tick and the module GroupScript adapter; never blocks the
// tick.
class HireLifecycle
{
public:
    static HireLifecycle& Instance();

    // Mark a freshly hired bot and write its durable hire-ledger row. Called
    // once per Hire(); false means the ledger could not be written and the
    // caller must abort the hire without charging. A character with no ledger
    // row is never deleted (see runtime/HireDeletionPolicy.h).
    bool Claim(ObjectGuid botGuid, ObjectGuid masterGuid, uint32_t ownerAccountId,
        uint32_t characterAccountId);

    // End a hire now, without a grace period. `.bot remove`/`.bot logout` on a
    // companion and any other explicit release go through here; a bot that is
    // not a hire is left alone.
    void DismissNow(ObjectGuid botGuid, char const* reason);

    // Drop the lifecycle record without touching the character: the
    // managed-pool reset deletes the character (and the ledger row) itself.
    void Forget(ObjectGuid botGuid);

    bool IsHired(ObjectGuid botGuid) const;
    ObjectGuid GetMaster(ObjectGuid botGuid) const;

    // The hire was confirmed grouped with its master (called from both
    // Reunite paths). Until this is set the hire is still being provisioned
    // and the master-departure rule leaves it alone.
    void MarkGrouped(ObjectGuid botGuid);

    // Group hooks (called from the module GroupScript adapter).
    void OnGroupMemberRemoved(Group* group, ObjectGuid guid);
    void OnGroupDisband(Group* group);

    // Human master login/logout (called from the module PlayerScript adapter).
    void OnMasterLogin(Player* master);
    void OnMasterLogout(Player* master);

    void Update(uint32_t diff);

private:
    HireLifecycle() = default;

    struct HiredRecord
    {
        ObjectGuid botGuid;
        ObjectGuid masterGuid;
        uint32_t ownerAccountId = 0;
        // Master offline: bot guards in place until grace expires, then
        // dismisses. Zero = master online (or never seen offline).
        time_t masterOfflineSince = 0;
        bool greeted = false;
        // Issue #382: the spec+spells intro goes out once the hire is
        // confirmed grouped. Hires grouped by the provisioner announce there;
        // hires grouped here (master-offline grace path) announce below.
        bool introSent = false;
        // Set once the hire was seen sharing its master's group (issue #378).
        // A departure before that is a provisioning artefact, never an end of
        // hire: only then is a master-less group a dismissal.
        bool everGrouped = false;
    };

    // A dismissed hire whose character is waiting for its asynchronous
    // deletion. The durable state lives in tortoise_bots_hire ('dismissed');
    // this queue only tracks the in-flight work.
    struct PendingDeletion
    {
        uint32_t guidLow = 0;
        std::string reason;
        time_t queuedAt = 0;
        // Set once the bounded retry escalated to an explicit Headless stop
        // request, so a stuck session is not spammed and is abandoned instead.
        bool stopRequested = false;
    };

    void Dismiss(HiredRecord const& record, char const* reason, bool removeFromGroup = true);
    bool MasterOnline(HiredRecord const& record) const;
    // Issue #378: the record's master is online but no longer shares the
    // hire's group, so the hire is over. Pure-rule wrapper (HireDeparturePolicy.h).
    bool MasterLeftGroup(HiredRecord const& record) const;
    void Reunite(HiredRecord& record, Player* master);
    void SweepGracePeriod(time_t now);

    // Durable hire ledger.
    bool LedgerRow(ObjectGuid botGuid, uint32_t& characterAccountId) const;
    // Guard, mark dismissed, and queue the character for deletion.
    void QueueDeletion(uint32_t guidLow, uint32_t ledgerAccountId, char const* reason);
    // First ticks after startup: every ledger row is a stale hire (no hire
    // survives a restart), so it is recovered and deleted.
    bool RecoverStaleHires();
    void SweepDeletions(time_t now);
    bool ProcessDeletion(PendingDeletion& entry, time_t now);

    std::unordered_map<uint32_t, HiredRecord> m_hired;
    std::unordered_set<uint32_t> m_dismissing;
    std::vector<PendingDeletion> m_pendingDeletions;
    bool m_ledgerRecovered = false;
    time_t m_recoveryRetryAt = 0;
    uint32_t m_updateElapsedMs = 0;
    uint32_t m_restockElapsedMs = 0;
};

} // namespace TortoiseBots
