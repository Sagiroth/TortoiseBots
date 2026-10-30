// pi-lens-ignore-file: all
#include "BotCommandHandlers.h"
#include "BotCommands.h"
#include "CommandShared.h"
#include "BotCommandContext.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "../runtime/BotManager.h"
#include "../runtime/BotActivityLease.h"
#include "../runtime/HireLifecycle.h"
#include "../runtime/ClaimLifecycle.h"
#include "../runtime/AltBotPolicy.h"
#include "../runtime/AltBotService.h"
#include "../runtime/HireProvisionService.h"
#include "../runtime/HireRaidPolicy.h"
#include "../runtime/HireSpecPolicy.h"
#include "../runtime/HireLevelPolicy.h"
#include "../runtime/HireTierRequestPolicy.h"
#include "BotCommandContext.h"
#include "../behavior/PlayerConvenience.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "../runtime/AhMarketService.h"
#include "../host/ModuleVersion.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "../runtime/PlayerbotAIStorage.h"
#include "../runtime/RandomBotAccountRegistry.h"
#include "../runtime/RandomBotPoolReset.h"
#include "../runtime/RandomBotService.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "../ai/playerbot/PlayerbotAI.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "../ai/playerbot/PlayerbotDbStore.h"
#include "../ai/playerbot/ChatHelper.h"
#include "../ai/playerbot/BotState.h"
#include "../ai/playerbot/PullOpeningPolicy.h"
#include "../ai/playerbot/PullStartPolicy.h"
#include "../ai/playerbot/PullRefusalPolicy.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "../ai/playerbot/strategy/generic/PullStrategy.h"
#include "../ai/playerbot/strategy/values/RtiTargetValue.h"
#include "../ai/playerbot/strategy/values/Stances.h"
#include "../ai/playerbot/strategy/values/AttackersValue.h"
#include "../ai/playerbot/strategy/values/PossibleAttackTargetsValue.h"
#include "../ai/playerbot/strategy/values/PositionValue.h"
#include "../ai/playerbot/strategy/values/ItemUsageValue.h"
#include "../ai/playerbot/strategy/actions/EquipAction.h"
#include "../ai/playerbot/strategy/actions/UnequipAction.h"
#include "../ai/playerbot/strategy/actions/SellAction.h"
#include "../ai/playerbot/policy/gear/SellRefusalPolicy.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "playerbot/PlayerbotFactory.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/SurvivePolicy.h"
#include "runtime/OwnedBotQolPolicy.h"
#include "../runtime/RemoteCommanderPolicy.h"
#include "../ai/playerbot/AiFactory.h"
#include "../runtime/RemoteCommanderView.h"
#include "../runtime/RemoteCommanderTasks.h"
#include "../runtime/RemoteCommanderEnter.h"
#include "../runtime/CommanderTelemetry.h"
#include "../ai/playerbot/LootObjectStack.h"
#include "../ai/playerbot/strategy/Engine.h"
#include "../ai/playerbot/strategy/Value.h"
#include "../ai/playerbot/GuidPosition.h"
#include "../host/ModuleLog.h"
#include "../ai/playerbot/strategy/shaman/ShamanTotemCommandPolicy.h"
#include "../ai/playerbot/strategy/shaman/ShamanTotemGroupPolicy.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "Chat.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "ObjectMgr.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "ObjectAccessor.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "Player.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "World.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "Group.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "WorldSession.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "WorldPacket.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "Log.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "Group/Group.h"
#include "Database/DatabaseEnv.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "Maps/PathFinder.h"
#include "Movement/spline/MoveSplineInit.h"
#include "Maps/Cell.h"
#include "Maps/Map.h"
#include "Database/DBCStores.h"
#include "Database/SQLStorages.h"
#include "Maps/CellImpl.h"
#include "Maps/GridNotifiers.h"
#include "Maps/GridNotifiersImpl.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "Creature.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "Spell.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "Threat/ThreatManager.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <initializer_list>
#include <memory>
#include <cstring>
#include <sstream>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace TortoiseBots {
namespace BotCommands {

// `.bot action flee` parks bots in passive follow; an explicit tactical order
// is the owner calling them back into the fight.
static void EndFlee(PlayerbotAI* ai)
{
    if (!ai)
        return;
    if (ai->HasStrategy("passive", BotState::BOT_STATE_NON_COMBAT))
        ai->ChangeStrategy("-passive", BotState::BOT_STATE_NON_COMBAT);
    if (ai->HasStrategy("passive", BotState::BOT_STATE_COMBAT))
        ai->ChangeStrategy("-passive", BotState::BOT_STATE_COMBAT);
}

static bool BindMovementMaster(Player* requester, Player* bot)
{
    if (!requester || !bot)
        return false;

    BotRecord* record = BotManager::Instance().FindBot(bot->GetObjectGuid());
    if (!record)
        return false;

    // Rebind even when the durable GUID already matches: the live AI pointer
    // may have been cleared by a reconnect, and FollowTargetValue caches the
    // old pointer until its next calculation.
    return BotManager::Instance().BindBotMaster(bot->GetObjectGuid(), requester->GetObjectGuid());
}

bool ExecuteInterruptAction(ChatHandler* handler, BotCommandContext const& context,
    Player* requester)
{
    Unit* target = context.enemyTarget;
    if (!target)
    {
        SendActionError(handler, "interrupt", "no-target", "Select a live enemy target first.");
        return true;
    }

    if (!target->IsNonMeleeSpellCasted(true))
    {
        SendActionError(handler, "interrupt", "not-casting", "The target is not casting an interruptible spell.");
        return true;
    }

    std::string actionName;
    Player* executor = ResolveInterruptExecutor(context, target, &actionName);
    if (!executor || actionName.empty())
    {
        SendActionError(handler, "interrupt", "no-interrupt",
            "No owned bot has a ready interrupt for this target.");
        return true;
    }

    PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(executor);
    if (!ai)
    {
        SendActionError(handler, "interrupt", "no-ai", "The selected interrupt bot has no AI.");
        return true;
    }

    // Interrupt is an executor command, so break a stale hold/follow mode while
    // leaving movement and combat decisions to the mature action graph.
    RelaxTacticalMovement(ai);
    executor->SetSelectionGuid(target->GetObjectGuid());
    ai->GetAiObjectContext()->GetValue<Unit*>("current target")->Set(target);

    ai::Event event("action interrupt", "", requester);
    bool accepted = ExecuteQuietAction(ai, actionName, event);
    if (!accepted)
    {
        if (!target->IsInWorld() || !target->IsAlive())
        {
            SendActionError(handler, "interrupt", "no-target",
                "The selected interrupt target is no longer available.");
            return true;
        }
        if (!target->IsNonMeleeSpellCasted(true))
        {
            SendActionError(handler, "interrupt", "not-casting",
                "The target finished casting before the interrupt could start.");
            return true;
        }

        // Direct execution deliberately bypasses prerequisites. Queue the
        // same mature action on the active engine instead, allowing its
        // existing reach prerequisite to move and then cast without a
        // command-side melee/ranged table.
        // This request came from the networked owner. Keep its queued
        // continuation eligible even if a later population tick is minimal.
        if (!QueueMatureAction(ai, actionName, event, ACTION_PASSTROUGH))
        {
            SendActionError(handler, "interrupt", "failed",
                "The mature interrupt action could not be started.");
            return true;
        }

        // Do not wrap this tick in ScopedSilentStrategy: changing the
        // non-combat strategy rebuilds that engine and would discard the
        // queued action/continuation before the reach prerequisite can run.
        ai->DoNextAction(false, true);
        accepted = true;
    }
    else
    {
        ExecuteQuietNextAction(ai, true);
    }

    SendActionAck(handler, "interrupt",
        context.selectedBot == executor
            ? "bot:" + std::string(executor->GetName()) : "party",
        1, executor->GetName());
    return accepted;
}

// The canonical text of a parsed pull order's words, "[seconds][ ranged]": what
// RunPullOrder takes as its option.
std::string PullOptionText(ai::PullOrderOption const& parsed)
{
    std::string text = parsed.hasSeconds ? std::to_string(parsed.seconds) : std::string();
    if (parsed.ranged)
        text += text.empty() ? "ranged" : " ranged";
    return text;
}

static bool ParseAction(std::string input, std::string& intent, std::string& option)
{
    intent.clear();
    option.clear();
    input = Trim(input);
    if (input.empty())
        return false;

    size_t firstSeparator = input.find_first_of(" \t");
    std::string first = input.substr(0, firstSeparator);
    for (char& character : first)
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));

    std::string remainder = firstSeparator == std::string::npos
        ? std::string() : Trim(input.substr(firstSeparator + 1));
    if (first == "focus" || first == "cc")
    {
        size_t secondSeparator = remainder.find_first_of(" \t");
        std::string mark = remainder.substr(0, secondSeparator);
        for (char& character : mark)
            character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
        // "cc clear [<BotName>]" is the dismissal intent (no mark named
        // "clear" exists: GetRtiIndex returns -1 for it).
        if (first == "cc" && mark == "clear")
        {
            std::string botToken = secondSeparator == std::string::npos
                ? std::string() : Trim(remainder.substr(secondSeparator + 1));
            if (mark.empty() || botToken.find_first_of(" \t") != std::string::npos)
                return false;
            intent = botToken.empty() ? first + " " + mark : first + " " + mark + " " + botToken;
            return true;
        }
        // "cc <mark> [<BotName>]": an explicit bot name assigns the mark to
        // that owned bot without juggling the player's target.
        if (first == "cc" && IsRaidTargetMark(mark))
        {
            std::string botToken = secondSeparator == std::string::npos
                ? std::string() : Trim(remainder.substr(secondSeparator + 1));
            if (mark.empty() || botToken.find_first_of(" \t") != std::string::npos)
                return false;
            intent = botToken.empty() ? first + " " + mark : first + " " + mark + " " + botToken;
            return true;
        }
        if ((first == "focus" && mark != "skull") ||
            (first == "cc" && !IsRaidTargetMark(mark)) ||
            mark.empty() ||
            (secondSeparator != std::string::npos &&
                !Trim(remainder.substr(secondSeparator + 1)).empty()))
            return false;
        intent = first + " " + mark;
        return true;
    }
    if (first == "corpse")
    {
        std::string rest = remainder;
        for (char& character : rest)
            character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
        if (rest != "run")
            return false;
        intent = "corpse run";
        return true;
    }

    if (first != "attack" && first != "interrupt" && first != "stop" && first != "pull" &&
        first != "pullback" && first != "come" && first != "stay" &&
        first != "follow" && first != "auto" && first != "autocc" && first != "aoe" && first != "hold" &&
        first != "comestay" && first != "ready" && first != "loot" &&
        first != "repair" && first != "sell" && first != "rest" &&
        first != "drink" && first != "eat" && first != "release" &&
        first != "corpse run" && first != "corpserun" && first != "learn" &&
        first != "trade" && first != "raid" && first != "flee" && first != "sap" &&
        first != "stance" && first != "rebuff" && first != "guard" && first != "free" &&
        first != "end")
        return false;

    if (first == "raid")
    {
        std::string rest = remainder;
        for (char& character : rest)
            character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
        if (rest != "status" && rest != "tankface" && rest != "douse" && rest != "custom status" && rest != "custom on" && rest != "custom off")
            return false;
        intent = "raid " + rest;
        return true;
    }
    else if (first == "corpse run" || first == "corpserun")
    {
        intent = "corpse run";
        return true;
    }
    else if (first == "auto")
    {
        std::string rest = remainder;
        for (char& character : rest)
            character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
        // Only "auto cc" exists today; keep the surface extensible.
        // Split "auto cc [on|off]" into intent + option like aoe/loot.
        if (rest == "cc")
        {
            intent = "auto cc";
            return true;
        }
        if (rest == "cc on" || rest == "cc off" || rest == "cc status")
        {
            intent = "auto cc";
            option = rest.substr(3);
            return true;
        }
        return false;
    }
    else if (first == "aoe" || first == "loot" || first == "autocc")
    {
        // "aoe|autocc|loot [on|off|status]": bare form toggles, "status"
        // answers the current state without changing it.
        std::string rest = remainder;
        for (char& character : rest)
            character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
        if (!rest.empty() && rest != "on" && rest != "off" && rest != "status")
            return false;
        intent = first == "autocc" ? "auto cc" : first;
        option = rest;
        return true;
    }
    else if (first == "sap")
    {
        // "sap [on|off] [<BotName>]": pre-pull Sap switch. Bare form toggles,
        // like aoe/loot; an explicit name scopes it to that owned bot, like
        // "cc <mark> [bot]". The mode token is lowercased, the name keeps its
        // case for resolution. option = "<on|off|toggle>[ <BotName>]".
        size_t modeSeparator = remainder.find_first_of(" \t");
        std::string mode = remainder.substr(0, modeSeparator);
        for (char& character : mode)
            character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
        std::string botToken = modeSeparator == std::string::npos
            ? std::string() : Trim(remainder.substr(modeSeparator + 1));
        if (mode != "on" && mode != "off")
        {
            // "sap <BotName>": toggle for that bot.
            if (!botToken.empty())
                return false;
            botToken = remainder.substr(0, modeSeparator);
            mode = "toggle";
        }
        if (botToken.find_first_of(" \t") != std::string::npos)
            return false;
        intent = first;
        option = botToken.empty() ? mode : mode + " " + botToken;
        return true;
    }
    else if (first == "stance")
    {
        // "stance <near|tank|turnback|behind>": per-bot combat facing.
        // "turn" is the mature short form of "turnback". Anything else is
        // rejected here so the usage line stays the single source of truth.
        std::string stanceRest = remainder;
        for (char& character : stanceRest)
            character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
        if (stanceRest != "near" && stanceRest != "tank" && stanceRest != "turnback" &&
            stanceRest != "turn" && stanceRest != "behind")
            return false;
        intent = "stance";
        option = stanceRest == "turn" ? "turnback" : stanceRest;
        return true;
    }
    else if (first == "end")
    {
        // "end pull": abort a running pull maneuver on the dynamic scope.
        std::string endRest = remainder;
        for (char& character : endRest)
            character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
        if (endRest != "pull")
            return false;
        intent = "end pull";
        return true;
    }
    else if (first == "guard" || first == "free" || first == "rebuff")
    {
        // Party/dynamic-scope forms of the single-bot movement verbs and the
        // mature rebuff pass. Guard captures each bot's current spot.
        if (!remainder.empty())
            return false;
        intent = first;
        return true;
    }
    else if (first == "pull" || first == "pullback")
    {
        // Owner-specified adjustable delay: ".bot action pull [seconds]" and
        // ".bot action pullback [seconds]", each with an optional trailing
        // "ranged" (open from range whatever the mob does). Plain
        // "pull"/"pullback" keeps the configured default; the TBM addon sends
        // the slider value here. Anything else is rejected here (not in
        // HandleAction) so the usage line stays the single source of truth for
        // the surface. option = "[seconds][ ranged]".
        if (remainder.empty())
        {
            intent = first;
            return true;
        }
        ai::PullOrderOption parsed;
        if (!ai::ParsePullOrderOption(remainder, parsed))
            return false;
        intent = first;
        option = PullOptionText(parsed);
        return true;
    }
    else if (!remainder.empty())
        return false;
    intent = first;
    return true;
}

