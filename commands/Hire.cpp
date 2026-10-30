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

// Issue #900: `.bot hire raid <preset|composition> [tier] [level=<1-60>]`. The grammar and
// the roster live in runtime/HireRaidPolicy.h, the cap, quote, raid conversion
// and the job queue in HireProvisionService::HireRaid / StepRaidJobs. This
// command answers refusals and a "started" line at once; the service sends the
// closing summary and the structured line for the TBM addon when the job ends:
//   TBM:HIRE_RAID|<ok|partial|refused>|<hired>|<planned>|<tank>|<healer>|<dps>|<tier>|<feeCopper>|<reason>
static bool HandleHireRaid(ChatHandler* handler, Player* requester, std::vector<std::string> tokens)
{
    // tokens[0] is "raid"; the rest is the preset or composition.
    tokens.erase(tokens.begin());
    std::vector<HireRaidPreset> presets;
    if (!ParseHireRaidPresets(sPlayerbotAIConfig.hireRaidPresets, presets))
        ParseHireRaidPresets(kHireRaidPresetsDefault, presets);
    ExpandHireRaidPreset(presets, tokens);

    HireRaidRequest request;
    std::string error;
    if (!ParseHireRaidSpec(tokens, request, error))
    {
        handler->PSendSysMessage("%s", error.c_str());
        handler->PSendSysMessage("Usage: .bot hire raid <preset|composition> [tier] [level=<1-60>], for example .bot hire raid 3t 12h 24d t6");
        handler->PSendSysMessage("TBM:HIRE_RAID|refused|0|0|0|0|0|0|0|%s", ProtocolSafe(error).c_str());
        return true;
    }

    HireRaidOutcome outcome = HireProvisionService::Instance().HireRaid(requester, request);
    if (outcome.refused)
    {
        handler->PSendSysMessage("%s", outcome.message.c_str());
        handler->PSendSysMessage("TBM:HIRE_RAID|refused|0|%u|0|0|0|%u|0|%s", outcome.planned,
            static_cast<uint32>(outcome.tier), ProtocolSafe(outcome.message).c_str());
        return true;
    }

    // Accepted: the hires run over the next seconds (HireProvisionService::
    // StepRaidJobs) and the service sends the "Hired N: ..." summary and the
    // TBM:HIRE_RAID line when the job ends.
    uint32 const quote = static_cast<uint32>(std::min<uint64_t>(outcome.quote, 0xFFFFFFFFull));
    char const* tierName = HireTierName(outcome.tier);
    handler->PSendSysMessage("Hiring %u companions: %u tank / %u healer / %u dps, tier %u (%s), total fee %s.%s%s "
        "They arrive over the next few seconds; a summary follows.",
        outcome.planned, outcome.planTanks, outcome.planHealers, outcome.planDps,
        static_cast<uint32>(outcome.tier), tierName ? tierName : "?", ai::ChatHelper::formatMoney(quote).c_str(),
        outcome.capped ? " Capped by AiPlayerbot.HireMaxBotsPerPlayer or the free raid seats." : "",
        outcome.convertedToRaid ? " Your party became a raid." : "");
    return true;
}

