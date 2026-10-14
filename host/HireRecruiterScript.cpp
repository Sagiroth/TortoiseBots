// pi-lens-ignore-file: clang:pp_file_not_found,clang:unknown_typename,clang:use_of_undeclared_identifier,clang:unknown_type_name,clang:undeclared_var_use,clang:incomplete_member_access
#include <sstream>
#include <set>
#include <vector>
#include "HireRecruiterScript.h"
#include "../runtime/HireProvisionService.h"
#include "../runtime/HireCost.h"
#include "../runtime/HireFeePolicy.h"
#include "../runtime/HireSpecPolicy.h"
#include "../runtime/HireTierRequestPolicy.h"
#include "../runtime/HireUnlockPolicy.h"
#include "../runtime/WorldBuffPolicy.h"
#include "../runtime/WorldBuffService.h"
#include "../runtime/HireUnlockSync.h"
#include "../runtime/HireUnlockKillStore.h"
#include "HireUnlockKillAdapter.h"
#include "../runtime/BotManager.h"
#include "../ai/playerbot/playerbot.h"
#include "../ai/playerbot/ChatHelper.h"
#include "ModuleLog.h"
#include "ScriptObjects.h"
#include "GossipDef.h"
#include "Player.h"
#include "Creature.h"
#include "ObjectMgr.h"
#include "ObjectAccessor.h"
#include "World.h"
#include "WorldSession.h"
#include "Group/Group.h"
#include "Chat.h"
#include "SharedDefines.h"
#include "Log.h"
#include "Database/DatabaseEnv.h"

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

// Issue #490 PR 2 (kills): "Mercenary contracts" branch senders. The hire
// wizard owns 501-504 and world buffs 505-509; these continue the sequence
// so no menu can collide. Actions stay small (< 254) like the world-buff
// ones: the packed hire confirm/back actions (254/255) match on action
// alone and must never see them.
constexpr uint32 kContractsSenderRoot = 510;
constexpr uint32 kContractsSenderCounter = 511;
constexpr uint32 kContractsSenderBack = 512;

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

void CloseWithHint(Player* player, char const* hint);
void ShowClassMenu(Player* player, Creature* creature);
void ShowRootMenu(Player* player, Creature* creature);
void ShowWorldBuffMenu(Player* player, Creature* creature);
void ShowSaygeMenu(Player* player, Creature* creature);
// Issue #490 PR 2 (kills): contracts branch (pure math in HireUnlockSync.h).
bool ContractsVisible(Player* player, Creature* creature);
void ShowContractsMenu(Player* player, Creature* creature);
void ShowContractsCounterMenu(Player* player, Creature* creature, uint32_t creditEntry);
void FilterQuestMenu(Player* player, Creature* creature, uint32_t first, uint32_t last);
void CollectBuffTargets(Player* buyer, Creature* npc, std::vector<Player*>& out);
void BuyWorldBuff(Player* player, Creature* creature, uint32_t confirmAction);
void ShowBuyConfirm(Player* player, Creature* creature, uint32_t buyAction);

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

// Issue #490 PR 2: the fee quoted and charged for this hire at a tier: the
// base quote (same slot count as the confirm menu) times the tier
// multiplier. The confirm step and the provisioner read the same value.
uint32_t QuoteHireCostAtTier(Player* player, uint8_t tier)
{
    uint32_t base = HireCost::ForNextHire(CurrentCosts(), QuoteHireCount(player),
        player ? player->GetLevel() : 1);
    uint32_t cost = HireCostAtTier(base, tier, sPlayerbotAIConfig.hireGearTierCostMult);
    // AiPlayerbot.HireFreeMinSecurity: the quote shown is what Hire() charges.
    return player ? TortoiseBots::HireFeeCharged(cost, static_cast<int>(player->GetSession()->GetSecurity()),
        sPlayerbotAIConfig.hireFreeMinSecurity) : cost;
}

// The fee as the menus print it: "free (GM)" for an account that hires free.
std::string HireFeeLabel(Player* player, uint32_t cost)
{
    if (player && TortoiseBots::HireFeeWaived(static_cast<int>(player->GetSession()->GetSecurity()),
        sPlayerbotAIConfig.hireFreeMinSecurity))
        return "free (GM)";
    return ai::ChatHelper::formatMoney(cost);
}

// Issue #490 PR 2: "Choose equipment" step after the spec step. Lists tier 0
// up to the player's highest unlocked tier with each tier's fee for this
// hire. The packed class/race/gender/spec selection rides in the action (the
// spec step's packing); the tier rides in the sender (520+tier), so the full
// 32-bit packing needs no repacking. Back returns to the spec menu.
void ShowTierMenu(Player* player, Creature* creature, uint8 classId, uint8 race, uint8 gender,
    uint8 specIndex, uint8 highestTier);