bool IsRaidTargetMark(std::string const& mark)
{
    return RtiTargetValue::GetRtiIndex(mark) >= 0;
}

// A refused CC order puts the group's raid marks back. The slots are restored
// in order: SetTargetIcon clears any other slot holding the mob it is given,
// and a mob holds one mark, so a slot already put back never holds a mob that
// a later slot gets back.
static void SnapshotRaidMarks(Group* group, ObjectGuid (&marks)[TARGET_ICON_COUNT])
{
    for (uint8 i = 0; i < TARGET_ICON_COUNT; ++i)
        marks[i] = group->GetTargetIcon(i);
}

static void RestoreRaidMarks(Group* group, ObjectGuid const (&marks)[TARGET_ICON_COUNT])
{
    for (uint8 i = 0; i < TARGET_ICON_COUNT; ++i)
        if (group->GetTargetIcon(i) != marks[i])
            group->SetTargetIcon(i, marks[i]);
}

// One mark = one owner. After assigning <mark> to <owner>, every other owned
// live bot in the same command scope holding that mark is reset to "none" and
// persisted. "none" resolves to GetRtiIndex -1, so CurrentCcMark reports "-"
// and the CC_ASSIGN snapshot drops the stale owner without extra plumbing.
// Returns the AIs it reset (each held exactly <mark>), so a refused order can
// put them back.
static std::vector<PlayerbotAI*> RevokeDuplicateCcOwners(Player* owner, std::string const& mark,
    std::vector<Player*> const& scope)
{
    std::vector<PlayerbotAI*> revoked;
    for (Player* other : scope)
    {
        if (!other || other == owner || !IsLiveHeadlessBot(other))
            continue;
        PlayerbotAI* otherAi = PlayerbotAIStorage::Instance().GetAI(other);
        if (!otherAi || !otherAi->GetAiObjectContext())
            continue;
        ai::Value<std::string>* otherMark =
            otherAi->GetAiObjectContext()->GetValue<std::string>("rti cc");
        if (!otherMark || otherMark->Get() != mark)
            continue;
        otherMark->Set("none");
        sPlayerbotDbStore.Save(otherAi);
        revoked.push_back(otherAi);
    }
    return revoked;
}

