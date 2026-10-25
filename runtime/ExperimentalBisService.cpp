#include "ExperimentalBisService.h"
#include "PlayerbotAIStorage.h"
#include "HireLifecycle.h"
#include "../ai/playerbot/playerbot.h"
#include "../ai/playerbot/PlayerbotFactory.h"
#include "../ai/playerbot/AiFactory.h"
#include "Objects/Item.h"
#include "ObjectMgr.h"
#include <array>
#include <memory>
#include <sstream>

namespace TortoiseBots {
namespace {
constexpr std::array<uint8,17> slots = {0,1,2,14,4,8,9,5,6,7,10,11,12,13,15,16,17};
struct Profile { char const* name; uint8 cls, tab, role; std::array<uint32,17> items; };
Profile const profiles[] = {
    {"Protection Warrior", 1, 2, 1, {22418,22732,22419,22938,22416,22423,21674,22422,22417,22420,23059,21601,19341,19431,19019,23043,22811}},
    {"Fury Warrior", 1, 1, 4, {12640,23053,21330,23045,23000,22936,21581,23219,23068,19387,23038,18821,22954,23041,23054,23577,22811}},
    {"Arcane Mage", 8, 0, 4, {22498,23057,22983,23050,22496,23021,21585,22730,23070,22500,23062,21709,19379,19339,22807,23049,22821}},
    {"Holy Priest", 5, 1, 2, {22514,23036,22515,22960,16923,16926,22517,21582,16922,22516,23061,22939,23047,19395,23056,23048,23009}},
    {"Restoration Druid", 11, 2, 2, {19132,21712,18810,22960,22488,21604,22493,21582,19385,19437,19382,22939,19395,23047,23056,23048,22399}},
    {"Holy Paladin", 2, 0, 2, {22428,21712,22429,22960,22425,21604,20264,21582,21667,22430,23066,19382,23047,19395,23056,22819,23006}},
};
}
std::string InspectExperimentalBis(Player* bot)
{
    auto* ai = bot ? PlayerbotAIStorage::Instance().GetAI(bot) : nullptr;
    if (!ai) return "companion AI unavailable";
    int tab = AiFactory::GetPlayerSpecTab(bot);
    auto points = AiFactory::GetPlayerSpecTabs(bot);
    uint8 role = ai->GetForcedRole() ? ai->GetForcedRole() : AiFactory::GetPlayerRoles(bot->GetClass(), tab);
    static char const* specs[12][3] = {{},{"Arms","Fury","Protection"},{"Holy","Protection","Retribution"},
        {"Beast Mastery","Marksmanship","Survival"},{"Assassination","Combat","Subtlety"},
        {"Discipline","Holy","Shadow"},{},{"Elemental","Enhancement","Restoration"},
        {"Arcane","Fire","Frost"},{"Affliction","Demonology","Destruction"},{},{"Balance","Feral","Restoration"}};
    std::ostringstream out;
    bool unique = points[tab] > 0 && points[tab] > points[(tab+1)%3] && points[tab] > points[(tab+2)%3];
    out << "spec=" << (unique && bot->GetClass()<12 ? specs[bot->GetClass()][tab] : "ambiguous")
        << " (" << points[0] << "/" << points[1] << "/" << points[2] << "); role="
        << (role==1 ? "tank" : role==2 ? "healer" : role==4 ? "dps" : "mixed");
    for (auto const& profile : profiles)
    {
        if (!unique || profile.cls != bot->GetClass() || profile.tab != tab || profile.role != role) continue;
        unsigned matched=0;
        for (size_t i=0;i<slots.size();++i)
        {
            Item* item=bot->GetItemByPos(INVENTORY_SLOT_BAG_0,slots[i]);
            if (item && item->GetEntry()==profile.items[i]) ++matched;
        }
        out << "; locked profile=" << profile.name << "; gear=" << matched << "/17";
        return out.str();
    }
    out << "; locked profile=unsupported or conflicting role";
    return out.str();
}

int ApplyExperimentalBis(Player* bot, std::string& report)
{
    if (!bot) { report = "no companion"; return 0; }
    auto* ai = PlayerbotAIStorage::Instance().GetAI(bot);
    if (!bot || !ai || !HireLifecycle::Instance().IsHired(bot->GetObjectGuid()))
    { report = "not a ready hired companion"; return 0; }
    if (bot->GetLevel() != 60 || !bot->IsAlive() || !bot->IsInWorld() ||
        bot->IsInCombat() || bot->IsBeingTeleported() || bot->IsTaxiFlying() || bot->IsNonMeleeSpellCasted(false))
    { report = "requires level 60, alive, out of combat, not casting/travelling"; return 0; }
    auto tabs = AiFactory::GetPlayerSpecTabs(bot);
    int tab = AiFactory::GetPlayerSpecTab(bot);
    if (tabs[tab] == 0 || tabs[tab] == tabs[(tab+1)%3] || tabs[tab] == tabs[(tab+2)%3])
    { report = "ambiguous or missing talent specialization"; return 0; }
    Profile const* profile = nullptr;
    for (auto const& candidate : profiles)
        if (candidate.cls == bot->GetClass() && candidate.tab == tab)
            profile = &candidate;
    if (!profile || (ai->GetForcedRole() && ai->GetForcedRole() != profile->role) ||
        (!ai->GetForcedRole() && AiFactory::GetPlayerRoles(bot->GetClass(), tab) != profile->role))
    { report = "unsupported specialization or conflicting role; unchanged"; return 0; }

    // Resolve every definition before modifying skills or equipment.
    for (size_t i=0; i<slots.size(); ++i)
    {
        auto const* proto = sObjectMgr.GetItemPrototype(profile->items[i]);
        if (!proto || !(proto->AllowableClass & bot->GetClassMask()) || proto->RequiredLevel > 60)
        { report = "invalid item " + std::to_string(profile->items[i]) + " in slot " + std::to_string(slots[i]); return -1; }
    }
    PlayerbotFactory factory(bot, 60);
    factory.InitSkills();
    PlayerbotFactory::EnsureSkillRewardedSpells(bot);
    bot->UpdateSkillsToMaxSkillsForLevel();
    std::array<std::unique_ptr<Item>,17> staged;
    for (size_t i=0; i<slots.size(); ++i)
    {
        staged[i].reset(Item::CreateItem(profile->items[i], 1, bot));
        if (!staged[i]) { report = "could not create item " + std::to_string(profile->items[i]); return -1; }
    }
    // Hold old items in memory until the full set has passed native equip checks.
    // No SQL injection, save, or asynchronous yield occurs inside this swap.
    std::array<Item*,17> old{};
    for (size_t i=0; i<slots.size(); ++i)
    {
        old[i] = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slots[i]);
        if (old[i]) bot->RemoveItem(INVENTORY_SLOT_BAG_0, slots[i], true);
    }
    size_t equipped = 0;
    for (; equipped<slots.size(); ++equipped)
    {
        uint16 dest = 0;
        InventoryResult error = bot->CanEquipItem(slots[equipped], dest, staged[equipped].get(), false);
        if (error != EQUIP_ERR_OK || (dest & 255) != slots[equipped])
        {
            report = "item " + std::to_string(profile->items[equipped]) + " slot " + std::to_string(slots[equipped]) +
                " rejected (code " + std::to_string(error) + "); old equipment restored";
            break;
        }
        bot->EquipItem(dest, staged[equipped].release(), true);
    }
    if (equipped != slots.size())
    {
        for (size_t i=0; i<equipped; ++i) bot->DestroyItem(INVENTORY_SLOT_BAG_0, slots[i], true);
        for (size_t i=0; i<slots.size(); ++i)
            if (old[i]) bot->EquipItem((INVENTORY_SLOT_BAG_0 << 8) | slots[i], old[i], true);
        bot->SaveToDB();
        return -1;
    }
    for (Item* item : old)
    {
        if (!item) continue;
        bot->ItemRemovedQuestCheck(item->GetEntry(), item->GetCount());
        if (item->IsInWorld()) { item->RemoveFromWorld(); item->DestroyForPlayer(bot); }
        item->SetState(ITEM_REMOVED, bot);
    }
    uint32 omitted = 0;
    auto enchant = [&](uint8 slot, uint32 spell) {
        Item* item = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
        if (!item) { ++omitted; return; }
        ai->EnchantItemT(spell, slot, item);
        if (!item->GetEnchantmentId(PERM_ENCHANTMENT_SLOT)) ++omitted;
    };
    bool healer = profile->role == ai::BOT_ROLE_HEALER;
    bool tank = profile->role == ai::BOT_ROLE_TANK;
    enchant(EQUIPMENT_SLOT_CHEST, 20025); // +4 all stats
    enchant(EQUIPMENT_SLOT_BACK, tank ? 25086 : 25084); // dodge / subtlety
    enchant(EQUIPMENT_SLOT_WRISTS, tank ? 20011 : healer ? 23802 : bot->GetClass()==CLASS_WARRIOR ? 20010 : 20008);
    enchant(EQUIPMENT_SLOT_FEET, 13890); // minor speed for unbuffed raid movement
    if (bot->GetClass()==CLASS_WARRIOR) enchant(EQUIPMENT_SLOT_HANDS, tank ? 25072 : 25080);
    else if (healer) enchant(EQUIPMENT_SLOT_HANDS, 25079);
    enchant(EQUIPMENT_SLOT_MAINHAND, healer ? 22750 : bot->GetClass()==CLASS_WARRIOR ? 20034 : 22749);
    if (tank || bot->GetClass()==CLASS_PALADIN) enchant(EQUIPMENT_SLOT_OFFHAND, 20017);
    else if (bot->GetClass()==CLASS_WARRIOR) enchant(EQUIPMENT_SLOT_OFFHAND, 20034);
    factory.InitAmmo();
    ai->UpdateTalentSpec();
    bot->UpdateAllStats();
    bot->SaveToDB();
    report = std::string(profile->name) + " applied (17 slots); enchant failures=" + std::to_string(omitted) +
        "; head/shoulder/leg/ranged enchants omitted";
    return 1;
}
}