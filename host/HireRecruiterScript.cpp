// pi-lens-ignore-file: clang:pp_file_not_found,clang:unknown_typename,clang:use_of_undeclared_identifier,clang:unknown_type_name,clang:undeclared_var_use,clang:incomplete_member_access
#include <sstream>
#include <vector>
#include "HireRecruiterScript.h"
#include "../runtime/HireProvisionService.h"
#include "../runtime/HireCost.h"
#include "../runtime/HireSpecPolicy.h"
#include "../runtime/WorldBuffPolicy.h"
#include "../runtime/WorldBuffService.h"
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

void CloseWithHint(Player* player, char const* hint);
void ShowClassMenu(Player* player, Creature* creature);
void ShowRootMenu(Player* player, Creature* creature);
void ShowWorldBuffMenu(Player* player, Creature* creature);
void ShowSaygeMenu(Player* player, Creature* creature);
void CollectBuffTargets(Player* buyer, Creature* npc, std::vector<Player*>& out);
void BuyWorldBuff(Player* player, Creature* creature, uint8_t purchase, uint32_t saygePick);

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

void ShowRootMenu(Player* player, Creature* creature)
{
    // Issue #492: the six capital recruiters split into Hire vs World
    // buffs. Every other recruiter skips this and opens the hire wizard
    // directly (unchanged behaviour outside the capitals).
    if (!IsCapitalRecruiter(creature->GetEntry()) ||
        !ShouldShowWorldBuffs(sPlayerbotAIConfig.worldBuffsEnabled,
            BotManager::Instance().IsBot(player->GetObjectGuid()),
            player->GetLevel(), sPlayerbotAIConfig.worldBuffsMinLevel))
    {
        ShowClassMenu(player, creature);
        return;
    }
    AddItem(player, GOSSIP_ICON_CHAT, "Hire bots", kWorldBuffSenderRoot, kWorldBuffRootHire);
    AddItem(player, GOSSIP_ICON_MONEY_BAG, "World buffs", kWorldBuffSenderRoot, kWorldBuffRootBuffs);
    player->PlayerTalkClass->SendGossipMenu(GossipText(player), creature->GetObjectGuid());
}