std::string CurrentCcMark(PlayerbotAI* ai)
{
    if (!ai || !ai->GetAiObjectContext())
        return "-";

    std::string mark = ai->GetAiObjectContext()->GetValue<std::string>("rti cc")->Get();
    return IsRaidTargetMark(mark) ? mark : "-";
}

// The `cc <mark> [bot]` order against context.enemyTarget (or the group's
// existing mark). Shared by `.bot action cc` and the remote commander's
// `.bot cmd cc` (issue #884): requester is the event owner and the unit whose
// map and attack rules judge the target; reportIntent names the order in the
// ACK/ERR line.
bool RunCcOrder(ChatHandler* handler, BotCommandContext const& context, Player* requester,
    std::string const& intent, std::string const& reportIntent)
{
    // Optional explicit bot name ("cc <mark> <BotName>") assigns that
    // owned bot without juggling the player's target. The mark token
    // itself is lowercase; the name keeps its case for resolution.
    std::string ccBotToken;
    std::string ccMark = intent.substr(3);
    size_t ccSpace = ccMark.find_first_of(" \t");
    if (ccSpace != std::string::npos)
    {
        ccBotToken = Trim(ccMark.substr(ccSpace + 1));
        ccMark = ccMark.substr(0, ccSpace);
    }
    int markIndex = RtiTargetValue::GetRtiIndex(ccMark);
    Unit* ccTarget = context.enemyTarget;

    // An explicit name wins over the live target: same assignment path
    // as the selected-bot case, including the immediate-cast probe when
    // an enemy is selected or the mark already exists. It is resolved
    // before the target checks, so an unknown name still reports no-bot.
    Player* namedBot = nullptr;
    if (!ccBotToken.empty())
    {
        BotRecord* namedRecord = nullptr;
        std::string namedName;
        if (!ResolveOwnedBot(handler, ccBotToken.c_str(), namedBot, namedRecord, namedName))
        {
            SendActionError(handler, reportIntent, "no-bot",
                "Unknown or uncontrollable bot name for cc assignment.");
            return true;
        }
    }

    // The group's marks before this order moves one: a refusal after the
    // move puts them back (RestoreRaidMarks).
    bool markMoved = false;
    ObjectGuid marksBefore[TARGET_ICON_COUNT];

    // Selecting an enemy is the most explicit request: make that unit the
    // requested raid mark. If the player selected a bot instead, preserve
    // the existing group mark and use it as the assignment target.
    if (ccTarget)
    {
        // Refuse before the mark moves: a corpse or non-attackable unit
        // would take the mark, and the order would still ACK success with
        // no CC target behind it. A GM requester is faction 35
        // (Player::SetGameMaster), which the attackability test refuses for
        // every hostile mob, so a GM needs only a live, targetable unit.
        bool const requesterIsGm = requester->IsGameMaster();
        bool const badTarget = !ccTarget->IsInWorld() || !ccTarget->IsAlive()
            || (requesterIsGm ? !ccTarget->IsTargetable(true, true)
                              : !requester->IsValidAttackTarget(ccTarget));
        if (badTarget)
        {
            SendActionError(handler, reportIntent, "no-target",
                "Your target is dead, out of the world or not attackable. Select a live enemy first.");
            return true;
        }
        // A named or selected executor is known before the mark moves, so
        // an executor without an AI is refused before any mark changes.
        Player* assignee = namedBot ? namedBot
            : (context.selectedBot && IsLiveHeadlessBot(context.selectedBot) ? context.selectedBot : nullptr);
        if (assignee && !PlayerbotAIStorage::Instance().GetAI(assignee))
        {
            SendActionError(handler, reportIntent, "no-ai", "The selected CC bot has no AI.");
            return true;
        }

        if (context.group)
        {
            SnapshotRaidMarks(context.group, marksBefore);
            context.group->SetTargetIcon(static_cast<uint8>(markIndex), ccTarget->GetObjectGuid());
            markMoved = true;
        }
    }
    else if (context.group && requester->GetMap())
    {
        ObjectGuid markGuid = context.group->GetTargetIcon(static_cast<uint8>(markIndex));
        if (!markGuid.IsEmpty())
            ccTarget = requester->GetMap()->GetUnit(markGuid);
    }

    if (ccTarget && (!ccTarget->IsInWorld() || !ccTarget->IsAlive()))
        ccTarget = nullptr;

    Player* executor = nullptr;
    std::string ccAction;
    if (namedBot)
    {
        executor = namedBot;
        if (ccTarget)
        {
            // Probe the named bot itself, not the party's best executor,
            // so the immediate cast uses an action this bot owns.
            BotCommandContext namedContext = context;
            namedContext.selectedBot = namedBot;
            ResolveCcExecutor(namedContext, ccTarget, ccMark, &ccAction);
        }
    }
    else if (context.selectedBot && IsLiveHeadlessBot(context.selectedBot))
    {
        // A selected bot is an assignment request. It may be configured
        // before an enemy is marked; when a live target is available, keep
        // the mature capability probe for an immediate attempt, but never
        // reject the assignment just because this current creature is not
        // legal for that bot's CC spell.
        executor = context.selectedBot;
        if (ccTarget)
        {
            ResolveCcExecutor(context, ccTarget, ccMark, &ccAction);
        }
    }
    else
    {
        if (!ccTarget)
        {
            SendActionError(handler, reportIntent, "no-target",
                "Select an enemy target or an owned bot with an existing raid mark.");
            return true;
        }

        executor = ResolveCcExecutor(context, ccTarget, ccMark, &ccAction);
        if (!executor)
        {
            if (markMoved)
                RestoreRaidMarks(context.group, marksBefore);
            SendActionError(handler, reportIntent, "no-cc",
                "No owned bot can CC this target (check class, target type, and spell state).");
            return true;
        }
    }

    PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(executor);
    if (!ai)
    {
        if (markMoved)
            RestoreRaidMarks(context.group, marksBefore);
        SendActionError(handler, reportIntent, "no-ai", "The selected CC bot has no AI.");
        return true;
    }

    // RtiCcValue is a per-bot preference. Persist it through the existing
    // mature AI store so the assignment survives AI recreation/relogin.
    std::string const previousCcMark = ai->GetAiObjectContext()->GetValue<std::string>("rti cc")->Get();
    if (!ExecuteQuietAction(ai, "rti", ai::Event(intent, "cc " + ccMark, requester)))
    {
        if (markMoved)
            RestoreRaidMarks(context.group, marksBefore);
        SendActionError(handler, reportIntent, "failed", "The mature CC assignment was rejected.");
        return true;
    }
    sPlayerbotDbStore.Save(ai);
    // Exclusive ownership: the new owner keeps the mark; every other owned
    // live party bot holding it is reset to "none" and persisted.
    std::vector<PlayerbotAI*> const revokedOwners =
        RevokeDuplicateCcOwners(executor, ccMark, context.partyBots);

    if (ccTarget && !ccAction.empty())
    {
        // Break stay/follow in combat so a ranged executor can reach the
        // target. The mature AI remains responsible for subsequent casts.
        RelaxTacticalMovement(ai);
        executor->SetSelectionGuid(ccTarget->GetObjectGuid());
        ai->GetAiObjectContext()->GetValue<Unit*>("current target")->Set(ccTarget);
        sServerFacade.SetFacingTo(executor, ccTarget);
        ai::Event ccEvent(intent, "cc " + ccMark, requester);
        if (!ExecuteQuietAction(ai, ccAction, ccEvent))
        {
            // Direct execution deliberately bypasses prerequisites. Keep
            // the mature CC action queued so its existing reach
            // prerequisite can move and then cast without a command-side
            // class/range table.
            // Keep this explicit owner request eligible through later
            // minimal ticks while the mature reach chain is in progress.
            if (!QueueMatureAction(ai, ccAction, ccEvent, ACTION_PASSTROUGH))
            {
                // The assignment was saved above: put back the marks, this
                // bot's previous mark and every owner the order revoked.
                if (markMoved)
                    RestoreRaidMarks(context.group, marksBefore);
                ai->GetAiObjectContext()->GetValue<std::string>("rti cc")->Set(previousCcMark);
                sPlayerbotDbStore.Save(ai);
                for (PlayerbotAI* revokedAi : revokedOwners)
                {
                    revokedAi->GetAiObjectContext()->GetValue<std::string>("rti cc")->Set(ccMark);
                    sPlayerbotDbStore.Save(revokedAi);
                }
                SendActionError(handler, reportIntent, "failed",
                    "The mature CC action could not be started.");
                return true;
            }
            // Keep the queued prerequisite chain intact for the first
            // movement/cast decision; the quiet helper would rebuild the
            // non-combat engine and drop it.
            ai->DoNextAction(false, true);
        }
        // While the CC cast bar runs, a forced decision can only cancel
        // it (the order's one Polymorph start never landed); the cast's
        // action duration already schedules the next decision.
        else if (!executor->IsNonMeleeSpellCasted(false, true, true))
            ExecuteQuietNextAction(ai, true);
    }

    SendActionAck(handler, reportIntent, (namedBot || context.selectedBot == executor)
        ? "bot:" + std::string(executor->GetName()) : "party", 1, executor->GetName());
    return true;
}