// pi-lens-ignore: clang:incomplete_member_access,clang:unknown_typename,clang:undeclared_var_use
// Issue #192: fast-path companion hire. Same service, costs, and caps as the
// <Mercenary Hire> gossip wizard, but only available when the server sets
// AiPlayerbot.HireAnywhere; otherwise it points players to a recruiter. Usage:
//   .bot hire <class> [dps|tank|healer] [race] [male|female] [tier] [level]
//   .bot hire raid <preset|composition> [tier] [level=<1-60>]   (issue #900, see HandleHireRaid)
// Tokens after the class may come in any order: "warrior tauren male tank",
// "mage female frost", and "paladin protection" all parse. Bare spec words
// (protection/arms/fury/holy/retribution/...) imply their role. The optional
// tier word (t<N>, tier<N>, or the tier name like mc) picks the equipment
// tier up to the player's highest unlock (default tier 0, Standard kit).
// Issue #936: the optional level is level=<n> (or level:<n>, lvl=<n>) in any
// position, or a bare number of 12 or more as the last word. HireLevelPolicy.h
// holds the rule: 1 up to your level plus AiPlayerbot.HireMaxLevelAboveHirer
// (a GM up to 60); no level hires at your own level.
bool HandleHire(ChatHandler* handler, char const* args)
{
    Player* requester = Requester(handler);
    if (!requester)
    {
        handler->PSendSysMessage("You must be in-game to hire a companion.");
        return true;
    }
    std::string input = Trim(args ? args : "");
    if (input.empty())
    {
        handler->PSendSysMessage("Usage: .bot hire <class> [dps/tank/healer] [race] [male/female] [tier] [level=<1-60>], or .bot hire raid <preset|3t 12h 24d> [tier]");
        if (!sPlayerbotAIConfig.hireAllowPvP)
            handler->PSendSysMessage("Hired companions are PvE-only: they leave you when you enter a battleground.");
        return true;
    }
    // Split on whitespace; class/race/gender/role/spec/tier tokens in any order.
    std::vector<std::string> tokens;
    {
        std::istringstream stream(input);
        std::string token;
        while (stream >> token)
            tokens.push_back(token);
    }
    {
        std::string first = tokens.front();
        for (char& c : first)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (first == "raid")
        {
            for (std::string& t : tokens)
                for (char& c : t)
                    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return HandleHireRaid(handler, requester, tokens);
        }
    }
    HireSelection sel;
    // Issue #936: requested hire level (0 = none). Bad level words refuse.
    uint32 requestedLevel = 0;
    // Tokens that are not class/race/gender/role words: resolved against the
    // class after the loop, so "frost mage" works in either order.
    std::vector<std::string> specWords;
    for (size_t tokenIndex = 0; tokenIndex < tokens.size(); ++tokenIndex)
    {
        std::string token = tokens[tokenIndex];
        for (char& c : token)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        // Level word first: level=45 is no class/race/role/tier word.
        uint32 levelWord = 0;
        HireLevelWord levelKind = ParseHireLevelWord(token, levelWord);
        if (levelKind == HireLevelWord::Bad)
        {
            handler->PSendSysMessage("Bad level \"%s\": use level=<1-60> or level <1-60>, for example .bot hire mage level=42.", tokens[tokenIndex].c_str());
            return true;
        }
        if (levelKind == HireLevelWord::Ok)
        {
            if (requestedLevel)
            {
                handler->PSendSysMessage("Give only one level.");
                return true;
            }
            requestedLevel = levelWord;
            continue;
        }
        // "level 42" / "lvl 42": the number is the next word.
        if (levelKind == HireLevelWord::Prefix)
        {
            if (tokenIndex + 1 >= tokens.size() || !ParseHireLevelNumber(tokens[tokenIndex + 1], levelWord))
            {
                handler->PSendSysMessage("Bad level: use level=<1-60> or level <1-60>, for example .bot hire mage level=42.");
                return true;
            }
            if (requestedLevel)
            {
                handler->PSendSysMessage("Give only one level.");
                return true;
            }
            requestedLevel = levelWord;
            ++tokenIndex;
            continue;
        }
        // A bare number of 12+ (not a race/gender/class id) is a level only as
        // the last word after the class; anywhere else it is refused rather
        // than dropped.
        if (ParseBareHireLevel(token, levelWord))
        {
            if (tokenIndex + 1 != tokens.size() || tokenIndex == 0 || !sel.classId)
            {
                handler->PSendSysMessage("Unclear number \"%s\": put the level last, or write level=<1-60>, for example .bot hire mage level=42.", tokens[tokenIndex].c_str());
                return true;
            }
            if (requestedLevel)
            {
                handler->PSendSysMessage("Give only one level.");
                return true;
            }
            requestedLevel = levelWord;
            continue;
        }
        if (!sel.classId)
        {
            uint32 parsed = ai::ChatHelper::parseClass(token);
            if (parsed >= CLASS_WARRIOR && parsed <= CLASS_DRUID && parsed != 6 && parsed != 10)
            {
                sel.classId = static_cast<uint8>(parsed);
                continue;
            }
        }
        if (!sel.race)
        {
            uint32 parsed = ai::ChatHelper::parseRace(token);
            if (parsed >= RACE_HUMAN && parsed <= RACE_HIGH_ELF)
            {
                sel.race = static_cast<uint8>(parsed);
                continue;
            }
        }
        if (!sel.gender)
        {
            uint32 parsed = ai::ChatHelper::parseGender(token);
            if (parsed == GENDER_MALE || parsed == GENDER_FEMALE)
            {
                sel.gender = static_cast<uint8>(parsed);
                continue;
            }
        }
        // Issue #490 PR 2: optional tier word (t<N>/tier<N>/tier name like
        // mc) in any position, same any-order form as the other words.
        // Before role/spec so a tier name never reaches the spec matcher.
        uint8_t tierWord = 0;
        if (ParseHireTierWord(token, tierWord))
        {
            sel.gearTier = tierWord;
            continue;
        }
        if (ai::ChatHelper::parseRole(token) == ai::BOT_ROLE_NONE)
            specWords.push_back(token);
        if (!sel.role)
        {
            ai::BotRoles parsed = ai::ChatHelper::parseRole(token);
            if (parsed != ai::BOT_ROLE_NONE)
            {
                sel.role = static_cast<uint8>(parsed);
                continue;
            }
            // Bare spec words imply their role (protection -> tank, holy ->
            // healer). Match the gossip labels loosely: any token containing
            // the spec stem counts ("prot", "ret", "feral bear"). Feral Cat
            // stays DPS on the shared feral path (specIndex 3); other feral
            // words keep the Bear tank default (specIndex 0).
            std::string lower = token;
            bool catWord = lower.find("cat") != std::string::npos;
            bool tankWord = !catWord && (lower.find("prot") != std::string::npos || lower.find("tank") != std::string::npos ||
                lower.find("bear") != std::string::npos || lower.find("feral") != std::string::npos);
            bool healWord = lower.find("holy") != std::string::npos || lower.find("heal") != std::string::npos ||
                lower.find("resto") != std::string::npos || lower.find("discipline") != std::string::npos ||
                lower.find("disc") != std::string::npos;
            if (tankWord)
            {
                sel.role = static_cast<uint8>(ai::BOT_ROLE_TANK);
                if (sel.classId == CLASS_DRUID)
                    sel.specIndex = 0;
                // A bare "tank" word on a shaman means the tank row (the only
                // shaman tank option); the level-10 gate applies to it too.
                if (sel.classId == CLASS_SHAMAN)
                    sel.specIndex = 3;
                continue;
            }
            if (healWord)
            {
                sel.role = static_cast<uint8>(ai::BOT_ROLE_HEALER);
                if (sel.classId == CLASS_DRUID)
                    sel.specIndex = 1;
                continue;
            }
            bool balanceWord = lower.find("balance") != std::string::npos;
            if (lower.find("dps") != std::string::npos || lower.find("arms") != std::string::npos ||
                lower.find("fury") != std::string::npos || lower.find("shadow") != std::string::npos ||
                lower.find("ret") != std::string::npos || balanceWord ||
                lower.find("frost") != std::string::npos || lower.find("fire") != std::string::npos ||
                lower.find("arcane") != std::string::npos || lower.find("afflic") != std::string::npos ||
                lower.find("demon") != std::string::npos || lower.find("destro") != std::string::npos ||
                lower.find("assassin") != std::string::npos || lower.find("combat") != std::string::npos ||
                lower.find("subtle") != std::string::npos || lower.find("beast") != std::string::npos ||
                lower.find("marks") != std::string::npos || lower.find("surv") != std::string::npos ||
                lower.find("elem") != std::string::npos || lower.find("enhance") != std::string::npos ||
                catWord)
            {
                sel.role = static_cast<uint8>(ai::BOT_ROLE_DPS);
                if (sel.classId == CLASS_DRUID)
                    sel.specIndex = catWord ? 3 : balanceWord ? 2 : -1;
                continue;
            }
        }
    }
    if (!sel.classId)
    {
        handler->PSendSysMessage("Usage: .bot hire <class> [dps/tank/healer] [race] [male/female] [tier] [level=<1-60>], or .bot hire raid <preset|3t 12h 24d> [tier]");
        return true;
    }
    // Issue #936: the level is clamped against the hirer in Hire() (the one
    // authority); the early tier check below needs the same answer.
    if (requestedLevel > 255)
        requestedLevel = 255;
    sel.level = static_cast<uint8>(requestedLevel);
    uint32 hireLevel = ResolveHireLevel(requester->GetLevel(), requestedLevel,
        sPlayerbotAIConfig.hireMaxLevelAboveHirer,
        requester->GetSession()->GetSecurity() >= SEC_GAMEMASTER).level;
    // Issue #490 PR 2: a tier above the player's unlock fails fast with the
    // highest available tier named (Hire() re-validates against stale state).
    // Below 60 or with tiers disabled the tier is ignored, so skip the lookup.
    if (sel.gearTier > 0 && sPlayerbotAIConfig.hireGearTiersEnabled &&
        hireLevel == kHireGearHireLevel)
    {
        uint8_t highest = HireProvisionService::Instance().HighestUnlockedHireTierFor(requester);
        if (sel.gearTier > highest)
        {
            handler->PSendSysMessage("That equipment is not unlocked yet. Your highest available tier is %s (tier %u).",
                HireTierKitName(highest).c_str(), highest);
            return true;
        }
    }
    // A spec word picks the recruiter's spec option for every class, not only
    // druids: without a specIndex provisioning draws any premade tree of the
    // role, so "mage frost" could arrive Arcane. The spec word's role wins, as
    // in the gossip menu. Druids keep the bear/cat handling above.
    if (sel.classId != CLASS_DRUID)
    {
        uint32 specCount = 0;
        HireSpecOption const* specs = HireSpecOptions(sel.classId, specCount);
        for (std::string const& word : specWords)
        {
            int specIndex = HireSpecIndexForWord(sel.classId, word);
            if (specs && specIndex >= 0 && static_cast<uint32>(specIndex) < specCount)
            {
                sel.specIndex = specIndex;
                sel.role = specs[specIndex].role;
                break;
            }
        }
    }
    // Shaman tank needs the hirer's level 10 (taunt): a role-only request
    // ("tank" word above, spec word match, or a tank-role fill) resolves to
    // the tank row, so refuse it here as one gate instead of per word.
    if (sel.classId == CLASS_SHAMAN && sel.role == static_cast<uint8>(ai::BOT_ROLE_TANK))
    {
        if (!sPlayerbotAIConfig.shamanTankEnabled)
        {
            handler->PSendSysMessage("The experimental shaman tank is turned off on this server (AiPlayerbot.ShamanTankEnabled = 0).");
            return true;
        }
        if (!HireSpecOffered(CLASS_SHAMAN, 3, requester->GetLevel(), sPlayerbotAIConfig.shamanTankEnabled))
        {
            handler->PSendSysMessage("Enhancement (Tank) needs level 10 (Earthshaker Slam taunt). Level up and hire again.");
            return true;
        }
        if (sel.specIndex < 0)
            sel.specIndex = 3;
    }
    // Defaults: requester's faction race pool, random gender, class role.
    if (!HireProvisionService::FillDefaultRaceGender(requester, sel))
    {
        handler->PSendSysMessage("No race of your faction can be that class.");
        return true;
    }
    if (!sel.role)
        sel.role = HireProvisionService::DefaultRoleForClass(sel.classId);
    if (sel.classId == CLASS_DRUID && sel.specIndex < 0 && sel.role == static_cast<uint8>(ai::BOT_ROLE_DPS))
        sel.specIndex = 3;
    HireOutcome outcome = HireProvisionService::Instance().Hire(requester, sel, false);
    handler->PSendSysMessage("%s", outcome.message.c_str());
    return true;
}