void ShowConfirmMenu(Player* player, Creature* creature, uint8 classId, uint8 race, uint8 gender,
    uint8 specIndex, uint8 gearTier);

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

void ShowRootMenu(Player* player, Creature* creature)
{
    // Issue #492: the six capital recruiters split into Hire vs World
    // buffs. Every other recruiter skips this and opens the hire wizard
    // directly (unchanged behaviour outside the capitals). Issue #490 PR 2
    // adds "Mercenary contracts" whenever any branch beyond hiring is
    // visible, so the root shows at capitals when buffs or contracts apply.
    if (!IsCapitalRecruiter(creature->GetEntry()))
    {
        ShowClassMenu(player, creature);
        return;
    }
    bool buffsVisible = ShouldShowWorldBuffs(sPlayerbotAIConfig.worldBuffsEnabled,
        BotManager::Instance().IsBot(player->GetObjectGuid()),
        player->GetLevel(), sPlayerbotAIConfig.worldBuffsMinLevel);
    bool contractsVisible = ContractsVisible(player, creature);
    if (!buffsVisible && !contractsVisible)
    {
        ShowClassMenu(player, creature);
        return;
    }
    AddItem(player, GOSSIP_ICON_CHAT, "Hire bots", kWorldBuffSenderRoot, kWorldBuffRootHire);
    if (buffsVisible)
        AddItem(player, GOSSIP_ICON_MONEY_BAG, "World buffs", kWorldBuffSenderRoot, kWorldBuffRootBuffs);
    if (contractsVisible)
        AddItem(player, GOSSIP_ICON_BATTLE, "Mercenary contracts", kContractsSenderRoot, 1);
    player->PlayerTalkClass->SendGossipMenu(GossipText(player), creature->GetObjectGuid());
}

bool CurrentPrices(uint8_t purchase, WorldBuffPricePair& prices)
{
    return WorldBuffPrices(purchase, prices,
        sPlayerbotAIConfig.worldBuffsPriceBaseCopper, sPlayerbotAIConfig.worldBuffsPricePerPersonCopper,
        sPlayerbotAIConfig.worldBuffsSaygePriceBaseCopper, sPlayerbotAIConfig.worldBuffsSaygePricePerPersonCopper,
        sPlayerbotAIConfig.worldBuffsSongflowerPriceBaseCopper, sPlayerbotAIConfig.worldBuffsSongflowerPricePerPersonCopper,
        sPlayerbotAIConfig.worldBuffsSilithystPriceBaseCopper, sPlayerbotAIConfig.worldBuffsSilithystPricePerPersonCopper);
}

char const* PurchaseName(uint8_t purchase)
{
    switch (purchase)
    {
        case kWorldBuffBuyRally: return "Rallying Cry of the Dragonslayer";
        case kWorldBuffBuyWarchief: return "Warchief's Blessing";
        case kWorldBuffBuyZandalar: return "Spirit of Zandalar";
        case kWorldBuffBuyDmPack: return "Dire Maul Tribute (all three)";
        case kWorldBuffBuySayge: return "Sayge's Dark Fortune";
        case kWorldBuffBuySongflower: return "Songflower Serenade";
        case kWorldBuffBuySilithyst: return "Traces of Silithyst";
        default: return nullptr;
    }
}

