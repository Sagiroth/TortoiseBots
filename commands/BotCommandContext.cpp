#include "BotCommandContext.h"

#include "../host/BotSessionAdapter.h"
#include "../runtime/PlayerbotAIStorage.h"
#include "../ai/playerbot/PlayerbotAI.h"
#include "../ai/playerbot/strategy/generic/PullStrategy.h"

// pi-lens-ignore: clang:pp_file_not_found
#include "ObjectAccessor.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "Player.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "WorldSession.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "Group/Group.h"

#include <algorithm>

namespace TortoiseBots {
namespace BotCommands {

bool IsBotAdministrator(Player* requester)
{
    return requester && requester->GetSession() &&
        requester->GetSession()->GetSecurity() >= SEC_GAMEMASTER;
}

bool CanControlBot(Player* requester, BotRecord const* record)
{
    if (!requester || !requester->GetSession() || !record)
        return false;

    if (IsBotAdministrator(requester))
        return true;

    uint32_t ownerAccount = record->ownerAccountId ? record->ownerAccountId : record->accountId;
    return ownerAccount != 0 && ownerAccount == requester->GetSession()->GetAccountId();
}

bool IsLiveHeadlessBot(Player* bot, BotRecord const* suppliedRecord)
{
    if (!bot || !bot->IsInWorld() || !bot->GetSession())
        return false;

    BotRecord* record = suppliedRecord
        ? const_cast<BotRecord*>(suppliedRecord)
        : BotManager::Instance().FindBot(bot->GetObjectGuid());
    if (!record || record->lifecycle == BotLifecycle::Removing)
        return false;

    WorldSession* session = bot->GetSession();
    if (!session->IsHeadless() || session->HasNetworkTransport())
        return false;

    // A player row can still say online for a human session or a stale crash.
    // Only the core-owned Headless state makes a module bot controllable.
    return BotSessionAdapter::GetHeadlessSessionState(record->characterGuid) ==
        HeadlessSessionState::Active;
}

namespace {

class ScopedSilentStrategy
{
public:
    explicit ScopedSilentStrategy(PlayerbotAI* ai) : ai_(ai)
    {
        if (!ai_ || ai_->HasStrategy("silent", BotState::BOT_STATE_NON_COMBAT))
            return;

        ai_->ChangeStrategy("+silent", BotState::BOT_STATE_NON_COMBAT);
        added_ = ai_->HasStrategy("silent", BotState::BOT_STATE_NON_COMBAT);
    }

    ~ScopedSilentStrategy()
    {
        if (added_)
            ai_->ChangeStrategy("-silent", BotState::BOT_STATE_NON_COMBAT);
    }