bool HandleAdd(ChatHandler* handler, char const* args)
{
    if (!handler)
        return false;
    if (!args || !*args)
    {
        handler->PSendSysMessage("Usage: .bot add <characterName>");
        return true;
    }

    std::string name = args;
    // Trim
    name.erase(0, name.find_first_not_of(" \t"));
    name.erase(name.find_last_not_of(" \t") + 1);

    if (name.empty())
    {
        handler->PSendSysMessage("Usage: .bot add <characterName>");
        return true;
    }

    // Normalize name
    if (!normalizePlayerName(name))
    {
        handler->PSendSysMessage("Invalid character name.");
        return true;
    }

    ::Player* requester = handler->GetSession() ? handler->GetSession()->GetPlayer() : nullptr;
    if (!requester)
    {
        handler->PSendSysMessage("You must be in-game to add a bot.");
        return true;
    }

    PlayerCacheData* data = sObjectMgr.GetPlayerDataByName(name);
    if (!data)
    {
        // Try online lookup
        if (::Player* online = sObjectAccessor.FindPlayerByName(name.c_str()))
            data = sObjectMgr.GetPlayerDataByGUID(online->GetObjectGuid().GetCounter());
    }

    if (!data)
    {
        handler->PSendSysMessage("Character '%s' not found.", name.c_str());
        return true;
    }

    ::ObjectGuid guid(HIGHGUID_PLAYER, data->uiGuid);
    uint32_t accountId = data->uiAccount;
    ::ObjectGuid masterGuid = requester->GetObjectGuid();

    if (::Player* online = sObjectAccessor.FindPlayer(guid))
    {
        // Issue #872: `.bot add` on the requester's own alt-bot takes it back
        // as a companion without a relog.
        if (AltBotService::Instance().IsAutonomous(guid) && requester->GetSession() &&
            (AltBotService::Instance().GetOwnerAccount(guid.GetCounter()) == requester->GetSession()->GetAccountId() ||
             IsBotAdministrator(requester)))
        {
            if (AltBotService::Instance().TakeBack(requester, online))
                handler->PSendSysMessage("%s is your companion again and follows you.", name.c_str());
            else
                handler->PSendSysMessage("Could not take %s back as a companion.", name.c_str());
            return true;
        }
        handler->PSendSysMessage("Character '%s' is already online and cannot be claimed as a Headless bot.", name.c_str());
        return true;
    }

    if (!requester->GetSession() ||
        (accountId != requester->GetSession()->GetAccountId() && requester->GetSession()->GetSecurity() < SEC_GAMEMASTER))
    {
        handler->PSendSysMessage("You may only control characters on your account.");
        return true;
    }

    uint32 ownerAccountId = requester->GetSession()->GetAccountId();
    OwnedCharacter existingOwnership;
    if (BotManager::Instance().GetOwnedCharacter(guid, existingOwnership) &&
        existingOwnership.ownerAccountId != ownerAccountId &&
        !IsBotAdministrator(requester))
    {
        handler->PSendSysMessage("Character '%s' is already owned by another account.", name.c_str());
        return true;
    }

    if (BotManager::Instance().AddBotWithMaster(accountId, guid, masterGuid))
    {
        if (!BotManager::Instance().RegisterOwnedCharacter(
            ownerAccountId, accountId, guid, masterGuid))
        {
            // Do not leave an unowned runtime after a failed durable write.
            BotManager::Instance().RemoveBot(guid, false);
            handler->PSendSysMessage("Failed to persist ownership for bot %s; login was cancelled.", name.c_str());
            return true;
        }
        handler->PSendSysMessage("Bot %s queued for login; it will follow %s after entering the world.",
            name.c_str(), requester->GetName());
    }
    else
        handler->PSendSysMessage("Failed to add bot %s (already exists or error).", name.c_str());
    return true;
}