void ShowWorldBuffMenu(Player* player, Creature* creature)
{
    // Issue #492: one window for unlocks and purchases. The script owns the
    // gossip menu, so core never appends the quest list itself: after the
    // buy options, PrepareQuestMenu merges the recruiter's quests (PR2) and
    // SendGossipMenu ships gossip + quest items in one message. Only
    // unlocked buffs are offered (rewarded unlock quest for the faction);
    // anything else shows its quest instead, so there is exactly one row
    // per buff: quest or purchase, never both. Prices are for everyone the
    // purchase would buff right now (buyer + group members in range).
    Team team = player ? player->GetTeam() : TEAM_NONE;
    bool horde = team == HORDE;
    std::vector<Player*> targets;
    CollectBuffTargets(player, creature, targets);
    uint32_t people = static_cast<uint32_t>(targets.size());
    for (uint8_t purchase = kWorldBuffBuyRally; purchase <= kWorldBuffBuyCount; ++purchase)
    {
        uint32_t questId = PurchaseUnlockQuest(purchase, horde);
        bool unlocked = questId != 0 && player->GetQuestRewardStatus(questId);
        if (!unlocked)
            continue;
        WorldBuffPricePair prices;
        char const* name = PurchaseName(purchase);
        if (!name || !CurrentPrices(purchase, prices))
            continue;
        uint32_t total = WorldBuffTotalPrice(prices.baseCopper, prices.perPersonCopper, people);
        std::ostringstream label;
        label << name << (purchase == kWorldBuffBuySayge ? "..." : "") << " (" << ai::ChatHelper::formatMoney(total) << ")";
        if (purchase == kWorldBuffBuySayge)
            AddItem(player, GOSSIP_ICON_MONEY_BAG, label.str().c_str(), kWorldBuffSenderSayge, 0);
        else
            AddItem(player, GOSSIP_ICON_MONEY_BAG, label.str().c_str(), kWorldBuffSenderBuy, purchase);
    }
    AddItem(player, GOSSIP_ICON_TALK, "< Back", kWorldBuffSenderBack, kWorldBuffSenderBack);
    // Issue #490 PR 2: the tier quests share these NPCs now. The buff page
    // keeps only the world-buff range (90000-90013); contracts own 90100+.
    FilterQuestMenu(player, creature, kWorldBuffQuestRallyA, kWorldBuffQuestSilithystH);
    player->PlayerTalkClass->SendGossipMenu(GossipText(player), creature->GetObjectGuid());
}

// Issue #490 PR 2 (kills): "Mercenary contracts" branch. Capital
// recruiters only, non-bot level-60 players, tiers enabled. ShowContractsMenu
// syncs first so the counters include kills since the quest was taken (and
// optional-map bosses), then lists one line per incomplete counter of every
// in-progress unlock quest. Short rosters render inline; a roster that would
// overflow a 1.12 gossip line opens as a sub-page naming every missing boss.
bool ContractsVisible(Player* player, Creature* creature)
{
    if (!player || !creature)
        return false;
    return ShouldShowHireContracts(sPlayerbotAIConfig.hireGearTiersEnabled,
        IsCapitalRecruiter(creature->GetEntry()),
        BotManager::Instance().IsBot(player->GetObjectGuid()),
        player->GetLevel());
}

void FilterQuestMenu(Player* player, Creature* creature, uint32_t first, uint32_t last)
{
    // PrepareQuestMenu merges every quest the NPC offers; rebuild the menu
    // keeping only the wanted id range (with their icons/status), so the
    // world-buff and contracts pages never show each other's quests.
    player->PrepareQuestMenu(creature->GetObjectGuid());
    QuestMenu& menu = player->PlayerTalkClass->GetQuestMenu();
    uint8_t count = menu.MenuItemCount();
    std::vector<std::pair<uint32_t, uint8_t>> kept;
    for (uint8_t i = 0; i < count; ++i)
    {
        QuestMenuItem const& item = menu.GetItem(i);
        if (item.m_qId >= first && item.m_qId <= last)
            kept.push_back(std::make_pair(item.m_qId, item.m_qIcon));
    }
    menu.ClearMenu();
    for (auto const& keep : kept)
        menu.AddMenuItem(keep.first, keep.second);
}

static uint8_t ContractsCounterHave(Player* player, HireUnlockCounter const& counter)
{
    uint32_t questId = HireUnlockQuestForTier(counter.tier);
    QuestStatusData const* status = questId ? player->GetQuestStatusData(questId) : nullptr;
    return status ? static_cast<uint8_t>(status->m_creatureOrGOcount[counter.slot]) : 0;
}

void ShowContractsMenu(Player* player, Creature* creature);

void ShowContractsCounterMenu(Player* player, Creature* creature, uint32_t creditEntry)
{
    // Sub-page for one counter whose roster overflows a single gossip line:
    // header with the x/N count, then one line per missing boss.
    for (HireUnlockCounter const& counter : kHireUnlockCounters)
    {
        if (counter.creditEntry != creditEntry)
            continue;
        uint32_t questId = HireUnlockQuestForTier(counter.tier);
        if (questId == 0 || player->GetQuestStatus(questId) != QUEST_STATUS_INCOMPLETE)
        {
            ShowContractsMenu(player, creature);
            return;
        }
        std::set<uint32_t> recorded = HireUnlockKillStore::Instance().KillsFor(player->GetGUIDLow());
        std::set<uint32_t> optionalMaps(
            sPlayerbotAIConfig.hireGearOptionalMaps.begin(), sPlayerbotAIConfig.hireGearOptionalMaps.end());
        std::vector<HireUnlockBoss const*> missing;
        MissingHireUnlockBosses(counter, recorded, optionalMaps, missing);
        uint8_t have = ContractsCounterHave(player, counter);
        std::ostringstream header;
        header << counter.label << ": " << uint32_t(have) << "/" << uint32_t(counter.bossCount);
        AddItem(player, GOSSIP_ICON_BATTLE, header.str().c_str(), kContractsSenderBack, kContractsSenderBack);
        uint32_t shown = 0;
        for (HireUnlockBoss const* boss : missing)
        {
            if (shown >= kMaxMenuItems - 2)
                break;
            AddItem(player, GOSSIP_ICON_DOT, boss->name, kContractsSenderBack, kContractsSenderBack);
            ++shown;
        }
        AddItem(player, GOSSIP_ICON_TALK, "< Back", kContractsSenderBack, kContractsSenderBack);
        player->PlayerTalkClass->SendGossipMenu(GossipText(player), creature->GetObjectGuid());
        return;
    }
    ShowContractsMenu(player, creature);
}

