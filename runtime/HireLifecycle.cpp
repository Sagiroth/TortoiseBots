// pi-lens-ignore-file: clang:pp_file_not_found,clang:unknown_typename,clang:use_of_undeclared_identifier,clang:unknown_type_name,clang:undeclared_var_use,clang:incomplete_member_access
#include "HireLifecycle.h"
#include "BotManager.h"
#include "BotActivityLease.h"
#include "CharacterCleanup.h"
#include "HireDeletionPolicy.h"
#include "HireDeparturePolicy.h"
#include "PlayerbotAIStorage.h"
#include "RandomBotAccountRegistry.h"
#include "RandomBotPoolReset.h"
#include "../host/BotSessionAdapter.h"
#include "../host/ModuleLog.h"
#include "../ai/playerbot/PlayerbotAI.h"
#include "../ai/playerbot/PlayerbotAIConfig.h"
#include "../ai/playerbot/PlayerbotFactory.h"
#include "../ai/playerbot/strategy/Event.h"

#include "ObjectAccessor.h"
#include "Player.h"
#include "WorldSession.h"
#include "Chat.h"
#include "Group/Group.h"
#include "Database/DatabaseEnv.h"
#include "Log.h"

#include <cstddef>
#include <ctime>
#include <memory>

namespace TortoiseBots
{

namespace
{
// A dismissed hire's Headless session is expected to close on the next ticks.
// One that refuses is escalated to an explicit stop request, and then
// abandoned: the character is never deleted while it may still be live, and
// its 'dismissed' ledger row makes the next server start recover it.
constexpr time_t kDeletionForceStopAfterSec = 30;
constexpr time_t kDeletionAbandonAfterSec = 120;
} // namespace

HireLifecycle& HireLifecycle::Instance()
{
    static HireLifecycle instance;
    return instance;
}

bool HireLifecycle::Claim(ObjectGuid botGuid, ObjectGuid masterGuid, uint32_t ownerAccountId,
    uint32_t characterAccountId)
{
    if (botGuid.IsEmpty() || !characterAccountId)
        return false;

    // Durable hire ledger first: it is written exactly once per hire and is the
    // only proof that this character was created by Hire(). Deleting a hired
    // character is refused without it (HireDeletionPolicy). A failed write
    // aborts the hire before the fee is charged.
    uint32_t botGuidLow = botGuid.GetCounter();
    if (!CharacterDatabase.DirectPExecute(
            "INSERT INTO `tortoise_bots_hire` "
            "(`character_guid`, `character_account_id`, `owner_account_id`, `state`) "
            "VALUES ('%u', '%u', '%u', 'active')",
            botGuidLow, characterAccountId, ownerAccountId))
    {
        sLog.outError("TortoiseBots: hire ledger write failed for character %u on account %u",
            botGuidLow, characterAccountId);
        return false;
    }

    HiredRecord record;
    record.botGuid = botGuid;
    record.masterGuid = masterGuid;
    record.ownerAccountId = ownerAccountId;
    record.masterOfflineSince = 0;
    record.greeted = false;
    m_hired[botGuidLow] = record;
    return true;
}

void HireLifecycle::Forget(ObjectGuid botGuid)
{
    m_hired.erase(botGuid.GetCounter());
}

void HireLifecycle::DismissNow(ObjectGuid botGuid, char const* reason)
{
    auto it = m_hired.find(botGuid.GetCounter());
    if (it == m_hired.end())
        return; // an owned alt or an untracked bot: nothing here may touch it
    HiredRecord record = it->second;
    // The bot is being logged off anyway, so no group surgery is needed.
    Dismiss(record, reason ? reason : "released", false);
}

bool HireLifecycle::IsHired(ObjectGuid botGuid) const
{
    return m_hired.find(botGuid.GetCounter()) != m_hired.end();
}

ObjectGuid HireLifecycle::GetMaster(ObjectGuid botGuid) const
{
    auto it = m_hired.find(botGuid.GetCounter());
    if (it == m_hired.end())
        return ObjectGuid();
    return it->second.masterGuid;
}

void HireLifecycle::MarkGrouped(ObjectGuid botGuid)
{
    auto it = m_hired.find(botGuid.GetCounter());
    if (it != m_hired.end())
        it->second.everGrouped = true;
}

bool HireLifecycle::MasterOnline(HiredRecord const& record) const
{
    Player* master = sObjectAccessor.FindPlayer(record.masterGuid);
    return master && master->IsInWorld() && master->GetSession() &&
        !master->GetSession()->IsHeadless();
}

bool HireLifecycle::MasterLeftGroup(HiredRecord const& record) const
{
    Player* master = sObjectAccessor.FindPlayer(record.masterGuid);
    Player* bot = sObjectAccessor.FindPlayer(record.botGuid);
    // A logged-out master can still be in world for a moment; the recorded
    // logout grace keeps that hire on the grace path instead.
    bool masterOnline = MasterOnline(record) && !record.masterOfflineSince;
    // A hire that is not live (mid-login, a stale session) counts as still
    // grouped: the runtime-record watchdog owns that case, not this rule.
    // Issue #387: raid membership, not subgroup: a hire in another raid
    // subgroup still shares the master's group.
    bool inSameGroup = !bot || !bot->IsInWorld() || !master || bot->IsInSameRaidWith(master);
    return ShouldDismissHireOnGroupDeparture(true, masterOnline, record.everGrouped, inSameGroup);
}

void HireLifecycle::Dismiss(HiredRecord const& record, char const* reason, bool removeFromGroup)
{
    uint32_t botGuidLow = record.botGuid.GetCounter();
    if (!m_dismissing.insert(botGuidLow).second)
        return;

    // Issue #243: Erase from active hired registry up-front so that any subsequent
    // group callbacks (e.g. Group::Disband triggered by RemoveMember) do not see this bot as an active hire.
    m_hired.erase(botGuidLow);

    Player* bot = sObjectAccessor.FindPlayer(record.botGuid);
    if (removeFromGroup && bot && bot->GetGroup())
        bot->GetGroup()->RemoveMember(record.botGuid, 0);

    // The hire is over: the companion is not a roaming bot and must not carry
    // master-level gear back into the organic world. Drop every master binding
    // now, mark the ledger, and log the bot off; the character is deleted from
    // the database as soon as its Headless session is gone (a live character
    // cannot be deleted).
    BotManager::Instance().ClearBotMaster(record.botGuid);
    BotActivityLeaseManager::Instance().ReleaseMaster(botGuidLow);
    uint32_t characterAccountId = 0;
    if (LedgerRow(record.botGuid, characterAccountId))
        QueueDeletion(botGuidLow, characterAccountId, reason ? reason : "released");
    else
        sLog.outError("TortoiseBots: hired bot %s has no hire ledger row; its character is left in place",
            record.botGuid.GetString().c_str());

    TB_LOG_BASIC("TortoiseBots: hired bot %s dismissed (%s); logging off for deletion",
        record.botGuid.GetString().c_str(), reason ? reason : "released");
    BotManager::Instance().RemoveBot(record.botGuid, false);
    m_dismissing.erase(botGuidLow);
}

void HireLifecycle::OnGroupMemberRemoved(Group* group, ObjectGuid guid)
{
    if (!group || guid.IsEmpty())
        return;

    // The removed member is itself a hire: the hire ends. Kicked or left by
    // choice; master-offline grace is only for disconnects (master object
    // gone), never for explicit removals. Core has already removed the member
    // from the group before this hook.
    if (auto it = m_hired.find(guid.GetCounter()); it != m_hired.end())
    {
        HiredRecord record = it->second;
        Dismiss(record, "removed from group", false);
        return;
    }

    // Issue #378: the removed member is the master of one or more hires that
    // are members of this group. In a group of more than two the group
    // survives the master's departure, so nothing else would end the hire: the
    // master stays online, the disconnect grace clock never starts, and the
    // companions trail a master who is no longer in their party forever. Only
    // the hire registry is iterated — a pool bot or an alt in the same party
    // is never a hire and never reaches Dismiss. A logged-out master is left
    // to the grace timer (MasterLeftGroup gates on the recorded logout).
    std::vector<HiredRecord> departing;
    for (auto const& kv : m_hired)
    {
        HiredRecord const& record = kv.second;
        if (record.masterGuid == guid && group->IsMember(record.botGuid) && MasterLeftGroup(record))
            departing.push_back(record);
    }
    for (HiredRecord const& record : departing)
        Dismiss(record, "master left the group", false);
}

void HireLifecycle::OnGroupDisband(Group* group)
{
    if (!group)
        return;
    // Issue #243: The group is already being disbanded by the core, which will
    // clean up member slots and session state. Snapshot hired members first,
    // then dismiss without calling RemoveMember to prevent infinite Disband recursion.
    std::vector<HiredRecord> departing;
    for (auto const& kv : m_hired)
    {
        if (group->IsMember(kv.second.botGuid))
            departing.push_back(kv.second);
    }
    for (HiredRecord const& record : departing)
    {
        // Issue #378: a two-member party disbands when its master logs out,
        // but the disconnect grace period owns that hire; only a genuine
        // disband with an online master ends it here.
        if (record.masterOfflineSince)
            continue;
        Dismiss(record, "group disbanded", false);
    }
}

void HireLifecycle::OnMasterLogin(Player* master)
{
    if (!master || !master->GetSession() || master->GetSession()->IsHeadless())
        return;
    ObjectGuid masterGuid = master->GetObjectGuid();
    for (auto& kv : m_hired)
    {
        HiredRecord& record = kv.second;
        if (record.masterGuid != masterGuid)
            continue;
        record.masterOfflineSince = 0;
        Reunite(record, master);
    }
}

void HireLifecycle::OnMasterLogout(Player* master)
{
    if (!master)
        return;
    ObjectGuid masterGuid = master->GetObjectGuid();
    time_t now = time(nullptr);
    for (auto& kv : m_hired)
    {
        HiredRecord& record = kv.second;
        if (record.masterGuid != masterGuid)
            continue;
        if (!record.masterOfflineSince)
            record.masterOfflineSince = now;
        // Guard stance: hold position where the master vanished. The mature
        // "stay" shortcut anchors both strategies and the return position.
        if (Player* bot = sObjectAccessor.FindPlayer(record.botGuid))
        {
            if (PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(bot))
            {
                ai::Event stayEvent("stay", "", nullptr);
                ai->DoSpecificAction("stay chat shortcut", stayEvent, true);
            }
        }
        TB_LOG_BASIC("TortoiseBots: master %s offline; hired bot %s guards for %us",
            master->GetName(),
            record.botGuid.GetString().c_str(),
            sPlayerbotAIConfig.hireDisconnectGracePeriod);
    }
}

void HireLifecycle::Reunite(HiredRecord& record, Player* master)
{
    if (!master)
        return;
    Player* bot = sObjectAccessor.FindPlayer(record.botGuid);
    if (!bot || !BotManager::Instance().IsControllableBot(bot))
        return;
    if (!bot->IsInSameRaidWith(master))
    {
        Group* masterGroup = master->GetGroup();
        if (masterGroup && masterGroup->isBGGroup())
            masterGroup = master->GetOriginalGroup();
        if (masterGroup && !masterGroup->isRaidGroup() && masterGroup->GetMembersCount() > 4)
            masterGroup->ConvertToRaid();
        auto* previousInvite = bot->GetGroupInvite();
        WorldPacket packet;
        packet << bot->GetName() << uint32(0);
        master->GetSession()->HandleGroupInviteOpcode(packet);
        if (bot->GetGroupInvite() != previousInvite)
        {
            if (PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(bot))
            {
                ai::Event inviteEvent("group invite", "", master);
                ai->DoSpecificAction("accept invitation", inviteEvent, true);
            }
        }
    }
    if (bot->IsInSameRaidWith(master))
    {
        // Issue #378: the hire is confirmed grouped, so a later departure of
        // the master ends the hire instead of being treated as a provisioning
        // artefact.
        record.everGrouped = true;
        if (PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(bot))
        {
            ai::Event followEvent("follow", "", master);
            ai->DoSpecificAction("follow chat shortcut", followEvent, true);
        }
        // Issue #382: grace-path hires missed the provision window (master
        // offline), so they never got the spec+spells intro. Same once-guard
        // as the provision path; no-op when it already went out.
        HireProvisionService::AnnounceForGraceHire(bot, master, record.introSent);
        if (!record.greeted)
        {
            record.greeted = true;
            std::string text = std::string("Welcome back, ") + master->GetName() + ". We stand ready.";
            WorldPacket data;
            ChatHandler::BuildChatPacket(data, bot->GetGroup() && bot->GetGroup()->isRaidGroup() ? CHAT_MSG_RAID : CHAT_MSG_PARTY,
                text.c_str(), LANG_UNIVERSAL, CHAT_TAG_NONE, bot->GetObjectGuid(), bot->GetName());
            if (Group* group = bot->GetGroup())
                group->BroadcastPacket(&data, true);
        }
    }
}

void HireLifecycle::SweepGracePeriod(time_t now)
{
    uint32_t grace = sPlayerbotAIConfig.hireDisconnectGracePeriod;
    for (auto it = m_hired.begin(); it != m_hired.end();)
    {
        HiredRecord& record = it->second;
        // The runtime record is gone (a failed Headless login, a reclaim, a
        // removal the group hooks never saw): the hire is over either way, so
        // end it the same way every other dismissal ends — delete the
        // character. Bookkeeping is never dropped silently.
        if (!BotManager::Instance().FindBot(record.botGuid))
        {
            HiredRecord orphan = record;
            it = m_hired.erase(it);
            Dismiss(orphan, "runtime record gone");
            continue;
        }
        // Issue #378 backstop: a master that left the party in a way no group
        // hook reported (a raid removal before the module registered, a group
        // change the core handled elsewhere). Only a live hire whose master is
        // still online and that was once grouped reaches this: a hire still
        // being provisioned and an offline/logged-out master (grace period)
        // are excluded by MasterLeftGroup.
        if (MasterLeftGroup(record))
        {
            HiredRecord departing = record;
            it = m_hired.erase(it);
            Dismiss(departing, "master left the group");
            continue;
        }
        if (!record.masterOfflineSince)
        {
            // Passive disconnect detection: the master object vanished without
            // a logout hook (crash). Start the clock on first observation.
            Player* master = sObjectAccessor.FindPlayer(record.masterGuid);
            bool online = master && master->IsInWorld() && master->GetSession() &&
                !master->GetSession()->IsHeadless();
            if (!online && MasterOnline(record) == false)
            {
                // Only start the clock when the master is genuinely gone, not
                // while they are teleporting (no Player object for a tick).
                if (!master)
                    record.masterOfflineSince = now;
            }
            ++it;
            continue;
        }
        if (MasterOnline(record))
        {
            record.masterOfflineSince = 0;
            ++it;
            continue;
        }
        if (now - record.masterOfflineSince < static_cast<time_t>(grace))
        {
            ++it;
            continue;
        }
        HiredRecord expired = record;
        it = m_hired.erase(it);
        Dismiss(expired, "master grace expired");
    }
}

bool HireLifecycle::LedgerRow(ObjectGuid botGuid, uint32_t& characterAccountId) const
{
    std::unique_ptr<QueryResult> row(CharacterDatabase.PQuery(
        "SELECT `character_account_id` FROM `tortoise_bots_hire` WHERE `character_guid` = '%u'",
        botGuid.GetCounter()));
    if (!row)
        return false;
    characterAccountId = row->Fetch()[0].GetUInt32();
    return true;
}

void HireLifecycle::QueueDeletion(uint32_t guidLow, uint32_t ledgerAccountId, char const* reason)
{
    // Hard guard: the ledger row is the only proof that this character was
    // created by Hire(). Without it nothing is deleted, ever.
    if (!ledgerAccountId)
    {
        sLog.outError("TortoiseBots: refusing to delete character %u after '%s': no hire ledger row",
            guidLow, reason ? reason : "released");
        return;
    }
    for (PendingDeletion const& pending : m_pendingDeletions)
        if (pending.guidLow == guidLow)
            return;

    // Durable intent first: a crash between here and the deletion is resumed
    // from this row on the next server start.
    CharacterDatabase.DirectPExecute(
        "UPDATE `tortoise_bots_hire` SET `state` = 'dismissed', `dismissed_at` = NOW() "
        "WHERE `character_guid` = '%u'", guidLow);

    PendingDeletion pending;
    pending.guidLow = guidLow;
    pending.reason = reason ? reason : "released";
    pending.queuedAt = time(nullptr);
    m_pendingDeletions.push_back(pending);
    TB_LOG_BASIC("TortoiseBots: hired character %u scheduled for deletion (%s)",
        guidLow, pending.reason.c_str());
}

bool HireLifecycle::RecoverStaleHires()
{
    if (!RandomBotAccountRegistry::Instance().IsValidated())
    {
        sLog.outError("TortoiseBots: stale hires cannot be recovered: the managed-pool account registry is not validated");
        return false;
    }

    std::unique_ptr<QueryResult> rows(CharacterDatabase.PQuery(
        "SELECT `character_guid`, `character_account_id` FROM `tortoise_bots_hire`"));
    if (!rows)
    {
        // A null result may mean "no rows" or "the table could not be read";
        // COUNT(*) cannot return an empty row set, so it tells them apart.
        std::unique_ptr<QueryResult> count(CharacterDatabase.PQuery(
            "SELECT COUNT(*) FROM `tortoise_bots_hire`"));
        if (!count)
        {
            sLog.outError("TortoiseBots: hire ledger could not be read; stale hires are not recovered yet");
            return false;
        }
        return true; // empty ledger
    }

    uint32_t recovered = 0;
    do
    {
        Field* fields = rows->Fetch();
        uint32_t guidLow = fields[0].GetUInt32();
        uint32_t accountId = fields[1].GetUInt32();
        if (m_hired.find(guidLow) != m_hired.end())
            continue; // a live hire is never stale
        QueueDeletion(guidLow, accountId, "server restart");
        ++recovered;
    } while (rows->NextRow());

    if (recovered)
        TB_LOG_BASIC("TortoiseBots: %u stale hired companion(s) recovered for deletion after a restart", recovered);
    return true;
}

bool HireLifecycle::ProcessDeletion(PendingDeletion& entry, time_t now)
{
    // Snapshot the entry: deleting a character removes it from its group,
    // which re-enters the group hooks and can append to the queue (and
    // reallocate it) while this call is on the stack.
    uint32_t guidLow = entry.guidLow;
    std::string reason = entry.reason;
    time_t queuedAt = entry.queuedAt;

    ObjectGuid guid(HIGHGUID_PLAYER, guidLow);
    Player* player = sObjectAccessor.FindPlayer(guid);
    if (player && player->GetSession() && !player->GetSession()->IsHeadless())
    {
        // A network session owns the character now (an operator logging into a
        // managed account, for example): human sessions are never deleted.
        sLog.outError("TortoiseBots: refusing to delete dismissed hire %u: a network session owns it", guidLow);
        return true;
    }

    // A live character cannot be deleted. Ask once per pass for the logout
    // (RemoveBot is what drives BotManager's own removal state machine).
    if (BotRecord* record = BotManager::Instance().FindBot(guid))
    {
        if (record->lifecycle != BotLifecycle::Removing)
            BotManager::Instance().RemoveBot(guid, false);
    }
    if (player || BotManager::Instance().IsBot(guid) ||
        BotSessionAdapter::GetHeadlessSessionState(guid) != HeadlessSessionState::NotFound)
    {
        // Bounded retry: a session that refuses to close is escalated to an
        // explicit stop request once, and then abandoned instead of being
        // polled and logged forever. The character is never deleted while it
        // may still be live; its ledger row stays 'dismissed', so the next
        // server start recovers and deletes it.
        time_t waited = now - queuedAt;
        if (waited >= kDeletionForceStopAfterSec && !entry.stopRequested)
        {
            entry.stopRequested = true;
            sLog.outError("TortoiseBots: dismissed hire %u is still online after %llds; forcing the Headless session stop",
                guidLow, static_cast<long long>(waited));
            BotSessionAdapter::StopHeadlessSession(guid, false);
        }
        else if (waited >= kDeletionAbandonAfterSec)
        {
            sLog.outError("TortoiseBots: dismissed hire %u never went offline; leaving the character in place "
                "(it is deleted on the next server start)", guidLow);
            return true;
        }
        return false;
    }

    // Offline: revalidate every durable fact before touching anything.
    uint32_t ledgerAccountId = 0;
    std::unique_ptr<QueryResult> ledger(CharacterDatabase.PQuery(
        "SELECT `character_account_id` FROM `tortoise_bots_hire` WHERE `character_guid` = '%u'", guidLow));
    bool hasLedger = ledger != nullptr;
    if (hasLedger)
        ledgerAccountId = ledger->Fetch()[0].GetUInt32();

    std::unique_ptr<QueryResult> character(CharacterDatabase.PQuery(
        "SELECT `account` FROM `characters` WHERE `guid` = '%u'", guidLow));
    bool characterExists = character != nullptr;
    uint32_t accountId = characterExists ? character->Fetch()[0].GetUInt32() : 0;

    if (!characterExists)
    {
        // Already gone (the managed-pool reset deletes pool characters too).
        // Only the ledger row is left to clear.
        CharacterDatabase.DirectPExecute("DELETE FROM `tortoise_bots_hire` WHERE `character_guid` = '%u'", guidLow);
        return true;
    }

    HireDeletionDecision decision = DecideHireDeletion(
        hasLedger,
        RandomBotAccountRegistry::Instance().IsRegistered(ledgerAccountId),
        accountId == ledgerAccountId);
    if (decision != HireDeletionDecision::Allowed)
    {
        sLog.outError("TortoiseBots: refusing to delete dismissed hire %u: %s",
            guidLow, HireDeletionDecisionName(decision));
        return true; // terminal: a refusal is never retried into a deletion
    }

    // Auctions are not part of the core character deletion; refund the bidders
    // and remove the listings first, or nothing is deleted at all.
    uint32_t settled = 0;
    std::string error;
    if (!SettleCharacterAuctions(guidLow, settled, error))
    {
        sLog.outError("TortoiseBots: dismissed hire %u could not be deleted: %s", guidLow, error.c_str());
        return false; // retry next tick
    }

    DeleteCharacterEverywhere(guidLow, accountId);
    TB_LOG_BASIC("TortoiseBots: hired character %u deleted (%s; %u auction(s) settled)",
        guidLow, reason.c_str(), settled);
    return true;
}

void HireLifecycle::SweepDeletions(time_t now)
{
    if (m_pendingDeletions.empty())
        return;
    // The managed-pool reset rebuilds whole accounts and settles every auction
    // while all pool bidders still exist; a concurrent deletion here could
    // remove a bidder mid-reset. It deletes pool characters (stale hires
    // included) itself, so this queue simply waits it out.
    if (RandomBotPoolReset::Instance().IsActive())
        return;

    // One character per world tick, the same bounded step the pool reset's
    // deletion phase uses: a core character wipe (auctions settled first) plus
    // the module rows. Entries that are still logged in are skipped so one
    // stalled session cannot starve the rest.
    for (size_t i = 0; i < m_pendingDeletions.size(); ++i)
    {
        if (!ProcessDeletion(m_pendingDeletions[i], now))
            continue;
        m_pendingDeletions.erase(m_pendingDeletions.begin() + static_cast<std::ptrdiff_t>(i));
        return;
    }
}

void HireLifecycle::Update(uint32_t diff)
{
    time_t now = time(nullptr);

    // The deletion queue is pumped on every world tick (one character per
    // pass), so a dismissed companion is gone within ticks - a kicked party of
    // four, or the stale hires recovered after a restart, drains in seconds
    // rather than one character per five.
    SweepDeletions(now);

    m_updateElapsedMs += diff;
    if (m_updateElapsedMs < 5000)
        return;
    m_updateElapsedMs = 0;

    // Issue #192 follow-up: a hire lives only while its master is online plus
    // the grace period, so no hire survives a restart. Every ledger row found
    // by a fresh process is stale and is recovered and deleted.
    if (!m_ledgerRecovered && (m_recoveryRetryAt == 0 || now >= m_recoveryRetryAt))
    {
        if (RecoverStaleHires())
            m_ledgerRecovered = true;
        else
            m_recoveryRetryAt = now + 60;
    }

    if (m_hired.empty())
        return;
    SweepGracePeriod(now);
    // Cheap periodic top-up for hired companions (no item cheat): class
    // reagents, food/drink, potions and level-tier bandages, each bounded
    // to a small stack by the factory. Hourly per bot, module-owned bots
    // only, never unbounded; tools/bags stay one-time seed.
    m_restockElapsedMs += 5000;
    if (m_restockElapsedMs < 3600000)
        return;
    m_restockElapsedMs = 0;
    for (auto const& kv : m_hired)
    {
        Player* bot = sObjectAccessor.FindPlayer(kv.second.botGuid);
        if (!bot || !bot->IsInWorld() || !BotManager::Instance().IsControllableBot(bot))
            continue;
        PlayerbotFactory factory(bot, bot->GetLevel());
        factory.RestockCompanion();
        bot->SaveToDB();
    }
}

} // namespace TortoiseBots
