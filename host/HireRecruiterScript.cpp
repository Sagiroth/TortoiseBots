// pi-lens-ignore-file: clang:pp_file_not_found,clang:unknown_typename,clang:use_of_undeclared_identifier,clang:unknown_type_name,clang:undeclared_var_use,clang:incomplete_member_access
#include "HireRecruiterScript.h"
#include "../runtime/HireProvisionService.h"
#include "../runtime/HireCost.h"
#include "../runtime/HireSpecPolicy.h"
#include "../runtime/BotManager.h"
#include "../ai/playerbot/playerbot.h"
#include "../ai/playerbot/ChatHelper.h"
#include "ModuleLog.h"

#include "ScriptObjects.h"
#include "GossipDef.h"
#include "Player.h"
#include "Creature.h"
#include "ObjectMgr.h"
#include "World.h"
#include "WorldSession.h"
#include "Chat.h"
#include "SharedDefines.h"
#include "Log.h"

namespace TortoiseBots
{

namespace
{

// Gossip sender ids carve the recruiter menu out of the shared sender/action
// space: sender picks the wizard step, action carries the choice id.
// DB-driven gossip (gossip_menu_option) also uses sender/action, but those
// rows are keyed by menu id and never collide with a script-built menu.
constexpr uint32 kSenderClass = 501;
constexpr uint32 kSenderRace = 502;
constexpr uint32 kSenderGender = 503;
constexpr uint32 kSenderSpec = 504;

// All action ids fit in one byte: every wizard step packs prior choices into
// individual bytes of the 32-bit action/sender (class | race | gender |
// specIndex), so any value > 255 bleeds into the neighboring byte and
// corrupts the decoded race/gender (900 = 0x384 garbled Human into Dwarf).
// 255 is the highest non-choice byte: races are 1-10, genders 0-1, spec
// indices 0-3, so it never collides with a real choice.
constexpr uint32 kBackAction = 255;
// Confirm rides in the sender-packed selection with action 254 (likewise one
// byte, likewise collision-free).
constexpr uint32 kConfirmAction = 254;

constexpr uint32 kMaxMenuItems = 30;

uint8 const kHireClasses[] = { CLASS_WARRIOR, CLASS_PALADIN, CLASS_HUNTER, CLASS_ROGUE, CLASS_PRIEST, CLASS_SHAMAN, CLASS_MAGE, CLASS_WARLOCK, CLASS_DRUID };

char const* ClassName(uint8 classId)
{
    switch (classId)
    {
        case CLASS_WARRIOR: return "Warrior";
        case CLASS_PALADIN: return "Paladin";
        case CLASS_HUNTER: return "Hunter";
        case CLASS_ROGUE: return "Rogue";
        case CLASS_PRIEST: return "Priest";
        case CLASS_SHAMAN: return "Shaman";
        case CLASS_MAGE: return "Mage";
        case CLASS_WARLOCK: return "Warlock";
        case CLASS_DRUID: return "Druid";
        default: return "Unknown";
    }
}

char const* RaceName(uint8 race)
{
    switch (race)
    {
        case RACE_HUMAN: return "Human";
        case RACE_ORC: return "Orc";
        case RACE_DWARF: return "Dwarf";
        case RACE_NIGHTELF: return "Night Elf";
        case RACE_UNDEAD: return "Undead";
        case RACE_TAUREN: return "Tauren";
        case RACE_GNOME: return "Gnome";
        case RACE_TROLL: return "Troll";
        case RACE_GOBLIN: return "Goblin";
        case RACE_HIGH_ELF: return "High Elf";
        default: return "Unknown";
    }
}

// The spec step lists talent-flavoured choices but hires by role: the option
// maps to a BOT_ROLE bit consumed by provisioning (premade build + forced
// role). The option's pathName selects the premade tree, so the wizard is
// honoured for every class (issue #386). The table lives in
// runtime/HireSpecPolicy.h so the recruiter and the provisioner share it.

bool ClassForFaction(uint8 classId, Team team)
{
    if (team == ALLIANCE)
        return classId != CLASS_SHAMAN;
    if (team == HORDE)
        return classId != CLASS_PALADIN;
    return true;
}

bool KnownHireClass(uint8 classId)
{
    for (uint8 candidate : kHireClasses)
        if (candidate == classId)
            return true;
    return false;
}

void AddItem(Player* player, uint8 icon, char const* text, uint32 sender, uint32 action)
{
    player->PlayerTalkClass->GetGossipMenu().AddMenuItem(icon, text, sender, action, "", false);
}

uint32 GossipText(Player* /*player*/)
{
    // DEFAULT_GOSSIP_MESSAGE renders the client "Greetings" fallback; the
    // menu items carry the wizard. Custom text ids would need a locales row
    // per recruiter, so the wizard stays text-light by design.
    return DEFAULT_GOSSIP_MESSAGE;
}

void CloseWithHint(Player* player, char const* hint)
{
    player->PlayerTalkClass->CloseGossip();
    if (hint && *hint)
        ChatHandler(player).PSendSysMessage("%s", hint);
}

HireCost::CostConfig CurrentCosts()
{
    HireCost::CostConfig costs;
    costs.baseCopper = sPlayerbotAIConfig.hireBaseCostCopper;
    costs.mult2 = sPlayerbotAIConfig.hirePartyMult2;
    costs.mult3 = sPlayerbotAIConfig.hirePartyMult3;
    costs.mult4 = sPlayerbotAIConfig.hirePartyMult4;
    costs.raidFlatCopper = sPlayerbotAIConfig.hireRaidFlatCostCopper;
    return costs;
}

// Quote-only hire-slot count for the confirm menu: live hires + mustering
// for THIS master guid (same rule as the provisioner's cap). Same-account
// alts are not hires and never inflate the quote.
uint32_t QuoteHireCount(Player* player)
{
    return HireProvisionService::Instance().CountHired(player);
}

void ShowClassMenu(Player* player, Creature* creature)
{
    Team team = player ? player->GetTeam() : TEAM_NONE;
    for (uint8 classId : kHireClasses)
    {
        if (!ClassForFaction(classId, team))
            continue;
        AddItem(player, GOSSIP_ICON_CHAT, ClassName(classId), kSenderClass, classId);
    }
    player->PlayerTalkClass->SendGossipMenu(GossipText(player), creature->GetObjectGuid());
}

void ShowRaceMenu(Player* player, Creature* creature, uint8 classId)
{
    Team team = player ? player->GetTeam() : TEAM_NONE;
    uint32 shown = 0;
    for (uint32 race = 1; race < MAX_RACES && shown < kMaxMenuItems - 1; ++race)
    {
        if (!sObjectMgr.GetPlayerInfo(race, classId))
            continue;
        if (Player::TeamForRace(race) != team)
            continue;
        AddItem(player, GOSSIP_ICON_CHAT, RaceName(race), kSenderRace, (uint32(classId) << 8) | race);
        ++shown;
    }
    AddItem(player, GOSSIP_ICON_TALK, "< Back", kSenderRace, (uint32(classId) << 8) | kBackAction);
    player->PlayerTalkClass->SendGossipMenu(GossipText(player), creature->GetObjectGuid());
}

void ShowGenderMenu(Player* player, Creature* creature, uint8 classId, uint8 race)
{
    uint32 packed = (uint32(classId) << 8) | race;
    AddItem(player, GOSSIP_ICON_CHAT, "Male", kSenderGender, (packed << 8) | GENDER_MALE);
    AddItem(player, GOSSIP_ICON_CHAT, "Female", kSenderGender, (packed << 8) | GENDER_FEMALE);
    AddItem(player, GOSSIP_ICON_TALK, "< Back", kSenderGender, (packed << 8) | kBackAction);
    player->PlayerTalkClass->SendGossipMenu(GossipText(player), creature->GetObjectGuid());
}

void ShowSpecMenu(Player* player, Creature* creature, uint8 classId, uint8 race, uint8 gender)
{
    uint32_t specCount = 0;
    HireSpecOption const* specs = HireSpecOptions(classId, specCount);
    for (uint32 i = 0; i < specCount && i < kMaxMenuItems - 1; ++i)
        AddItem(player, GOSSIP_ICON_BATTLE, specs[i].label, kSenderSpec,
            (uint32(classId) << 24) | (uint32(race) << 16) | (uint32(gender) << 8) | i);
    AddItem(player, GOSSIP_ICON_TALK, "< Back", kSenderSpec,
        (uint32(classId) << 24) | (uint32(race) << 16) | (uint32(gender) << 8) | kBackAction);
    player->PlayerTalkClass->SendGossipMenu(GossipText(player), creature->GetObjectGuid());
}

void ShowConfirmMenu(Player* player, Creature* creature, uint8 classId, uint8 race, uint8 gender, uint8 specIndex)
{
    uint32_t specCount = 0;
    HireSpecOption const* specs = HireSpecOptions(classId, specCount);
    if (!specs || specIndex >= specCount)
    {
        ShowSpecMenu(player, creature, classId, race, gender);
        return;
    }
    uint32_t level = player->GetLevel();
    uint32_t cost = HireCost::ForNextHire(CurrentCosts(), QuoteHireCount(player), level);
    std::ostringstream label;
    label << "Hire " << RaceName(race) << " " << ClassName(classId) << " (" << specs[specIndex].label << ") for "
        << ai::ChatHelper::formatMoney(cost) << ". Confirm?";
    // Packed selection travels in the sender; action distinguishes confirm/back.
    uint32 packed = (uint32(classId) << 24) | (uint32(race) << 16) | (uint32(gender) << 8) | specIndex;
    AddItem(player, GOSSIP_ICON_MONEY_BAG, label.str().c_str(), packed, kConfirmAction);
    AddItem(player, GOSSIP_ICON_TALK, "< Back", packed, kBackAction);
    player->PlayerTalkClass->SendGossipMenu(GossipText(player), creature->GetObjectGuid());
}
} // namespace

bool HireRecruiterScript::OnHello(Player* player, Creature* creature)
{
    if (!player || !creature)
        return false;
    if (!sPlayerbotAIConfig.hireEnabled)
    {
        CloseWithHint(player, "No mercenaries are mustering right now.");
        return true;
    }
    if (player->GetSession()->GetSecurity() < static_cast<int>(sPlayerbotAIConfig.hireMinAccountSecurity))
    {
        CloseWithHint(player, "The recruiter sizes you up and shakes their head. (Your account may not hire.)");
        return true;
    }
    player->PlayerTalkClass->ClearMenus();
    ShowClassMenu(player, creature);
    return true;
}

bool HireRecruiterScript::OnSelect(Player* player, Creature* creature, uint32_t sender, uint32_t action)
{
    if (!player || !creature)
        return false;
    player->PlayerTalkClass->ClearMenus();

    if (sender == kSenderClass)
    {
        uint8 classId = static_cast<uint8>(action);
        if (!KnownHireClass(classId) || !ClassForFaction(classId, player->GetTeam()))
        {
            ShowClassMenu(player, creature);
            return true;
        }
        ShowRaceMenu(player, creature, classId);
        return true;
    }
    if (sender == kSenderRace)
    {
        uint8 classId = static_cast<uint8>((action >> 8) & 0xFF);
        uint32 low = action & 0xFF;
        if (low == kBackAction)
        {
            ShowClassMenu(player, creature);
            return true;
        }
        uint8 race = static_cast<uint8>(low);
        if (!sObjectMgr.GetPlayerInfo(race, classId) || Player::TeamForRace(race) != player->GetTeam())
        {
            ShowRaceMenu(player, creature, classId);
            return true;
        }
        ShowGenderMenu(player, creature, classId, race);
        return true;
    }
    if (sender == kSenderGender)
    {
        uint8 classId = static_cast<uint8>((action >> 16) & 0xFF);
        uint8 race = static_cast<uint8>((action >> 8) & 0xFF);
        uint32 low = action & 0xFF;
        if (low == kBackAction)
        {
            ShowRaceMenu(player, creature, classId);
            return true;
        }
        if (low != GENDER_MALE && low != GENDER_FEMALE)
        {
            ShowGenderMenu(player, creature, classId, race);
            return true;
        }
        ShowSpecMenu(player, creature, classId, race, static_cast<uint8>(low));
        return true;
    }
    if (sender == kSenderSpec)
    {
        uint8 classId = static_cast<uint8>((action >> 24) & 0xFF);
        uint8 race = static_cast<uint8>((action >> 16) & 0xFF);
        uint8 gender = static_cast<uint8>((action >> 8) & 0xFF);
        uint32 low = action & 0xFF;
        uint32_t specCount = 0;
        HireSpecOptions(classId, specCount);
        if (low == kBackAction)
        {
            ShowGenderMenu(player, creature, classId, race);
            return true;
        }
        if (low >= specCount)
        {
            ShowSpecMenu(player, creature, classId, race, gender);
            return true;
        }
        ShowConfirmMenu(player, creature, classId, race, gender, static_cast<uint8>(low));
        return true;
    }
    // Packed confirm: sender IS the packed class/race/gender/spec selection,
    // action distinguishes confirm from back.
    if (action == kConfirmAction || action == kBackAction)
    {
        uint8 classId = static_cast<uint8>((sender >> 24) & 0xFF);
        uint8 race = static_cast<uint8>((sender >> 16) & 0xFF);
        uint8 gender = static_cast<uint8>((sender >> 8) & 0xFF);
        uint8 specIndex = static_cast<uint8>(sender & 0xFF);
        uint32_t specCount = 0;
        HireSpecOption const* specs = HireSpecOptions(classId, specCount);
        if (!KnownHireClass(classId) || !specs || specIndex >= specCount ||
            !sObjectMgr.GetPlayerInfo(race, classId) ||
            (gender != GENDER_MALE && gender != GENDER_FEMALE))
        {
            ShowClassMenu(player, creature);
            return true;
        }
        if (action == kBackAction)
        {
            ShowSpecMenu(player, creature, classId, race, gender);
            return true;
        }
        HireSelection sel;
        sel.classId = classId;
        sel.race = race;
        sel.gender = gender;
        sel.role = specs[specIndex].role;
        sel.specIndex = static_cast<int>(specIndex);
        HireOutcome outcome = HireProvisionService::Instance().Hire(player, sel, true);
        CloseWithHint(player, outcome.message.c_str());
        return true;
    }
    ShowClassMenu(player, creature);
    return true;
}

} // namespace TortoiseBots