void ShowContractsMenu(Player* player, Creature* creature)
{
    // Refresh counters before rendering: kills since the page was last
    // opened (and optional-map bosses on accept) show immediately.
    HireUnlockKillAdapter::SyncCounters(player);
    std::set<uint32_t> recorded = HireUnlockKillStore::Instance().KillsFor(player->GetGUIDLow());
    std::set<uint32_t> optionalMaps(
        sPlayerbotAIConfig.hireGearOptionalMaps.begin(), sPlayerbotAIConfig.hireGearOptionalMaps.end());
    uint32_t shown = 0;
    for (HireUnlockCounter const& counter : kHireUnlockCounters)
    {
        uint32_t questId = HireUnlockQuestForTier(counter.tier);
        if (questId == 0 || player->GetQuestStatus(questId) != QUEST_STATUS_INCOMPLETE)
            continue;
        uint8_t have = ContractsCounterHave(player, counter);
        if (have >= counter.bossCount)
            continue;
        std::string line;
        if (HireUnlockCounterLine(counter, have, recorded, optionalMaps, line))
            AddItem(player, GOSSIP_ICON_BATTLE, line.c_str(), kContractsSenderBack, kContractsSenderBack);
        else
            AddItem(player, GOSSIP_ICON_BATTLE, line.c_str(), kContractsSenderCounter, counter.creditEntry);
        if (++shown >= kMaxMenuItems - 2)
            break;
    }
    // This branch's quest list only: tier quests 90100-90106. The buff page
    // keeps 90000-90013 the same way (FilterQuestMenu above).
    FilterQuestMenu(player, creature, kHireUnlockQuestFirst, kHireUnlockQuestLast);
    AddItem(player, GOSSIP_ICON_TALK, "< Back", kContractsSenderBack, kContractsSenderBack);
    player->PlayerTalkClass->SendGossipMenu(GossipText(player), creature->GetObjectGuid());
}

// 1.12 gossip has no confirmation popup, so a buy click opens this page:
// the exact total for everyone who would be buffed, then Confirm or Back.
// The quoted head count rides in the confirm action; if the group changed
// before Confirm, BuyWorldBuff re-quotes instead of charging a new price.
void ShowBuyConfirm(Player* player, Creature* creature, uint32_t buyAction)
{
    uint8_t purchase = DecodeBuyIndex(buyAction);
    uint32_t saygePick = DecodeSaygePick(buyAction);
    WorldBuffPricePair prices;
    char const* name = PurchaseName(purchase);
    if (!name || !CurrentPrices(purchase, prices))
    {
        ShowWorldBuffMenu(player, creature);
        return;
    }
    std::vector<Player*> targets;
    CollectBuffTargets(player, creature, targets);
    uint32_t people = static_cast<uint32_t>(targets.size());
    uint32_t total = WorldBuffTotalPrice(prices.baseCopper, prices.perPersonCopper, people);
    std::ostringstream label;
    label << "Pay " << ai::ChatHelper::formatMoney(total) << " for " << name;
    if (purchase == kWorldBuffBuySayge)
    {
        uint32_t count = 0;
        WorldBuffSaygePick const* picks = WorldBuffSaygePicks(count);
        if (saygePick >= 1 && saygePick <= count)
            label << " of " << picks[saygePick - 1].label;
    }
    if (people > 1)
        label << " on you and " << (people - 1) << (people == 2 ? " group member" : " group members") << " nearby";
    else
        label << " on you";
    label << ". Confirm?";
    AddItem(player, GOSSIP_ICON_MONEY_BAG, label.str().c_str(), kWorldBuffSenderConfirm, EncodeConfirmAction(buyAction, people));
    AddItem(player, GOSSIP_ICON_TALK, "< Back", kWorldBuffSenderBack, kWorldBuffSenderSayge);
    player->PlayerTalkClass->SendGossipMenu(GossipText(player), creature->GetObjectGuid());
}

