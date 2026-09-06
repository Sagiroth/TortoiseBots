#include "BotCommandContext.h"

#include "../host/BotSessionAdapter.h"
#include "../runtime/PlayerbotAIStorage.h"
#include "../ai/playerbot/PlayerbotAI.h"
#include "../ai/playerbot/strategy/Action.h"
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
#include <set>

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
    (void)suppliedRecord;
    // BotManager is the single readiness gate. A live Player and an Active
    // Headless state are not enough unless the adapter also registered a
    // usable PlayerbotAI for that exact object.
    return BotManager::Instance().IsControllableBot(bot);
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
    // This helper is reached only from a human-facing module command. Give
    // that command one full AI decision even if the bot's cached population
    // activity verdict says it is currently inactive.
    ai->DoNextAction(minimal, true);
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

bool ConfigurePullMode(PlayerbotAI* ai, bool pullback)
{
    if (!ai)
        return false;

    ai->ChangeStrategy((pullback ? "+" : "-") + std::string("pull back"),
        BotState::BOT_STATE_ALL);
    return pullback
        ? (ai->HasStrategy("pull back", BotState::BOT_STATE_COMBAT) ||
            ai->HasStrategy("pull back", BotState::BOT_STATE_NON_COMBAT))
        : (!ai->HasStrategy("pull back", BotState::BOT_STATE_COMBAT) &&
            !ai->HasStrategy("pull back", BotState::BOT_STATE_NON_COMBAT));
}

namespace {

// These are the Vanilla/Turtle interrupt actions already registered by the
// nine class contexts (or by a warlock's pet context). We deliberately probe
// the action graph and spell data instead of encoding class/spec assumptions:
// talent changes, Turtle spell ranks, and pet choice remain AI-owned.
char const* const kInterruptActions[] = {
    "counterspell",
    "silence",
    "spell lock",
    "kick",
    "pummel",
    "shield bash",
    "bash",
    "hammer of justice",
    "repentance",
    "earth shock",
    "death coil",
};

std::string FindInterruptAction(PlayerbotAI* ai, Unit* target)
{
    if (!ai || !target || !target->IsInWorld() || !target->IsAlive() ||
        !target->IsNonMeleeSpellCasted(true))
    {
        return {};
    }

    for (char const* action : kInterruptActions)
    {
        // IsInterruptableSpellCasting validates that the target is casting and
        // that this actual spell has an interrupt/silence effect. CanCastSpell
        // then filters out missing, cooling-down, stance, resource, and target
        // legality failures. Range is intentionally ignored here: the command
        // can queue the mature reach action below when the executor is distant.
        if (ai->HasSpell(action) &&
            ai->IsInterruptableSpellCasting(target, action, true) &&
            ai->CanCastSpell(action, target, 0, nullptr, true, true, true))
        {
            return action;
        }
    }

    return {};
}

} // namespace

Player* ResolveInterruptExecutor(BotCommandContext const& context, Unit* target,
    std::string* outAction)
{
    if (!target || !target->IsInWorld() || !target->IsAlive() ||
        !target->IsNonMeleeSpellCasted(true))
    {
        return nullptr;
    }

    auto tryBot = [&](Player* bot) -> Player*
    {
        if (!IsLiveHeadlessBot(bot) || bot->GetMap() != target->GetMap())
            return nullptr;

        PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(bot);
        std::string action = FindInterruptAction(ai, target);
        if (action.empty())
            return nullptr;

        if (outAction)
            *outAction = action;
        return bot;
    };

    if (context.selectedBot)
        return tryBot(context.selectedBot);

    for (Player* bot : context.partyBots)
    {
        if (Player* executor = tryBot(bot))
            return executor;
    }

    return nullptr;
}

namespace {

static std::string FindCcAction(PlayerbotAI* ai, Unit* target, std::string const& mark)
{
    if (!ai || !target || !target->IsInWorld() || !target->IsAlive())
        return {};

    ai::AiObjectContext* context = ai->GetAiObjectContext();
    if (!context)
        return {};

    ai::Value<std::string>* markValue = context->GetValue<std::string>("rti cc");
    if (!markValue)
        return {};

    // CcTargetValue reads the bot's rti cc preference. Temporarily querying
    // with the requested mark lets the mature action graph answer capability
    // without a duplicate class/spell policy table.
    std::string previousMark = markValue->Get();
    markValue->Set(mark);

    std::set<std::string> actionNames;
    context->GetSupportedActions(actionNames);
    for (std::string const& actionName : actionNames)
    {
        ai::Action* action = context->GetAction(actionName);
        if (!action || !action->IsCrowdControlAction())
            continue;

        std::string spell = action->GetCrowdControlSpellName();
        if (spell.empty() || !ai->HasSpell(spell))
            continue;

        bool possible = action->isPossible();
        bool spellLegal = ai->CanCastSpell(spell, target, 0, nullptr, true, true, true);
        if (!possible && !spellLegal)
            continue;

        markValue->Set(previousMark);
        return actionName;
    }

    markValue->Set(previousMark);
    return {};
}

} // namespace

Player* ResolveCcExecutor(BotCommandContext const& context, Unit* target, std::string const& mark,
    std::string* outAction)
{
    if (!target)
        return nullptr;

    if (context.selectedBot && IsLiveHeadlessBot(context.selectedBot))
    {
        PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(context.selectedBot);
        if (ai)
        {
            std::string action = FindCcAction(ai, target, mark);
            if (!action.empty())
            {
                if (outAction) *outAction = action;
                return context.selectedBot;
            }
        }
        return nullptr;
    }

    for (Player* bot : context.partyBots)
    {
        if (!IsLiveHeadlessBot(bot))
            continue;
        PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(bot);
        if (!ai)
            continue;
        std::string action = FindCcAction(ai, target, mark);
        if (!action.empty())
        {
            if (outAction) *outAction = action;
            return bot;
        }
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