void ShowWorldBuffMenu(Player* player, Creature* creature)
{
    // Issue #492: one window for unlocks and purchases. The script owns the
    // gossip menu, so core never appends the quest list itself: after the
    // buy options, PrepareQuestMenu merges the recruiter's quests (PR2) and
    // SendGossipMenu ships gossip + quest items in one message. Only
    // unlocked buffs are offered (rewarded unlock quest for the faction);
    // anything else shows its quest instead, so there is exactly one row
    // per buff: quest or purchase, never both.
    Team team = player ? player->GetTeam() : TEAM_NONE;
    bool horde = team == HORDE;
    for (uint8_t purchase = kWorldBuffBuyRally; purchase <= kWorldBuffBuyCount; ++purchase)
    {
        uint32_t questId = PurchaseUnlockQuest(purchase, horde);
        bool unlocked = questId != 0 && player->GetQuestRewardStatus(questId);
        if (!unlocked)
            continue;
        if (purchase == kWorldBuffBuySayge)
        {
            WorldBuffPricePair prices;
            if (WorldBuffPrices(purchase, prices,
                sPlayerbotAIConfig.worldBuffsPriceBaseCopper, sPlayerbotAIConfig.worldBuffsPricePerPersonCopper,
                sPlayerbotAIConfig.worldBuffsSaygePriceBaseCopper, sPlayerbotAIConfig.worldBuffsSaygePricePerPersonCopper,
                sPlayerbotAIConfig.worldBuffsSongflowerPriceBaseCopper, sPlayerbotAIConfig.worldBuffsSongflowerPricePerPersonCopper,
                sPlayerbotAIConfig.worldBuffsSilithystPriceBaseCopper, sPlayerbotAIConfig.worldBuffsSilithystPricePerPersonCopper))
            {
                uint32_t total = WorldBuffTotalPrice(prices.baseCopper, prices.perPersonCopper, 1);
                std::ostringstream label;
                label << "Sayge's Dark Fortune... (" << ai::ChatHelper::formatMoney(total) << "+)";
                AddItem(player, GOSSIP_ICON_MONEY_BAG, label.str().c_str(), kWorldBuffSenderSayge, 0);
            }
            else
            {
                AddItem(player, GOSSIP_ICON_MONEY_BAG, "Sayge's Dark Fortune...", kWorldBuffSenderSayge, 0);
            }
            continue;
        }
        WorldBuffPricePair prices;
        if (!WorldBuffPrices(purchase, prices,
            sPlayerbotAIConfig.worldBuffsPriceBaseCopper, sPlayerbotAIConfig.worldBuffsPricePerPersonCopper,
            sPlayerbotAIConfig.worldBuffsSaygePriceBaseCopper, sPlayerbotAIConfig.worldBuffsSaygePricePerPersonCopper,
            sPlayerbotAIConfig.worldBuffsSongflowerPriceBaseCopper, sPlayerbotAIConfig.worldBuffsSongflowerPricePerPersonCopper,
            sPlayerbotAIConfig.worldBuffsSilithystPriceBaseCopper, sPlayerbotAIConfig.worldBuffsSilithystPricePerPersonCopper))
            continue;
        uint32_t total = WorldBuffTotalPrice(prices.baseCopper, prices.perPersonCopper, 1);
        std::ostringstream label;
        switch (purchase)
        {
            case kWorldBuffBuyRally: label << "Rallying Cry of the Dragonslayer"; break;
            case kWorldBuffBuyWarchief: label << "Warchief's Blessing"; break;
            case kWorldBuffBuyZandalar: label << "Spirit of Zandalar"; break;
            case kWorldBuffBuyDmPack: label << "Dire Maul Tribute (all three)"; break;
            case kWorldBuffBuySongflower: label << "Songflower Serenade"; break;
            case kWorldBuffBuySilithyst: label << "Traces of Silithyst"; break;
            default: continue;
        }
        label << " (" << ai::ChatHelper::formatMoney(total) << "+)";
        AddItem(player, GOSSIP_ICON_MONEY_BAG, label.str().c_str(), kWorldBuffSenderBuy, purchase);
    }
    AddItem(player, GOSSIP_ICON_TALK, "< Back", kWorldBuffSenderBack, kWorldBuffSenderBack);
    player->PrepareQuestMenu(creature->GetObjectGuid());
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

void BuyWorldBuff(Player* player, Creature* creature, uint8_t purchase, uint32_t saygePick)
{
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
    if (!WorldBuffPrices(purchase, prices,
        sPlayerbotAIConfig.worldBuffsPriceBaseCopper, sPlayerbotAIConfig.worldBuffsPricePerPersonCopper,
        sPlayerbotAIConfig.worldBuffsSaygePriceBaseCopper, sPlayerbotAIConfig.worldBuffsSaygePricePerPersonCopper,
        sPlayerbotAIConfig.worldBuffsSongflowerPriceBaseCopper, sPlayerbotAIConfig.worldBuffsSongflowerPricePerPersonCopper,
        sPlayerbotAIConfig.worldBuffsSilithystPriceBaseCopper, sPlayerbotAIConfig.worldBuffsSilithystPricePerPersonCopper))
    {
        ShowWorldBuffMenu(player, creature);
        return;
    }
    std::vector<Player*> targets;
    CollectBuffTargets(player, creature, targets);
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
    // Pool bots only ever see the hire wizard: no world-buff branch, no
    // purchase path. The gate lives in ShowRootMenu itself; OnSelect below
    // re-checks it per click so a stale menu cannot bypass it.
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
    bool buffsVisible = IsCapitalRecruiter(creature->GetEntry()) &&
        ShouldShowWorldBuffs(sPlayerbotAIConfig.worldBuffsEnabled,
            BotManager::Instance().IsBot(player->GetObjectGuid()),
            player->GetLevel(), sPlayerbotAIConfig.worldBuffsMinLevel);
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
            BuyWorldBuff(player, creature, purchase, saygePick);
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