void ShowSaygeMenu(Player* player, Creature* creature)
{
    uint32_t count = 0;
    WorldBuffSaygePick const* picks = WorldBuffSaygePicks(count);
    for (uint32_t i = 0; i < count; ++i)
        AddItem(player, GOSSIP_ICON_MONEY_BAG, picks[i].label, kWorldBuffSenderBuy, EncodeSaygeAction(kWorldBuffBuySayge, i + 1));
    AddItem(player, GOSSIP_ICON_TALK, "< Back", kWorldBuffSenderBack, kWorldBuffSenderSayge);
    player->PlayerTalkClass->SendGossipMenu(GossipText(player), creature->GetObjectGuid());
}

// Buyer + live in-world group/raid members within 40 yd of the NPC. Random
// bots standing at the bank are never in the buyer's group, so they get
// nothing; a member 100 yd away is out of range too.
void CollectBuffTargets(Player* buyer, Creature* npc, std::vector<Player*>& out)
{
    out.clear();
    if (!buyer || !npc)
        return;
    out.push_back(buyer);
    Group* group = buyer->GetGroup();
    if (!group)
        return;
    for (auto const& slot : group->GetMemberSlots())
    {
        if (slot.guid == buyer->GetObjectGuid())
            continue;
        Player* member = sObjectAccessor.FindPlayer(slot.guid);
        if (!member || !member->IsInWorld() || !member->IsAlive())
            continue;
        if (!npc->IsWithinDistInMap(member, 40.0f))
            continue;
        out.push_back(member);
    }
}

void BuyWorldBuff(Player* player, Creature* creature, uint32_t confirmAction)
{
    uint32_t buyAction = DecodeConfirmBuyAction(confirmAction);
    uint8_t purchase = DecodeBuyIndex(buyAction);
    uint32_t saygePick = DecodeSaygePick(buyAction);
    if (!IsValidWorldBuffPurchase(purchase))
    {
        ShowWorldBuffMenu(player, creature);
        return;
    }
    bool horde = player->GetTeam() == HORDE;
    uint32_t questId = PurchaseUnlockQuest(purchase, horde);
    if (questId == 0 || !player->GetQuestRewardStatus(questId))
    {
        CloseWithHint(player, "That blessing is not unlocked yet. Complete its unlock quest first.");
        return;
    }
    uint32_t saygeSpell = 0;
    if (purchase == kWorldBuffBuySayge)
    {
        uint32_t count = 0;
        WorldBuffSaygePick const* picks = WorldBuffSaygePicks(count);
        if (saygePick < 1 || saygePick > count)
        {
            ShowSaygeMenu(player, creature);
            return;
        }
        saygeSpell = picks[saygePick - 1].spellId;
    }
    std::vector<uint32_t> apply;
    std::vector<uint32_t> strip;
    if (!WorldBuffSpells(purchase, saygeSpell, apply, strip))
    {
        ShowWorldBuffMenu(player, creature);
        return;
    }
    WorldBuffPricePair prices;
    if (!CurrentPrices(purchase, prices))
    {
        ShowWorldBuffMenu(player, creature);
        return;
    }
    std::vector<Player*> targets;
    CollectBuffTargets(player, creature, targets);
    // Someone joined or walked off since the quote: show the new price
    // instead of charging one the player never saw.
    if (targets.size() != DecodeConfirmCount(confirmAction))
    {
        ShowBuyConfirm(player, creature, buyAction);
        return;
    }
    uint32_t total = WorldBuffTotalPrice(prices.baseCopper, prices.perPersonCopper,
        static_cast<uint32_t>(targets.size()));
    if (player->GetMoney() < total)
    {
        std::ostringstream hint;
        hint << "You cannot afford that blessing (" << ai::ChatHelper::formatMoney(total) << ").";
        CloseWithHint(player, hint.str().c_str());
        return;
    }
    player->ModifyMoney(-static_cast<int32>(total));
    player->SaveToDB();
    // The NPC is the caster (AddAura takes duration from spell data, so the
    // original 2h/1h/30m and death rules apply unchanged). Purchased auras
    // therefore never credit aura unlocks: PR5 ignores recruiter casters.
    // Refund only when nobody got the blessing; otherwise the group keeps
    // auras that were paid for.
    bool anyBuffed = false;
    for (Player* target : targets)
    {
        if (!target || !target->IsInWorld() || !target->IsAlive())
            continue;
        for (uint32_t stripId : strip)
            target->RemoveAurasDueToSpellByCancel(stripId);
        bool shown = false;
        for (uint32_t spellId : apply)
        {
            if (!target->AddAura(spellId, 0, creature))
                continue;
            anyBuffed = true;
            // Visual only: a spell-go packet plays the original cast/impact
            // effect from the recruiter onto this target without running the
            // spell (its 100-yd area would buff the whole capital). One per
            // target, so the DM pack does not triple the effect.
            if (!shown)
            {
                creature->SendSpellGo(target, spellId);
                shown = true;
            }
        }
    }
    if (!anyBuffed)
    {
        player->ModifyMoney(static_cast<int32>(total));
        player->SaveToDB();
        CloseWithHint(player, "The blessing could not be applied. Your gold has been refunded.");
        return;
    }
    player->PlayerTalkClass->CloseGossip();
    ChatHandler(player).PSendSysMessage("The recruiter's criers raise the blessing over your company.");
}