// pi-lens-ignore: clang:incomplete_member_access,clang:unknown_typename
bool HandleRemove(ChatHandler* handler, char const* args)
{
    if (!handler)
        return false;
    Player* requester = Requester(handler);
    if (!requester)
    {
        handler->PSendSysMessage("You must be in-game to remove a bot.");
        return true;
    }
    if (!args || !*args)
    {
        handler->PSendSysMessage("Usage: .bot remove <characterName>");
        return true;
    }
    std::string name = args;
    name.erase(0, name.find_first_not_of(" \t"));
    name.erase(name.find_last_not_of(" \t") + 1);
    if (!normalizePlayerName(name))
    {
        handler->PSendSysMessage("Invalid character name.");
        return true;
    }

    PlayerCacheData* data = sObjectMgr.GetPlayerDataByName(name);
    if (!data)
    {
        handler->PSendSysMessage("Character '%s' not found.", name.c_str());
        return true;
    }
    ::ObjectGuid guid(HIGHGUID_PLAYER, data->uiGuid);
    BotRecord* record = BotManager::Instance().FindBot(guid);
    if (!record)
    {
        handler->PSendSysMessage("Character '%s' is not a module-owned bot.", name.c_str());
        return true;
    }
    if (!CanControlBot(requester, record))
    {
        handler->PSendSysMessage("You may only control characters on your account.");
        return true;
    }
    if (HireLifecycle::Instance().IsHired(guid))
    {
        // Issue #192 follow-up: a hired companion is temporary. An explicit
        // removal ends the hire now and deletes its character; the fee is not
        // refunded and the next hire is a new character.
        HireLifecycle::Instance().DismissNow(guid, "removed by master");
        handler->PSendSysMessage("Hire ended for %s; its character is being deleted.", name.c_str());
        return true;
    }
    if (BotManager::Instance().RemoveBot(guid, true))
        handler->PSendSysMessage("Removal requested for bot %s; Headless cleanup completes asynchronously.", name.c_str());
    else
        handler->PSendSysMessage("Bot %s not found or not removable.", name.c_str());
    return true;
}