// The pull/pullback order against context.enemyTarget. Shared by `.bot action
// pull|pullback` and the remote commander's `.bot cmd pull|pullback` (issue
// #884): requester is the event owner, so the pull returns to its position;
// option is the join delay in seconds (empty = configured default);
// reportIntent names the order in the ACK/ERR line.
bool RunPullOrder(ChatHandler* handler, BotCommandContext const& context, Player* requester,
    std::string const& intent, std::string const& option, std::string const& reportIntent)
{
    Player* executor = ResolvePullExecutor(context);
    if (!executor)
    {
        bool const tankBusy = PullTankIsBusy(context);
        std::string const detail = PullBlockerDetail(context);
        std::string const text = ai::PullNoExecutorText(tankBusy, detail);
        SendActionError(handler, reportIntent, ai::PullNoExecutorCode(tankBusy), text.c_str());
        return true;
    }

    PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(executor);
    // Validate before any state changes: a refused pull must not park the
    // party, end a flee or flip the tank's pull mode.
    std::string refuseCode;
    std::string refuseMessage;
    if (!CheckPullTarget(executor, context.enemyTarget, refuseCode, refuseMessage))
    {
        SendActionError(handler, reportIntent, refuseCode.c_str(), refuseMessage.c_str());
        return true;
    }
    // Join delay: per-command [seconds] (0-60, parsed) or the configured
    // default. For a pullback the window runs from the tank's arrival at
    // the anchor; the hold placed below keeps DPS waiting through the
    // return leg and is re-stamped on arrival.
    bool pullback = intent == "pullback";
    uint32 joinDelay = pullback
        ? sPlayerbotAIConfig.pullBackDpsJoinDelay
        : sPlayerbotAIConfig.pullDpsJoinDelay;
    // option is "[seconds][ ranged]" (PullOptionText).
    ai::PullOrderOption pullWords;
    ai::ParsePullOrderOption(option, pullWords);
    if (pullWords.hasSeconds)
        joinDelay = pullWords.seconds;
    // Pull and Pullback share the mature request action; the per-command
    // return mode (plus join delay and the `ranged` word) is recorded on the
    // PullStrategy (never sticky). The tank's default is restored when the pull
    // ends.
    PullModeSnapshot pullModeBefore = SavePullMode(ai);
    if (!ConfigurePullMode(ai, pullback, joinDelay, pullWords.ranged))
    {
        char const* message = pullback
            ? "The mature pullback strategy is unavailable."
            : "The mature ordinary-pull strategy is unavailable.";
        SendActionError(handler, reportIntent, "pull-policy", message);
        return true;
    }

    // Snapshot the target GUID: the live selection can change between
    // command validation and the pull tick.
    ai::Event pullEvent(intent, context.enemyTarget->GetObjectGuid(), requester);
    if (!ExecuteQuietAction(ai, "pull my target", pullEvent))
    {
        // Not caught by the pre-check: put the tank's pull mode back.
        // No bot's movement, flee or hold was touched yet.
        RestorePullMode(ai, pullModeBefore);
        SendActionError(handler, reportIntent, "failed", "The native pull strategy rejected the target.");
        return true;
    }
    // How the pull opens, now that the request has the target and has decided:
    // "reach pull" means the tank walks in and hits the mob in melee once. When
    // the order wanted range (a `ranged` order, a pullback, an unreachable or
    // far mob) and the tank cannot fire - no weapon, skill, shoot spell or ammo
    // - the pull walks in and the answer names what is missing. Reported
    // honestly for both intents, in the ACK detail and in a whisper to the
    // requester.
    PullStrategy* openingProbe = PullStrategy::Get(ai);
    std::string pullActionName;
    if (openingProbe)
        pullActionName = openingProbe->GetPullActionName();
    bool bodyPull = pullActionName == "reach pull";
    std::string openingNote;
    if (openingProbe && openingProbe->CommandWantsRanged())
    {
        if (!bodyPull)
        {
            // Opens from range because the order, the ledge or the distance
            // said so. A pullback's ACK stays what it always was.
            if (!pullback)
                openingNote = std::string("ranged-") + ai::PullOpeningReasonName(openingProbe->GetOpeningReason());
        }
        else
        {
            Unit* openingTarget = openingProbe->GetTarget();
            ai::PullRangedBlocker const blocker = openingProbe->GetRangedBlocker(
                openingTarget && openingTarget->IsInWorld() ? executor->GetDistance(openingTarget) : -1.0f);
            if (blocker != ai::PullRangedBlocker::None)
            {
                openingNote = ai::PullRangedBlockerName(blocker);
                // Whispered only when the order said `ranged`, or the tank has
                // a ranged weapon it cannot fire (no ammo, skill or spell).
                // A tank with no ranged weapon at all, or a mob that is simply
                // too close, walks in quietly on the automatic rules; the ACK
                // detail still names the blocker.
                if (openingProbe->IsCommandRangedOrder() ||
                    (blocker != ai::PullRangedBlocker::NoWeapon && blocker != ai::PullRangedBlocker::TooClose))
                {
                    char const* missing =
                        blocker == ai::PullRangedBlocker::NoWeapon ? "no ranged weapon equipped"
                        : blocker == ai::PullRangedBlocker::Untrained ? "the weapon skill is missing"
                        : blocker == ai::PullRangedBlocker::NoSpell ? "the shoot spell is not learned"
                        : blocker == ai::PullRangedBlocker::NoAmmo ? "no ammo"
                        : "the target is inside the minimum shooting range";
                    ai->TellPlayerNoFacing(requester, std::string(executor->GetName()) +
                        " cannot pull from range (" + missing + "), so it walks in.");
                }
            }
        }
    }
    // The pull is accepted (the request only records state): now the tank
    // may break its stay, a fled party comes back and the DPS hold at your
    // position.
    RelaxTacticalMovement(ai);
    for (Player* member : context.partyBots)
        EndFlee(PlayerbotAIStorage::Instance().GetAI(member));
    PausePartyDpsForPull(context, executor, joinDelay, pullback);
    // A human order acts now: the tank's own action clock (left over from
    // whatever it was doing) would otherwise stall the pull for the rest of
    // its delay before the second pull tick ever runs - the reported "tank
    // stands still for seconds, then starts pulling".
    ai->ClearActionClock();
    // The same for a meal or any cast: "reach pull" does not move a casting
    // bot (ai/playerbot/PullStartPolicy.h, ReadyTankForPull): an explicit or
    // commander order beats the meal.
    ai::PullReadyFacts readyFacts;
    readyFacts.casting = executor->IsNonMeleeSpellCasted(true, false, true);
    readyFacts.seated = executor->GetStandState() != UNIT_STAND_STATE_STAND;
    ai::PullReadySteps const ready = ai::ReadyTankForPull(readyFacts);
    // Every non-melee cast, channelled ones and those without an interrupt
    // flag too (PlayerbotAI::InterruptSpell skips both).
    if (ready.interrupt)
        executor->InterruptNonMeleeSpells(false);
    if (ready.stand)
        executor->SetStandState(UNIT_STAND_STATE_STAND);
    // Pull start/action are normal movement-priority actions.  A minimal
    // step filters them out and leaves the tank waiting for a later tick.
    ExecuteQuietNextAction(ai, false);
    // The first step only runs "pull start". The walk to the firing position
    // is the next one ("pull action" with its "reach pull" prerequisite), so
    // whether the move started can only be judged after that step - judging
    // it after the first one aborted every ranged pull the moment it began.
    if (!bodyPull)
        ExecuteQuietNextAction(ai, false);

    // A ranged pull closes the distance with a point move to a firing
    // position. If that move cannot be launched the tank stands where it
    // was and the pull can only run out its 15 s window - report it instead
    // of acknowledging a pull that never started (issue #389). A body pull
    // is exempt: its melee chase is launched by the core on the next
    // movement tick, not inside this AI tick.
    if (!bodyPull)
    {
        Unit* pullTarget = nullptr;
        float pullRange = 0.0f;
        if (PullStrategy* probe = PullStrategy::Get(ai))
        {
            pullTarget = probe->GetTarget();
            pullRange = probe->GetRange();
        }

        // A point move is registered with the motion master at once but the
        // unit reads as moving only after the next movement tick, so a launched
        // point or chase motion counts as started (ai/playerbot/PullStartPolicy.h).
        auto readStart = [&]()
        {
            MovementGeneratorType const motion = executor->GetMotionMaster()->GetCurrentMovementGeneratorType();
            ai::PullStartFacts facts;
            facts.hasTarget = pullTarget && pullTarget->IsInWorld();
            facts.moving = sServerFacade.isMoving(executor);
            facts.motionLaunched = motion == POINT_MOTION_TYPE || motion == CHASE_MOTION_TYPE;
            if (facts.hasTarget)
                facts.distance = executor->GetDistance(pullTarget);
            facts.range = pullRange;
            return facts;
        };
        ai::PullStartFacts startFacts = readStart();
        // A decision that went to another action leaves the walk unlaunched:
        // one direct "reach pull" (the pull's own prerequisite) before refusing.
        if (ai::RetryPullWalk(startFacts, bodyPull))
        {
            ExecuteQuietAction(ai, "reach pull", ai::Event(intent, "", requester));
            startFacts = readStart();
        }
        bool started = ai::PullWalkStarted(startFacts);

        if (!started)
        {
            // Abort the way the pull timeout would, so the tank is not left
            // holding the pull target or the party.
            ExecuteQuietAction(ai, "pull end", ai::Event(intent, "", requester));
            SendActionError(handler, reportIntent, "unreachable",
                "The tank could not start moving to a firing position for the pull.");
            return true;
        }
    }

    std::string scope = context.selectedBot == executor
        ? "bot:" + std::string(executor->GetName()) : "party";
    std::string executorDetail = executor->GetName();
    if (bodyPull)
        executorDetail += " body-pull";
    if (!openingNote.empty())
        executorDetail += " " + openingNote;
    SendActionAck(handler, reportIntent, scope, 1, executorDetail);
    return true;
}

