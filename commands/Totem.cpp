// pi-lens-ignore-file: all
#include "BotCommandHandlers.h"
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
#include "../ai/playerbot/strategy/values/AttackersValue.h"
#include "../ai/playerbot/strategy/values/PossibleAttackTargetsValue.h"
#include "../ai/playerbot/strategy/values/PositionValue.h"
#include "../ai/playerbot/strategy/values/ItemUsageValue.h"
#include "../ai/playerbot/strategy/actions/EquipAction.h"
#include "../ai/playerbot/strategy/actions/UnequipAction.h"
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

// Totem order helpers (`.bot totem`): read the per-bot stored orders and
// resolve the wanted totem per slot. Explicit orders win; auto/unset falls
// back to the subgroup-aware pick (same census the triggers use), so
// `drop` in default auto mode places the automatically chosen set instead
// of answering "nothing to drop".
static std::string TotemStatusWord(PlayerbotAI* ai, int slot)
{
    std::string stored = ai->GetAiObjectContext()->GetValue<std::string>("manual saved string", ai::TotemOrderKey(slot))->Get();
    if (stored.empty())
        return "none";
    return stored;
}

static std::string TotemModeWord(PlayerbotAI* ai)
{
    std::string stored = ai->GetAiObjectContext()->GetValue<std::string>("manual saved string", ai::TotemModeKey())->Get();
    if (stored.empty())
        return "auto";
    return stored;
}

// One TBM:TOTEM record: stored orders, drop mode, then every totem the bot
// has trained (each name once, slot order earth..air).
static void SendTotemLine(ChatHandler* handler, Player* bot, PlayerbotAI* ai)
{
    std::string known;
    for (int slot = 0; slot < 4; ++slot)
    {
        std::uint32_t count = 0;
        char const* const* choices = ai::TotemChoicesForSlot(slot, count);
        for (std::uint32_t i = 0; i < count; ++i)
        {
            std::string const name = choices[i];
            if (!ai->HasSpell(name))
                continue;
            if (!known.empty() && ("+" + known + "+").find("+" + name + "+") != std::string::npos)
                continue;
            if (!known.empty())
                known += "+";
            known += name;
        }
    }
    handler->PSendSysMessage("TBM:TOTEM|%s|earth=%s,fire=%s,water=%s,air=%s,mode=%s,known=%s",
        ProtocolSafe(bot->GetName()).c_str(), TotemStatusWord(ai, 0).c_str(), TotemStatusWord(ai, 1).c_str(),
        TotemStatusWord(ai, 2).c_str(), TotemStatusWord(ai, 3).c_str(), TotemModeWord(ai).c_str(), known.c_str());
}

// Subgroup census for one bot (the trigger's ReadTotemSubgroup shape, game
// state translated here so the policy stays testable): own-subgroup live
// mates on the same map within 100 yd. Hunters count as melee (pet swings),
// warriors/rogues always, feral-tank druids and melee-flagged shamans too.
static ai::TotemSubgroupCensus TotemDropCensus(PlayerbotAI* ai, Player* bot)
{
    ai::TotemGroupMember members[6];
    std::uint8_t count = 0;
    Group* group = bot ? bot->GetGroup() : nullptr;
    if (group && bot)
    {
        for (Group::MemberSlot const& slot : group->GetMemberSlots())
        {
            Player* member = ObjectAccessor::FindPlayer(slot.guid);
            if (!member || member == bot || !member->IsInWorld())
                continue;
            if (!group->SameSubGroup(bot, member))
                continue;
            if (member->GetMapId() != bot->GetMapId() || bot->GetDistance(member) > 100.0f)
                continue;
            if (!sServerFacade.IsAlive(member))
                continue;
            std::uint8_t cls = member->GetClass();
            bool melee = cls == CLASS_WARRIOR || cls == CLASS_ROGUE ||
                cls == CLASS_HUNTER ||
                (cls == CLASS_DRUID && ai->IsTank(member)) ||
                (cls == CLASS_SHAMAN && ai->IsMelee(member));
            bool mana = member->GetMaxPower(POWER_MANA) > 0;
            if (count < 6)
                members[count++] = ai::TotemGroupMember{false, melee, mana};
        }
    }
    return ai::CountTotemSubgroup(members, count);
}

