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
// pi-lens-ignore: clang:pp_file_not_found
#include "Creature.h"

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

static std::string GetBotCcSpell(PlayerbotAI* ai, Unit* target)
{
    if (!ai || !target)
        return "";

    Player* bot = ai->GetBot();
    if (!bot || !bot->IsInWorld() || !bot->IsAlive())
        return "";

    Creature* creature = target->ToCreature();
    CreatureInfo const* cInfo = creature ? creature->GetCreatureInfo() : nullptr;
    uint32 creatureType = cInfo ? cInfo->type : 0;

    uint8 cls = bot->GetClass();
    if (cls == CLASS_MAGE)
    {
        // Polymorph works on Beast, Humanoid, Critter
        if (!creature || creatureType == CREATURE_TYPE_BEAST ||
            creatureType == CREATURE_TYPE_HUMANOID ||
            creatureType == CREATURE_TYPE_CRITTER)
        {
            if (ai->CanCastSpell("polymorph", target, 0, nullptr, true, true, true))
                return "polymorph";
        }
    }
    else if (cls == CLASS_WARLOCK)
    {
        if (creature && (creatureType == CREATURE_TYPE_DEMON || creatureType == CREATURE_TYPE_ELEMENTAL))
        {
            if (ai->CanCastSpell("banish", target, 0, nullptr, true, true, true))
                return "banish";
        }
        if (ai->CanCastSpell("fear", target, 0, nullptr, true, true, true))
            return "fear";
    }
    else if (cls == CLASS_PRIEST)
    {
        if (creature && creatureType == CREATURE_TYPE_UNDEAD)
        {
            if (ai->CanCastSpell("shackle undead", target, 0, nullptr, true, true, true))
                return "shackle undead";
        }
    }
    else if (cls == CLASS_DRUID)
    {
        if (creature && (creatureType == CREATURE_TYPE_BEAST || creatureType == CREATURE_TYPE_DRAGONKIN))
        {
            if (ai->CanCastSpell("hibernate", target, 0, nullptr, true, true, true))
                return "hibernate";
        }
        if (ai->CanCastSpell("entangling roots", target, 0, nullptr, true, true, true))
            return "entangling roots";
    }
    else if (cls == CLASS_ROGUE)
    {
        if (creature && creatureType == CREATURE_TYPE_HUMANOID && !creature->IsInCombat())
        {
            if (ai->CanCastSpell("sap", target, 0, nullptr, true, true, true))
                return "sap";
        }
    }

    return "";
}

} // namespace

Player* ResolveCcExecutor(BotCommandContext const& context, Unit* target, std::string* outSpell)
{
    if (!target)
        return nullptr;

    if (context.selectedBot && IsLiveHeadlessBot(context.selectedBot))
    {
        PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(context.selectedBot);
        if (ai)
        {
            std::string spell = GetBotCcSpell(ai, target);
            if (!spell.empty())
            {
                if (outSpell) *outSpell = spell;
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
        std::string spell = GetBotCcSpell(ai, target);
        if (!spell.empty())
        {
            if (outSpell) *outSpell = spell;
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