    bool IsActive() const { return added_ || (ai_ &&
        ai_->HasStrategy("silent", BotState::BOT_STATE_NON_COMBAT)); }

private:
    PlayerbotAI* ai_ = nullptr;
    bool added_ = false;
};

} // namespace

bool ExecuteQuietAction(PlayerbotAI* ai, std::string const& action, ai::Event const& event)
{
    ScopedSilentStrategy silent(ai);
    if (!silent.IsActive())
        return ai && ai->DoSpecificAction(action, ai::Event(event), true);
    return ai->DoSpecificAction(action, ai::Event(event), true);
}

void ExecuteQuietNextAction(PlayerbotAI* ai, bool minimal)
{
    if (!ai)
        return;
    ScopedSilentStrategy silent(ai);
    ai->DoNextAction(minimal);
}

BotCommandContext BuildContext(Player* requester)
{
    BotCommandContext context;
    context.requester = requester;
    if (!requester || !requester->GetSession() || !requester->IsInWorld())
        return context;

    context.group = requester->GetGroup();

    // Do not use ChatHandler::GetSelectedUnit() until SelectionGuid is checked:
    // the core intentionally returns the requester for an empty selection.
    ObjectGuid selectionGuid = requester->GetSelectionGuid();
    if (!selectionGuid.IsEmpty())
        context.requesterTarget = requester->GetSelectedUnit();

    if (context.requesterTarget && context.requesterTarget != requester)
    {
        Player* selectedPlayer = context.requesterTarget->ToPlayer();
        BotRecord* selectedRecord = selectedPlayer
            ? BotManager::Instance().FindBot(selectedPlayer->GetObjectGuid()) : nullptr;
        if (selectedPlayer && selectedRecord &&
            CanControlBot(requester, selectedRecord) &&
            IsLiveHeadlessBot(selectedPlayer, selectedRecord))
        {
            context.selectedBot = selectedPlayer;
        }

        // Actions must never be aimed at a module bot. A non-owned bot is also
        // intentionally not treated as an enemy target.
        if (!BotManager::Instance().IsBot(context.requesterTarget->GetObjectGuid()))
            context.enemyTarget = context.requesterTarget;
    }

    auto appendPartyBot = [&](Player* member)
    {
        if (!member || member == requester)
            return;
        BotRecord* record = BotManager::Instance().FindBot(member->GetObjectGuid());
        if (!record || !CanControlBot(requester, record) ||
            !IsLiveHeadlessBot(member, record))
            return;
        if (std::find(context.partyBots.begin(), context.partyBots.end(), member) == context.partyBots.end())
            context.partyBots.push_back(member);
    };

    if (context.group)
    {
        for (GroupReference* ref = context.group->GetFirstMember(); ref; ref = ref->next())
            appendPartyBot(ref->GetSource());
    }
    else
    {
        // Preserve the native convenience behavior for a just-added bot before
        // an invite: no group means the durable master binding is the party.
        for (Player* bot : BotManager::Instance().GetBotsForMaster(requester->GetObjectGuid()))
            appendPartyBot(bot);
    }

    return context;
}

std::vector<Player*> ResolveDynamicScope(BotCommandContext const& context)
{
    if (context.selectedBot)
        return { context.selectedBot };
    return context.partyBots;
}

namespace {

bool IsPullCandidate(Player* requester, Player* bot)
{
    if (!requester || !bot || !bot->IsInWorld() || !bot->IsAlive() ||
        bot->IsInCombat() || bot->GetMap() != requester->GetMap())
        return false;

    PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(bot);
    return ai && PlayerbotAI::IsTank(bot, true) && PullStrategy::Get(ai);
}

} // namespace

Player* ResolvePullExecutor(BotCommandContext const& context, bool allowSelected)
{
    if (!context.requester || !context.enemyTarget)
        return nullptr;

    if (allowSelected && context.selectedBot &&
        IsPullCandidate(context.requester, context.selectedBot))
        return context.selectedBot;

    for (Player* bot : context.partyBots)
    {
        if (IsPullCandidate(context.requester, bot))
            return bot;
    }

    return nullptr;
}

namespace {

bool IsCcCandidate(BotCommandContext const& context, Player* bot, Unit* target)
{
    if (!context.requester || !bot || !target || !IsLiveHeadlessBot(bot))
        return false;

    PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(bot);
    if (!ai || (!ai->HasStrategy("cc", BotState::BOT_STATE_COMBAT) &&
        !ai->HasStrategy("cc", BotState::BOT_STATE_NON_COMBAT)))
        return false;

    // The mature AI has no generic CC action. Setting the existing RTI-CC
    // value and checking its resolved target is the narrow capability seam;
    // class strategies still choose the actual spell.
    if (!ExecuteQuietAction(ai, "rti",
        ai::Event("cc moon", "cc moon", context.requester)))
        return false;

    ai::Value<Unit*>* ccTarget = ai->GetAiObjectContext()->GetValue<Unit*>("rti cc target");
    return ccTarget && ccTarget->Get() == target;
}

} // namespace

Player* ResolveCcExecutor(BotCommandContext const& context, Unit* target)
{
    if (!target)
        return nullptr;

    if (context.selectedBot)
        return IsCcCandidate(context, context.selectedBot, target) ? context.selectedBot : nullptr;

    for (Player* bot : context.partyBots)
    {
        if (IsCcCandidate(context, bot, target))
            return bot;
    }

    return nullptr;
}

std::string RosterState(Player* bot, BotRecord const* suppliedRecord)
{
    BotRecord* record = suppliedRecord
        ? const_cast<BotRecord*>(suppliedRecord)
        : (bot ? BotManager::Instance().FindBot(bot->GetObjectGuid()) : nullptr);
    if (!record)
        return "offline";

    HeadlessSessionState state = BotSessionAdapter::GetHeadlessSessionState(record->characterGuid);
    if (record->lifecycle == BotLifecycle::Removing)
        return "removing";
    if (bot && IsLiveHeadlessBot(bot, record) && state == HeadlessSessionState::Active)
        return "online";
    if (state == HeadlessSessionState::Pending || state == HeadlessSessionState::Loading ||
        record->lifecycle == BotLifecycle::PendingAdd)
        return "starting";
    return "offline";
}

} // namespace BotCommands
} // namespace TortoiseBots