static std::string TotemWantForBot(PlayerbotAI* ai, Player* bot, int slot)
{
    std::string stored = ai->GetAiObjectContext()->GetValue<std::string>("manual saved string", ai::TotemOrderKey(slot))->Get();
    std::string ordered = ai::OrderedTotemForSlot(stored);
    if (!ordered.empty())
        return ordered;
    // Explicit "none" ("") holds the slot empty; auto/unset resolves the
    // subgroup pick at drop time (drop never forces a totem the subgroup
    // would not want — caster groups keep earth empty the same way).
    if (!stored.empty() && stored != "auto")
        return "";
    ai::TotemSubgroupCensus census = TotemDropCensus(ai, bot);
    switch (slot)
    {
    case 0:
        return ai::BestEarthTotemForSubgroup(census, ai->HasSpell("strength of earth totem"));
    case 2:
        return ai::BestWaterTotemForSubgroup(census,
            ai->HasSpell("mana spring totem"), ai->HasSpell("healing stream totem"));
    case 3:
        return ai::BestAirTotemForSubgroup(census,
            ai->HasSpell("windfury totem"), ai->HasSpell("grace of air totem"),
            ai->HasSpell("tranquil air totem"));
    default:
        return "";
    }
}

// `.bot totem <bot|party|raid> <earth|fire|water|air> <totem|auto|none>` —
// per-slot totem choice, persisted per bot like the behaviour toggles.
// `.bot totem <scope> mode <auto|prepull|manual>`, `.bot totem <scope>
// drop` (queue the configured set now, in or out of combat), `.bot totem
// <bot> status`, `.bot totem <scope> set <earth>,<fire>,<water>,<air>` (all
// four slots at once). Answers the action protocol (ACK/ERR) so the addon
// button/lines can drive it. Unknown totems and totems the bot never
// trained answer ERR naming the reason.
bool HandleTotem(ChatHandler* handler, char const* args)
{
    Player* requester = Requester(handler);
    std::istringstream input(Trim(args ? args : ""));
    std::string scope;
    std::string word2;
    std::string rest;
    input >> scope >> word2;
    std::getline(input, rest);
    rest = Trim(rest);
    for (char& c : scope)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    for (char& c : word2)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    std::string loweredRest = rest;
    for (char& c : loweredRest)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    std::string const usage =
        "Usage: .bot totem <bot|party|raid> <earth|fire|water|air> <totem name|auto|none> | set <earth>,<fire>,<water>,<air> | mode <auto|prepull|manual> | drop | status";
    bool const scopeIsParty = scope == "party" || scope == "raid";
    if (!scopeIsParty && scope.empty())
    {
        SendActionError(handler, "totem", "invalid", usage.c_str());
        return true;
    }

    // Gather target bots: explicit name, or the live owned party scope.
    std::vector<Player*> bots;
    if (scopeIsParty)
    {
        if (!requester)
            return true;
        BotCommandContext context = BuildContext(requester);
        bots = ResolveDynamicScope(context);
        if (bots.empty())
        {
            SendActionError(handler, "totem", "no-bot", "No live owned party bots are controllable.");
            return true;
        }
    }
    else
    {
        Player* bot = nullptr;
        BotRecord* record = nullptr;
        std::string name;
        if (!requester || !ResolveOwnedBot(handler, scope.c_str(), bot, record, name))
        {
            SendActionError(handler, "totem", "no-bot", "Unknown or uncontrollable bot name.");
            return true;
        }
        bots.push_back(bot);
    }

    std::string const ackScope = scopeIsParty ? "party" : std::string("bot:") + bots.front()->GetName();

    // `status`: report choice/mode per bot (addon/roster line, no chat spam
    // beyond one line per bot).
    if (word2 == "status")
    {
        for (Player* bot : bots)
        {
            PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(bot);
            if (!ai)
                continue;
            SendTotemLine(handler, bot, ai);
        }
        SendActionAck(handler, "totem status", ackScope, (uint32)bots.size(), "-");
        return true;
    }

    // `mode <auto|prepull|manual>`.
    if (word2 == "mode")
    {
        if (loweredRest != "auto" && loweredRest != "prepull" && loweredRest != "pre-pull" && loweredRest != "manual")
        {
            SendActionError(handler, "totem mode", "invalid", usage.c_str());
            return true;
        }
        ai::TotemDropMode const mode = ai::ParseTotemDropMode(loweredRest);
        uint32 succeeded = 0;
        for (Player* bot : bots)
        {
            PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(bot);
            if (!ai || bot->GetClass() != CLASS_SHAMAN)
                continue;
            ai->GetAiObjectContext()->GetValue<std::string>("manual saved string", ai::TotemModeKey())->Set(ai::TotemDropModeName(mode));
            sPlayerbotDbStore.Save(ai);
            SendTotemLine(handler, bot, ai);
            ++succeeded;
        }
        if (!succeeded)
        {
            SendActionError(handler, "totem mode", "no-shaman", "No shaman bots in scope.");
            return true;
        }
        SendActionAck(handler, "totem mode", ackScope, succeeded, ai::TotemDropModeName(mode));
        return true;
    }

    // `drop`: queue the configured set now, in or out of combat. Reach
    // rides the totem actions' own chain; occupied slots are skipped.
    if (word2 == "drop")
    {
        uint32 queued = 0;
        for (Player* bot : bots)
        {
            PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(bot);
            if (!ai || bot->GetClass() != CLASS_SHAMAN)
                continue;
            ai::Event dropEvent("totem drop", "", requester);
            for (int slot = 0; slot < 4; ++slot)
            {
                std::string want = TotemWantForBot(ai, bot, slot);
                if (want.empty() || !ai->HasSpell(want))
                    continue;
                if (ai->GetAiObjectContext()->GetValue<bool>("has totem", want)->Get())
                    continue;
                if (QueueMatureAction(ai, want, dropEvent, ai::ACTION_PASSTROUGH))
                    ++queued;
            }
        }
        if (!queued)
        {
            SendActionError(handler, "totem drop", "nothing", "No totems to drop (slots full, none configured, or not trained).");
            return true;
        }
        SendActionAck(handler, "totem drop", ackScope, queued, "-");
        return true;
    }

    // `set <earth>,<fire>,<water>,<air>`: all four slots validated first, then
    // stored per shaman; a bot missing any named totem is skipped whole.
    if (word2 == "set")
    {
        std::string stored[4];
        if (!ai::ParseTotemSetList(loweredRest, stored))
        {
            SendActionError(handler, "totem set", "invalid", usage.c_str());
            return true;
        }
        uint32 succeeded = 0;
        bool refused = false;
        for (Player* bot : bots)
        {
            PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(bot);
            if (!ai || bot->GetClass() != CLASS_SHAMAN)
                continue;
            bool trained = true;
            for (int slot = 0; slot < 4 && trained; ++slot)
            {
                if (!stored[slot].empty() && stored[slot] != "auto" && !ai->HasSpell(stored[slot]))
                {
                    std::string msg = std::string(bot->GetName()) + " has not trained " + stored[slot] + ".";
                    SendActionError(handler, "totem set", "unknown-spell", msg.c_str());
                    trained = false;
                    refused = true;
                }
            }
            if (!trained)
                continue;
            for (int slot = 0; slot < 4; ++slot)
                ai->GetAiObjectContext()->GetValue<std::string>("manual saved string", ai::TotemOrderKey(slot))->Set(stored[slot]);
            sPlayerbotDbStore.Save(ai);
            SendTotemLine(handler, bot, ai);
            ++succeeded;
        }
        if (!succeeded)
        {
            if (!refused)
                SendActionError(handler, "totem set", "no-shaman", "No shaman bots in scope.");
            return true;
        }
        SendActionAck(handler, "totem set", ackScope, succeeded, "-");
        return true;
    }

    // `<slot> <totem|auto|none>`.
    int const slot = ai::TotemSlotIndex(word2);
    if (slot < 0)
    {
        SendActionError(handler, "totem", "invalid", usage.c_str());
        return true;
    }
    if (loweredRest.empty())
    {
        SendActionError(handler, "totem", "invalid", usage.c_str());
        return true;
    }
    bool valid = false;
    std::string const stored = ai::NormalizeTotemOrder(slot, loweredRest, valid);
    if (!valid)
    {
        std::string msg = std::string("Unknown totem '") + rest + "' for the " + ai::TotemSlotName(slot) + " slot.";
        SendActionError(handler, "totem", "unknown-totem", msg.c_str());
        return true;
    }
    uint32 succeeded = 0;
    bool refused = false;
    for (Player* bot : bots)
    {
        PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(bot);
        if (!ai || bot->GetClass() != CLASS_SHAMAN)
            continue;
        // Name the reason when the bot never trained it: the order would
        // otherwise sit stored and silently never fire.
        if (!stored.empty() && stored != "auto" && !ai->HasSpell(stored))
        {
            std::string msg = std::string(bot->GetName()) + " has not trained " + stored + ".";
            SendActionError(handler, "totem", "unknown-spell", msg.c_str());
            refused = true;
            continue;
        }
        ai->GetAiObjectContext()->GetValue<std::string>("manual saved string", ai::TotemOrderKey(slot))->Set(stored);
        sPlayerbotDbStore.Save(ai);
        SendTotemLine(handler, bot, ai);
        ++succeeded;
    }
    if (!succeeded)
    {
        if (!refused)
            SendActionError(handler, "totem", "no-shaman", "No shaman bots in scope.");
        return true;
    }
    std::string const detail = std::string(ai::TotemSlotName(slot)) + "=" + (stored.empty() ? "none" : stored);
    SendActionAck(handler, "totem", ackScope, succeeded, detail.c_str());
    return true;
}


} // namespace BotCommands
} // namespace TortoiseBots