bool HandleAction(ChatHandler* handler, char const* args)
{
    Player* requester = Requester(handler);
    std::string intent;
    std::string option;
    bool const parsed = requester && ParseAction(Trim(args ? args : ""), intent, option);
    // Another player of a bound party (in-party binding): its bots take no `.bot action` of his.
    if (parsed)
        if (uint64 const owner = BoundForeignCommander(requester))
        {
            Player* commander = ObjectAccessor::FindPlayerNotInWorld(ObjectGuid(owner)); // FindCharacter is file-local to Commander.cpp
            SendActionError(handler, "action " + intent, "run-bound",
                RemoteCommander::RunBoundRefusalText(commander ? commander->GetName() : "its commander").c_str());
            return true;
        }
    if (parsed)
        PauseDungeonRunForPlayer(requester->GetObjectGuid().GetRawValue(), "action " + intent);
    if (!parsed)
    {
        if (intent.empty())
        {
            // The addon drops an ACTION_ERR with an empty intent as malformed,
            // so name what was typed: the first word, at most 32 bytes and never
            // cut inside a UTF-8 character. ProtocolSafe runs before the trim, so
            // a word of only '|' characters becomes "unknown" like an empty one.
            std::string const typed = Trim(args ? args : "");
            size_t const end = typed.find_first_of(" \t");
            size_t length = end == std::string::npos ? typed.size() : end;
            if (length > 32)
            {
                length = 32;
                // typed[length] is the first byte left out. A continuation byte
                // (10xxxxxx) there means the cut would split a multibyte character,
                // so step back to that character's lead byte.
                while (length > 0 && (static_cast<unsigned char>(typed[length]) & 0xC0) == 0x80)
                    --length;
            }
            intent = Trim(ProtocolSafe(typed.substr(0, length)));
            if (intent.empty())
                intent = "unknown";
        }
        SendActionError(handler, intent, "invalid", "Usage: .bot action attack/interrupt/stop/pull [seconds] [ranged]/pullback [seconds] [ranged]/end pull/come/stay/hold/follow/flee/guard/free/stance <near|tank|turnback|behind>/rebuff/focus skull/cc <mark> [bot]/cc clear [bot]/auto cc [on/off/status]/autocc [on/off/status]/sap [on/off] [bot]/aoe [on/off/status]/loot [on/off/status]/repair/sell/rest/drink/eat/release/corpse run/learn/trade/ready/raid [status/tankface/douse/custom status/custom on/custom off]");
        return true;
    }
    if (!requester->IsInWorld() || !requester->IsAlive() || requester->IsBeingTeleported())
    {
        SendActionError(handler, intent, "requester", "Requester must be alive, in world, and not teleporting.");
        return true;
    }

    BotCommandContext context = BuildContext(requester);
    bool tactical = intent == "pull" || intent == "pullback";
    if ((intent == "attack" || intent == "interrupt" || tactical) && !context.enemyTarget)
    {
        SendActionError(handler, intent, "no-target", "Select a live non-bot target first.");
        return true;
    }
    // A corpse stays selected after the kill; an order against it would ACK
    // and do nothing (or, for a pull, be refused without a reason).
    if ((intent == "attack" || intent == "interrupt" || tactical) && !context.enemyTarget->IsAlive())
    {
        SendActionError(handler, intent, "dead-target", "Your target is dead. Select a live enemy first.");
        return true;
    }
    if (intent == "interrupt")
        return ExecuteInterruptAction(handler, context, requester);

    std::vector<Player*> scope;
    Player* sapNamedBot = nullptr;
    if (intent == "focus skull")
    {
        if (context.group && context.enemyTarget)
        {
            context.group->SetTargetIcon(static_cast<uint8>(RtiTargetValue::GetRtiIndex("skull")), context.enemyTarget->GetObjectGuid());
        }
        else if (context.group)
        {
            int skullIndex = RtiTargetValue::GetRtiIndex("skull");
            ObjectGuid skullGuid = context.group->GetTargetIcon(static_cast<uint8>(skullIndex));
            if (skullGuid.IsEmpty())
            {
                SendActionError(handler, intent, "no-target", "Select an enemy target or mark a target with Skull first.");
                return true;
            }
        }
        else if (!context.enemyTarget)
        {
            SendActionError(handler, intent, "no-target", "Select an enemy target or mark a target with Skull first.");
            return true;
        }
        scope = context.partyBots;
    }
    else if (intent == "cc clear" || intent.compare(0, 9, "cc clear ") == 0)
    {
        // Dismissal: an explicit name clears only that bot; a targeted bot
        // clears itself; otherwise the whole owned party is cleared. "none"
        // is not a raid mark, so CurrentCcMark reports "-" and the next
        // CC_ASSIGN snapshot drops the owner.
        std::vector<Player*> clearScope;
        if (intent.size() > 9)
        {
            Player* named = nullptr;
            BotRecord* namedRecord = nullptr;
            std::string namedName;
            if (!ResolveOwnedBot(handler, intent.substr(9).c_str(), named, namedRecord, namedName))
            {
                SendActionError(handler, intent, "no-bot",
                    "Unknown or uncontrollable bot name for cc clear.");
                return true;
            }
            clearScope.push_back(named);
        }
        else if (context.selectedBot && IsLiveHeadlessBot(context.selectedBot))
            clearScope.push_back(context.selectedBot);
        else
            clearScope = context.partyBots;
        uint32 cleared = 0;
        std::string clearedName = "-";
        for (Player* bot : clearScope)
        {
            if (!bot || !IsLiveHeadlessBot(bot))
                continue;
            PlayerbotAI* botAi = PlayerbotAIStorage::Instance().GetAI(bot);
            if (!botAi || !botAi->GetAiObjectContext())
                continue;
            botAi->GetAiObjectContext()->GetValue<std::string>("rti cc")->Set("none");
            sPlayerbotDbStore.Save(botAi);
            clearedName = bot->GetName();
            ++cleared;
        }
        if (!cleared)
        {
            SendActionError(handler, intent, "no-target",
                "Select an owned bot or have owned party bots to clear CC marks.");
            return true;
        }
        SendActionAck(handler, intent,
            clearScope.size() == 1 && cleared == 1
                ? "bot:" + clearedName : "party",
            cleared, clearedName);
        return true;
    }
    else if (intent.compare(0, 3, "cc ") == 0)
        return RunCcOrder(handler, context, requester, intent, intent);
    else if (intent == "raid status" || intent == "raid tankface" || intent == "raid douse" || intent == "raid custom status" || intent == "raid custom on" || intent == "raid custom off")
        return HandleRaidAction(handler, context, requester, intent);
    else if (intent == "sap" && option.find(' ') != std::string::npos)
    {
        // "sap <mode> <BotName>": only that owned bot; option keeps the mode.
        std::string sapBotToken = option.substr(option.find(' ') + 1);
        option = option.substr(0, option.find(' '));
        BotRecord* namedRecord = nullptr;
        std::string namedName;
        if (!ResolveOwnedBot(handler, sapBotToken.c_str(), sapNamedBot, namedRecord, namedName))
        {
            SendActionError(handler, intent, "no-bot", "Unknown or uncontrollable bot name for sap.");
            return true;
        }
        scope.push_back(sapNamedBot);
    }
    else
        scope = ResolveDynamicScope(context);
    if (tactical)
        return RunPullOrder(handler, context, requester, intent, option, intent);

    if (scope.empty())
    {
        SendActionError(handler, intent, "no-bots", "No live owned party bots are controllable.");
        return true;
    }

    uint32 succeeded = 0;
    std::string aoeState;
    // Per-bot reasons a rest order did not start, for the refusal (#916).
    std::vector<std::pair<std::string, std::string>> restRefusals;
    std::vector<std::string> sellRefusals;
    for (Player* bot : scope)
    {
        // Scope entries are resolved live from ObjectAccessor, but earlier
        // iterations run full AI actions; re-check before dereferencing so a
        // bot that logged out mid-loop is skipped, not crashed into (#225).
        if (!bot || !bot->IsInWorld())
            continue;

        PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(bot);
        if (!ai)
            continue;

        // A movement order supersedes the bot's movement orders still queued
        // from chat: `/p stay` then `.bot action follow` in one tick reach the
        // AI in that order, but the chat line only runs on the bot's next AI
        // turns, after this order (runtime/QueuedOrderPolicy.h). The tactical
        // orders do the same through RelaxTacticalMovement.
        if (intent == "follow" || intent == "stay" || intent == "flee" ||
            intent == "come" || intent == "hold" || intent == "comestay" ||
            intent == "guard" || intent == "free")
            ai->DropQueuedMovementOrders();

        bool accepted = false;
        if (intent == "attack")
        {
            // AttackMyTargetAction intentionally reads requester's selection,
            // preserving the mature target validation and combat path.
            // Break stay and follow in combat so bots can move to and fight the target!
            RelaxTacticalMovement(ai);
            // Explicit attack unleashes DPS early: drop the wait window and
            // our anchor stay (never a player-placed stay). No-op when absent.
            // Release this bot itself, before it acts (releasing only the
            // other party bots left the first bot attacking while still held
            // and a lone scoped bot never released).
            ai->ChangeStrategy("-wait for attack", BotState::BOT_STATE_ALL);
            // Dedicated healers should support the party, not be forced to target and attack the enemy
            if (!PlayerbotAI::IsHeal(bot))
            {
                accepted = ExecuteQuietAction(ai, "attack my target",
                    ai::Event("action attack", "", requester));
            }
            else
            {
                accepted = true;
            }
            // Caster rotations and party heals are below the minimal-action
            // cutoff.  Run a normal first combat decision after committing the
            // explicit target so ranged bots engage alongside melee bots.
            ExecuteQuietNextAction(ai, false);
        }
        else if (intent == "stop")
        {
            // Keep healing and movement strategies enabled: stop only combat,
            // then use the mature narrow reset/target cleanup.
            bot->CombatStopWithPets(true);
            ai->Reset(false);
            bot->GetMotionMaster()->Clear();
            accepted = true;
        }
        else if (intent == "follow")
        {
            // FollowChatShortcutAction owns the cross-engine movement
            // transition; this narrow clear only discards a stale movement
            // generator left by the preceding tactical action.
            if (!BindMovementMaster(requester, bot))
                continue;
            bot->GetMotionMaster()->Clear();
            accepted = ExecuteQuietAction(ai, "follow chat shortcut",
                ai::Event(intent, "", requester));
        }
        else if (intent == "flee")
        {
            // Break off: drop combat and any pull hold, then the mature flee
            // shortcut parks the bot in passive follow on the requester. The
            // next tactical order (attack, pull, follow, stay...) ends it.
            ai->ChangeStrategy("-wait for attack", BotState::BOT_STATE_ALL);
            bot->CombatStopWithPets(true);
            ai->Reset(false);
            bot->GetMotionMaster()->Clear();
            if (!BindMovementMaster(requester, bot))
                continue;
            accepted = ExecuteQuietAction(ai, "flee chat shortcut",
                ai::Event(intent, "", requester));
        }
        else if (intent == "stay")
        {
            accepted = ExecuteQuietAction(ai, "stay chat shortcut",
                ai::Event(intent, "", requester));
        }
        else if (intent == "come" || intent == "hold" || intent == "comestay")
        {
            // The latest explicit order wins over a pending summon's follow
            // restore: hold/comestay keep the bot holding, come ("come, then
            // follow") lets the restore run again.
            if (intent == "come")
                TortoiseBots::PlayerConvenience::Instance().NoteComeOrder(bot->GetObjectGuid());
            else
                TortoiseBots::PlayerConvenience::Instance().NoteHoldOrder(bot->GetObjectGuid());
            ai->Reset();
            // Use the same owner-controlled transition as the mature Stay
            // shortcut; the captured stay position below remains Come's
            // regroup anchor.
            ai->SetMovementStrategy("stay");
            SetStayAnchor(ai, requester->GetPositionX(), requester->GetPositionY(),
                requester->GetPositionZ(), requester->GetMapId());
            accepted = ExecuteQuietAction(ai, "return to stay position",
                ai::Event("return to stay position", "stay", requester));
        }
        else if (intent == "ready")
        {
            accepted = ExecuteQuietAction(ai, "ready check",
                ai::Event(intent, "", requester));
        }
        else if (intent == "rest" || intent == "drink" || intent == "eat")
        {
            bool rested = false;
            if (bot->GetPowerType() == POWER_MANA && bot->GetPower(POWER_MANA) < bot->GetMaxPower(POWER_MANA))
                rested = ExecuteQuietAction(ai, "drink", ai::Event(intent, "drink", requester)) || rested;
            if (bot->GetHealth() < bot->GetMaxHealth())
                rested = ExecuteQuietAction(ai, "food", ai::Event(intent, "food", requester)) || rested;
            accepted = rested || (bot->GetHealth() >= bot->GetMaxHealth() &&
                (bot->GetPowerType() != POWER_MANA || bot->GetPower(POWER_MANA) >= bot->GetMaxPower(POWER_MANA)));
            if (!accepted)
            {
                // Name the gate that held so the caller can wait instead of
                // guessing (#916). The stop lines and the should-values are
                // the ones the drink/food actions read.
                bool const cheat = ai->HasCheat(BotCheatMask::item);
                ai::AiObjectContext* aiContext = ai->GetAiObjectContext();
                ai::RestRefusalInput in;
                in.alive = bot->IsAlive();
                in.inCombat = bot->IsInCombat();
                in.groupFighting = aiContext->GetValue<bool>("has attackers")->Get() ||
                    ai->IsGroupMemberNearInCombat();
                in.usesMana = bot->GetPowerType() == POWER_MANA;
                in.manaPct = in.usesMana && bot->GetMaxPower(POWER_MANA)
                    ? bot->GetPower(POWER_MANA) * 100 / bot->GetMaxPower(POWER_MANA) : 100;
                in.drinkStopPct = ai::DrinkStopManaPct(cheat, sPlayerbotAIConfig.almostFullHealth);
                in.healthPct = bot->GetMaxHealth() ? bot->GetHealth() * 100 / bot->GetMaxHealth() : 100;
                in.eatStopPct = ai::RestStopHealthPct(cheat, sPlayerbotAIConfig.mediumHealth,
                    sPlayerbotAIConfig.lowHealth, sPlayerbotAIConfig.almostFullHealth);
                in.hasDrink = cheat ||
                    !aiContext->GetValue<std::list<Item*>>("inventory items", "drink")->Get().empty();
                in.hasFood = cheat ||
                    !aiContext->GetValue<std::list<Item*>>("inventory items", "food")->Get().empty();
                in.shouldDrink = aiContext->GetValue<bool>("should drink")->Get();
                in.shouldEat = aiContext->GetValue<bool>("should eat")->Get();
                restRefusals.emplace_back(bot->GetName(), ai::RestRefusalReason(in));
            }
        }
        else if (intent == "aoe")
        {
            // Same switch as `.bot behavior <bot> aoe`: off adds the
            // "no aoe" veto on every area spell, on removes it. Persisted
            // like the behaviour toggles. "status" answers without changing.
            BehaviorToggle const& aoeToggle = *FindBehaviorToggle("aoe");
            if (option == "status")
            {
                accepted = true;
                aoeState = IsBehaviorOn(ai, aoeToggle) ? "on" : "off";
            }
            else
            {
                bool enable = option == "on" ||
                    (option.empty() && !IsBehaviorOn(ai, aoeToggle));
                if (option == "off")
                    enable = false;
                SetBehavior(ai, aoeToggle, enable);
                sPlayerbotDbStore.Save(ai);
                ExecuteQuietNextAction(ai, true);
                accepted = IsBehaviorOn(ai, aoeToggle) == enable;
            }
        }
        else if (intent == "auto cc")
        {
            // Opt-in smart auto CC, OFF by default. Persisted like the loot
            // toggle so the choice survives relog; explicit `.bot action cc`
            // marks always win (CcTargetValue returns them first).
            // "status" answers without changing.
            if (option == "status")
            {
                accepted = true;
                aoeState = ai->HasStrategy("auto cc", BotState::BOT_STATE_COMBAT) ? "on" : "off";
            }
            else
            {
                bool enable = option == "on" ||
                    (option.empty() && !ai->HasStrategy("auto cc", BotState::BOT_STATE_COMBAT));
                if (option == "off")
                    enable = false;
                ai->ChangeStrategy((enable ? "+" : "-") + std::string("auto cc"),
                    BotState::BOT_STATE_COMBAT);
                sPlayerbotDbStore.Save(ai);
                ExecuteQuietNextAction(ai, true);
                accepted = ai->HasStrategy("auto cc", BotState::BOT_STATE_COMBAT) == enable;
            }
        }
        else if (intent == "sap")
        {
            // Pre-pull Sap on the CC mark, ON by default: "off" adds the
            // opt-out strategy "no sap", "on" removes it. Persisted like the
            // loot toggle. Other classes have no such strategy: skip them.
            if (bot->GetClass() != CLASS_ROGUE)
                continue;
            bool enable = option == "on" ||
                (option == "toggle" && ai->HasStrategy("no sap", BotState::BOT_STATE_NON_COMBAT));
            ai->ChangeStrategy((enable ? "-" : "+") + std::string("no sap"),
                BotState::BOT_STATE_NON_COMBAT);
            sPlayerbotDbStore.Save(ai);
            accepted = ai->HasStrategy("no sap", BotState::BOT_STATE_NON_COMBAT) != enable;
        }
        else if (intent == "focus skull")
        {
            RelaxTacticalMovement(ai);
            bool set = ExecuteQuietAction(ai, "rti",
                ai::Event("focus skull", "skull", requester));
            accepted = set && ExecuteQuietAction(ai, "attack rti target",
                ai::Event("focus skull", "", requester));
            ExecuteQuietNextAction(ai, true);
        }
        else if (intent == "loot")
        {
            // "status" answers without changing.
            if (option == "status")
            {
                accepted = true;
                aoeState = ai->HasStrategy("loot", BotState::BOT_STATE_NON_COMBAT) ? "on" : "off";
            }
            else
            {
                bool enable = option == "on" ||
                    (option.empty() && !ai->HasStrategy("loot", BotState::BOT_STATE_NON_COMBAT));
                if (option == "off")
                    enable = false;
                ai->ChangeStrategy((enable ? "+" : "-") + std::string("loot"),
                    BotState::BOT_STATE_NON_COMBAT);
                sPlayerbotDbStore.Save(ai);
                accepted = ai->HasStrategy("loot", BotState::BOT_STATE_NON_COMBAT) == enable;
            }
        }
        else if (intent == "repair")
        {
            accepted = ExecuteQuietAction(ai, "repair", ai::Event(intent, "repair", requester));
        }
        else if (intent == "sell")
        {
            accepted = ExecuteQuietAction(ai, "sell", ai::Event(intent, "", requester));
            if (!accepted)
                sellRefusals.push_back(ai::SellAction::RefusalReason(ai));
        }
        else if (intent == "release")
        {
            if (!bot || bot->IsAlive())
                continue;
            accepted = ExecuteQuietAction(ai, "release", ai::Event(intent, "release", requester));
        }
        else if (intent == "corpse run")
        {
            if (!bot || bot->IsAlive())
                continue;
            accepted = ExecuteQuietAction(ai, "corpse run", ai::Event(intent, "corpse run", requester));
        }
        else if (intent == "learn")
        {
            accepted = ExecuteQuietAction(ai, "trainer", ai::Event(intent, "learn", requester));
        }
        else if (intent == "end pull")
        {
            // Abort a stuck pull through the same bookkeeping as the state
            // machine's own pull end. Ordered past the gates: the order put
            // the bot in the pull, so "no pull running" never refuses it.
            accepted = ExecuteOrderedAction(ai, "end pull",
                ai::Event(intent, "", requester));
        }
        else if (intent == "stance")
        {
            accepted = ExecuteOrderedAction(ai, "stance",
                ai::Event(intent, option, requester));
            if (accepted)
                option = ai->GetAiObjectContext()->GetValue<ai::Stance*>("stance")->Get()->GetName();
        }
        else if (intent == "rebuff")
        {
            accepted = ExecuteOrderedAction(ai, "force rebuff",
                ai::Event(intent, "", requester));
            ExecuteQuietNextAction(ai, true);
        }
        else if (intent == "guard")
        {
            accepted = ExecuteOrderedAction(ai, "guard chat shortcut",
                ai::Event(intent, "", requester));
        }
        else if (intent == "free")
        {
            accepted = ExecuteOrderedAction(ai, "free chat shortcut",
                ai::Event(intent, "", requester));
        }
        else if (intent == "trade")
        {
            if (context.selectedBot != bot)
                continue;
            accepted = ExecuteQuietAction(ai, "trade", ai::Event(intent, requester->GetName(), requester));
        }

        if (accepted)
        {
            ++succeeded;
            // Toggle state accumulates into one line: "status" set it per
            // bot above, the changing forms re-read it here.
            std::string current;
            if (intent == "aoe")
                current = IsBehaviorOn(ai, *FindBehaviorToggle("aoe")) ? "on" : "off";
            else if (intent == "loot")
                current = ai->HasStrategy("loot", BotState::BOT_STATE_NON_COMBAT) ? "on" : "off";
            else if (intent == "auto cc")
                current = ai->HasStrategy("auto cc", BotState::BOT_STATE_COMBAT) ? "on" : "off";
            else if (intent == "sap")
                current = ai->HasStrategy("no sap", BotState::BOT_STATE_NON_COMBAT) ? "off" : "on";
            if (!current.empty())
            {
                if (aoeState.empty())
                    aoeState = current;
                else if (aoeState != current && aoeState != "mixed")
                    aoeState = "mixed";
            }
        }
    }

    if (!succeeded && intent == "sap")
    {
        SendActionError(handler, intent, "no-rogue", "No rogue bot in scope: name, select or group a rogue bot.");
        return true;
    }

    if (!succeeded && intent == "sell")
    {
        SendActionError(handler, intent, "failed", ai::FormatSellRefusal(sellRefusals).c_str());
        return true;
    }

    if (!succeeded)
    {
        std::string const restText = ai::FormatRestRefusal(restRefusals);
        SendActionError(handler, intent, "failed", restText.empty()
            ? "No scoped bot accepted the mature action." : restText.c_str());
        return true;
    }

    std::string scopeName = sapNamedBot ? "bot:" + std::string(sapNamedBot->GetName())
        : context.selectedBot && scope.size() == 1
        ? "bot:" + std::string(context.selectedBot->GetName()) : "party";
    SendActionAck(handler, intent, scopeName, succeeded,
        intent == "aoe" || intent == "loot" || intent == "auto cc" || intent == "sap" ? aoeState
        : intent == "stance" ? option : "");
    return true;
}