void ShowTierMenu(Player* player, Creature* creature, uint8 classId, uint8 race, uint8 gender,
    uint8 specIndex, uint8 highestTier)
{
    uint32 packed = (uint32(classId) << 24) | (uint32(race) << 16) | (uint32(gender) << 8) | specIndex;
    for (uint8_t tier = 0; tier <= highestTier; ++tier)
    {
        std::ostringstream label;
        label << HireTierKitName(tier) << " (" << HireFeeLabel(player, QuoteHireCostAtTier(player, tier)) << ")";
        AddItem(player, GOSSIP_ICON_BATTLE, label.str().c_str(), HireTierStepSender(tier), packed);
    }
    AddItem(player, GOSSIP_ICON_TALK, "< Back", kHireTierBackSender, packed);
    player->PlayerTalkClass->SendGossipMenu(GossipText(player), creature->GetObjectGuid());
}

void ShowConfirmMenu(Player* player, Creature* creature, uint8 classId, uint8 race, uint8 gender, uint8 specIndex)
{
    // Old path for players with nothing unlocked above tier 0 (and below
    // 60): the equipment step is skipped, the hire goes at tier 0.
    ShowConfirmMenu(player, creature, classId, race, gender, specIndex, kHireGearTierBase);
}

void ShowConfirmMenu(Player* player, Creature* creature, uint8 classId, uint8 race, uint8 gender,
    uint8 specIndex, uint8 gearTier)
{
    uint32_t specCount = 0;
    HireSpecOption const* specs = HireSpecOptions(classId, specCount);
    if (!specs || specIndex >= specCount || gearTier > kHireGearTierMax)
    {
        ShowSpecMenu(player, creature, classId, race, gender);
        return;
    }
    uint32_t cost = QuoteHireCostAtTier(player, gearTier);
    std::ostringstream label;
    label << "Hire " << RaceName(race) << " " << ClassName(classId) << " (" << specs[specIndex].label << ")";
    if (gearTier > 0)
        label << " in " << HireTierKitName(gearTier);
    label << " for " << HireFeeLabel(player, cost) << ". Confirm?";
    // The tier rides in the sender (530+tier); the packed selection rides in
    // the action. Back returns to the equipment step when the player has a
    // choice, else to the spec menu (unchanged old path).
    uint32 packed = (uint32(classId) << 24) | (uint32(race) << 16) | (uint32(gender) << 8) | specIndex;
    AddItem(player, GOSSIP_ICON_MONEY_BAG, label.str().c_str(), HireTierConfirmSender(gearTier), packed);
    AddItem(player, GOSSIP_ICON_TALK, "< Back", kHireTierBackSender, packed);
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
    // Pool bots only ever see the hire wizard: no world-buff branch, no
    // contracts branch, no purchase path. The gate lives in ShowRootMenu
    // itself; OnSelect below re-checks it per click so a stale menu cannot
    // bypass it.
    ShowRootMenu(player, creature);
    return true;
}

