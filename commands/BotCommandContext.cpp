#include "BotCommandContext.h"

#include "../host/BotSessionAdapter.h"
#include "../runtime/PlayerbotAIStorage.h"
#include "../runtime/ClaimLifecycle.h"
#include "../ai/playerbot/PlayerbotAI.h"
#include "../ai/playerbot/ServerFacade.h"
#include "../ai/playerbot/strategy/Engine.h"
#include "../ai/playerbot/strategy/Action.h"
#include "../ai/playerbot/strategy/AiObjectContext.h"
#include "../ai/playerbot/strategy/generic/PullStrategy.h"
#include "../ai/playerbot/strategy/values/PositionValue.h"
#include "../ai/playerbot/WorldPosition.h"
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

    if (record->masterGuid && record->masterGuid == requester->GetObjectGuid())
        return true;

    uint32_t ownerAccount = record->ownerAccountId ? record->ownerAccountId : record->accountId;
    if (ownerAccount != 0 && ownerAccount == requester->GetSession()->GetAccountId())
        return true;

    // Issue #489: Claimed guild bot owned by this player
    if (ClaimLifecycle::Instance().IsClaimedByPlayer(record->characterGuid.GetCounter(), requester->GetGUIDLow()))
        return true;

    return false;
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

bool ExecuteOrderedAction(PlayerbotAI* ai, std::string const& action, ai::Event const& event)
{
    if (!ai || !ai->GetAiObjectContext())
        return false;
    ai::Action* ordered = ai->GetAiObjectContext()->GetAction(action);
    if (!ordered)
        return false;
    ScopedSilentStrategy silent(ai);
    ai::Event copy(event);
    return ordered->Execute(copy);
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

bool QueueMatureAction(PlayerbotAI* ai, std::string const& action,
    ai::Event const& event, float relevance)
{
    if (!ai || !ai->GetCurrentEngine())
        return false;

    // If the bot is already in core combat, keep the queued action on the
    // combat engine. This avoids PlayerbotAI's stale non-combat target cleanup
    // from clearing the command target before the interrupt/CC prerequisite
    // gets a turn, without sending a melee attack or inventing combat state.
    if (ai->GetBot() && sServerFacade.IsInCombat(ai->GetBot()))
        ai->OnCombatStarted();

    return ai->GetCurrentEngine()->QueueAction(action, relevance, event);
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
        // Iterate the Group's own member slots, not the GroupReference list.
        // The slots carry only ObjectGuids and are owned by the Group; a
        // Player* from the reference list can outlive the Player object during
        // a bot's headless relogin window, and build-time checks on that stale
        // pointer read freed memory (SIGSEGV in GetAI, issue #225). Resolve
        // every member through ObjectAccessor so only live players enter the
        // command scope - the same contract BotManager::GetBotsForMaster uses.
        for (Group::MemberSlot const& slot : context.group->GetMemberSlots())
            appendPartyBot(ObjectAccessor::FindPlayer(slot.guid));
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

// Player Agency First: a pull candidate is (1) the explicitly selected bot,
// (2) a player-designated tank (forced role), or (3) a native tank spec. Never
// guess DPS/cloth bots. The forced-role check must precede the
// ContainsStrategy probe: a leveling Arms warrior designated as tank has the
// STRATEGY_TYPE_TANK bit only after the role kit is applied, and a native
// Protection bot without the mature pull policy still needs it attached.
bool IsPullCandidate(Player* requester, Player* bot)
{
    if (!requester || !bot || !bot->IsInWorld() || !bot->IsAlive() ||
        bot->IsInCombat() || bot->GetMap() != requester->GetMap())
        return false;

    PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(bot);
    if (!ai)
        return false;

    if (ai->GetForcedRole() == static_cast<uint8>(BOT_ROLE_TANK) ||
        PlayerbotAI::IsTank(bot, true))
    {
        if (!PullStrategy::Get(ai))
        {
            ai->ChangeStrategy("+pull", BotState::BOT_STATE_NON_COMBAT);
            ai->ChangeStrategy("+pull", BotState::BOT_STATE_COMBAT);
        }
        return PullStrategy::Get(ai) != nullptr;
    }

    return false;
}

} // namespace

Player* ResolvePullExecutor(BotCommandContext const& context, bool allowSelected)
{
    if (!context.requester || !context.enemyTarget)
        return nullptr;

    // Ordered precedence: explicit selection, designated tank, native spec.
    if (allowSelected && context.selectedBot &&
        IsPullCandidate(context.requester, context.selectedBot))
        return context.selectedBot;

    for (Player* bot : context.partyBots)
    {
        PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(bot);
        if (ai && ai->GetForcedRole() == static_cast<uint8>(BOT_ROLE_TANK) &&
            IsPullCandidate(context.requester, bot))
            return bot;
    }

    for (Player* bot : context.partyBots)
    {
        if (IsPullCandidate(context.requester, bot))
            return bot;
    }

    return nullptr;
}

bool PullTankIsBusy(BotCommandContext const& context)
{
    if (!context.requester)
        return false;

    auto busyTank = [&](Player* bot)
    {
        if (!bot || !bot->IsInWorld() || !bot->IsAlive() || !bot->IsInCombat() ||
            bot->GetMap() != context.requester->GetMap())
            return false;
        PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(bot);
        return ai && (ai->GetForcedRole() == static_cast<uint8>(BOT_ROLE_TANK) ||
            PlayerbotAI::IsTank(bot, true));
    };

    if (busyTank(context.selectedBot))
        return true;
    for (Player* bot : context.partyBots)
        if (busyTank(bot))
            return true;
    return false;
}

// Threat window: while the tank pulls and establishes threat, party DPS bots
// hold at the command anchor. Healers stay active. Uses the mature
// "wait for attack" gate the rotation already honors (AttackAction skips
// bot->Attack and the multiplier zeroes combat actions), plus an explicit
// stay hold so the allowed "keep safe distance" action cannot drift the bots
// toward the mob. The stay is released together with the wait window, or
// earlier by combat end, death or a movement order; the window itself only
// ends on its own clock or with its strategy (attack, flee, co -wait for
// attack; see PullJoinWindowPolicy.h).
// joinDelaySeconds is clamped to the uint8 value range (0-60 at the parser).
// For a pullback the DPS must additionally wait out the tank's return leg:
// the window is widened to cover the capped return plus the join delay, and
// re-stamped to just the join delay when the tank arrives (see the arrival
// brake). A plain pull keeps exactly the join delay from the landing.
void PausePartyDpsForPull(BotCommandContext const& context, Player* executor, uint32 joinDelaySeconds, bool pullback)
{
    if (!context.requester)
        return;

    uint32 windowSeconds = joinDelaySeconds;
    if (pullback)
        windowSeconds = std::min<uint32>(255, sPlayerbotAIConfig.pullBackMaxReturnTime + joinDelaySeconds);
    WorldPosition anchor(context.requester);
    for (Player* bot : context.partyBots)
    {
        if (!bot || bot == executor)
            continue;
        PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(bot);
        // A healer is held only in the run leader's pulls: it stands on its
        // stand and keeps healing (Uldaman run 4: the one bot not put on stay
        // followed the tank to the camp, wanded and died).
        bool const healer = ai && PlayerbotAI::IsHeal(bot, true);
        if (!ai || (healer && !context.runLeader))
            continue;
        if (ai->GetForcedRole() == static_cast<uint8>(BOT_ROLE_TANK) ||
            PlayerbotAI::IsTank(bot, true))
            continue;
        WorldPosition stand = anchor;
        if (healer)
        {
            // Heals only, from behind the tank (PlayerbotAI::RunHealOnly; the
            // run leader's sync keeps it set for the run).
            ai->SetRunHealOnly(true);
            float hx = 0.0f, hy = 0.0f, hz = 0.0f;
            if (context.healerStand && context.healerStand(bot, hx, hy, hz))
                stand = WorldPosition(anchor.GetMapId(), hx, hy, hz);
        }
        // Hold this bot at the anchor: stay owns the movement mode, and the
        // anchor copy lets the release step distinguish our hold from a
        // player-placed stay (only ours is cleared).
        ai->SetMovementStrategy("stay");
        ai::PositionMap& posMap = ai->GetAiObjectContext()->GetValue<ai::PositionMap&>("position")->Get();
        ai::PositionEntry stayPos = posMap["stay"];
        stayPos.Set(stand.getX(), stand.getY(), stand.getZ(), stand.GetMapId());
        posMap["stay"] = stayPos;
        ai::PositionEntry holdPos = posMap["pull hold"];
        holdPos.Set(stand.getX(), stand.getY(), stand.getZ(), stand.GetMapId());
        posMap["pull hold"] = holdPos;
        // "wait for attack" with this pull's delay, on its own clock from here
        // (PullJoinWindowPolicy.h). A "wait for attack" or a delay the player
        // set is theirs and comes back unchanged when the window ends; one an
        // earlier pull left (a chain pull) stays the pull's.
        ai->StartPullWindow(static_cast<uint8>(windowSeconds));
        // Fresh combat window: a stale combat-start timestamp would expire the
        // hold immediately, so reset it for the incoming pull engagement.
        // Re-stamped when the pull lands (plain pull) or when the tank
        // arrives at the anchor (pullback), so the join delay runs from the
        // event, not from the command.
        ai->GetAiObjectContext()->GetValue<time_t>("combat start time")->Set(time(0));
    }
}

// Per-command return mode: the `pull back` strategy stays the mature switch,
// but the command snapshots the tank's default and records the per-request
// mode on the PullStrategy. PullEndAction restores the default, so a pull
// never leaks into the next command and a pullback never sticks.
bool ConfigurePullMode(PlayerbotAI* ai, bool pullback, uint32 joinDelaySeconds, bool rangedOrder)
{
    if (!ai)
        return false;

    bool hadPullBack = ai->HasStrategy("pull back", BotState::BOT_STATE_COMBAT) ||
        ai->HasStrategy("pull back", BotState::BOT_STATE_NON_COMBAT);
    ai->ChangeStrategy((pullback ? "+" : "-") + std::string("pull back"),
        BotState::BOT_STATE_ALL);
    bool applied = pullback
        ? (ai->HasStrategy("pull back", BotState::BOT_STATE_COMBAT) ||
            ai->HasStrategy("pull back", BotState::BOT_STATE_NON_COMBAT))
        : (!ai->HasStrategy("pull back", BotState::BOT_STATE_COMBAT) &&
            !ai->HasStrategy("pull back", BotState::BOT_STATE_NON_COMBAT));
    if (applied)
    {
        if (PullStrategy* strategy = PullStrategy::Get(ai))
            strategy->BeginCommand(pullback, hadPullBack, joinDelaySeconds, rangedOrder);
    }
    return applied;
}

namespace {

// These are the Vanilla/Tortoise interrupt actions already registered by the
// nine class contexts (or by a warlock's pet context). We deliberately probe
// the action graph and spell data instead of encoding class/spec assumptions:
// talent changes, Tortoise spell ranks, and pet choice remain AI-owned.
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
        !target->IsNonMeleeSpellCasted(true) ||
        sServerFacade.getDistance2d(ai->GetBot(), target) > sPlayerbotAIConfig.sightDistance)
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
        if (ai->GetAiObjectContext()->GetAction(action) &&
            ai->HasSpell(action) &&
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

// Issue #58: preference among CC spells the mature action graph already
// proved usable. This ranks fit, it never grants capability. Lower is better:
// Sap for an unengaged target, type-specific holds next, Fear last because a
// feared mob runs into other packs.
static uint32 CcSpellPreference(std::string const& spell)
{
    static char const* const order[] = { "sap", "shackle undead", "banish", "hibernate",
        "polymorph", "freezing trap", "turn undead", "scare beast", "entangling roots", "fear" };
    for (uint32 i = 0; i < sizeof(order) / sizeof(order[0]); ++i)
        if (spell == order[i])
            return i;
    return sizeof(order) / sizeof(order[0]);
}

static std::string FindCcAction(PlayerbotAI* ai, Unit* target, std::string const& mark,
    uint32* outPreference, CcDenyReason* outDeny, std::string* outDenySpell)
{
    if (!ai || !target || !target->IsInWorld() || !target->IsAlive())
        return {};

    if (sServerFacade.getDistance2d(ai->GetBot(), target) > sPlayerbotAIConfig.sightDistance)
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

    auto resetProbeValues = [&](std::string const& spell)
    {
        if (ai::Value<Unit*>* rtiTarget = context->GetValue<Unit*>("rti cc target"))
            rtiTarget->Reset();
        if (!spell.empty())
        {
            if (ai::Value<Unit*>* ccTarget = context->GetValue<Unit*>("cc target", spell))
                ccTarget->Reset();
        }
    };

    // Setting a manual value does not invalidate calculated dependents in
    // this older value graph. Reset the shared mark target before probing and
    // remember every spell-qualified CC value touched by the probe so none of
    // the temporary mark's result can be reused after restoration.
    resetProbeValues(std::string());

    std::set<std::string> actionNames;
    context->GetSupportedActions(actionNames);
    std::set<std::string> probedSpells;
    std::string selectedAction;
    uint32 selectedPreference = 0;
    bool anyCcSpellKnown = false;
    bool anyOutdoorsOnly = false;
    std::string outdoorsSpell;
    bool anyCreatureMismatch = false;
    for (std::string const& actionName : actionNames)
    {
        ai::Action* action = context->GetAction(actionName);
        if (!action || !action->IsCrowdControlAction())
            continue;

        std::string spell = action->GetCrowdControlSpellName();
        if (spell.empty() || !ai->HasSpell(spell))
            continue;
        anyCcSpellKnown = true;

        // Durable misses first: isPossible() only knows the spell is ready,
        // and CanCastSpell reports whatever check fails first, so an
        // outdoors-only Roots indoors or Hibernate on a humanoid slipped
        // through as castable.
        if (SpellEntry const* info = sServerFacade.LookupSpellInfo(context->GetValue<uint32>("spell id", spell)->Get()))
        {
            Player* bot = ai->GetBot();
            if ((info->Attributes & SPELL_ATTR_OUTDOORS_ONLY) &&
                !bot->GetMap()->GetTerrain()->IsOutdoors(bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ()))
            {
                anyOutdoorsOnly = true;
                outdoorsSpell = spell;
                continue;
            }
            if (info->TargetCreatureType && !(info->TargetCreatureType & target->GetCreatureTypeMask()))
            {
                anyCreatureMismatch = true;
                continue;
            }
        }

        uint32 preference = CcSpellPreference(spell);
        if (!selectedAction.empty() && preference >= selectedPreference)
            continue;

        // Sap only opens a pull; an engaged target is never a Sap target.
        if (spell == "sap" && target->IsInCombat())
            continue;

        probedSpells.insert(spell);
        resetProbeValues(spell);

        if (!action->isUseful())
            continue;

        bool possible = action->isPossible();
        SpellCastResult castResult = SPELL_FAILED_DONT_REPORT;
        bool spellLegal = ai->CanCastSpell(spell, target, 0, nullptr, true, true, true, &castResult);
        if (!possible && !spellLegal)
        {
            // Durable reasons only: a cooldown, a missing reagent, a move
            // in progress all clear on their own. Outdoors-only and a
            // creature-type mismatch never clear for this target here.
            if (castResult == SPELL_FAILED_ONLY_OUTDOORS)
            {
                anyOutdoorsOnly = true;
                outdoorsSpell = spell;
            }
            else if (castResult == SPELL_FAILED_BAD_TARGETS || castResult == SPELL_FAILED_TARGET_IS_PLAYER ||
                castResult == SPELL_FAILED_IMMUNE)
                anyCreatureMismatch = true;
            continue;
        }

        selectedAction = actionName;
        selectedPreference = preference;
    }

    markValue->Set(previousMark);
    resetProbeValues(std::string());
    for (std::string const& spell : probedSpells)
        resetProbeValues(spell);

    if (outPreference)
        *outPreference = selectedPreference;
    if (!selectedAction.empty())
    {
        if (outDeny)
            *outDeny = CcDenyReason::None;
        return selectedAction;
    }
    // Rank the durable failure for the caller's message: a spell the bot
    // knows but can never cast here beats a spell it never learned.
    if (outDeny)
        *outDeny = !anyCcSpellKnown ? CcDenyReason::NoSpellKnown
            : anyOutdoorsOnly ? CcDenyReason::OutdoorsOnly
            : anyCreatureMismatch ? CcDenyReason::WrongCreatureType : CcDenyReason::Transient;
    if (outDenySpell)
        *outDenySpell = outdoorsSpell;
    return selectedAction;
}

} // namespace

Player* ResolveCcExecutor(BotCommandContext const& context, Unit* target, std::string const& mark,
    std::string* outAction)
{
    return ResolveCcExecutor(context, target, mark, outAction, nullptr, nullptr);
}

Player* ResolveCcExecutor(BotCommandContext const& context, Unit* target, std::string const& mark,
    std::string* outAction, CcDenyReason* outDeny, std::string* outDenySpell)
{
    if (outDeny)
        *outDeny = CcDenyReason::None;
    if (outDenySpell)
        outDenySpell->clear();
    if (!target)
        return nullptr;

    if (context.selectedBot && IsLiveHeadlessBot(context.selectedBot))
    {
        PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(context.selectedBot);
        if (ai)
        {
            std::string action = FindCcAction(ai, target, mark, nullptr, outDeny, outDenySpell);
            if (!action.empty())
            {
                if (outAction) *outAction = action;
                return context.selectedBot;
            }
        }
        return nullptr;
    }
    // Best spell fit wins; ties go to the lowest GUID so the same target and
    // state pick the same executor regardless of party invite order. Bots
    // with a durable failure still report it, so the caller can refuse with
    // a reason instead of a bare no-cc.
    Player* best = nullptr;
    std::string bestAction;
    uint32 bestPreference = 0;
    CcDenyReason bestDeny = CcDenyReason::Transient;
    std::string bestDenySpell;
    for (Player* bot : context.partyBots)
    {
        if (!IsLiveHeadlessBot(bot))
            continue;
        PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(bot);
        if (!ai)
            continue;
        uint32 preference = 0;
        CcDenyReason deny = CcDenyReason::None;
        std::string denySpell;
        std::string action = FindCcAction(ai, target, mark, &preference, &deny, &denySpell);
        if (action.empty())
        {
            // First durable failure wins the message; transient ones stay
            // silent unless nothing durable was found.
            if (bestDeny == CcDenyReason::Transient && deny != CcDenyReason::None &&
                deny != CcDenyReason::Transient)
            {
                bestDeny = deny;
                bestDenySpell = denySpell;
            }
            continue;
        }
        if (!best || preference < bestPreference ||
            (preference == bestPreference && bot->GetObjectGuid() < best->GetObjectGuid()))
        {
            best = bot;
            bestAction = action;
            bestPreference = preference;
        }
    }

    if (best && outAction)
        *outAction = bestAction;
    if (!best)
    {
        if (outDeny)
            *outDeny = bestDeny == CcDenyReason::Transient && bestDenySpell.empty()
                ? CcDenyReason::Transient : bestDeny;
        if (outDenySpell)
            *outDenySpell = bestDenySpell;
    }
    else if (outDeny)
        *outDeny = CcDenyReason::None;
    return best;
}

std::string CcDenyText(CcDenyReason reason, std::string const& spell, std::string const& targetName)
{
    std::string const target = targetName.empty() ? "this target" : targetName;
    switch (reason)
    {
        case CcDenyReason::OutdoorsOnly:
            return "cannot CC " + target + ": " + spell + " only works outdoors.";
        case CcDenyReason::WrongCreatureType:
            return "cannot CC " + target + ": no known CC spell fits its creature type here.";
        case CcDenyReason::NoSpellKnown:
            return "cannot CC " + target + ": no CC spell known.";
        case CcDenyReason::Transient:
        case CcDenyReason::None:
            break;
    }
    return {};
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