bool HandleRaidAction(ChatHandler* handler, BotCommandContext const& context, Player* requester, std::string const& intent)
{
    std::vector<Player*> scope = ResolveDynamicScope(context);
    if (scope.empty())
    {
        SendActionError(handler, intent, "no-bots", "No live owned party bots are controllable.");
        return true;
    }
    if (intent == "raid status")
    {
        uint32 count = 0;
        for (Player* bot : scope)
        {
            if (!bot || !bot->IsInWorld())
                continue;
            PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(bot);
            if (!ai)
                continue;
            std::string active;
            for (char const* strategy : { "molten core", "onyxia's lair", "blackwing lair", "naxxramas",
                                          "zul'gurub", "ruins of ahn'qiraj", "ahn'qiraj temple",
                                          "onyxia", "magmadar", "suppression room", "four horseman", "sapphiron",
                                          "heigan", "anub'rekhan", "gluth", "thaddius", "kel'thuzad",
                                          "grobbulus",
                                          "loatheb",
                                          "ossirian",
                                          "geddon",
                                          "emerald sanctum", "lower karazhan", "karazhan crypt",
                                          "solnius", "araxxna", "moroes" })
            {
                if (ai->HasStrategy(strategy, BotState::BOT_STATE_COMBAT))
                    active += (active.empty() ? "" : ",") + std::string(strategy);
            }
            // No raid strategy active: say whether the bot is in an instance instead of always "outdoor".
            Map* statusMap = bot->GetMap();
            std::string const idle = statusMap && statusMap->IsDungeon()
                ? "instance map " + std::to_string(bot->GetMapId()) + ", no raid strategy"
                : "outdoor";
            handler->PSendSysMessage("TBM:ACTION_ACK|raid status|bot:%s|1|%s",
                bot->GetName(), active.empty() ? idle.c_str() : active.c_str());
            ++count;
        }
        if (!count)
            SendActionError(handler, intent, "failed", "No scoped bot accepted the mature action.");
        return true;
    }
    if (intent == "raid custom status" || intent == "raid custom on" || intent == "raid custom off")
    {
        // Custom Turtle raid tactics gate (Emerald Sanctum / Lower Karazhan /
        // Karazhan Crypt): enabling adds the three transition strategies so
        // zone-ins swap them in; disabling drops them for manual control.
        // Playtesting-dependent geometry stays out regardless (#237).
        uint32 succeeded = 0;
        for (Player* bot : scope)
        {
            if (!bot || !bot->IsInWorld())
                continue;
            PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(bot);
            if (!ai)
                continue;
            if (intent == "raid custom status")
            {
                std::string active;
                for (char const* strategy : { "emerald sanctum", "lower karazhan", "karazhan crypt" })
                {
                    if (ai->HasStrategy(strategy, BotState::BOT_STATE_COMBAT))
                        active += (active.empty() ? "" : ",") + std::string(strategy);
                }
                handler->PSendSysMessage("TBM:ACTION_ACK|raid custom status|bot:%s|1|%s",
                    bot->GetName(), active.empty() ? "off" : active.c_str());
                handler->PSendSysMessage("Custom raid tactics are %s",
                    active.empty() ? "off" : active.c_str());
                ++succeeded;
                continue;
            }
            bool enable = intent == "raid custom on";
            for (BotState state : { BotState::BOT_STATE_COMBAT, BotState::BOT_STATE_NON_COMBAT })
            {
                ai->ChangeStrategy((enable ? "+" : "-") + std::string("emerald sanctum"), state);
                ai->ChangeStrategy((enable ? "+" : "-") + std::string("lower karazhan"), state);
                ai->ChangeStrategy((enable ? "+" : "-") + std::string("karazhan crypt"), state);
            }
            ++succeeded;
        }
        if (!succeeded)
        {
            SendActionError(handler, intent, "failed", "No scoped bot accepted the mature action.");
            return true;
        }
        std::string scopeName = context.selectedBot && scope.size() == 1
            ? "bot:" + std::string(context.selectedBot->GetName()) : "party";
        SendActionAck(handler, intent, scopeName, succeeded, "");
        return true;
    }
    uint32 succeeded = 0;
    char const* matureAction = intent == "raid tankface" ? "dragon tank face away" : "douse mc rune eternal";
    for (Player* bot : scope)
    {
        if (!bot || !bot->IsInWorld())
            continue;
        PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(bot);
        if (!ai)
            continue;
        bool accepted = ExecuteQuietAction(ai, matureAction, ai::Event(intent, "", requester));
        if (!accepted && intent == "raid douse")
            accepted = ExecuteQuietAction(ai, "douse mc rune aqual", ai::Event(intent, "", requester));
        if (accepted)
            ++succeeded;
    }
    if (!succeeded)
    {
        SendActionError(handler, intent, "failed", "No scoped bot accepted the mature action.");
        return true;
    }
    std::string scopeName = context.selectedBot && scope.size() == 1
        ? "bot:" + std::string(context.selectedBot->GetName()) : "party";
    SendActionAck(handler, intent, scopeName, succeeded, "");
    return true;
}


} // namespace BotCommands
} // namespace TortoiseBots