bool HireRecruiterScript::OnSelect(Player* player, Creature* creature, uint32_t sender, uint32_t action)
{
    if (!player || !creature)
        return false;
    player->PlayerTalkClass->ClearMenus();

    // Issue #492: world-buff branch senders (505-508), matched before the
    // hire wizard and its class-menu fallback. The gate is re-checked per
    // click, so a stale menu cannot bypass a flag flip or a bot check that
    // happened after OnHello. Purchases: charge first (hire pattern), then
    // AddAura on the buyer + live in-world group members within 40 yd of
    // the NPC, refund on every failure. Buffs are never cast as spells: the
    // originals are 100-yd area effects that would hit the whole capital.
    bool capital = IsCapitalRecruiter(creature->GetEntry());
    bool buffsVisible = capital &&
        ShouldShowWorldBuffs(sPlayerbotAIConfig.worldBuffsEnabled,
            BotManager::Instance().IsBot(player->GetObjectGuid()),
            player->GetLevel(), sPlayerbotAIConfig.worldBuffsMinLevel);
    // Issue #490 PR 2: contracts have their own gate (tiers + capital +
    // real level-60 player), independent of the world-buff flag: the root
    // shows whenever either branch is visible.
    if (capital && ContractsVisible(player, creature))
    {
        if (sender == kContractsSenderRoot)
        {
            ShowContractsMenu(player, creature);
            return true;
        }
        if (sender == kContractsSenderCounter)
        {
            ShowContractsCounterMenu(player, creature, action);
            return true;
        }
        if (sender == kContractsSenderBack)
        {
            ShowContractsMenu(player, creature);
            return true;
        }
    }
    if (buffsVisible)
    {
        if (sender == kWorldBuffSenderRoot)
        {
            if (action == kWorldBuffRootHire)
            {
                ShowClassMenu(player, creature);
                return true;
            }
            if (action == kWorldBuffRootBuffs)
            {
                ShowWorldBuffMenu(player, creature);
                return true;
            }
            ShowRootMenu(player, creature);
            return true;
        }
        if (sender == kWorldBuffSenderSayge)
        {
            if (action == 0)
            {
                ShowSaygeMenu(player, creature);
                return true;
            }
            ShowWorldBuffMenu(player, creature);
            return true;
        }
        if (sender == kWorldBuffSenderBuy)
        {
            uint8_t purchase = DecodeBuyIndex(action);
            uint32_t saygePick = DecodeSaygePick(action);
            if (purchase == kWorldBuffBuySayge && saygePick == 0)
            {
                ShowSaygeMenu(player, creature);
                return true;
            }
            ShowBuyConfirm(player, creature, action);
            return true;
        }
        if (sender == kWorldBuffSenderConfirm)
        {
            BuyWorldBuff(player, creature, action);
            return true;
        }
        if (sender == kWorldBuffSenderBack)
        {
            if (action == kWorldBuffSenderSayge)
            {
                ShowWorldBuffMenu(player, creature);
                return true;
            }
            ShowRootMenu(player, creature);
            return true;
        }
    }

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
        // Issue #490 PR 2: equipment step between spec and confirm. One
        // unlock lookup per page; skipped entirely for players with nothing
        // above tier 0 (old wizard unchanged).
        uint8_t highest = HireProvisionService::Instance().HighestUnlockedHireTierFor(player);
        if (HireTierStepVisible(sPlayerbotAIConfig.hireGearTiersEnabled, player->GetLevel(), highest))
        {
            ShowTierMenu(player, creature, classId, race, gender, static_cast<uint8>(low), highest);
            return true;
        }
        ShowConfirmMenu(player, creature, classId, race, gender, static_cast<uint8>(low));
        return true;
    }
    // Issue #490 PR 2: the tier step and its confirm. The tier rides in the
    // sender, the packed selection in the action. The unlock is re-validated
    // at confirm (Hire() checks too), so a stale menu cannot hire above it.
    uint8_t tierPick = 0;
    if (DecodeHireTierStepSender(sender, tierPick) || DecodeHireTierConfirmSender(sender, tierPick))
    {
        uint8 classId = static_cast<uint8>((action >> 24) & 0xFF);
        uint8 race = static_cast<uint8>((action >> 16) & 0xFF);
        uint8 gender = static_cast<uint8>((action >> 8) & 0xFF);
        uint8 specIndex = static_cast<uint8>(action & 0xFF);
        uint32_t specCount = 0;
        HireSpecOption const* specs = HireSpecOptions(classId, specCount);
        if (!KnownHireClass(classId) || !specs || specIndex >= specCount ||
            !sObjectMgr.GetPlayerInfo(race, classId) ||
            (gender != GENDER_MALE && gender != GENDER_FEMALE))
        {
            ShowClassMenu(player, creature);
            return true;
        }
        uint8_t highest = HireProvisionService::Instance().HighestUnlockedHireTierFor(player);
        bool visible = HireTierStepVisible(sPlayerbotAIConfig.hireGearTiersEnabled,
            player->GetLevel(), highest);
        if (tierPick > highest || (!visible && tierPick > 0))
        {
            // Stale menu (unlock revoked, tiers disabled, level changed):
            // fall back to the tier-0 confirm, never hire above the unlock.
            ShowConfirmMenu(player, creature, classId, race, gender, specIndex);
            return true;
        }
        if (DecodeHireTierConfirmSender(sender, tierPick))
        {
            HireSelection sel;
            sel.classId = classId;
            sel.race = race;
            sel.gender = gender;
            sel.role = specs[specIndex].role;
            sel.specIndex = static_cast<int>(specIndex);
            sel.gearTier = tierPick;
            HireOutcome outcome = HireProvisionService::Instance().Hire(player, sel, true);
            CloseWithHint(player, outcome.message.c_str());
            return true;
        }
        if (!visible)
        {
            ShowConfirmMenu(player, creature, classId, race, gender, specIndex);
            return true;
        }
        ShowConfirmMenu(player, creature, classId, race, gender, specIndex, tierPick);
    }
    // Tier-step back: the packed selection rides in the action. With a real
    // choice, back returns to the equipment step; otherwise the spec menu.
    if (sender == kHireTierBackSender)
    {
        uint8 classId = static_cast<uint8>((action >> 24) & 0xFF);
        uint8 race = static_cast<uint8>((action >> 16) & 0xFF);
        uint8 gender = static_cast<uint8>((action >> 8) & 0xFF);
        uint8 specIndex = static_cast<uint8>(action & 0xFF);
        uint32_t specCount = 0;
        HireSpecOption const* specs = HireSpecOptions(classId, specCount);
        uint8_t highest = HireProvisionService::Instance().HighestUnlockedHireTierFor(player);
        if (specs && specIndex < specCount &&
            HireTierStepVisible(sPlayerbotAIConfig.hireGearTiersEnabled, player->GetLevel(), highest))
        {
            ShowTierMenu(player, creature, classId, race, gender, specIndex, highest);
            return true;
        }
        if (specs && specIndex < specCount)
        {
            ShowSpecMenu(player, creature, classId, race, gender);
            return true;
        }
        ShowClassMenu(player, creature);
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

bool HireRecruiterScript::OnQuestAccept(Player* player, Creature* /*creature*/, Quest const* quest)
{
    if (!player || !quest)
        return false;
    // Quest accept scripts run from Player::AddQuest after the log slot is
    // set; returning false lets the core finish normally. Sync here restores
    // progress after abandon + retake and credits optional-map bosses.
    if (HireUnlockTierForQuest(quest->GetQuestId()) == 0)
        return false;
    if (!sPlayerbotAIConfig.hireGearTiersEnabled)
        return false;
    HireUnlockKillAdapter::SyncCounters(player);
    return false;
}

void HireRecruiterScript::SyncUnlockFees()
{
    uint32 changed = 0;
    for (uint32 questId = kWorldBuffQuestRallyA; questId <= kWorldBuffQuestSilithystH; ++questId)
    {
        uint32 fee = IsRaidUnlockQuest(questId) ? sPlayerbotAIConfig.worldBuffsUnlockFeeRaidCopper
                                                : sPlayerbotAIConfig.worldBuffsUnlockFeeCopper;
        auto result = WorldDatabase.PQuery("SELECT RewOrReqMoney, Objectives FROM quest_template WHERE entry = %u", questId);
        if (!result)
            continue;
        Field* fields = result->Fetch();
        std::string objectives = WorldBuffObjectivesWithFee(fields[1].GetCppString(), fee);
        if (fields[0].GetInt32() == -static_cast<int32>(fee) && objectives == fields[1].GetCppString())
            continue;
        WorldDatabase.escape_string(objectives);
        WorldDatabase.PExecute("UPDATE quest_template SET RewOrReqMoney = %d, Objectives = '%s' WHERE entry = %u",
            -static_cast<int32>(fee), objectives.c_str(), questId);
        ++changed;
    }
    if (changed)
        TB_LOG_BASIC("World-buff unlock fees changed on %u quests; restart the server to apply them", changed);
}

void HireRecruiterScript::SyncHireUnlockChain()
{
    // Same pattern as SyncUnlockFees: the quests are already loaded, so the
    // write lands in the DB and is live after the next restart.
    uint32 changed = 0;
    for (uint8_t tier = 1; tier <= kHireGearTierMax; ++tier)
    {
        uint32 questId = HireUnlockQuestForTier(tier);
        int32 want = HireTierChainPrevQuest(tier, sPlayerbotAIConfig.hireGearAllQuestsAvailable);
        auto result = WorldDatabase.PQuery("SELECT PrevQuestId FROM quest_template WHERE entry = %u", questId);
        if (!result)
            continue;
        if (result->Fetch()[0].GetInt32() == want)
            continue;
        WorldDatabase.PExecute("UPDATE quest_template SET PrevQuestId = %d WHERE entry = %u", want, questId);
        ++changed;
    }
    if (changed)
        TB_LOG_BASIC("Hire unlock quest chain changed on %u quests; restart the server to apply them", changed);
}
} // namespace TortoiseBots