bool HandleLogout(ChatHandler* handler, char const* args)
{
    Player* requester = Requester(handler);
    Player* bot = nullptr;
    BotRecord* record = nullptr;
    std::string name;
    if (!requester || !ResolveOwnedBot(handler, args, bot, record, name))
    {
        handler->PSendSysMessage("Usage: .bot logout <online bot name> (same account only)");
        return true;
    }

    if (HireLifecycle::Instance().IsHired(bot->GetObjectGuid()))
    {
        // A hire is temporary: logging it out ends the hire and deletes its
        // character instead of keeping a pool character alive with the
        // master's level and gear.
        HireLifecycle::Instance().DismissNow(bot->GetObjectGuid(), "logged out by master");
        handler->PSendSysMessage("Hire ended for %s; its character is being deleted.", name.c_str());
        return true;
    }

    if (!BotManager::Instance().RemoveBot(bot->GetObjectGuid(), true))
    {
        handler->PSendSysMessage("Bot %s could not be logged out.", name.c_str());
        return true;
    }

    // Remove/logout deliberately leaves tortoise_bots_owned_character intact.
    handler->PSendSysMessage("Bot %s logout requested; durable ownership was retained.", name.c_str());
    return true;
}


} // namespace BotCommands
} // namespace TortoiseBots
