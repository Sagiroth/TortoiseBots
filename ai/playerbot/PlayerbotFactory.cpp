
#include "playerbot/playerbot.h"
#include "playerbot/PlayerbotFactory.h"
#include "playerbot/PerformanceMonitor.h"

#include "Database/SQLStorages.h"
#include "Objects/ItemPrototype.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "AccountMgr.h"
#include "Database/DBCStore.h"
#include "SharedDefines.h"
#include "RandomItemMgr.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/AiFactory.h"
#include "Guild/GuildMgr.h"

#include "strategy/ItemVisitors.h"
#include "strategy/values/MountValues.h"

using namespace ai;

namespace
{
std::vector<std::pair<uint32, uint32>> const& CollectionMounts()
{
    static std::vector<std::pair<uint32, uint32>> mounts;
    static bool loaded = false;
    if (loaded)
        return mounts;

    loaded = true;
    std::unique_ptr<QueryResult> result(WorldDatabase.Query("SELECT itemId, spellId FROM collection_mount"));
    if (!result)
        return mounts;

    do
    {
        Field* fields = result->Fetch();
        mounts.emplace_back(fields[0].GetUInt32(), fields[1].GetUInt32());
    } while (result->NextRow());

    return mounts;
}
}


void PlayerbotFactory::MakeComplete()
{
    if (!bot || !ai)
        return;
    // Talents first: roll once, then grow the same tree as points arrive.
    // SelectPremadeSpecNo is idempotent-safe here only when no spec is
    // stored yet; otherwise keep the stored build (restart-safe fallback
    // reads spent talents, never specNo).
    if (!sRandomBotFacade.GetValue(bot, "specNo"))
        SelectPremadeSpecNo();
    ai->DoSpecificAction("auto talents");
    // Spells second, each gated by its own knob inside the action.
    ai->DoSpecificAction("auto learn spell");
    // Skills on thresholds every level (armor steps around 40 included).
    InitSkills();
    // Gear last and incremental only — never wipe earned gear.
    // Enchants apply per item inside InitEquipment (ApplyBestEnchant) plus
    // the EnchantEquipment() sweep below; both read the candidate pool.
    InitEquipment(true, false);
    // Missing kit, idempotent: bags (level-tier vendor, quiver intact),
    // profession tools (pick/knife/hammer/spanner/pole), mounts 40/60,
    // fresh-seed money (once, not on re-seed), reagents, potions, food,
    // class oils/stones/poisons and level-tier bandages. Every helper
    // checks current state first, so re-seed is a safe backstop.
    InitBags();
    InitInventorySkill();
    InitMounts();
    SeedFreshMoney();
    InitAmmo();
    InitReagents();
    InitPotions();
    InitFood();
    AddConsumables();
    InitBandages();
    // Two equips can land in one slot: the starter kit row never enters this
    // session's item map, so DestroyItem cannot see it and the loser row
    // survives in the DB (3243 live duplicates found). Keep only the best
    // item per contested slot and drop the loser plus its orphaned instance
    // row — two indexed statements, idempotent.
    PruneDuplicateEquipRows();
    EnchantEquipment();
    bot->SaveToDB();
}

void PlayerbotFactory::PruneDuplicateEquipRows()
{
    if (!bot)
        return;

    uint32 guid = bot->GetGUIDLow();
    CharacterDatabase.PExecute(
        "DELETE ci FROM character_inventory ci WHERE ci.guid = %u AND ci.bag = 0 "
        "AND ci.slot IN (4,6,7,15,16,17) "
        "AND ci.item NOT IN (SELECT item FROM (SELECT x.item, "
        "ROW_NUMBER() OVER (PARTITION BY x.slot ORDER BY it.item_level DESC, it.Quality DESC, x.item DESC) rn "
        "FROM character_inventory x JOIN tw_world.item_template it ON it.entry = x.item_template "
        "WHERE x.guid = %u AND x.bag = 0 AND x.slot IN (4,6,7,15,16,17)) t WHERE t.rn = 1)",
        guid, guid);
    CharacterDatabase.PExecute(
        "DELETE ii FROM item_instance ii LEFT JOIN character_inventory ci ON ci.item = ii.guid "
        "WHERE ci.item IS NULL AND ii.owner_guid = %u",
        guid);
}

// Issue #192: spells + skills + incremental gear for a hired companion.
// Public wrapper around the private init steps so the provisioner never
// touches wiping paths. Talents are owned by the provisioner (role-matching
// premade build), so this covers only the level-bound follow-ups.
void PlayerbotFactory::ProvisionSpellsAndGear()
{
    if (!bot)
        return;
    InitAllSkills();
    InitAvailableSpells();
    InitSpecialSpells();
    InitEquipment(true, false);
    // Field kit, idempotent like the pool path: bags, tools, mounts,
    // ammo, reagents, potions, food, class consumables and bandages.
    // Mounts/riding are level-gated inside (40/60); tools/bags only fill
    // gaps so a companion hired at any level arrives with a usable kit.
    InitBags();
    InitInventorySkill();
    InitMounts();
    InitAmmo();
    InitReagents();
    InitPotions();
    InitFood();
    AddConsumables();
    InitBandages();
    // Hunter pets (level 10+) and warlock summons need their pet objects;
    // InitPet is a no-op for other classes.
    if ((bot->GetClass() == CLASS_HUNTER && bot->GetLevel() >= 10) ||
        bot->GetClass() == CLASS_WARLOCK)
    {
        InitPet();
        InitPetSpells();
    }
    EnchantEquipment();
    bot->SaveToDB();
}


void PlayerbotFactory::Refresh()
{
    // Periodic cheap top-up for hired/owned companions (no item cheat).
    // The old item-cheat gate made this dead for exactly the bots that
    // need it; RestockCompanion reuses the same bounded seed helpers, so
    // there is one restock path, not two.
    RestockCompanion();
    bot->SaveToDB();
}

// Periodic cheap top-up for hired/owned companions: class reagents, food,
// drink, potions and level-tier bandages, each topped to a small bounded
// stack. Idempotent (helpers check current counts first) and never
// unbounded; tools/bags are deliberately excluded (one-time seed, the
// re-seed backstop already covers gaps).
void PlayerbotFactory::RestockCompanion()
{
    InitReagents();
    InitPotions();
    InitFood();
    AddConsumables();
    InitBandages();
}

// Level-tier bandage matching the First Aid ladder in UseBandageAction
// (skill 129 thresholds). Vendored cloth bandages, never raid. Keeps the
// current stack and tops it to one half-stack when low; never unbounded.
void PlayerbotFactory::InitBandages()
{
    auto pmo = sPerformanceMonitor.start(PERF_MON_RNDBOT, "PlayerbotFactory_Bandages");
    uint16 firstAid = bot->GetSkillValue(SKILL_FIRST_AID);
    uint32 itemId = 1251;
    if (firstAid >= 225) itemId = 14530;
    else if (firstAid >= 200) itemId = 14529;
    else if (firstAid >= 175) itemId = 8545;
    else if (firstAid >= 150) itemId = 8544;
    else if (firstAid >= 125) itemId = 6451;
    else if (firstAid >= 100) itemId = 6450;
    else if (firstAid >= 75) itemId = 3531;
    else if (firstAid >= 50) itemId = 3530;
    else if (firstAid >= 20) itemId = 2581;
    ItemPrototype const* proto = sObjectMgr.GetItemPrototype(itemId);
    if (!proto)
        return;
    uint32 have = bot->GetItemCount(itemId);
    uint32 halfStack = proto->GetMaxStackSize() / 2;
    if (have >= halfStack)
        return;
    Item* stored = StoreItem(itemId, halfStack - have, true);
    if (stored)
        sLog.outDetail("Bot %d got bandage %s x%d", bot->GetGUIDLow(), proto->Name1, halfStack - have);
}

// Fresh-seed money: a sensible level-scaled amount on first seed only.
// Guards on the current coinage so a re-seed/backstop never refills it
// (vendor restock, repairs and AH buying stay earned after the seed).
void PlayerbotFactory::SeedFreshMoney()
{
    if (!bot || bot->GetMoney() > 0)
        return;
    uint32 lvl = bot->GetLevel();
    uint32 money = 10000 * urand(1, std::max<uint32>(1, lvl * 5));
    bot->SetMoney(money);
    sLog.outDetail("Bot %d seeded with %u copper (level %u)", bot->GetGUIDLow(), money, lvl);
}

// Weapon oils (casters), sharpening/weight stones (melee) and rogue poisons
// are granted as one small stack of the CURRENT level tier only (oils: 2
// like the donor, stones/poisons: 5 as seeded before). In this DB oils are
// stackable=1, so one extra item is one extra bag slot. The helper below
// only ever removes STRICTLY lower tiers of the same line and only tops up
// toward the target: anything at or above the current tier that the bot
// owns (raid consumables, self-made stacks, higher ranks) is left alone,
// and target stacks are never trimmed down.
void PlayerbotFactory::TopUpConsumableFamily(std::vector<uint32> const& familyLowToHigh, uint32 targetId, uint32 target)
{
    if (!bot || !targetId || !target)
        return;
    // Index of the current tier; anything above it is kept unconditionally.
    size_t targetIdx = familyLowToHigh.size();
    for (size_t i = 0; i < familyLowToHigh.size(); ++i)
    {
        if (familyLowToHigh[i] == targetId)
        {
            targetIdx = i;
            break;
        }
    }
    if (targetIdx >= familyLowToHigh.size())
        return;
    // Count held target items (oils are stackable=1: count items, not stacks).
    uint32 heldTarget = 0;
    std::vector<Item*> lowerTier;
    {
        FindItemByIdsVisitor visitor(ItemIds(familyLowToHigh.begin(), familyLowToHigh.end()));
        ai->InventoryIterateItems(&visitor, IterateItemsMask::ITERATE_ITEMS_IN_BAGS);
        for (Item* item : visitor.GetResult())
        {
            if (!item)
                continue;
            if (item->GetEntry() == targetId)
                heldTarget += std::max<uint32>(1, item->GetCount());
            else
            {
                for (size_t i = 0; i < targetIdx; ++i)
                {
                    if (familyLowToHigh[i] == item->GetEntry())
                    {
                        lowerTier.push_back(item);
                        break;
                    }
                }
            }
        }
    }
    for (Item* item : lowerTier)
        bot->DestroyItem(item->GetBagSlot(), item->GetSlot(), true);
    if (heldTarget < target)
        StoreItem(targetId, target - heldTarget, true);
}

void PlayerbotFactory::AddConsumables()
{
    auto pmo = sPerformanceMonitor.start(PERF_MON_RNDBOT, "PlayerbotFactory_Consumables");
   switch (bot->GetClass())
   {
      case CLASS_PRIEST:
      case CLASS_MAGE:
      case CLASS_WARLOCK:
      {
         // Wizard vs mana oil by class/spec (donor InitConsumables is
         // spec-aware: shadow priests imbue spellpower, holy/disc imbue
         // mana; mages/warlocks always spellpower). Two separate families
         // so holding one line never purges the other.
         static std::vector<uint32> const wizardFamily = {
             CONSUM_ID_MINOR_WIZARD_OIL, CONSUM_ID_LESSER_WIZARD_OIL,
             CONSUM_ID_WIZARD_OIL, CONSUM_ID_BRILLIANT_WIZARD_OIL };
         static std::vector<uint32> const manaFamily = {
             CONSUM_ID_MINOR_MANA_OIL, CONSUM_ID_LESSER_MANA_OIL,
             CONSUM_ID_BRILLIANT_MANA_OIL };
         // Priest tab 2 is shadow (RandomItemMgr::GetPlayerSpecName,
         // AiFactory strategies); pre-10 default tab 1 reads as holy.
         bool wantWizard = bot->GetClass() == CLASS_MAGE || bot->GetClass() == CLASS_WARLOCK ||
             (bot->GetClass() == CLASS_PRIEST && AiFactory::GetPlayerSpecTab(bot) == 2);
         if (wantWizard)
         {
            uint32 target = 0;
            if (level >= 45) target = CONSUM_ID_BRILLIANT_WIZARD_OIL;
            else if (level >= 40) target = CONSUM_ID_WIZARD_OIL;
            else if (level >= 5) target = CONSUM_ID_MINOR_WIZARD_OIL;
            if (target)
               TopUpConsumableFamily(wizardFamily, target, 2);
         }
         else
         {
            uint32 target = 0;
            if (level >= 45) target = CONSUM_ID_BRILLIANT_MANA_OIL;
            else if (level >= 5) target = CONSUM_ID_MINOR_MANA_OIL;
            if (target)
               TopUpConsumableFamily(manaFamily, target, 2);
         }
   }
      break;
      case CLASS_PALADIN:
      case CLASS_WARRIOR:
       {
         // Hunters get no melee stones (ranged class; review MEDIUM-01).
         static std::vector<uint32> const sharpeningFamily = {
             CONSUM_ID_ROUGH_SHARPENING_STONE, CONSUM_ID_COARSE_SHARPENING_STONE,
             CONSUM_ID_HEAVY_SHARPENING_STONE, CONSUM_ID_SOL_SHARPENING_STONE,
             CONSUM_ID_DENSE_SHARPENING_STONE, CONSUM_ID_ELEMENTAL_SHARPENING_STONE,
             CONSUM_ID_CONSECRATED_SHARPENING_STONE };
         static std::vector<uint32> const weightFamily = {
             CONSUM_ID_ROUGH_WEIGHTSTONE, CONSUM_ID_COARSE_WEIGHTSTONE,
             CONSUM_ID_HEAVY_WEIGHTSTONE, CONSUM_ID_SOLID_WEIGHTSTONE,
             CONSUM_ID_DENSE_WEIGHTSTONE };
         uint32 sharpenTarget = 0, weightTarget = 0;
         if (level >= 35) { sharpenTarget = CONSUM_ID_DENSE_SHARPENING_STONE; weightTarget = CONSUM_ID_DENSE_WEIGHTSTONE; }
         else if (level >= 25) { sharpenTarget = CONSUM_ID_SOL_SHARPENING_STONE; weightTarget = CONSUM_ID_SOLID_WEIGHTSTONE; }
         else if (level >= 15) { sharpenTarget = CONSUM_ID_HEAVY_SHARPENING_STONE; weightTarget = CONSUM_ID_HEAVY_WEIGHTSTONE; }
         else if (level >= 5) { sharpenTarget = CONSUM_ID_COARSE_SHARPENING_STONE; weightTarget = CONSUM_ID_COARSE_WEIGHTSTONE; }
         else if (level >= 1) { sharpenTarget = CONSUM_ID_ROUGH_SHARPENING_STONE; weightTarget = CONSUM_ID_ROUGH_WEIGHTSTONE; }
         if (sharpenTarget)
            TopUpConsumableFamily(sharpeningFamily, sharpenTarget, 5);
         if (weightTarget)
            TopUpConsumableFamily(weightFamily, weightTarget, 5);
   }
       break;
       case CLASS_ROGUE:
      {
         static std::vector<uint32> const instantFamily = {
             CONSUM_ID_INSTANT_POISON, CONSUM_ID_INSTANT_POISON_II, CONSUM_ID_INSTANT_POISON_III,
             CONSUM_ID_INSTANT_POISON_IV, CONSUM_ID_INSTANT_POISON_V, CONSUM_ID_INSTANT_POISON_VI,
             CONSUM_ID_INSTANT_POISON_VII };
         static std::vector<uint32> const deadlyFamily = {
             CONSUM_ID_DEADLY_POISON, CONSUM_ID_DEADLY_POISON_II, CONSUM_ID_DEADLY_POISON_III,
             CONSUM_ID_DEADLY_POISON_IV, CONSUM_ID_DEADLY_POISON_V,
             CONSUM_ID_DEADLY_POISON_VI, CONSUM_ID_DEADLY_POISON_VII };
         static std::vector<uint32> const cripplingFamily = {
             CONSUM_ID_CRIPPLING_POISON, CONSUM_ID_CRIPPLING_POISON_II };
         static std::vector<uint32> const mindFamily = {
             CONSUM_ID_MIND_POISON, CONSUM_ID_MIND_POISON_II, CONSUM_ID_MIND_POISON_III };
         uint32 instantTarget = 0, deadlyTarget = 0, cripplingTarget = 0, mindTarget = 0;
         if (level >= 60) { deadlyTarget = CONSUM_ID_DEADLY_POISON_V; instantTarget = CONSUM_ID_INSTANT_POISON_VI; cripplingTarget = CONSUM_ID_CRIPPLING_POISON_II; mindTarget = CONSUM_ID_MIND_POISON_III; }
         else if (level >= 54) { deadlyTarget = CONSUM_ID_DEADLY_POISON_IV; instantTarget = CONSUM_ID_INSTANT_POISON_V; cripplingTarget = CONSUM_ID_CRIPPLING_POISON_II; mindTarget = CONSUM_ID_MIND_POISON_III; }
         else if (level >= 52) { deadlyTarget = CONSUM_ID_DEADLY_POISON_III; instantTarget = CONSUM_ID_INSTANT_POISON_V; cripplingTarget = CONSUM_ID_CRIPPLING_POISON_II; mindTarget = CONSUM_ID_MIND_POISON_III; }
         else if (level >= 46) { deadlyTarget = CONSUM_ID_DEADLY_POISON_III; instantTarget = CONSUM_ID_INSTANT_POISON_IV; cripplingTarget = CONSUM_ID_CRIPPLING_POISON; mindTarget = CONSUM_ID_MIND_POISON_II; }
         else if (level >= 44) { deadlyTarget = CONSUM_ID_DEADLY_POISON_II; instantTarget = CONSUM_ID_INSTANT_POISON_IV; cripplingTarget = CONSUM_ID_CRIPPLING_POISON; mindTarget = CONSUM_ID_MIND_POISON_II; }
         else if (level >= 38) { deadlyTarget = CONSUM_ID_DEADLY_POISON_II; instantTarget = CONSUM_ID_INSTANT_POISON_III; cripplingTarget = CONSUM_ID_CRIPPLING_POISON; mindTarget = CONSUM_ID_MIND_POISON_II; }
         else if (level >= 36) { deadlyTarget = CONSUM_ID_DEADLY_POISON; instantTarget = CONSUM_ID_INSTANT_POISON_III; cripplingTarget = CONSUM_ID_CRIPPLING_POISON; mindTarget = CONSUM_ID_MIND_POISON; }
         else if (level >= 30) { deadlyTarget = CONSUM_ID_DEADLY_POISON; instantTarget = CONSUM_ID_INSTANT_POISON_II; cripplingTarget = CONSUM_ID_CRIPPLING_POISON; mindTarget = CONSUM_ID_MIND_POISON; }
         else if (level >= 28) { instantTarget = CONSUM_ID_INSTANT_POISON_II; cripplingTarget = CONSUM_ID_CRIPPLING_POISON; mindTarget = CONSUM_ID_MIND_POISON; }
         else if (level >= 20) { instantTarget = CONSUM_ID_INSTANT_POISON; cripplingTarget = CONSUM_ID_CRIPPLING_POISON; }
         if (deadlyTarget)
            TopUpConsumableFamily(deadlyFamily, deadlyTarget, 5);
         if (instantTarget)
            TopUpConsumableFamily(instantFamily, instantTarget, 5);
         if (cripplingTarget)
            TopUpConsumableFamily(cripplingFamily, cripplingTarget, 5);
         if (mindTarget)
            TopUpConsumableFamily(mindFamily, mindTarget, 5);
         break;
      }
   }
}

void PlayerbotFactory::InitPet()
{
    // Randomize a new pet (only for hunters)
    if (bot->GetClass() != CLASS_HUNTER)
        return;

    Pet* pet = bot->GetPet();
    if (!pet)
    {
        Map* map = bot->GetMap();
        if (!map)
            return;

        std::vector<uint32> ids;
        for (uint32 id = 0; id < sCreatureStorage.GetMaxEntry(); ++id)
        {
            CreatureInfo const* co = sCreatureStorage.LookupEntry<CreatureInfo>(id);
			if (!co)
				continue;

            if (!co->isTameable())
                continue;

            if ((int)co->level_min > (int)bot->GetLevel())
                continue;

			ids.push_back(id);
		}

        if (ids.empty())
        {
            sLog.outError("No pets available for bot %s (%d level)", bot->GetName(), bot->GetLevel());
            return;
        }

		for (int i = 0; i < 100; i++)
		{
			int index = urand(0, ids.size() - 1);
            CreatureInfo const* co = sCreatureStorage.LookupEntry<CreatureInfo>(ids[index]);
            if (!co)
                continue;

            uint32 guid = map->GenerateLocalLowGuid(HIGHGUID_PET);
            CreatureCreatePos pos(map, bot->getPositionX(), bot->getPositionY(), bot->getPositionZ(), bot->getOrientation());
            uint32 pet_number = sObjectMgr.GeneratePetNumber();
            pet = new Pet(HUNTER_PET);
            if (!pet->Create(guid, pos, co, pet_number))
            {
                delete pet;
                pet = NULL;
                continue;
            }

            pet->SetOwnerGuid(bot->getObjectGuid());
            pet->SetGuidValue(UNIT_FIELD_CREATEDBY, bot->getObjectGuid());
            pet->SetFactionTemplateId(bot->GetFactionTemplateId());
            pet->SetLevel(bot->GetLevel());
            pet->InitStatsForLevel(bot->GetLevel());
            pet->SetLoyaltyLevel(BEST_FRIEND);
            pet->SetPower(POWER_HAPPINESS, HAPPINESS_LEVEL_SIZE * 2);
            pet->GetCharmInfo()->SetPetNumber(pet->getObjectGuid().GetEntry(), true);
            pet->GetMap()->Add((Creature*)pet);
            pet->AIM_Initialize();
            pet->SetReactState(REACT_DEFENSIVE);
            pet->InitPetCreateSpells();
            pet->LearnPetPassives();
            pet->CastPetAuras(true);
            pet->UpdateAllStats();
            bot->SetPet(pet);
            bot->SetPetGuid(pet->getObjectGuid());

            sLog.outDebug(  "Bot %s: assign pet %d (%d level)", bot->GetName(), co->entry, bot->GetLevel());
            pet->SavePetToDB(PET_SAVE_AS_CURRENT);
            bot->PetSpellInitialize();
            break;
        }
    }

    pet = bot->GetPet();
    if (pet)
    {
        pet->InitStatsForLevel(bot->GetLevel());
        pet->SetLevel(bot->GetLevel());
        pet->SetLoyaltyLevel(BEST_FRIEND);
        pet->SetPower(POWER_HAPPINESS, HAPPINESS_LEVEL_SIZE * 2);
        pet->SetHealth(pet->GetMaxHealth());
        pet->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_PLAYER_CONTROLLED);
        pet->SetReactState(REACT_DEFENSIVE);
    }
    else
    {
        sLog.outError("Cannot create pet for bot %s", bot->GetName());
        return;
    }

    for (PetSpellMap::const_iterator itr = pet->m_petSpells.begin(); itr != pet->m_petSpells.end(); ++itr)
    {
        if(itr->second.state == PETSPELL_REMOVED)
            continue;

        uint32 spellId = itr->first;
        if(IsPassiveSpell(spellId))
            continue;

        pet->ToggleAutocast(spellId, true);
    }

    // Force dismiss pet to fix missing flags
    if (pet->IsAlive())
    {
        pet->SetDeathState(JUST_DIED);
    }
}

void PlayerbotFactory::InitPetSpells()
{
    Map* map = bot->GetMap();
    if (!map)
        return;

    Pet* pet = bot->GetPet();
    if (!pet)
        return;

     // TODO: Proper Training Point calculation for build variety
    if (bot->GetClass() == CLASS_HUNTER)
    {
        enum HunterPetType
        {
            PET_WOLF,
            PET_CAT,
            PET_SPIDER,
            PET_BEAR,
            PET_BOAR,
            PET_CROCOLISK,
            PET_CARRION_BIRD,
            PET_CRAB,
            PET_GORILLA,
            PET_RAPTOR,
            PET_TALLSTRIDER,
            PET_SCORPID,
            PET_TURTLE,
            PET_BAT,
            PET_HYENA,
            PET_OWL,
            PET_WIND_SERPENT,
            PET_UNKNOWN
        };

        std::map<HunterPetType, std::vector<std::pair<uint32, uint32>>> hunterPetSpells;

        hunterPetSpells[PET_BAT] = {
            // Bite
            {1,  17253},
            {8,  17255},
            {16, 17256},
            {24, 17257},
            {32, 17258},
            {40, 17259},
            {48, 17260},
            {56, 17261},
            // Cower
            {5,  1742 },
            {15, 1753 },
            {25, 1754 },
            {35, 1755 },
            {45, 1756 },
            {55, 16697},
            // Dive
            {30, 23145},
            {40, 23146},
            {50, 23147},
            // Screech
            {8,  24423},
            {24, 24577},
            {40, 24578},
        };

        hunterPetSpells[PET_BEAR] = {
            // Bite
            {1,  17253},
            {8,  17255},
            {16, 17256},
            {24, 17257},
            {32, 17258},
            {40, 17259},
            {48, 17260},
            {56, 17261},
            // Claw
            {1,  16827},
            {8,  16828},
            {15, 16829},
            {22, 16830},
            {29, 16831},
            {36, 16832},
            {48, 3010 },
            {56, 3009 },
            // Cower
            {5,  1742 },
            {15, 1753 },
            {25, 1754 },
            {35, 1755 },
            {45, 1756 },
            {55, 16697}
        };

        hunterPetSpells[PET_BOAR] = {
            // Bite
            {1,  17253},
            {8,  17255},
            {16, 17256},
            {24, 17257},
            {32, 17258},
            {40, 17259},
            {48, 17260},
            {56, 17261},
            // Charge
            {1,  7371 },
            {12, 26177},
            {24, 26178},
            {36, 26179},
            {48, 26201},
            {60, 27685},
            // Cower
            {5,  1742 },
            {15, 1753 },
            {25, 1754 },
            {35, 1755 },
            {45, 1756 },
            {55, 16697},
            // Dash
            {30, 23099},
            {40, 23109},
            {50, 23110}
        };

        hunterPetSpells[PET_CARRION_BIRD] = {
            // Bite
            {1,  17253},
            {8,  17255},
            {16, 17256},
            {24, 17257},
            {32, 17258},
            {40, 17259},
            {48, 17260},
            {56, 17261},
            // Claw
            {1,  16827},
            {8,  16828},
            {15, 16829},
            {22, 16830},
            {29, 16831},
            {36, 16832},
            {48, 3010 },
            {56, 3009 },
            // Cower
            {5,  1742 },
            {15, 1753 },
            {25, 1754 },
            {35, 1755 },
            {45, 1756 },
            {55, 16697},
            // Dive
            {30, 23145},
            {40, 23146},
            {50, 23147},
            // Screech
            {8,  24423},
            {24, 24577},
            {40, 24578},
        };

        hunterPetSpells[PET_CAT] = {
            // Bite
            {1,  17253},
            {8,  17255},
            {16, 17256},
            {24, 17257},
            {32, 17258},
            {40, 17259},
            {48, 17260},
            {56, 17261},
            // Claw
            {1,  16827},
            {8,  16828},
            {15, 16829},
            {22, 16830},
            {29, 16831},
            {36, 16832},
            {48, 3010 },
            {56, 3009 },
            // Cower
            {5,  1742 },
            {15, 1753 },
            {25, 1754 },
            {35, 1755 },
            {45, 1756 },
            {55, 16697},
            // Dash
            {30, 23099},
            {40, 23109},
            {50, 23110},
            // Prowl
            {30, 24450},
            {40, 24452},
            {50, 24453}
        };

        hunterPetSpells[PET_CRAB] = {
            // Claw
            {1,  16827},
            {8,  16828},
            {15, 16829},
            {22, 16830},
            {29, 16831},
            {36, 16832},
            {48, 3010 },
            {56, 3009 },
            // Cower
            {5,  1742 },
            {15, 1753 },
            {25, 1754 },
            {35, 1755 },
            {45, 1756 },
            {55, 16697}
        };

        hunterPetSpells[PET_CROCOLISK] = {
            // Bite
            {1,  17253},
            {8,  17255},
            {16, 17256},
            {24, 17257},
            {32, 17258},
            {40, 17259},
            {48, 17260},
            {56, 17261},
            // Cower
            {5,  1742 },
            {15, 1753 },
            {25, 1754 },
            {35, 1755 },
            {45, 1756 },
            {55, 16697}
        };

        hunterPetSpells[PET_GORILLA] = {
            // Bite
            {1,  17253},
            {8,  17255},
            {16, 17256},
            {24, 17257},
            {32, 17258},
            {40, 17259},
            {48, 17260},
            {56, 17261},
            // Cower
            {5,  1742 },
            {15, 1753 },
            {25, 1754 },
            {35, 1755 },
            {45, 1756 },
            {55, 16697},
            // Thunderstomp
            {30, 26090},
            {40, 26187},
            {50, 26188}
        };

        hunterPetSpells[PET_HYENA] = {
            // Bite
            {1,  17253},
            {8,  17255},
            {16, 17256},
            {24, 17257},
            {32, 17258},
            {40, 17259},
            {48, 17260},
            {56, 17261},
            // Cower
            {5,  1742 },
            {15, 1753 },
            {25, 1754 },
            {35, 1755 },
            {45, 1756 },
            {55, 16697},
            // Dash
            {30, 23099},
            {40, 23109},
            {50, 23110}
        };

        hunterPetSpells[PET_OWL] = {
            // Claw
            {1,  16827},
            {8,  16828},
            {15, 16829},
            {22, 16830},
            {29, 16831},
            {36, 16832},
            {48, 3010 },
            {56, 3009 },
            // Cower
            {5,  1742 },
            {15, 1753 },
            {25, 1754 },
            {35, 1755 },
            {45, 1756 },
            {55, 16697},
            // Dive
            {30, 23145},
            {40, 23146},
            {50, 23147},
            // Screech
            {8,  24423},
            {24, 24577},
            {40, 24578},
            {56, 24579}
        };

        hunterPetSpells[PET_RAPTOR] = {
            // Bite
            {1,  17253},
            {8,  17255},
            {16, 17256},
            {24, 17257},
            {32, 17258},
            {40, 17259},
            {48, 17260},
            {56, 17261},
            // Claw
            {1,  16827},
            {8,  16828},
            {15, 16829},
            {22, 16830},
            {29, 16831},
            {36, 16832},
            {48, 3010 },
            {56, 3009 },
            // Cower
            {5,  1742 },
            {15, 1753 },
            {25, 1754 },
            {35, 1755 },
            {45, 1756 },
            {55, 16697}
        };

        hunterPetSpells[PET_SCORPID] = {
            // Claw
            {1,  16827},
            {8,  16828},
            {15, 16829},
            {22, 16830},
            {29, 16831},
            {36, 16832},
            {48, 3010 },
            {56, 3009 },
            // Cower
            {5,  1742 },
            {15, 1753 },
            {25, 1754 },
            {35, 1755 },
            {45, 1756 },
            {55, 16697},
            // Scorpid Poison
            {8,  24640},
            {24, 24583},
            {40, 24586},
            {56, 24587}
        };

        hunterPetSpells[PET_SPIDER] = {
            // Bite
            {1,  17253},
            {8,  17255},
            {16, 17256},
            {24, 17257},
            {32, 17258},
            {40, 17259},
            {48, 17260},
            {56, 17261},
            // Cower
            {5,  1742 },
            {15, 1753 },
            {25, 1754 },
            {35, 1755 },
            {45, 1756 },
            {55, 16697}
        };

        hunterPetSpells[PET_TALLSTRIDER] = {
            // Bite
            {1,  17253},
            {8,  17255},
            {16, 17256},
            {24, 17257},
            {32, 17258},
            {40, 17259},
            {48, 17260},
            {56, 17261},
            // Cower
            {5,  1742 },
            {15, 1753 },
            {25, 1754 },
            {35, 1755 },
            {45, 1756 },
            {55, 16697},
            // Dash
            {30, 23099},
            {40, 23109},
            {50, 23110}
        };

        hunterPetSpells[PET_TURTLE] = {
            // Bite
            {1,  17253},
            {8,  17255},
            {16, 17256},
            {24, 17257},
            {32, 17258},
            {40, 17259},
            {48, 17260},
            {56, 17261},
            // Cower
            {5,  1742 },
            {15, 1753 },
            {25, 1754 },
            {35, 1755 },
            {45, 1756 },
            {55, 16697},
            // Shell Shield
            {20, 26064}
        };

        hunterPetSpells[PET_WIND_SERPENT] = {
            // Bite
            {1,  17253},
            {8,  17255},
            {16, 17256},
            {24, 17257},
            {32, 17258},
            {40, 17259},
            {48, 17260},
            {56, 17261},
            // Cower
            {5,  1742 },
            {15, 1753 },
            {25, 1754 },
            {35, 1755 },
            {45, 1756 },
            {55, 16697},
            // Dive
            {30, 23145},
            {40, 23146},
            {50, 23147},
            // Lightning Breath
            {1,  24844},
            {12, 25008},
            {24, 25009},
            {36, 25010},
            {48, 25011},
            {60, 25012}
        };

        hunterPetSpells[PET_WOLF] = {
            // Bite
            {1,  17253},
            {8,  17255},
            {16, 17256},
            {24, 17257},
            {32, 17258},
            {40, 17259},
            {48, 17260},
            {56, 17261},
            // Cower
            {5,  1742 },
            {15, 1753 },
            {25, 1754 },
            {35, 1755 },
            {45, 1756 },
            {55, 16697},
            // Dash
            {30, 23099},
            {40, 23109},
            {50, 23110},
            // Furious Howl
            {10, 24604},
            {20, 24605},
            {30, 24603},
            {40, 24597}
        };

        // Determine petType from creature template family
        auto GetHunterPetTypeFromEntry = [](uint32 entry) -> HunterPetType {
            CreatureInfo const* ci = sObjectMgr.GetCreatureTemplate(entry);
            if (!ci)
                return PET_UNKNOWN;

            switch (ci->beast_family)
            {
                case 1: return PET_WOLF;
                case 2: return PET_CAT;
                case 3: return PET_SPIDER;
                case 4: return PET_BEAR;
                case 5: return PET_BOAR;
                case 6: return PET_CROCOLISK;
                case 7: return PET_CARRION_BIRD;
                case 8: return PET_CRAB;
                case 9: return PET_GORILLA;
                case 11: return PET_RAPTOR;
                case 12: return PET_TALLSTRIDER;
                case 20: return PET_SCORPID;
                case 21: return PET_TURTLE;
                case 24: return PET_BAT;
                case 25: return PET_HYENA;
                case 26: return PET_OWL;
                case 27: return PET_WIND_SERPENT;
                default: return PET_UNKNOWN;
            }
        };

        HunterPetType petType = GetHunterPetTypeFromEntry(pet->GetEntry());

        auto it = hunterPetSpells.find(petType);
        if (it != hunterPetSpells.end())
        {
            // Find Cower spells
            static const std::unordered_set<uint32> cowerSpellIds = {1742, 1753, 1754, 1755, 1756, 16697};

            for (const auto& pair : it->second)
            {
                const uint32& levelRequired = pair.first;
                const uint32& spellID = pair.second;

                if (pet->GetLevel() >= levelRequired)
                {
                    if (!pet->HasSpell(spellID))
                    {
                        pet->LearnSpell(spellID);
                    }

                    if (!IsPassiveSpell(spellID))
                    {
                        // Toggle Cower off by default
                        const bool autocast = (cowerSpellIds.find(spellID) == cowerSpellIds.end());
                        if (pet->HasSpell(spellID))
                        {
                            pet->ToggleAutocast(spellID, autocast);
                        }
                    }
                }
            }
        }

        // Growl
        struct GrowlRank
        {
            uint32 minLevel;
            uint32 spellId;
        };
        static const GrowlRank growlRanks[] = {
            {1,  2649 }, // Growl rank 1
            {10, 14916}, // Growl rank 2
            {20, 14917}, // Growl rank 3
            {30, 14918}, // Growl rank 4
            {40, 14919}, // Growl rank 5
            {50, 14920}, // Growl rank 6
            {60, 14921}, // Growl rank 7
        };
        uint32 growlSpellId = 0;
        for (const auto& rank : growlRanks)
        {
            if (pet->GetLevel() >= rank.minLevel)
                growlSpellId = rank.spellId;
        }
        if (growlSpellId && !pet->HasSpell(growlSpellId))
        {
            pet->LearnSpell(growlSpellId);
        }

        // Natural Armor
        struct NaturalArmorRank
        {
            uint32 minLevel;
            uint32 spellId;
        };
        static const NaturalArmorRank naturalArmorRanks[] = {
            {1,  24545},
            {12, 24549},
            {18, 24550},
            {24, 24551}
        };
        uint32 naturalArmorSpellId = 0;
        for (const auto& rank : naturalArmorRanks)
        {
            if (pet->GetLevel() >= rank.minLevel)
                naturalArmorSpellId = rank.spellId;
        }
        if (naturalArmorSpellId && !pet->HasSpell(naturalArmorSpellId))
        {
            pet->LearnSpell(naturalArmorSpellId);
        }

        // Great Stamina
        struct GreatStaminaRank
        {
            uint32 minLevel;
            uint32 spellId;
        };
        static const GreatStaminaRank greatStaminaRanks[] = {
            {1,  4187},
            {12, 4188},
            {18, 4189},
            {24, 4190},
            {30, 4191},
            {36, 4192},
            {42, 4193},
            {48, 4194},
            {54, 5041},
            {60, 5042}
        };
        uint32 greatStaminaSpellId = 0;
        for (const auto& rank : greatStaminaRanks)
        {
            if (pet->GetLevel() >= rank.minLevel)
                greatStaminaSpellId = rank.spellId;
        }
        if (greatStaminaSpellId && !pet->HasSpell(greatStaminaSpellId))
        {
            pet->LearnSpell(greatStaminaSpellId);
        }

        // Resistances
        if (pet->GetLevel() >= 20)
        {
            struct ResistanceSpell
            {
                uint32 spellId;
            };
            static const ResistanceSpell resistances[] = {
                {24493}, // Arcane
                {23992}, // Fire
                {24446}, // Frost
                {24492}, // Nature
                {24488}  // Shadow
            };
            for (const auto& res : resistances)
            {
                if (!pet->HasSpell(res.spellId))
                    pet->LearnSpell(res.spellId);
            }
        }
    }


    if (bot->GetClass() == CLASS_WARLOCK)
    {
        constexpr uint32 PET_IMP = 416;
        constexpr uint32 PET_FELHUNTER = 417;
        constexpr uint32 PET_VOIDWALKER = 1860;
        constexpr uint32 PET_SUCCUBUS = 1863;

        //      pet type                    pet level  pet spell id
        std::map<uint32, std::vector<std::pair<uint32, uint32>>> spellList;

        // Imp spells
        {
            // Blood Pact
            spellList[PET_IMP].push_back(std::pair(4, 6307));
            spellList[PET_IMP].push_back(std::pair(14, 7804));
            spellList[PET_IMP].push_back(std::pair(26, 7805));
            spellList[PET_IMP].push_back(std::pair(38, 11766));
            spellList[PET_IMP].push_back(std::pair(50, 11767));

            // Fire Shield
            spellList[PET_IMP].push_back(std::pair(14, 2947));
            spellList[PET_IMP].push_back(std::pair(24, 8316));
            spellList[PET_IMP].push_back(std::pair(34, 8317));
            spellList[PET_IMP].push_back(std::pair(44, 11770));
            spellList[PET_IMP].push_back(std::pair(54, 11771));

            // Firebolt
            spellList[PET_IMP].push_back(std::pair(1, 3110));
            spellList[PET_IMP].push_back(std::pair(8, 7799));
            spellList[PET_IMP].push_back(std::pair(18, 7800));
            spellList[PET_IMP].push_back(std::pair(28, 7801));
            spellList[PET_IMP].push_back(std::pair(38, 7802));
            spellList[PET_IMP].push_back(std::pair(48, 11762));
            spellList[PET_IMP].push_back(std::pair(58, 11763));

            // Phase Shift
            spellList[PET_IMP].push_back(std::pair(12, 4511));
        }

        // Felhunter spells
        {
            // Devour Magic
            spellList[PET_FELHUNTER].push_back(std::pair(30, 19505));
            spellList[PET_FELHUNTER].push_back(std::pair(38, 19731));
            spellList[PET_FELHUNTER].push_back(std::pair(46, 19734));
            spellList[PET_FELHUNTER].push_back(std::pair(54, 19736));

            // Paranoia
            spellList[PET_FELHUNTER].push_back(std::pair(42, 19480));

            // Spell Lock
            spellList[PET_FELHUNTER].push_back(std::pair(36, 19244));
            spellList[PET_FELHUNTER].push_back(std::pair(52, 19647));

            // Tainted Blood
            spellList[PET_FELHUNTER].push_back(std::pair(32, 19478));
            spellList[PET_FELHUNTER].push_back(std::pair(40, 19655));
            spellList[PET_FELHUNTER].push_back(std::pair(48, 19656));
            spellList[PET_FELHUNTER].push_back(std::pair(56, 19660));
        }

        // Voidwalker spells
        {
            // Consume Shadows
            spellList[PET_VOIDWALKER].push_back(std::pair(18, 17767));
            spellList[PET_VOIDWALKER].push_back(std::pair(26, 17850));
            spellList[PET_VOIDWALKER].push_back(std::pair(34, 17851));
            spellList[PET_VOIDWALKER].push_back(std::pair(42, 17852));
            spellList[PET_VOIDWALKER].push_back(std::pair(50, 17853));
            spellList[PET_VOIDWALKER].push_back(std::pair(58, 17854));

            // Sacrifice
            spellList[PET_VOIDWALKER].push_back(std::pair(16, 7812));
            spellList[PET_VOIDWALKER].push_back(std::pair(24, 19438));
            spellList[PET_VOIDWALKER].push_back(std::pair(32, 19440));
            spellList[PET_VOIDWALKER].push_back(std::pair(40, 19441));
            spellList[PET_VOIDWALKER].push_back(std::pair(48, 19442));
            spellList[PET_VOIDWALKER].push_back(std::pair(56, 19443));

            // Suffering
            spellList[PET_VOIDWALKER].push_back(std::pair(24, 17735));
            spellList[PET_VOIDWALKER].push_back(std::pair(36, 17750));
            spellList[PET_VOIDWALKER].push_back(std::pair(48, 17751));
            spellList[PET_VOIDWALKER].push_back(std::pair(60, 17752));

            // Torment
            spellList[PET_VOIDWALKER].push_back(std::pair(10, 3716));
            spellList[PET_VOIDWALKER].push_back(std::pair(20, 7809));
            spellList[PET_VOIDWALKER].push_back(std::pair(30, 7810));
            spellList[PET_VOIDWALKER].push_back(std::pair(40, 7811));
            spellList[PET_VOIDWALKER].push_back(std::pair(50, 11774));
            spellList[PET_VOIDWALKER].push_back(std::pair(60, 11775));
        }

        // Succubus spells
        {
            // Lash of Pain
            spellList[PET_SUCCUBUS].push_back(std::pair(20, 7814));
            spellList[PET_SUCCUBUS].push_back(std::pair(28, 7815));
            spellList[PET_SUCCUBUS].push_back(std::pair(36, 7816));
            spellList[PET_SUCCUBUS].push_back(std::pair(44, 11778));
            spellList[PET_SUCCUBUS].push_back(std::pair(52, 11779));
            spellList[PET_SUCCUBUS].push_back(std::pair(60, 11780));

            // Lesser Invisibility
            spellList[PET_SUCCUBUS].push_back(std::pair(32, 7870));

            // Seduction
            spellList[PET_SUCCUBUS].push_back(std::pair(26, 6358));

            // Soothing Kiss
            spellList[PET_SUCCUBUS].push_back(std::pair(22, 6360));
            spellList[PET_SUCCUBUS].push_back(std::pair(34, 7813));
            spellList[PET_SUCCUBUS].push_back(std::pair(46, 11784));
            spellList[PET_SUCCUBUS].push_back(std::pair(58, 11785));
        }

        // Learn the appropriate spells by level and type
        const auto& petSpellListItr = spellList.find(pet->GetEntry());
        if (petSpellListItr != spellList.end())
        {
            const auto& petSpellList = petSpellListItr->second;
            for (const auto& pair : petSpellListItr->second)
            {
                const uint32& levelRequired = pair.first;
                const uint32& spellID = pair.second;

                if (pet->GetLevel() >= levelRequired)
                {
                    pet->LearnSpell(spellID);
                }
            }
        }
    }
}








bool PlayerbotFactory::SelectPremadeSpecNo()
{
    uint8 cls = bot->GetClass();
    std::vector<TalentPath>& paths = sPlayerbotAIConfig.classSpecs[cls].talentPath;
    if (paths.empty())
        return false;

    // Weighted roll across the configured premade specs (currently one PvE spec per
    // class, but this keeps working if more are added). GetBestPremadeSpec indexes
    // getPremadePath(cls, specNo - 1) by TalentPath::id, so store id + 1.
    uint32 totalProbability = 0;
    for (TalentPath& path : paths)
        totalProbability += std::max(0, path.probability);

    TalentPath* chosen = &paths.front();
    if (totalProbability > 0)
    {
        uint32 roll = urand(0, totalProbability - 1);
        uint32 cumulative = 0;
        for (TalentPath& path : paths)
        {
            cumulative += std::max(0, path.probability);
            if (roll < cumulative)
            {
                chosen = &path;
                break;
            }
        }
    }

    sLog.outBasic("SPECROLL: factory picked %s for class %u (%u paths, weight %u)",
        chosen->name.c_str(), uint32(cls), uint32(paths.size()), totalProbability);

    sRandomBotFacade.SetValue(bot, "specNo", chosen->id + 1);
    return true;
}

class DestroyItemsVisitor : public IterateItemsVisitor
{
public:
    DestroyItemsVisitor(Player* bot) : IterateItemsVisitor(), bot(bot) {}

    virtual bool Visit(Item* item) override
    {
        uint32 id = item->GetProto()->ItemId;
        if (CanKeep(id))
        {
            keep.insert(id);
            return true;
        }

        bot->DestroyItem(item->GetBagSlot(), item->GetSlot(), true);
        return true;
    }

private:
    bool CanKeep(uint32 id)
    {
        if (keep.find(id) != keep.end())
            return false;

        if (sPlayerbotAIConfig.IsInRandomQuestItemList(id))
            return true;

        return false;
    }

private:
    Player* bot;
    std::set<uint32> keep;

};

bool PlayerbotFactory::CanEquipArmor(ItemPrototype const* proto)
{
    if (bot->HasSkill(SKILL_SHIELD) && proto->SubClass == ITEM_SUBCLASS_ARMOR_SHIELD)
        return true;

    if (bot->HasSkill(SKILL_PLATE_MAIL))
    {
        if (proto->SubClass != ITEM_SUBCLASS_ARMOR_PLATE)
            return false;
    }
    else if (bot->HasSkill(SKILL_MAIL))
    {
        if (proto->SubClass != ITEM_SUBCLASS_ARMOR_MAIL)
            return false;
    }
    else if (bot->HasSkill(SKILL_LEATHER))
    {
        if (proto->SubClass != ITEM_SUBCLASS_ARMOR_LEATHER)
            return false;
    }

    if (proto->Quality <= ITEM_QUALITY_NORMAL)
        return true;

    for (uint8 slot = 0; slot < EQUIPMENT_SLOT_END; ++slot)
    {
       if (slot == EQUIPMENT_SLOT_TABARD || slot == EQUIPMENT_SLOT_BODY)
          continue;

    if (slot == EQUIPMENT_SLOT_OFFHAND && bot->GetClass() == CLASS_ROGUE && proto->Class != ITEM_CLASS_WEAPON)
       continue;

    if (slot == EQUIPMENT_SLOT_OFFHAND && bot->GetClass() == CLASS_PALADIN && proto->SubClass != ITEM_SUBCLASS_ARMOR_SHIELD)
       continue;
    }

    uint8 sp = 0, ap = 0, tank = 0;
    for (int j = 0; j < MAX_ITEM_PROTO_STATS; ++j)
    {
        // for ItemStatValue != 0
        if(!proto->ItemStat[j].ItemStatValue)
            continue;

        AddItemStats(proto->ItemStat[j].ItemStatType, sp, ap, tank);
    }

    return CheckItemStats(sp, ap, tank);
}

bool PlayerbotFactory::CheckItemStats(uint8 sp, uint8 ap, uint8 tank)
{
    switch (bot->GetClass())
    {
    case CLASS_PRIEST:
    case CLASS_MAGE:
    case CLASS_WARLOCK:
        if (!sp || ap > sp || tank > sp)
            return false;
        break;
    case CLASS_PALADIN:
    case CLASS_WARRIOR:
        if ((!ap && !tank) || sp > ap || sp > tank)
            return false;
        break;
    case CLASS_HUNTER:
    case CLASS_ROGUE:
        if (!ap || sp > ap || sp > tank)
            return false;
        break;
    }

    return sp || ap || tank;
}

void PlayerbotFactory::AddItemStats(uint32 mod, uint8 &sp, uint8 &ap, uint8 &tank)
{
    switch (mod)
    {
    case ITEM_MOD_HEALTH:
    case ITEM_MOD_STAMINA:
    case ITEM_MOD_MANA:
    case ITEM_MOD_INTELLECT:
    case ITEM_MOD_SPIRIT:
        sp++;
        break;
    }

    switch (mod)
    {
    case ITEM_MOD_AGILITY:
    case ITEM_MOD_STRENGTH:
    case ITEM_MOD_HEALTH:
    case ITEM_MOD_STAMINA:
        tank++;
        break;
    }

    switch (mod)
    {
    case ITEM_MOD_HEALTH:
    case ITEM_MOD_STAMINA:
    case ITEM_MOD_AGILITY:
    case ITEM_MOD_STRENGTH:
        ap++;
        break;
    }
}

void PlayerbotFactory::AddItemSpellStats(uint32 smod, uint8& sp, uint8& ap, uint8& tank)
{
    switch (smod)
    {
    case SPELL_AURA_MOD_DAMAGE_DONE:
    case SPELL_AURA_MOD_HEALING_DONE:
    case SPELL_AURA_MOD_SPELL_CRIT_CHANCE:
    case SPELL_AURA_MOD_POWER_REGEN:
        sp++;
        break;
    }

    switch (smod)
    {
    case SPELL_AURA_MOD_ATTACK_POWER:
    case SPELL_AURA_MOD_CRIT_PERCENT:
    case SPELL_AURA_MOD_HIT_CHANCE:
    case SPELL_AURA_MOD_RANGED_ATTACK_POWER:
    case SPELL_AURA_EXTRA_ATTACKS:
    case SPELL_AURA_MOD_MELEE_HASTE:
    case SPELL_AURA_MOD_RANGED_HASTE:
        ap++;
        break;
    }

    switch (smod)
    {
    case SPELL_AURA_MOD_PARRY_PERCENT:
    case SPELL_AURA_MOD_DODGE_PERCENT:
    case SPELL_AURA_MOD_BLOCK_PERCENT:
    case SPELL_AURA_MOD_DAMAGE_PERCENT_TAKEN:
    case SPELL_AURA_MOD_BASE_RESISTANCE_PCT:
    case SPELL_AURA_MOD_BASE_RESISTANCE:
        //case SPELL_AURA_MOD_BLOCK_SKILL:
    case SPELL_AURA_MOD_SKILL:
    case SPELL_AURA_MOD_SHIELD_BLOCKVALUE:
    case SPELL_AURA_MOD_SHIELD_BLOCKVALUE_PCT:
        //case SPELL_AURA_MOD_HEALING_RECEIVED:
        tank++;
        break;
    }
}


bool PlayerbotFactory::CanEquipWeapon(ItemPrototype const* proto)
{
   int tab = AiFactory::GetPlayerSpecTab(bot);

   switch (bot->GetClass())
   {
   case CLASS_PRIEST:
      if (proto->SubClass != ITEM_SUBCLASS_WEAPON_STAFF &&
         proto->SubClass != ITEM_SUBCLASS_WEAPON_WAND &&
         proto->SubClass != ITEM_SUBCLASS_WEAPON_MACE)
         return false;
      break;
   case CLASS_MAGE:
     if (proto->SubClass != ITEM_SUBCLASS_WEAPON_STAFF &&
         proto->SubClass != ITEM_SUBCLASS_WEAPON_WAND)
         return false;
      break;
   case CLASS_WARLOCK:
      if (proto->SubClass != ITEM_SUBCLASS_WEAPON_STAFF &&
         proto->SubClass != ITEM_SUBCLASS_WEAPON_DAGGER &&
         proto->SubClass != ITEM_SUBCLASS_WEAPON_WAND &&
         proto->SubClass != ITEM_SUBCLASS_WEAPON_SWORD)
         return false;
      break;
   case CLASS_WARRIOR:
      if (tab == 1) //fury
      {
         if (proto->SubClass != ITEM_SUBCLASS_WEAPON_MACE &&
            proto->SubClass != ITEM_SUBCLASS_WEAPON_SWORD &&
            proto->SubClass != ITEM_SUBCLASS_WEAPON_AXE &&
            proto->SubClass != ITEM_SUBCLASS_WEAPON_FIST &&
            proto->SubClass != ITEM_SUBCLASS_WEAPON_GUN &&
            proto->SubClass != ITEM_SUBCLASS_WEAPON_CROSSBOW &&
            proto->SubClass != ITEM_SUBCLASS_WEAPON_BOW &&
            proto->SubClass != ITEM_SUBCLASS_WEAPON_THROWN)
            return false;
      }
      if ((tab == 0) && (bot->GetLevel() > 10))   //arms
      {
         if (proto->SubClass != ITEM_SUBCLASS_WEAPON_MACE2 &&
            proto->SubClass != ITEM_SUBCLASS_WEAPON_SWORD2 &&
            proto->SubClass != ITEM_SUBCLASS_WEAPON_AXE2 &&
            proto->SubClass != ITEM_SUBCLASS_WEAPON_POLEARM &&
            proto->SubClass != ITEM_SUBCLASS_WEAPON_GUN &&
            proto->SubClass != ITEM_SUBCLASS_WEAPON_CROSSBOW &&
            proto->SubClass != ITEM_SUBCLASS_WEAPON_BOW &&
            proto->SubClass != ITEM_SUBCLASS_WEAPON_THROWN)
            return false;
      }
      else //prot +lowlvl
      {
         if (proto->SubClass != ITEM_SUBCLASS_WEAPON_MACE &&
            proto->SubClass != ITEM_SUBCLASS_WEAPON_SWORD &&
            proto->SubClass != ITEM_SUBCLASS_WEAPON_AXE &&
            proto->SubClass != ITEM_SUBCLASS_WEAPON_GUN &&
            proto->SubClass != ITEM_SUBCLASS_WEAPON_CROSSBOW &&
            proto->SubClass != ITEM_SUBCLASS_WEAPON_BOW &&
            proto->SubClass != ITEM_SUBCLASS_WEAPON_THROWN)
            return false;
      }
      break;
   case CLASS_PALADIN:
         if (proto->SubClass != ITEM_SUBCLASS_WEAPON_MACE2 &&
            proto->SubClass != ITEM_SUBCLASS_WEAPON_SWORD2 &&
            proto->SubClass != ITEM_SUBCLASS_WEAPON_MACE &&
            proto->SubClass != ITEM_SUBCLASS_WEAPON_SWORD)
         return false;
      break;
   case CLASS_SHAMAN:
      if (tab == 1) //enh
      {
         if (proto->SubClass != ITEM_SUBCLASS_WEAPON_MACE &&
            proto->SubClass != ITEM_SUBCLASS_WEAPON_FIST &&
            proto->SubClass != ITEM_SUBCLASS_WEAPON_AXE &&
            proto->SubClass != ITEM_SUBCLASS_WEAPON_AXE2 &&
            proto->SubClass != ITEM_SUBCLASS_WEAPON_MACE2)
            return false;
      }
      else //ele,resto
      {
         if (proto->SubClass != ITEM_SUBCLASS_WEAPON_DAGGER &&
            proto->SubClass != ITEM_SUBCLASS_WEAPON_MACE &&
            proto->SubClass != ITEM_SUBCLASS_WEAPON_STAFF)
            return false;
      }
      break;
   case CLASS_DRUID:
      if (tab == 1) //feral
      {
         if (proto->SubClass != ITEM_SUBCLASS_WEAPON_MACE2 &&
            proto->SubClass != ITEM_SUBCLASS_WEAPON_STAFF)
            return false;
      }
      else //ele,resto
      {
         if (proto->SubClass != ITEM_SUBCLASS_WEAPON_MACE &&
            proto->SubClass != ITEM_SUBCLASS_WEAPON_DAGGER &&
            proto->SubClass != ITEM_SUBCLASS_WEAPON_STAFF)
            return false;
      }
      break;
   case CLASS_HUNTER:
         if (proto->SubClass != ITEM_SUBCLASS_WEAPON_AXE2 &&
            proto->SubClass != ITEM_SUBCLASS_WEAPON_SWORD2 &&
            proto->SubClass != ITEM_SUBCLASS_WEAPON_POLEARM &&
            proto->SubClass != ITEM_SUBCLASS_WEAPON_STAFF &&
            proto->SubClass != ITEM_SUBCLASS_WEAPON_GUN &&
            proto->SubClass != ITEM_SUBCLASS_WEAPON_CROSSBOW &&
            proto->SubClass != ITEM_SUBCLASS_WEAPON_BOW)
            return false;
      break;
   case CLASS_ROGUE:
      if (tab == 0) //assa
      {
         if (proto->SubClass != ITEM_SUBCLASS_WEAPON_DAGGER &&
            proto->SubClass != ITEM_SUBCLASS_WEAPON_GUN &&
            proto->SubClass != ITEM_SUBCLASS_WEAPON_CROSSBOW &&
            proto->SubClass != ITEM_SUBCLASS_WEAPON_BOW &&
            proto->SubClass != ITEM_SUBCLASS_WEAPON_THROWN)
            return false;
      }
      else
      {
         if (proto->SubClass != ITEM_SUBCLASS_WEAPON_DAGGER &&
            proto->SubClass != ITEM_SUBCLASS_WEAPON_FIST &&
            proto->SubClass != ITEM_SUBCLASS_WEAPON_SWORD &&
            proto->SubClass != ITEM_SUBCLASS_WEAPON_MACE &&
            proto->SubClass != ITEM_SUBCLASS_WEAPON_GUN &&
            proto->SubClass != ITEM_SUBCLASS_WEAPON_CROSSBOW &&
            proto->SubClass != ITEM_SUBCLASS_WEAPON_BOW &&
            proto->SubClass != ITEM_SUBCLASS_WEAPON_THROWN)
            return false;
      }
      break;
   }

   return true;
}

bool PlayerbotFactory::CanEquipItem(ItemPrototype const* proto, uint32 desiredQuality)
{
    if (proto->Duration & 0x80000000)
        return false;

    if (proto->Quality != desiredQuality)
        return false;

    if (proto->Bonding == BIND_QUEST_ITEM || proto->Bonding == BIND_WHEN_USE)
        return false;

    if (proto->Class == ITEM_CLASS_CONTAINER)
        return true;

    uint32 requiredLevel = proto->RequiredLevel;
    if (!requiredLevel)
    {
        requiredLevel = sRandomItemMgr.GetMinLevelFromCache(proto->ItemId);
    }
    if (!requiredLevel)
        return false;

    return true;
}

void PlayerbotFactory::Shuffle(std::vector<uint32>& items)
{
    uint32 count = items.size();
    for (uint32 i = 0; i < count * 5; i++)
    {
        int i1 = urand(0, count - 1);
        int i2 = urand(0, count - 1);

        uint32 item = items[i1];
        items[i1] = items[i2];
        items[i2] = item;
    }
}

// One per-quality candidate query with the wearability descent: the cache
// window holds items up to 20 levels above their bracket, so ItemLevel
// alone accepts req-60 weapons for a 55 bot that CanEquipItem then rejects.
// Descends until at least one candidate is wearable by this bot. Shared by
// the main band loop, the epic path and the fallback — one copy.
void PlayerbotFactory::QuerySeedCandidates(Player* bot, uint32 specId, uint8 slot, uint32 searchLevel, uint32 maxItemLevel, uint32 q, std::vector<uint32>& ids)
{
    uint32 currSearchLevel = searchLevel;
    bool hasProperLevel = false;
    while (!hasProperLevel && currSearchLevel > 0)
    {
        std::vector<uint32> newItems = sRandomItemMgr.Query(currSearchLevel, bot->GetClass(), uint8(specId), slot, q);
        if (newItems.size())
            ids.insert(ids.begin(), newItems.begin(), newItems.end());
        for (auto id : ids)
        {
            ItemPrototype const* proto = sObjectMgr.GetItemPrototype(id);
            if (!proto)
                continue;
            if (proto->ItemLevel > maxItemLevel)
                continue;
            if (sRandomItemMgr.GetMinLevelFromCache(id) > (uint32)bot->GetLevel())
                continue;
            hasProperLevel = true;
            break;
        }
        if (!hasProperLevel)
        {
            ids.clear();
            currSearchLevel--;
        }
    }
}

bool PlayerbotFactory::TrySeedEpicIds(Player* bot, uint32 specId, uint8 slot, uint32 searchLevel, std::vector<uint32>& ids)
{
    ids.clear();
    QuerySeedCandidates(bot, specId, slot, searchLevel, sPlayerbotAIConfig.randomGearMaxLevel, ITEM_QUALITY_EPIC, ids);
    if (ids.empty())
        return false;
    // Strip non-attested epics; the caller shuffles the survivors. The EPIC
    // cache key also holds raid/dungeon epics and BoP drops. Tier gate still
    // applies downstream, so an over-tier attested epic cannot leak in.
    std::vector<uint32> attested;
    for (uint32 id : ids)
        if (sRandomItemMgr.IsWorldEpic(id))
            attested.push_back(id);
    ids.swap(attested);
    return !ids.empty();
}


// Fresh-seed gear policy (owner spec): a uniform green/blue world-drop mix
// (epics only through the rare world-epic gate), never a best-first min/max
// walk. The candidate pool is
// shuffled before filtering, so this sample cap stays uniform over the whole
// pool while bounding per-slot scoring cost; the equip retry then walks the
// sample until one candidate actually equips, so a failed roll never leaves
// the slot empty.
static constexpr uint32 kSeedCandidateSample = 48;

// Paired-slot dupe guard: the FINGER1/2 and TRINKET1/2 candidate lists are
// identical, so without this the loop equips the same entry twice. Data-side
// unique flags are unreliable (Flags=0, MaxCount=0 on most rings/trinkets),
// so compare against the already-equipped pair item directly.
static uint32 PairedSlot(uint8 slot)
{
    switch (slot)
    {
    case EQUIPMENT_SLOT_FINGER1:
        return EQUIPMENT_SLOT_FINGER2;
    case EQUIPMENT_SLOT_FINGER2:
        return EQUIPMENT_SLOT_FINGER1;
    case EQUIPMENT_SLOT_TRINKET1:
        return EQUIPMENT_SLOT_TRINKET2;
    case EQUIPMENT_SLOT_TRINKET2:
        return EQUIPMENT_SLOT_TRINKET1;
    default:
        return EQUIPMENT_SLOT_END;
    }
}


// Fresh-seed provenance gate (owner rules, roadmap #289): a newly created bot
// has done no raids, no end-game dungeons, no rep grinds, no PvP. The
// per-item source-tier classification (RandomItemMgr, lowest-tier-source
// wins, persisted in ai_playerbot_item_info_cache) enforces the tier cap and
// the REP/PVP flags in one check — this replaces the old ad-hoc raid-loot /
// raid-quest / PvP checks, no double logic. Non-raid quest rewards pass when
// the bot meets the quest level (doable per GetLiveStatWeight) — completion
// is NOT required, or fresh bots starve on jewelry. Vendor/world-drop/
// crafted items pass (no quest row at all). Fail-open: unknown items pass;
// only positively-identified over-tier loot is cut. Applies to the
// fresh-seed path only, never to earned upgrades.
static bool PassesSeedProvenance(Player* bot, uint32 newItemId)
{
    if (!sRandomItemMgr.PassesSourceTier(newItemId))
        return false;
    std::vector<uint32> questIds = sRandomItemMgr.GetQuestIdsForItem(newItemId);
    if (questIds.empty())
        return true;
    for (uint32 questId : questIds)
    {
        Quest const* quest = sObjectMgr.GetQuestTemplate(questId);
        if (!quest)
            continue;
        uint32 questLevel = quest->GetQuestLevel();
        if (!questLevel)
            questLevel = quest->GetMinLevel();
        if (!questLevel || questLevel <= (uint32)bot->GetLevel())
            return true;
    }
    return false;
}

void PlayerbotFactory::InitEquipment(bool incremental, bool syncWithMaster, bool progressive, bool partialUpgrade)
{
    // Bots below level 5 stay in their starting outfit: gear DB has little for them,
    // and specId is often 0 at low levels which would strip them naked (DestroyItemsVisitor
    // runs before the specId guard). Level 5 aligns with AcceptQuestAction's breadcrumb gate.
    if (bot->GetLevel() < 5)
    {
        sLog.outDetail("Bot #%d <%s> lvl %d: InitEquipment skipped (below level 5)",
            bot->GetGUIDLow(), bot->GetName(), bot->GetLevel());
        return;
    }

    uint32 oldGS = ai->GetEquipGearScore(bot, false, false);
    uint32 masterGS = 0;
    if(syncWithMaster && ai->GetMaster())
    {
        masterGS = ai->GetEquipGearScore(ai->GetMaster(), false, false);
    }

    // Check spec before wiping gear. Unknown spent-talents fall back to a
    // class-generic scale (issue #189 Phase 2) so gear never skips entirely.
    uint32 specId = sRandomItemMgr.GetPlayerSpecId(bot);
    if (specId == 0)
        specId = sRandomItemMgr.GetFallbackSpecId(bot->GetClass());
    if (specId == 0)
    {
        sLog.outDetail("Bot #%d <%s> lvl %d class %d: InitEquipment skipped (specId=0)",
            bot->GetGUIDLow(), bot->GetName(), bot->GetLevel(), bot->GetClass());
        return;
    }

    bool isRandomBot = sRandomBotFacade.IsRandomBot(bot) && PlayerbotAIStorage::Instance().GetAI(bot) && !PlayerbotAIStorage::Instance().GetAI(bot)->HasRealPlayerMaster() && !PlayerbotAIStorage::Instance().GetAI(bot)->IsInRealGuild();
    if (!incremental)
    {
        DestroyItemsVisitor visitor(bot);
        ai->InventoryIterateItems(&visitor, IterateItemsMask::ITERATE_ITEMS_IN_EQUIP);
    }

    // choose type of weapon
    uint32 weaponType = 0;
    if (bot->GetLevel() > 40 && (bot->GetClass() == CLASS_PRIEST || bot->GetClass() == CLASS_MAGE || bot->GetClass() == CLASS_WARLOCK || specId == 20 || specId == 22 || specId == 29 || specId == 31))
    {
        weaponType = sRandomBotFacade.GetValue(bot, "weaponType");
        if (!weaponType || !incremental)
        {
            weaponType = urand(0, 1) ? (uint32)INVTYPE_WEAPON : (uint32)INVTYPE_2HWEAPON;
            sRandomBotFacade.SetValue(bot, "weaponType", weaponType);
        }
    }

    // update only limited amount of slots with worst items
    std::map<uint32, bool> upgradeSlots;
    if (incremental && partialUpgrade)
    {
        std::vector<uint32> emptySlots;
        std::vector<uint32> itemIds;
        std::map<uint32, uint32> itemSlots;
        uint32 maxSlots = urand(1, 4);
        for (uint8 slot = 0; slot < EQUIPMENT_SLOT_END; ++slot)
            upgradeSlots[slot] = false;

        for (uint8 slot = 0; slot < EQUIPMENT_SLOT_END; ++slot)
        {
            if (slot == EQUIPMENT_SLOT_TABARD/* && !bot->GetGuildId()*/ || slot == EQUIPMENT_SLOT_BODY || slot == EQUIPMENT_SLOT_TRINKET1 || slot == EQUIPMENT_SLOT_TRINKET2)
                continue;

            Item* oldItem = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
            if (!oldItem)
            {
                emptySlots.push_back(slot);
                continue;
            }

            ItemPrototype const* proto = oldItem->GetProto();
            if (proto)
            {
                if (proto->ItemLevel > sPlayerbotAIConfig.randomGearMaxLevel)
                    continue;

                itemIds.push_back(proto->ItemId);
                itemSlots[proto->ItemId] = slot;
            }
        }

        std::sort(itemIds.begin(), itemIds.end(), [specId](int a, int b)
            {
                ItemPrototype const* proto1 = sObjectMgr.GetItemPrototype(a);
                ItemPrototype const* proto2 = sObjectMgr.GetItemPrototype(b);
                return proto1->Quality * proto1->ItemLevel <= proto2->Quality * proto2->ItemLevel;
            });

        uint32 counter = 0;
        for (auto emptySlot : emptySlots)
        {
            if (counter > maxSlots)
                break;

            upgradeSlots[emptySlot] = true;
            counter++;
        }
        for (auto itemId : itemIds)
        {
            if (counter > maxSlots)
                break;

            upgradeSlots[itemSlots[itemId]] = true;
            counter++;
        }
    }

    // unavailable legendaries list
    std::vector<uint32> lockedItems;
    lockedItems.push_back(18582); // Twin Blades of Azzinoth
    lockedItems.push_back(18583); // Right Blade
    lockedItems.push_back(18584); // Left Blade
    lockedItems.push_back(22736); // Andonisus, Reaper of Souls
    lockedItems.push_back(23051); // Glaive of the Defender
    lockedItems.push_back(13262); // Ashbringer
    lockedItems.push_back(17142); // Shard of the Defiler
    lockedItems.push_back(17782); // Talisman of Binding Shard
    lockedItems.push_back(12947); // Alex's Ring of Audacity

        // Item availability is derived from the active Tortoise item cache.

    // Fresh-seed path (owner spec): MakeComplete gears a brand-new bot.
    // Only this path gets the green/blue world-drop policy — epics only via
    // the rare world-epic gate, no PvP gear, uniform roll, every-slot retry.
    // Earned paths (syncWithMaster, explicit itemQuality, non-incremental
    // Randomize) keep deterministic best-first behaviour.
    bool const seedSpread = incremental && !syncWithMaster && itemQuality == 0;

    for(uint8 slot = 0; slot < EQUIPMENT_SLOT_END; ++slot)
    {
        if (slot == EQUIPMENT_SLOT_TABARD)
        {
            if (!sPlayerbotAIConfig.randomGearTabards || (urand(0, 100) < 100 * sPlayerbotAIConfig.randomGearTabardsChance))
                continue;
            if (bot->GetGuildId() && !sPlayerbotAIConfig.randomGearTabardsReplaceGuild)
                continue;
        }

        if (incremental && upgradeSlots.size() && upgradeSlots[slot] != true && !(slot == EQUIPMENT_SLOT_TRINKET1 || slot == EQUIPMENT_SLOT_TRINKET2))
            continue;

        // Fresh seed: a leftover starter shield sitting under a two-handed
        // main hand is not equippable in-game (2H disables the offhand slot),
        // so it is stale band-breaking junk — clear it and leave the slot
        // legally empty.
        if (seedSpread && slot == EQUIPMENT_SLOT_OFFHAND)
        {
            Item* mhItem = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_MAINHAND);
            Item* ohItem = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
            if (mhItem && mhItem->GetProto() && mhItem->GetProto()->InventoryType == INVTYPE_2HWEAPON && ohItem)
            {
                sLog.outDetail("Bot #%d <%s>: clearing stale offhand %u under 2H main hand",
                    bot->GetGUIDLow(), bot->GetName(), ohItem->GetEntry());
                bot->DestroyItem(ohItem->GetBagSlot(), ohItem->GetSlot(), true);
            }
        }

        uint32 searchLevel = level;
        uint32 quality = ITEM_QUALITY_POOR;
        uint32 maxItemLevel = sPlayerbotAIConfig.randomGearMaxLevel;
        bool progressiveGear = progressive;
        if(syncWithMaster && ai->GetMaster())
        {
            maxItemLevel = masterGS + sPlayerbotAIConfig.randomGearMaxDiff;
            progressiveGear = false;
            if (bot->GetLevel() != searchLevel)
            {
                searchLevel = bot->GetLevel();
            }
        }
        else
        {
            if (progressiveGear)
            {
                if (!incremental)
                {
                    if (level < 10)
                        quality = ITEM_QUALITY_POOR;
                    else if (level < 20)
                        quality = urand(ITEM_QUALITY_NORMAL, ITEM_QUALITY_UNCOMMON);
                    else if (level < 40)
                        quality = urand(ITEM_QUALITY_UNCOMMON, ITEM_QUALITY_RARE);
                    else if (level < 60)
                        quality = urand(ITEM_QUALITY_UNCOMMON, ITEM_QUALITY_RARE);
                    else
                        quality = urand(ITEM_QUALITY_RARE, ITEM_QUALITY_EPIC);
                }
                else
                {
                    if (level < 10)
                        quality = ITEM_QUALITY_POOR;
                    else if (level < 20)
                        quality = ITEM_QUALITY_NORMAL;
                    else if (level < 40)
                        quality = ITEM_QUALITY_UNCOMMON;
                    else if (level < 60)
                        quality = ITEM_QUALITY_UNCOMMON;
                    else
                        quality = ITEM_QUALITY_RARE;
                }
            }
            if (progressiveGear && !incremental && urand(0, 100) < 100 * sPlayerbotAIConfig.randomGearLoweringChance && quality > ITEM_QUALITY_NORMAL)
            {
                quality--;
            }
        }


        // quality selected from command
        bool setQuality = false;
        if (itemQuality > 0)
        {
            setQuality = true;
            quality = itemQuality;
        }

        // See seedSpread above for the fresh-seed vs earned-path split.
        bool found = false;
        uint32 attempts = 0;
        do
        {
            // pick random shirt or tabard
            if (slot == EQUIPMENT_SLOT_BODY || slot == EQUIPMENT_SLOT_TABARD)
            {
                std::vector<uint32> ids = sRandomItemMgr.Query(60, 1, 1, slot, 1);
                sLog.outDetail("Bot #%d %s:%d <%s>: %u possible items for slot %d", bot->GetGUIDLow(), bot->GetTeam() == ALLIANCE ? "A" : "H", bot->GetLevel(), bot->GetName(), uint32(ids.size()), slot);

                if (!ids.empty()) Shuffle(ids);

                for (uint32 index = 0; index < ids.size(); ++index)
                {
                    uint32 newItemId = ids[index];

                    // filter item level
                    ItemPrototype const* proto = sObjectMgr.GetItemPrototype(newItemId);
                    if (!proto)
                        continue;

                    // use only common items
                    if (proto->Quality > ITEM_QUALITY_UNCOMMON)
                        continue;

                    // skip unique-equippable items if already have one in inventory
                    if (proto->Flags & ITEM_FLAG_UNIQUE_EQUIPPABLE && bot->HasItemCount(proto->ItemId, 1))
                        continue;

                    if (proto->MaxCount && bot->HasItemCount(proto->ItemId, proto->MaxCount))
                        continue;

                    if (proto->ItemLevel > maxItemLevel)
                        continue;

                    uint32 newStatValue = sRandomItemMgr.GetLiveStatWeight(bot, newItemId, specId);
                    if (newStatValue <= 0)
                        continue;

                    Item* oldItem = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
                    ItemPrototype const* oldProto = oldItem ? oldItem->GetProto() : nullptr;

                    if (oldItem && oldProto->ItemId == newItemId)
                        continue;

                    uint16 eDest;
                    if (RandomBotFacade::CanEquipUnseenItem(bot, slot, eDest, newItemId) == EQUIP_ERR_OK)
                    {
                        if (oldItem)
                            bot->DestroyItem(oldItem->GetBagSlot(), oldItem->GetSlot(), true);

                        Item* pItem = bot->EquipNewItem(eDest, newItemId, true);
                        if (pItem)
                            found = true;
                    }
                    if (found)
                        break;
                }
            }
            else
            {
                std::vector<uint32> ids;
                // Rare world-epic gate (§6, owner spec): per slot, before the
                // shuffle, roll randomGearSeedEpicChance for an EPIC-only
                // query restricted to loot-attested BoE world epics. Falls
                // back to the normal band when the slot has no attested epic
                // (or the roll misses) — at 50-60 half the raw pool is epic,
                // so a merged shuffle would flood epics.
                bool seedEpic = false;
                if (seedSpread && !setQuality && level >= 30 &&
                    sPlayerbotAIConfig.randomGearSeedEpicChance > 0.0f &&
                    frand(0.0f, 1.0f) < sPlayerbotAIConfig.randomGearSeedEpicChance)
                    seedEpic = TrySeedEpicIds(bot, specId, slot, searchLevel, ids);
                // Fresh-seed quality band (owner spec): greens and blues mixed
                // from level 20, the progressive poor/normal floor below that.
                // Epics only through the rare world-epic gate above (a fresh
                // bot must not be overpowered). Earned/command paths keep
                // the quality-started band.
                uint32 qBegin = quality;
                uint32 qEnd = ITEM_QUALITY_ARTIFACT;
                if (seedSpread && !seedEpic)
                {
                    qBegin = level < 10 ? ITEM_QUALITY_POOR
                           : level < 20 ? ITEM_QUALITY_NORMAL
                                        : ITEM_QUALITY_UNCOMMON;
                    qEnd = ITEM_QUALITY_RARE + 1;
                }
                else if (seedEpic)
                {
                    qBegin = ITEM_QUALITY_EPIC;
                    qEnd = ITEM_QUALITY_EPIC + 1;
                }
                for (uint32 q = qBegin; q < qEnd; ++q)
                {
                    // quality selected from command
                    if (setQuality && q != quality)
                        continue;

                    QuerySeedCandidates(bot, specId, slot, searchLevel, maxItemLevel, q, ids);

                    // add one hand weapons for tanks
                    if ((specId == 3 || specId == 5) && slot == EQUIPMENT_SLOT_MAINHAND)
                    {
                        std::vector<uint32> oneHanded = sRandomItemMgr.Query(level, bot->GetClass(), uint8(specId), EQUIPMENT_SLOT_OFFHAND, q);
                        if (oneHanded.size())
                            ids.insert(ids.begin(), oneHanded.begin(), oneHanded.end());
                    }

                    // add one hand weapons for casters
                    if ((specId == 4 || (bot->GetClass() == CLASS_DRUID || bot->GetClass() == CLASS_PRIEST || bot->GetClass() == CLASS_MAGE || bot->GetClass() == CLASS_WARLOCK || (specId == 20 || specId == 22))) && slot == EQUIPMENT_SLOT_MAINHAND)
                    {
                        std::vector<uint32> oneHanded = sRandomItemMgr.Query(level, bot->GetClass(), uint8(specId), EQUIPMENT_SLOT_OFFHAND, q);
                        if (oneHanded.size())
                            ids.insert(ids.begin(), oneHanded.begin(), oneHanded.end());
                    }

                    // add weapons for dual wield
                    if (slot == EQUIPMENT_SLOT_MAINHAND && (bot->GetClass() == CLASS_ROGUE || specId == 2))
                    {
                        std::vector<uint32> oneHanded = sRandomItemMgr.Query(level, bot->GetClass(), uint8(specId), EQUIPMENT_SLOT_OFFHAND, q);
                        if (oneHanded.size())
                            ids.insert(ids.begin(), oneHanded.begin(), oneHanded.end());
                    }
                }

                // Epic roll missed the slot (no attested epic cached for it):
                // fall back to the normal band instead of leaving it empty.
                if (seedEpic && ids.empty())
                {
                    seedEpic = false;
                    for (uint32 q = level < 10 ? ITEM_QUALITY_POOR
                               : level < 20 ? ITEM_QUALITY_NORMAL
                                            : ITEM_QUALITY_UNCOMMON;
                         q < ITEM_QUALITY_RARE + 1; ++q)
                    {
                        if (setQuality && q != quality)
                            continue;
                        QuerySeedCandidates(bot, specId, slot, searchLevel, maxItemLevel, q, ids);
                    }
                }


                // Best-first for earned paths: the equip loop below takes the
                // first candidate that passes its filters, so ascending order
                // hands a level 19 bot a level 4 white (Issue #219).
                // Progressive variety comes from the quality band above, not
                // from starting at the worst item. The fresh seed instead
                // shuffles: a uniform roll over its green/blue band, so two
                // fresh bots never converge on the same best-in-slot kit
                // (owner spec: not super min-max).
                if (seedSpread)
                {
                    Shuffle(ids);
                }
                else if (incremental || !progressiveGear)
                {
                    // sort items based on stat value, ilvl or quality
                    std::sort(ids.begin(), ids.end(), [specId](int a, int b)
                        {
                            uint32 baseCompareA = (sRandomItemMgr.GetStatWeight(a, specId) + sRandomItemMgr.GetBestRandomEnchantStatWeight(a, specId)) * 1000;
                            uint32 baseCompareB = (sRandomItemMgr.GetStatWeight(b, specId) + sRandomItemMgr.GetBestRandomEnchantStatWeight(b, specId)) * 1000;
                            if (baseCompareA != baseCompareB)
                                return baseCompareA > baseCompareB;

                            ItemPrototype const* proto1 = sObjectMgr.GetItemPrototype(a);
                            ItemPrototype const* proto2 = sObjectMgr.GetItemPrototype(b);

                            baseCompareA += proto1->Quality * proto1->ItemLevel;
                            baseCompareB += proto2->Quality * proto2->ItemLevel;

                            return baseCompareA > baseCompareB;
                        });
                }
                else if (!ids.empty())
                {
                    Shuffle(ids);
                }

                std::vector<uint32> passingIds;

                Item* oldItem = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
                ItemPrototype const* oldProto = oldItem ? oldItem->GetProto() : nullptr;
                uint32 oldStatValue = oldItem ? sRandomItemMgr.GetLiveStatWeight(bot, oldProto->ItemId, specId) : 0;

                for (uint32 index = 0; index < ids.size(); ++index)
                {
                    uint32 newItemId = ids[index];

                    // Required-level gate (review fix): the cache window holds
                    // items up to 20 levels above their bracket, and the search
                    // descent only checks ItemLevel — so a level 55 bot is
                    // offered req-60 weapons that CanEquipItem then rejects,
                    // stranding the starter. Filter here, before sort/pick, so
                    // the window contains only wearable items.
                    uint32 reqLevelGate = sRandomItemMgr.GetMinLevelFromCache(newItemId);
                    if (reqLevelGate > (uint32)bot->GetLevel())
                        continue;

                    // Owner-configured exclusion list: applies to every gear
                    // path (seed, hire, upgrade), not just the fresh seed.
                    if (std::find(sPlayerbotAIConfig.randomGearBlacklist.begin(), sPlayerbotAIConfig.randomGearBlacklist.end(), newItemId) != sPlayerbotAIConfig.randomGearBlacklist.end())
                        continue;

                    // Fresh-seed provenance gate: tier cap + REP/PVP flags in
                    // one classification check (see PassesSeedProvenance).
                    // Earned-progression paths (syncWithMaster, explicit
                    // itemQuality, non-incremental Randomize) bypass it.
                    if (seedSpread && !PassesSeedProvenance(bot, newItemId))
                        continue;

                    // Paired-slot dupe guard: same entry in FINGER2/TRINKET2.
                    uint32 pairSlot = PairedSlot(slot);
                    if (pairSlot != EQUIPMENT_SLOT_END)
                    {
                        Item* pairItem = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, pairSlot);
                        if (pairItem && pairItem->GetEntry() == newItemId)
                            continue;
                    }
                    ItemPrototype const* proto = sObjectMgr.GetItemPrototype(newItemId);
                    if (!proto)
                        continue;
                    // filter tank weapons
                    if (slot == EQUIPMENT_SLOT_OFFHAND && (specId == 3 || specId == 5) && !(proto->Class == ITEM_CLASS_ARMOR && proto->SubClass == ITEM_SUBCLASS_ARMOR_SHIELD))
                        continue;

                    if (slot == EQUIPMENT_SLOT_MAINHAND && proto->Class == ITEM_CLASS_ARMOR && proto->SubClass == ITEM_SUBCLASS_ARMOR_SHIELD)
                        continue;

                    if (slot == EQUIPMENT_SLOT_MAINHAND && proto->InventoryType == INVTYPE_HOLDABLE)
                        continue;

                    // Caster off-hand ("held in off-hand") on a melee class:
                    // ShouldEquipArmorForSpec already rejects it, but the
                    // cached query can still return it - filter here too so
                    // warriors/rogues/hunters never equip it (Issue #219).
                    if (slot == EQUIPMENT_SLOT_OFFHAND && proto->InventoryType == INVTYPE_HOLDABLE &&
                        (bot->GetClass() == CLASS_WARRIOR || bot->GetClass() == CLASS_ROGUE || bot->GetClass() == CLASS_HUNTER))
                        continue;

                    // filter tank weapons
                    if (slot == EQUIPMENT_SLOT_MAINHAND && (specId == 3 || specId == 5) && !(proto->Class == ITEM_CLASS_WEAPON && proto->InventoryType != INVTYPE_HOLDABLE))
                        continue;

                    // make fury wear slow weapon as main hand
                    if (slot == EQUIPMENT_SLOT_MAINHAND && specId == 2 && proto->IsWeapon() && proto->Delay < 2000)
                        continue;

                    // classic enh shaman and retri paladin 60+ use weapon speed >= 3.0
                    if (slot == EQUIPMENT_SLOT_MAINHAND && (specId == 6 || specId == 21) && proto->IsWeapon() && bot->GetLevel() >= 60 && proto->InventoryType == INVTYPE_2HWEAPON && proto->Delay < 3000)
                        continue;

                    // filter caster weapons
                    if (weaponType)
                    {
                        if (slot == EQUIPMENT_SLOT_MAINHAND)
                        {
                            if (proto->Class == ITEM_CLASS_ARMOR && proto->SubClass == ITEM_SUBCLASS_ARMOR_SHIELD)
                                continue;
                            if (weaponType == INVTYPE_2HWEAPON && proto->Class == ITEM_CLASS_WEAPON && proto->InventoryType != INVTYPE_2HWEAPON)
                                continue;
                            if (weaponType != INVTYPE_2HWEAPON && proto->Class == ITEM_CLASS_WEAPON && proto->InventoryType == INVTYPE_2HWEAPON)
                                continue;
                            if (proto->InventoryType == INVTYPE_HOLDABLE)
                                continue;
                        }
                        if (slot == EQUIPMENT_SLOT_OFFHAND)
                        {
                            if (weaponType != INVTYPE_2HWEAPON && (bot->GetClass() == CLASS_PRIEST || bot->GetClass() == CLASS_MAGE || bot->GetClass() == CLASS_WARLOCK || (bot->GetClass() == CLASS_DRUID && (specId == 29 || specId == 31))) && proto->InventoryType != INVTYPE_HOLDABLE)
                                continue;
                            if (weaponType == INVTYPE_2HWEAPON && (bot->GetClass() == CLASS_PRIEST || bot->GetClass() == CLASS_MAGE || bot->GetClass() == CLASS_WARLOCK || (bot->GetClass() == CLASS_DRUID && (specId == 29 || specId == 31))))
                                continue;

                            if (weaponType != INVTYPE_2HWEAPON && proto->Class == ITEM_CLASS_WEAPON && proto->InventoryType == INVTYPE_2HWEAPON)
                                continue;
                        }
                    }


                    if (oldItem && oldProto->ItemId == newItemId)
                        continue;

                    // chance to get legendary
                    if (proto->Quality == ITEM_QUALITY_LEGENDARY && urand(0, 100) > 20)
                        continue;

                    // chance to not replace legendary
                    if (incremental && oldItem && oldProto->Quality == ITEM_QUALITY_LEGENDARY && urand(0, 100) > uint32(100 * 0.5f))
                        continue;

                    uint32 newStatValue = sRandomItemMgr.GetLiveStatWeight(bot, newItemId, specId);
                    if (newStatValue <= 0)
                        continue;

                    // check if already have reward
                    if (sRandomItemMgr.HasSameQuestRewards(bot, newItemId))
                        continue;

                    // Add random enchant value (the best one)
                    uint32 randomEnchBestId = 0;
                    uint32 randomEnchBestValue = 0;
                    if (proto->RandomProperty)
                    {
                        randomEnchBestId = sRandomItemMgr.CalculateBestRandomEnchantId(bot->GetClass(), specId, newItemId);
                        randomEnchBestValue = sRandomItemMgr.CalculateEnchantWeight(bot->GetClass(), specId, randomEnchBestId);
                        newStatValue += randomEnchBestValue;
                    }

                    // skip off hand if main hand is worse. Earned paths only:
                    // for the fresh seed this gate blocked every rogue/enhance
                    // offhand whose DPS did not beat the freshly rolled main
                    // hand (73/73 rogues came out two-handered-empty), and any
                    // green/blue offhand beats an empty slot (owner spec:
                    // every slot filled).
                    if (!seedSpread && proto->IsWeapon() && slot == EQUIPMENT_SLOT_OFFHAND && (bot->GetClass() == CLASS_ROGUE || specId == 2))
                        {
                            bool betterValue = false;
                            bool betterDamage = false;
                            bool betterDps = false;
                            Item* mhItem = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_MAINHAND);
                            if (mhItem && mhItem->GetProto())
                            {
                                ItemPrototype const* mhProto = mhItem->GetProto();
                                uint32 mhStatValue = sRandomItemMgr.GetLiveStatWeight(bot, mhProto->ItemId, specId);
                                if (newStatValue > mhStatValue)
                                    betterValue = true;

                                uint32 mhDps = 0;
                                uint32 ohDps = 0;
                                uint32 mhDamage = 0;
                                uint32 ohDamage = 0;
                                for (int i = 0; i < MAX_ITEM_PROTO_DAMAGES; i++)
                                {
                                    if (mhProto->Damage[i].DamageMax == 0)
                                        break;

                                    mhDamage = mhProto->Damage[i].DamageMax;

                                    mhDps = (mhProto->Damage[i].DamageMin + mhProto->Damage[i].DamageMax) / (float)(mhProto->Delay / 1000.0f) / 2;
                                }
                                for (int i = 0; i < MAX_ITEM_PROTO_DAMAGES; i++)
                                {
                                    if (proto->Damage[i].DamageMax == 0)
                                        break;

                                    ohDamage = proto->Damage[i].DamageMax;

                                    ohDps = (proto->Damage[i].DamageMin + proto->Damage[i].DamageMax) / (float)(proto->Delay / 1000.0f) / 2;
                                }
                                if (ohDps > mhDps)
                                    betterDps = true;
                                if (ohDamage > mhDamage)
                                    betterDamage = true;
                            }
                            if (betterDps || (betterDamage && betterValue))
                                continue;
                        }

                    if (incremental && oldItem && oldStatValue >= newStatValue && oldStatValue > 1)
                        continue;

                    // replace grey items right away
                    if ((incremental || progressiveGear) && oldItem && oldProto->Quality < ITEM_QUALITY_NORMAL && proto->Quality < ITEM_QUALITY_NORMAL && level > 5)
                        continue;

                    // Collect every passing candidate; CanEquipUnseenItem
                    // (slot rules, skill, unique-equip) is NOT evaluated here —
                    // it destroys nothing but is a per-candidate core call, so
                    // it runs in the retry loop on the equipping side. The
                    // seed's pool is pre-shuffled, so its sample cap keeps the
                    // roll uniform; earned paths collect unbounded so the retry
                    // can fall back exactly like the pre-window loop did.
                    passingIds.push_back(newItemId);
                    if (seedSpread && passingIds.size() >= kSeedCandidateSample)
                        break;
                }

                // Retry every passing candidate until one actually equips
                // (owner spec: every slot filled). Seed order = uniform
                // green/blue roll over the sample; earned order = best-first
                // fallback, matching the pre-window loop.
                for (uint32 attempt = 0; attempt < passingIds.size() && !found; ++attempt)
                {
                    uint32 newItemId = passingIds[attempt];
                    ItemPrototype const* proto = sObjectMgr.GetItemPrototype(newItemId);
                    if (proto)
                    {
                        uint32 newStatValue = sRandomItemMgr.GetLiveStatWeight(bot, newItemId, specId);
                        uint32 randomEnchBestId = 0;
                        if (proto->RandomProperty)
                        {
                            randomEnchBestId = sRandomItemMgr.CalculateBestRandomEnchantId(bot->GetClass(), specId, newItemId);
                            newStatValue += sRandomItemMgr.CalculateEnchantWeight(bot->GetClass(), specId, randomEnchBestId);
                        }
                        uint16 eDest;
                        if (RandomBotFacade::CanEquipUnseenItem(bot, slot, eDest, newItemId) == EQUIP_ERR_OK)
                        {
                            if (oldItem)
                                bot->DestroyItem(oldItem->GetBagSlot(), oldItem->GetSlot(), true);
                            Item* pItem = bot->EquipNewItem(eDest, newItemId, true);
                            if (pItem)
                            {
                                if (randomEnchBestId)
                                {
                                    pItem->SetItemRandomProperties(randomEnchBestId);
                                    bot->TransmogSetVisibleItemSlot(pItem->GetSlot(), pItem);
                                }
                                pItem->SetOwnerGuid(bot->getObjectGuid());
                                EnchantItem(pItem);
                                found = true;
                            }
                        }
                        if (found && incremental)
                        {
                            if (oldItem)
                                sLog.outDetail("Bot #%d %s:%d <%s>: Old Item: slot: %u, id: %u, value: %u (%s)", bot->GetGUIDLow(), bot->GetTeam() == ALLIANCE ? "A" : "H", bot->GetLevel(), bot->GetName(), slot, oldProto->ItemId, oldStatValue, oldProto->Name1);
                            sLog.outDetail("Bot #%d %s:%d <%s>: New Item: slot: %u, id: %u, value: %u (%s)", bot->GetGUIDLow(), bot->GetTeam() == ALLIANCE ? "A" : "H", bot->GetLevel(), bot->GetName(), slot, proto->ItemId, newStatValue, proto->Name1);
                        }
                    }
                }

                // Rejection forensics: pool>0 but nothing equipped — say whether
                // the filter chain emptied the list or the core equip check
                // refused every candidate (and why).
                if (!found)
                {
                    if (passingIds.empty())
                    {
                        sLog.outDetail("Bot #%d <%s>: slot %u: pool %zu, 0 passed filters (spec %u, weaponType %u)",
                            bot->GetGUIDLow(), bot->GetName(), slot, ids.size(), specId, weaponType);
                    }
                    else
                    {
                        uint16 probeDest = 0;
                        uint32 err = RandomBotFacade::CanEquipUnseenItem(bot, slot, probeDest, passingIds[0]);
                        sLog.outDetail("Bot #%d <%s>: slot %u: %zu passed filters, all equip attempts failed; first CanEquip err=%u, id=%u",
                            bot->GetGUIDLow(), bot->GetName(), slot, passingIds.size(), err, passingIds[0]);
                    }
                }
            }

            if (!found && quality > ITEM_QUALITY_NORMAL)
            {
                quality--;
            }

            attempts++;
            // The seed's quality band is level-derived, so quality degradation
            // cannot widen it — one round is the whole search on that path.
        } while (!found && !seedSpread && attempts < 3 && quality != ITEM_QUALITY_POOR);
        if (!found && seedSpread && level < 30 &&
            (slot == EQUIPMENT_SLOT_HEAD || slot == EQUIPMENT_SLOT_SHOULDERS ||
             slot == EQUIPMENT_SLOT_NECK || slot == EQUIPMENT_SLOT_FINGER1 ||
             slot == EQUIPMENT_SLOT_FINGER2 || slot == EQUIPMENT_SLOT_TRINKET1 ||
             slot == EQUIPMENT_SLOT_TRINKET2 || slot == EQUIPMENT_SLOT_RANGED) &&
            !bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
        {
            // Low-level coverage (§9): the scored pools for these slots are
            // thin at 10-29, so a usable item of the right slot/level beats
            // an empty slot. Sweep every cached quality for the slot and
            // take the first wearable, tier-passing candidate — no stat
            // gate beyond wearable, no junk force-fill above 30. Empty
            // slots only: never destroys anything.
            for (uint32 q = ITEM_QUALITY_POOR; q <= ITEM_QUALITY_RARE && !found; ++q)
            {
                std::vector<uint32> fallback = sRandomItemMgr.Query(level, bot->GetClass(), uint8(specId), slot, q);
                Shuffle(fallback);
                for (uint32 fallbackId : fallback)
                {
                    ItemPrototype const* proto = sObjectMgr.GetItemPrototype(fallbackId);
                    if (!proto)
                        continue;
                    if (sRandomItemMgr.GetMinLevelFromCache(fallbackId) > (uint32)bot->GetLevel())
                        continue;
                    if (!PassesSeedProvenance(bot, fallbackId))
                        continue;
                    uint32 pairSlot = PairedSlot(slot);
                    if (pairSlot != EQUIPMENT_SLOT_END)
                    {
                        Item* pairItem = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, pairSlot);
                        if (pairItem && pairItem->GetEntry() == fallbackId)
                            continue;
                    }
                    uint16 eDest;
                    if (RandomBotFacade::CanEquipUnseenItem(bot, slot, eDest, fallbackId) != EQUIP_ERR_OK)
                        continue;
                    // Guarded empty above: equip directly, destroy nothing.
                    if (bot->EquipNewItem(eDest, fallbackId, true))
                    {
                        sLog.outDetail("Bot #%d <%s>: slot %u low-level fallback equipped %u",
                            bot->GetGUIDLow(), bot->GetName(), slot, fallbackId);
                        found = true;
                        break;
                    }
                }
            }
        }
        if (!found)
        {
            if (slot != EQUIPMENT_SLOT_TRINKET1 && slot != EQUIPMENT_SLOT_TRINKET2)
                sLog.outDetail("Bot #%d %s:%d <%s>: no items for slot %d, quality >= %u", bot->GetGUIDLow(), bot->GetTeam() == ALLIANCE ? "A" : "H", bot->GetLevel(), bot->GetName(), slot, quality);
            continue;
        }
    }

    sLog.outDetail("Bot #%d %s:%d <%s>: InitEquipment done, GS %u -> %u",
        bot->GetGUIDLow(), bot->GetTeam() == ALLIANCE ? "A" : "H", bot->GetLevel(), bot->GetName(),
        oldGS, ai->GetEquipGearScore(bot, false, false));

    // Update stats here so the bots will benefit from the new equipped items' stats
    bot->InitStatsForLevel(true);
    bot->UpdateAllStats();

    if(syncWithMaster && ai->GetMaster())
    {
        uint32 newGS = ai->GetEquipGearScore(bot, false, false);
        std::stringstream message;
        message << "Synced gear with master. Old GS: " << oldGS << " New GS: " << newGS << " Master GS: " << masterGS;
        ai->TellPlayerNoFacing(ai->GetMaster(), message.str());
    }
}

void PlayerbotFactory::InitBags()
{
    auto pmo = sPerformanceMonitor.start(PERF_MON_RNDBOT, "PlayerbotFactory_Bags");
    InitLevelBags();
    for (uint8 slot = INVENTORY_SLOT_BAG_START; slot < INVENTORY_SLOT_BAG_END; ++slot)
    {
        Bag* pBag = (Bag*)bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
        if (!pBag)
        {
            bot->StoreNewItemInBestSlots(4500, 1); // add Traveler's Backpack if no bag in slot
        }
    }
}

// Level-tier vendor bags for the plain container slots. Quiver/ammo-pouch
// slots (ITEM_CLASS_QUIVER) are owned by InitAmmo's GetQuiver path and are
// never touched here; an occupied non-empty bag is never moved either (the
// core rejects relocating a non-empty bag from a specific slot). Tiers use
// npc_vendor rows only: 6 slot (4496 Small Brown Pouch) 1-9, 8 slot (4498
// Brown Leather Satchel) 10-19, 10 slot (4497 Heavy Brown Bag) 20-29,
// 12 slot (10050 Mageweave Bag) 30-39, 14 slot (14046 Runecloth Bag) 40-49,
// 16 slot (4500 Traveler's Backpack) 50+. Idempotent: keeps any bag that is
// already at or above the tier and only replaces a strictly smaller, empty
// plain-container bag.
void PlayerbotFactory::InitLevelBags()
{
    uint32 tierBag = 4496;
    uint32 tierSlots = 6;
    if (level >= 50) { tierBag = 4500; tierSlots = 16; }
    else if (level >= 40) { tierBag = 14046; tierSlots = 14; }
    else if (level >= 30) { tierBag = 10050; tierSlots = 12; }
    else if (level >= 20) { tierBag = 4497; tierSlots = 10; }
    else if (level >= 10) { tierBag = 4498; tierSlots = 8; }
    ItemPrototype const* tierProto = sObjectMgr.GetItemPrototype(tierBag);
    if (!tierProto)
        return;
    for (uint8 slot = INVENTORY_SLOT_BAG_START; slot < INVENTORY_SLOT_BAG_END; ++slot)
    {
        Item* bagItem = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
        if (!bagItem)
            continue;
        ItemPrototype const* proto = bagItem->GetProto();
        if (!proto || proto->Class == ITEM_CLASS_QUIVER)
            continue;
        if (bagItem->GetEntry() == tierBag || proto->ContainerSlots >= tierSlots)
            continue;
        Bag* pBag = bagItem->IsBag() ? (Bag*)bagItem : nullptr;
        if (pBag && !pBag->IsEmpty())
            continue;
        uint16 eDest = 0;
        if (RandomBotFacade::CanEquipUnseenItem(bot, slot, eDest, tierBag) != EQUIP_ERR_OK)
            continue;
        bot->DestroyItem(INVENTORY_SLOT_BAG_0, slot, true);
        Item* pItem = bot->EquipNewItem(eDest, tierBag, true);
        if (pItem)
            sLog.outDetail("Bot %d got level-tier bag %s (%u slots)", bot->GetGUIDLow(), tierProto->Name1, tierSlots);
    }
}

void PlayerbotFactory::EnchantItem(Item* item)
{
    if (!item)
        return;

    if (bot->GetLevel() < sPlayerbotAIConfig.minEnchantingBotLevel)
        return;

    ApplyBestEnchant(item);
}

void PlayerbotFactory::ApplyBestEnchant(Item* item)
{
    if (!item)
        return;

    uint32 specId = sRandomItemMgr.GetPlayerSpecId(bot);
    if (!specId)
        specId = sRandomItemMgr.GetFallbackSpecId(bot->GetClass());
    if (!specId)
        return;

    uint32 spellId = sRandomItemMgr.CalculateBestBotEnchantId(bot, specId, item);
    if (!spellId)
        return;

    ai->EnchantItemT(spellId, item->GetSlot(), item);
}

void PlayerbotFactory::InitAllSkills()
{
    auto pmo = sPerformanceMonitor.start(PERF_MON_RNDBOT, "PlayerbotFactory_Skills1");
    InitSkills();
    InitTradeSkills();
}

void PlayerbotFactory::InitTradeSkills()
{
    uint16 firstSkill = sRandomBotFacade.GetValue(bot, "firstSkill");
    uint16 secondSkill = sRandomBotFacade.GetValue(bot, "secondSkill");
    if (!firstSkill || !secondSkill)
    {
        std::vector<uint32> firstSkills;
        std::vector<uint32> secondSkills;
        switch (bot->GetClass())
        {
        case CLASS_WARRIOR:
        case CLASS_PALADIN:
            firstSkills.push_back(SKILL_BLACKSMITHING);
            secondSkills.push_back(SKILL_ENGINEERING);
            break;
        case CLASS_SHAMAN:
        case CLASS_DRUID:
        case CLASS_HUNTER:
        case CLASS_ROGUE:
            firstSkills.push_back(SKILL_SKINNING);
            firstSkills.push_back(SKILL_ENGINEERING);
            secondSkills.push_back(SKILL_LEATHERWORKING);
            break;
        }

        if (firstSkills.empty() || secondSkills.empty())
        {
            // Four pairs below: (0, 6) left three casters in seven without any profession
            // (firstSkill and secondSkill stayed 0, SetRandomSkill(0) is a no-op).
            switch (urand(0, 3))
            {
            case 0:
                firstSkill = SKILL_HERBALISM;
                secondSkill = SKILL_ALCHEMY;
                break;
            case 1:
                firstSkill = SKILL_HERBALISM;
                secondSkill = SKILL_MINING;
                break;
            case 2:
                firstSkill = SKILL_MINING;
                secondSkill = SKILL_SKINNING;
                break;
            case 3:
                firstSkill = SKILL_HERBALISM;
                secondSkill = SKILL_SKINNING;
            }
        }
        else
        {
            firstSkill = firstSkills[urand(0, firstSkills.size() - 1)];
            secondSkill = secondSkills[urand(0, secondSkills.size() - 1)];
        }

        sRandomBotFacade.SetValue(bot, "firstSkill", firstSkill);
        sRandomBotFacade.SetValue(bot, "secondSkill", secondSkill);
    }

    SetRandomSkill(SKILL_FIRST_AID);
    SetRandomSkill(SKILL_FISHING);
    SetRandomSkill(SKILL_COOKING);

    SetRandomSkill(firstSkill);
    SetRandomSkill(secondSkill);


    // learn recipies
    for (uint32 id = 0; id < sCreatureStorage.GetMaxEntry(); ++id)
    {
        CreatureInfo const* co = sCreatureStorage.LookupEntry<CreatureInfo>(id);
        if (!co)
            continue;

        if (co->trainer_type != TRAINER_TYPE_TRADESKILLS)
            continue;

        uint32 trainerId = co->trainer_id;
        if (!trainerId)
            trainerId = co->entry;

        TrainerSpellData const* trainer_spells = sObjectMgr.GetNpcTrainerTemplateSpells(trainerId);
        if (!trainer_spells)
            trainer_spells = sObjectMgr.GetNpcTrainerSpells(trainerId);

        if (!trainer_spells)
            continue;

        for (TrainerSpellMap::const_iterator itr = trainer_spells->spellList.begin(); itr != trainer_spells->spellList.end(); ++itr)
        {
            TrainerSpell const* tSpell = &itr->second;

            if (!tSpell)
                continue;

            TrainerSpellState state = bot->GetTrainerSpellState(tSpell);
            if (state != TRAINER_SPELL_GREEN)
                continue;

            SpellEntry const* proto = sServerFacade.LookupSpellInfo(tSpell->spell);
            if (!proto)
                continue;

            // Pet teaching spells (e.g. Survival Instinct 6666/6667) must not be learned by non-hunters
            bool teachesPetSpell = proto->Id == 6666 || proto->Id == 6667;
            for (int effect = 0; effect < 3 && !teachesPetSpell; ++effect)
            {
                if (proto->Effect[effect] == SPELL_EFFECT_LEARN_PET_SPELL ||
                    (proto->Effect[effect] == SPELL_EFFECT_LEARN_SPELL &&
                     (proto->EffectTriggerSpell[effect] == 6666 || proto->EffectTriggerSpell[effect] == 6667)))
                {
                    teachesPetSpell = true;
                }
            }

            if (teachesPetSpell && (bot->GetClass() != CLASS_HUNTER || bot->GetLevel() < 22))
                continue;

            SpellEntry const* spell = sServerFacade.LookupSpellInfo(tSpell->spell);
            if (spell)
            {
                std::string SpellName = spell->SpellName[0];
                if (spell->Effect[EFFECT_INDEX_1] == SPELL_EFFECT_SKILL_STEP)
                {
                    uint32 skill = spell->EffectMiscValue[EFFECT_INDEX_1];

                    if (skill && !bot->HasSkill(skill))
                    {
                        SkillLineEntry const* pSkill = sSkillLineStore.LookupEntry(skill);
                        if (pSkill)
                        {
                            if (SpellName.find("Apprentice") != std::string::npos && pSkill->categoryId == SKILL_CATEGORY_PROFESSION || pSkill->categoryId == SKILL_CATEGORY_SECONDARY)
                                continue;
                        }
                    }
                }
            }

            bool learned = false;
            for (int j = 0; j < 3; ++j)
            {
                if (proto->Effect[j] == SPELL_EFFECT_LEARN_SPELL && proto->EffectTriggerSpell[j])
                {
                    bot->LearnSpell(proto->EffectTriggerSpell[j], false);
                    learned = true;
                }
            }
            if (!learned)
                ai->CastSpell(tSpell->spell, bot);
        }
    }
}

void PlayerbotFactory::InitSkills()
{
// Riding skills requirements are different
    if (bot->GetLevel() >= 60)
        bot->SetSkill(SKILL_RIDING, 150, 150);
    else if (bot->GetLevel() >= 40)
        bot->SetSkill(SKILL_RIDING, 75, 75);
    else
        bot->SetSkill(SKILL_RIDING, 0, 0);

    // Defense only rises from being hit, which an instant level seed never
    // does: bots sat at 1/300 with 0% dodge/parry and took ~12% extra
    // crits/hits at 60. Grant the level cap like a played character.
    bot->SetSkill(SKILL_DEFENSE, bot->GetLevel() * 5, bot->GetLevel() * 5);

    uint32 skillLevel = bot->GetLevel() < 40 ? 0 : 1;
    switch (bot->GetClass())
    {
    case CLASS_WARRIOR:
    case CLASS_PALADIN:
        bot->SetSkill(SKILL_PLATE_MAIL, skillLevel, skillLevel);
        break;
    case CLASS_SHAMAN:
    case CLASS_HUNTER:
        bot->SetSkill(SKILL_MAIL, skillLevel, skillLevel);
    }

    switch (bot->GetClass())
    {
    case CLASS_DRUID:
        SetRandomSkill(SKILL_MACES);
        SetRandomSkill(SKILL_STAVES);
        SetRandomSkill(SKILL_2H_MACES);
        SetRandomSkill(SKILL_DAGGERS);
        SetRandomSkill(SKILL_FIST_WEAPONS);
        break;
    case CLASS_WARRIOR:
        SetRandomSkill(SKILL_SWORDS);
        SetRandomSkill(SKILL_AXES);
        SetRandomSkill(SKILL_BOWS);
        SetRandomSkill(SKILL_GUNS);
        SetRandomSkill(SKILL_MACES);
        SetRandomSkill(SKILL_2H_SWORDS);
        SetRandomSkill(SKILL_STAVES);
        SetRandomSkill(SKILL_2H_MACES);
        SetRandomSkill(SKILL_2H_AXES);
        SetRandomSkill(SKILL_DAGGERS);
        SetRandomSkill(SKILL_CROSSBOWS);
        SetRandomSkill(SKILL_POLEARMS);
        SetRandomSkill(SKILL_FIST_WEAPONS);
        SetRandomSkill(SKILL_THROWN);
        break;
    case CLASS_PALADIN:
        SetRandomSkill(SKILL_SWORDS);
        SetRandomSkill(SKILL_AXES);
        SetRandomSkill(SKILL_MACES);
        SetRandomSkill(SKILL_2H_SWORDS);
        SetRandomSkill(SKILL_2H_MACES);
        SetRandomSkill(SKILL_2H_AXES);
        SetRandomSkill(SKILL_POLEARMS);
        break;
    case CLASS_PRIEST:
        SetRandomSkill(SKILL_MACES);
        SetRandomSkill(SKILL_STAVES);
        SetRandomSkill(SKILL_DAGGERS);
        SetRandomSkill(SKILL_WANDS);
        break;
    case CLASS_SHAMAN:
        SetRandomSkill(SKILL_AXES);
        SetRandomSkill(SKILL_MACES);
        SetRandomSkill(SKILL_STAVES);
        SetRandomSkill(SKILL_2H_MACES);
        SetRandomSkill(SKILL_2H_AXES);
        SetRandomSkill(SKILL_DAGGERS);
        SetRandomSkill(SKILL_FIST_WEAPONS);
        break;
    case CLASS_MAGE:
    case CLASS_WARLOCK:
        SetRandomSkill(SKILL_SWORDS);
        SetRandomSkill(SKILL_STAVES);
        SetRandomSkill(SKILL_DAGGERS);
        SetRandomSkill(SKILL_WANDS);
        break;
    case CLASS_HUNTER:
        SetRandomSkill(SKILL_SWORDS);
        SetRandomSkill(SKILL_AXES);
        SetRandomSkill(SKILL_BOWS);
        SetRandomSkill(SKILL_GUNS);
        SetRandomSkill(SKILL_2H_SWORDS);
        SetRandomSkill(SKILL_STAVES);
        SetRandomSkill(SKILL_2H_AXES);
        SetRandomSkill(SKILL_DAGGERS);
        SetRandomSkill(SKILL_CROSSBOWS);
        SetRandomSkill(SKILL_POLEARMS);
        SetRandomSkill(SKILL_FIST_WEAPONS);
        SetRandomSkill(SKILL_THROWN);
        break;
    case CLASS_ROGUE:
        SetRandomSkill(SKILL_SWORDS);
        SetRandomSkill(SKILL_BOWS);
        SetRandomSkill(SKILL_GUNS);
        SetRandomSkill(SKILL_MACES);
        SetRandomSkill(SKILL_DAGGERS);
        SetRandomSkill(SKILL_CROSSBOWS);
        SetRandomSkill(SKILL_FIST_WEAPONS);
        SetRandomSkill(SKILL_THROWN);
        break;
    }

    // Dual Wield: warriors, hunters, rogues and shamans train it in the
    // world (91 trainers teach spell 1424, which chains to spell 674,
    // SPELL_EFFECT_DUAL_WIELD -> m_canDualWield). An instant GiveLevel
    // seed never runs that chain, the flag stays false, and FindEquipSlot
    // then refuses every offhand weapon for dual-wielders (NULL_SLOT ->
    // ERR_NOT_EQUIPPED), leaving them main-hand-only. Grant it once here;
    // re-learning is a no-op and the flag is idempotent.
    if (bot->GetLevel() >= 10 &&
        (bot->GetClass() == CLASS_WARRIOR || bot->GetClass() == CLASS_HUNTER ||
         bot->GetClass() == CLASS_ROGUE || bot->GetClass() == CLASS_SHAMAN))
    {
        if (!bot->HasSpell(1424))
            bot->LearnSpell(1424, false);
        if (!bot->HasSpell(674))
            bot->LearnSpell(674, false);
        bot->SetCanDualWield(true);
    }

    // Parry: same trainer-only chain (spell 3128 teaches 3127,
    // SPELL_EFFECT_PARRY -> m_canParry) at level 8 for these four classes.
    // Without it every bot parried 0% of attacks.
    if (bot->GetLevel() >= 8 &&
        (bot->GetClass() == CLASS_WARRIOR || bot->GetClass() == CLASS_PALADIN ||
         bot->GetClass() == CLASS_HUNTER || bot->GetClass() == CLASS_ROGUE))
    {
        if (!bot->HasSpell(3127))
            bot->LearnSpell(3127, false);
        bot->SetCanParry(true);
    }
}

void PlayerbotFactory::SetRandomSkill(uint16 id)
{
    uint32 maxValue = level * 5; // vanilla 60*5 = 300

// do not let skill go beyond limit even if maxlevel > blizzlike

    uint32 value = urand(maxValue - level, maxValue);
    uint32 curValue = bot->GetSkillValue(id);
    if (!bot->HasSkill(id) || value > curValue)
        bot->SetSkill(id, value, maxValue);
}

void PlayerbotFactory::InitClassLevelSpells()
{
    ChrClassesEntry const* classEntry = sChrClassesStore.LookupEntry(bot->GetClass());
    if (!classEntry)
        return;

    // The legacy Player::learnClassLevelSpells helper is compiled only for
    // the old vendored PlayerBots target and is intentionally a no-op in the
    // optional Tortoise core. Port its behavior here, using the core's live
    // quest, trainer, spell, talent, and class/race APIs instead of restoring
    // a PlayerBots-specific core dependency.
    for (auto const& questEntry : sObjectMgr.GetQuestTemplates())
    {
        Quest const* quest = questEntry.second.get();
        if (!quest || !quest->GetRequiredClasses() ||
            !bot->SatisfyQuestClass(quest, false) ||
            !bot->SatisfyQuestRace(quest, false) ||
            !bot->SatisfyQuestLevel(quest, false))
            continue;

        // This is the factory's level-60 initialization path, so include
        // high-level class quest rewards as the mature implementation does.
        bot->LearnQuestRewardedSpells(quest);
    }

    std::set<std::pair<bool, uint32>> processedTrainers;
    auto learnTrainerSpells = [this, classFamily = classEntry->spellfamily](TrainerSpellData const* trainerSpells)
    {
        if (!trainerSpells)
            return;

        for (auto const& spellEntry : trainerSpells->spellList)
        {
            TrainerSpell const* trainerSpell = &spellEntry.second;
            SpellEntry const* teachingSpell = sServerFacade.LookupSpellInfo(trainerSpell->spell);
            if (!teachingSpell || teachingSpell->Effect[0] != SPELL_EFFECT_LEARN_SPELL)
                continue;

            uint32 learnedSpellId = teachingSpell->EffectTriggerSpell[0];
            SpellEntry const* learnedSpell = sServerFacade.LookupSpellInfo(learnedSpellId);
            if (!learnedSpell)
                continue;

            uint32 reqLevel = 0;
            if (!bot->IsSpellFitByClassAndRace(learnedSpellId, &reqLevel))
                continue;

            TrainerSpellState state = bot->GetTrainerSpellState(trainerSpell);
            // A first-rank talent can appear in trainer data for a class that
            // already owns that talent spell; preserve the donor behavior that
            // allows the valid rank to be initialized without accepting other
            // red trainer entries.
            uint32 firstRank = sSpellMgr.GetFirstSpellInChain(learnedSpellId);
            bool validTalent = GetTalentSpellCost(firstRank) && bot->HasSpell(firstRank) &&
                reqLevel <= bot->GetLevel();

            if (state != TRAINER_SPELL_GREEN && !validTalent)
                continue;

            if (teachingSpell->SpellFamilyName != classFamily)
            {
                SkillLineAbilityMapBounds bounds = sSpellMgr.GetSkillLineAbilityMapBoundsBySpellId(learnedSpellId);
                if (bounds.first == bounds.second)
                    continue;

                SkillLineAbilityEntry const* skillInfo = bounds.first->second;
                if (!skillInfo)
                    continue;

                switch (skillInfo->skillId)
                {
                case SKILL_SUBTLETY:
                case SKILL_BEAST_MASTERY:
                case SKILL_SURVIVAL:
                case SKILL_DEFENSE:
                case SKILL_DUAL_WIELD:
                case SKILL_FERAL_COMBAT:
                case SKILL_PROTECTION:
                case SKILL_PLATE_MAIL:
                case SKILL_DEMONOLOGY:
                case SKILL_ENHANCEMENT:
                case SKILL_MAIL:
                case SKILL_HOLY2:
                case SKILL_LOCKPICKING:
                    break;
                default:
                    continue;
                }
            }

            if (!bot->IsSpellFitByClassAndRace(learnedSpellId) ||
                !SpellMgr::IsSpellValid(teachingSpell, bot, false))
                continue;

            for (uint32 effect = 0; effect < MAX_EFFECT_INDEX; ++effect)
            {
                if (teachingSpell->Effect[effect] != SPELL_EFFECT_LEARN_SPELL ||
                    !teachingSpell->EffectTriggerSpell[effect])
                    continue;

                bot->LearnSpell(teachingSpell->EffectTriggerSpell[effect], false);
            }
        }
    };

    for (auto const& creatureEntry : sObjectMgr.GetCreatureInfoMap())
    {
        CreatureInfo const* creature = creatureEntry.second.get();
        if (!creature || creature->trainer_type != TRAINER_TYPE_CLASS ||
            creature->trainer_class != bot->GetClass())
            continue;

        if (creature->trainer_id)
        {
            if (processedTrainers.insert({ true, creature->trainer_id }).second)
                learnTrainerSpells(sObjectMgr.GetNpcTrainerTemplateSpells(creature->trainer_id));
        }

        if (processedTrainers.insert({ false, creature->entry }).second)
            learnTrainerSpells(sObjectMgr.GetNpcTrainerSpells(creature->entry));
    }
}

void PlayerbotFactory::InitAvailableSpells()
{
    auto pmo = sPerformanceMonitor.start(PERF_MON_RNDBOT, "PlayerbotFactory_Spells1");
    bot->LearnDefaultSpells();
    InitClassLevelSpells();

    if (bot->GetClass() == CLASS_PALADIN)
    {
        // judgement missing
        if(!bot->HasSpell(20271))
        {
            bot->LearnSpell(20271, false);
        }
    }

    // add polymorph pig/turtle
    if (bot->GetClass() == CLASS_MAGE && bot->GetLevel() >= 60)
    {
        bot->LearnSpell(28271, false);
        bot->LearnSpell(28272, false);
    }

    // add inferno
    if (bot->GetClass() == CLASS_WARLOCK && !bot->HasSpell(1122) && bot->GetLevel() >= 50)
        bot->LearnSpell(1122, false);

    // Druid forms nobody teaches. Bear and Aquatic come from the quest "Body and
    // Heart" and appear on no trainer at all, so a bot never sees them - on the
    // realm this was found on, one character out of 2183 knew Bear Form while 31
    // had Cat Form from a trainer. Dire Bear follows from that: sixteen trainers
    // offer it at 40, but it needs Bear Form first, so nobody had it either.
    //
    // Without them a feral druid has no tanking shape at any level, whatever it
    // is specced as and whatever strategy it is handed.
    if (bot->GetClass() == CLASS_DRUID)
    {
        if (bot->GetLevel() >= 10 && !bot->HasSpell(5487))
            bot->LearnSpell(5487, false);   // Bear Form
        if (bot->GetLevel() >= 16 && !bot->HasSpell(1066))
            bot->LearnSpell(1066, false);   // Aquatic Form
        if (bot->GetLevel() >= 40 && !bot->HasSpell(9634))
            bot->LearnSpell(9634, false);   // Dire Bear Form
    }

    // add book spells
    if (bot->GetLevel() == 60)
    {
        std::vector<uint32> bookSpells;
        switch (bot->GetClass())
        {
        case CLASS_WARRIOR:
            bookSpells.push_back(25289);
            bookSpells.push_back(25288);
            bookSpells.push_back(25958);
            break;
        case CLASS_PALADIN:
            bookSpells.push_back(25291);
            bookSpells.push_back(25290);
            bookSpells.push_back(25292);
            break;
        case CLASS_HUNTER:
            bookSpells.push_back(25296);
            bookSpells.push_back(25294);
            bookSpells.push_back(25295);
            break;
        case CLASS_MAGE:
            bookSpells.push_back(23028);
            bookSpells.push_back(25345);
            bookSpells.push_back(25306);
            bookSpells.push_back(3723);
            bookSpells.push_back(28612);
            break;
        case CLASS_ROGUE:
            bookSpells.push_back(25300);
            bookSpells.push_back(25302);
            bookSpells.push_back(31016);
            break;
        case CLASS_PRIEST:
            bookSpells.push_back(25314);
            bookSpells.push_back(25315);
            bookSpells.push_back(25316);
            bookSpells.push_back(21564);
            bookSpells.push_back(27683);
            break;
        case CLASS_SHAMAN:
            bookSpells.push_back(29228);
            bookSpells.push_back(25359);
            bookSpells.push_back(25357);
            bookSpells.push_back(25361);
            break;
        case CLASS_WARLOCK:
            bookSpells.push_back(25311);
            bookSpells.push_back(25309);
            bookSpells.push_back(25307);
            bookSpells.push_back(28610);
            break;
        case CLASS_DRUID:
            bookSpells.push_back(31018);
            bookSpells.push_back(25297);
            bookSpells.push_back(25299);
            bookSpells.push_back(25298);
            bookSpells.push_back(21850);
            break;
        }

        for (auto spellId : bookSpells)
        {
            if (!bot->HasSpell(spellId))
                bot->LearnSpell(spellId, false);
        }
    }
}


void PlayerbotFactory::InitSpecialSpells()
{
    for (std::list<uint32>::iterator i = sPlayerbotAIConfig.randomBotSpellIds.begin(); i != sPlayerbotAIConfig.randomBotSpellIds.end(); ++i)
    {
        uint32 spellId = *i;

        SpellEntry const* spellInfo = sSpellTemplate.LookupEntry<SpellEntry>(spellId);

        if(spellInfo)
            bot->LearnSpell(spellId, false);
    }
}

void PlayerbotFactory::InitAmmo()
{
    auto pmo = sPerformanceMonitor.start(PERF_MON_RNDBOT, "PlayerbotFactory_Ammo");
    if (bot->GetClass() != CLASS_HUNTER && bot->GetClass() != CLASS_ROGUE && bot->GetClass() != CLASS_WARRIOR)
        return;

    Item* pItem = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_RANGED);
    if (!pItem)
        return;

    uint32 subClass = 0;
    switch (pItem->GetProto()->SubClass)
    {
    case ITEM_SUBCLASS_WEAPON_GUN:
        subClass = ITEM_SUBCLASS_BULLET;
        break;
    case ITEM_SUBCLASS_WEAPON_BOW:
    case ITEM_SUBCLASS_WEAPON_CROSSBOW:
        subClass = ITEM_SUBCLASS_ARROW;
        break;
    case ITEM_SUBCLASS_WEAPON_THROWN:
        if (bot->GetClass() != CLASS_HUNTER)
        {
            subClass = ITEM_SUBCLASS_THROWN;
            break;
        }
    }

    if (!subClass)
        return;

    // Ammo container (owner spec): nothing ever equipped a quiver — fresh
    // hunters carried the level-1 Light Quiver from CharacterCreation
    // forever. Give the best vendor-sold container the level allows
    // (RandomItemMgr::GetQuiver, vendor-joined so never a raid drop); the
    // core allows exactly one quiver/pouch, so a strictly worse one is
    // dropped first and CanEquipItem re-checks the same uniqueness.
    if (subClass == ITEM_SUBCLASS_ARROW || subClass == ITEM_SUBCLASS_BULLET)
    {
        uint32 quiverId = sRandomItemMgr.GetQuiver(level);
        ItemPrototype const* bestQuiver = quiverId ? sObjectMgr.GetItemPrototype(quiverId) : nullptr;
        if (bestQuiver)
        {
            bool haveGood = false;
            for (uint8 bagSlot = INVENTORY_SLOT_BAG_START; bagSlot < INVENTORY_SLOT_BAG_END; ++bagSlot)
            {
                Item* bagItem = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, bagSlot);
                if (!bagItem || bagItem->GetProto()->Class != ITEM_CLASS_QUIVER)
                    continue;
                if (bagItem->GetEntry() == quiverId || bagItem->GetProto()->RequiredLevel >= bestQuiver->RequiredLevel)
                {
                    haveGood = true;
                    continue;
                }
                bot->DestroyItem(INVENTORY_SLOT_BAG_0, bagSlot, true);
            }
            if (!haveGood)
            {
                uint16 eDest;
                if (RandomBotFacade::CanEquipUnseenItem(bot, INVENTORY_SLOT_BAG_START, eDest, quiverId) == EQUIP_ERR_OK)
                    bot->EquipNewItem(eDest, quiverId, true);
            }
        }
    }

    // Tortoise: thrown weapons are single repairable items (Stackable=1), not 200-stack ammo.
    // Give exactly 1 and return so we don't fill the bot's bags with 200 individual knives.
    if (subClass == ITEM_SUBCLASS_THROWN)
    {
        uint32 entry = sRandomItemMgr.GetAmmo(level, subClass);
        if (entry && bot->GetItemCount(entry) == 0)
            bot->StoreNewItemInInventorySlot(entry, 1);
        if (entry && bot->GetUInt32Value(PLAYER_AMMO_ID) != entry)
            bot->SetAmmo(entry);
        return;
    }

    uint32 entry = bot->GetUInt32Value(PLAYER_AMMO_ID);
    // A hunter that swapped gun->bow (or bow->gun) keeps the old ammo id
    // and a stale bullet/arrow stock: auto shot then has no valid ammo
    // (Issue #219). Verify against the equipped weapon and resync.
    if (entry)
    {
        ItemPrototype const* ammoProto = sObjectMgr.GetItemPrototype(entry);
        if (!ammoProto || ammoProto->Class != ITEM_CLASS_PROJECTILE || ammoProto->SubClass != subClass)
            entry = 0;
    }
    uint32 count = entry ? bot->GetItemCount(entry) / 200 : 0;
    uint32 maxCount = 5 + level / 10;

    if (ai->HasCheat(BotCheatMask::item))
        maxCount = 1;

    if (!entry || count <= 2)
    {
        entry = sRandomItemMgr.GetAmmo(level, subClass);
        count = bot->GetItemCount(entry) / 200;
    }

    if (!entry)
        return;

    // Owner spec: no starter-tier leftovers. Once the tier is known, remove
    // every other projectile stack from the bags — the fresh-seed case has
    // ammoId = 0 with a Rough Arrow stack already in the bags, which the
    // old stale-entry destroy could never match (52-stack sighting on the
    // live pool).
    {
        FindAmmoVisitor ammoVisitor(bot, pItem->GetProto()->SubClass);
        ai->InventoryIterateItems(&ammoVisitor, IterateItemsMask::ITERATE_ITEMS_IN_BAGS);
        for (Item* oldAmmo : ammoVisitor.GetResult())
        {
            if (oldAmmo && oldAmmo->GetEntry() != entry)
                bot->DestroyItem(oldAmmo->GetBagSlot(), oldAmmo->GetSlot(), true);
        }
    }

    if (count < maxCount)
    {
        for (uint32 i = 0; i < maxCount - count; i++)
        {
            Item* newItem = bot->StoreNewItemInInventorySlot(entry, 200);
        }
    }

    if(bot->GetUInt32Value(PLAYER_AMMO_ID) != entry)
        bot->SetAmmo(entry);
}

void PlayerbotFactory::InitTurtleMount()
{
    uint32 gate = sPlayerbotAIConfig.turtleMountAtLevel;
    if (!gate || bot->GetLevel() < gate || bot->GetLevel() >= 40)
        return;
    // Pool bots only. Hired companions are pool bots (record->random stays
    // true after Hire() claims them), so recruiter hires at 18-39 come
    // through InitMounts and get the turtle here. The player's own alts
    // (same-account characters driven via .bot add) are not pool bots, so
    // this gate keeps them quest-bound. Free-alts are non-pool accounts and
    // never reach here either.
    if (!sRandomBotFacade.IsRandomBot(bot))
        return;
    // Same as using item 23720: the core collection script teaches the mapped
    // mount spell (30174) from collection_mount. Item is class/race agnostic
    // (allowable masks -1) and has no required level, so the gate above is the
    // only filter; the quest MinLevel (18) is the default value.
    if (bot->HasSpell(30174))
        return;
    bot->LearnSpell(30174, false);
    sLog.outDetail("Bot %d (%d) learned turtle mount 30174", bot->GetGUIDLow(), bot->GetLevel());
}

void PlayerbotFactory::InitMounts()
{
    // Sub-40 turtle first: seed/refresh at 18..39 grants the Swift Riding
    // Turtle the same way item use would, without touching the 40/60 path.
    InitTurtleMount();
    auto pmo = sPerformanceMonitor.start(PERF_MON_RNDBOT, "PlayerbotFactory_Mounts");
    uint32 firstmount =
        40
        ;

    uint32 secondmount =
        60
        ;

    uint32 thirdmount =
        90
        ;

    uint32 fourthmount =
        90
        ;

    if (bot->GetLevel() < firstmount)
        return;

    std::map<uint8, std::map<uint32, std::vector<uint32> > > mounts;
    std::vector<uint32> slow, fast;
    switch (bot->GetRace())
    {
    case RACE_HUMAN:
        slow = { 470, 6648, 458, 472 };
        fast = { 23228, 23227, 23229 };
        break;
    case RACE_ORC:
        slow = { 6654, 6653, 580 };
        fast = { 23250, 23252, 23251 };
        break;
    case RACE_DWARF:
        slow = { 6899, 6777, 6898 };
        fast = { 23238, 23239, 23240 };
        break;
    case RACE_NIGHTELF:
        slow = { 10789, 8394, 10793 };
        fast = { 23221, 23219, 23338 };
        break;
    case RACE_UNDEAD:
        slow = { 17463, 17464, 17462 };
        fast = { 17465, 23246 };
        break;
    case RACE_TAUREN:
        slow = { 18990, 18989 };
        fast = { 23249, 23248, 23247 };
        break;
    case RACE_GNOME:
        slow = { 10969, 17453, 10873, 17454 };
        fast = { 23225, 23223, 23222 };
        break;
    case RACE_TROLL:
        slow = { 8395, 10796, 10799 };
        fast = { 23241, 23242, 23243 };
        break;
    default:
        // Tortoise/custom races do not have a safe racial spell list in this
        // donor-era switch. Leave the lists empty and rely only on the
        // authoritative collection_mount rows below, whose item class/race
        // masks are checked against the actual core character data.
        sLog.outDetail("Bot %d (%u) has no donor racial mount list; using core collection mounts only.",
            bot->GetGUIDLow(), bot->GetRace());
        break;
    }

    // Tortoise collection mounts are represented by a generic item spell and a
    // core-owned collection_mount mapping. Include eligible mapped spells in
    // the factory pool so randomized characters do not lose custom mounts
    // merely because their item has no classic per-item mount spell. Factory
    // initialization intentionally teaches the mapped spell, matching the
    // existing classic factory mount path; real item use remains owned by the
    // core collection spell script.
    for (auto const& [itemId, spellId] : CollectionMounts())
    {
        ItemPrototype const* proto = sObjectMgr.GetItemPrototype(itemId);
        SpellEntry const* spellInfo = sServerFacade.LookupSpellInfo(spellId);

        if (!proto || !spellInfo || MountValue::GetSpeed(spellId) == 0)
            continue;
        if (proto->RequiredLevel > bot->GetLevel())
            continue;
        if (proto->AllowableClass && (proto->AllowableClass & bot->GetClassMask()) == 0)
            continue;
        if (proto->AllowableRace && (proto->AllowableRace & bot->GetRaceMask()) == 0)
            continue;

        if (MountValue::GetSpeed(spellId) >= 100)
            fast.push_back(spellId);
        else
            slow.push_back(spellId);
    }

    mounts[bot->GetRace()][0] = slow;
    mounts[bot->GetRace()][1] = fast;
    for (uint32 type = 0; type < 2; type++)
    {
        if (bot->GetLevel() < secondmount && type == 1)
            continue;

        // The Vanilla product has slow and epic ground riding only.
        std::vector<uint32> const& available = mounts[bot->GetRace()][type];
        if (available.empty())
            continue;

        uint32 spell = available[urand(0, available.size() - 1)];
        if (spell)
        {
            bot->LearnSpell(spell, false);
            sLog.outDetail("Bot %d (%d) learned %s mount %d", bot->GetGUIDLow(), bot->GetLevel(), type == 0 ? "slow" : "fast", spell);
        }
    }
}

void PlayerbotFactory::InitPotions()
{
    auto pmo = sPerformanceMonitor.start(PERF_MON_RNDBOT, "PlayerbotFactory_Potions");
    uint32 effects[] = { SPELL_EFFECT_HEAL, SPELL_EFFECT_ENERGIZE };
    for (int i = 0; i < 2; ++i)
    {
        uint32 effect = effects[i];

        if (effect == SPELL_EFFECT_ENERGIZE && bot->GetPowerType() != POWER_MANA) // Do not give manapots to non-mana users.
            continue;

        FindPotionVisitor visitor(bot, effect);
        ai->InventoryIterateItems(&visitor, IterateItemsMask::ITERATE_ITEMS_IN_BAGS);
        if (!visitor.GetResult().empty()) continue;

        uint32 itemId = sRandomItemMgr.GetRandomPotion(level, effect);
        if (!itemId)
        {
            sLog.outDetail("No potions (type %d) available for bot %s (%d level)", effect, bot->GetName(), bot->GetLevel());
            continue;
        }

        ItemPrototype const* proto = sObjectMgr.GetItemPrototype(itemId);
        if (!proto) continue;

        uint32 maxCount = proto->GetMaxStackSize();
        Item* newItem = bot->StoreNewItemInInventorySlot(itemId, urand(maxCount / 2, maxCount));
    }
}

void PlayerbotFactory::InitFood()
{
    auto pmo = sPerformanceMonitor.start(PERF_MON_RNDBOT, "PlayerbotFactory_Food");
    uint32 categories[] = { 11, 59 };
    for (int i = 0; i < 2; ++i)
    {
        uint32 category = categories[i];

        if (category == 59 && bot->GetPowerType() != POWER_MANA) // Do not give drinks to non-mana users.
            continue;

        uint32 itemId = sRandomItemMgr.GetFood(level, category);
        if (!itemId)
        {
            sLog.outDetail("No food (category %d) available for bot %s (%d level)", category, bot->GetName(), bot->GetLevel());
            continue;
        }
        ItemPrototype const* proto = sObjectMgr.GetItemPrototype(itemId);
        if (!proto) continue;

        // Fresh seeds carry level-1 starter food; the old keep-if-any rule
        // preserved it forever (owner spec: food adequate to the level).
        // Drop strictly lower-tier food, keep anything already as good.
        FindFoodVisitor visitor(bot, category);
        ai->InventoryIterateItems(&visitor, IterateItemsMask::ITERATE_ITEMS_IN_BAGS);
        bool haveGood = false;
        for (Item* foodItem : visitor.GetResult())
        {
            if (!foodItem || !foodItem->GetProto())
                continue;
            if (foodItem->GetEntry() == itemId || foodItem->GetProto()->ItemLevel >= proto->ItemLevel)
                haveGood = true;
            else
                bot->DestroyItem(foodItem->GetBagSlot(), foodItem->GetSlot(), true);
        }
        if (haveGood) continue;

        uint32 maxCount = proto->GetMaxStackSize();
        Item* newItem = bot->StoreNewItemInInventorySlot(itemId, urand(maxCount / 2, maxCount));
   }
}

void PlayerbotFactory::InitReagents()
{
    auto pmo = sPerformanceMonitor.start(PERF_MON_RNDBOT, "PlayerbotFactory_Reagents");
    std::list<uint32> items;
    uint32 regCount = 1;
    switch (bot->GetClass())
    {
    case CLASS_MAGE:
        regCount = 2;
        if (bot->GetLevel() > 11)
            items = { 17056 };
        if (bot->GetLevel() > 19)
            items = { 17056, 17031 };
        if (bot->GetLevel() > 35)
            items = { 17056, 17031, 17032 };
        if (bot->GetLevel() > 55)
            items = { 17056, 17031, 17032, 17020 };
        break;
    case CLASS_DRUID:
        regCount = 2;
        if (bot->GetLevel() > 19)
            items = { 17034 };
        if (bot->GetLevel() > 29)
            items = { 17035 };
        if (bot->GetLevel() > 39)
            items = { 17036 };
        if (bot->GetLevel() > 49)
            items = { 17037, 17021 };
        if (bot->GetLevel() > 59)
            items = { 17038, 17026 };
        break;
    case CLASS_PALADIN:
        regCount = 3;
        // Divine Intervention (spell 19752) needs Symbol of Divinity 17033
        // from level 30; Greater Blessings need Symbol of Kings 21177 from
        // 52 (spell 25782). The old table seeded Kings only past 50 and
        // never Divinity, so live paladins match 0/5 brackets below 50.
        if (bot->GetLevel() >= 30)
            items = { 17033 };
        if (bot->GetLevel() >= 52)
            items = { 17033, 21177 };
        break;
    case CLASS_SHAMAN:
        regCount = 1;
        if (bot->GetLevel() > 22)
            items = { 17057 };
        if (bot->GetLevel() > 28)
            items = { 17057, 17058 };
        if (bot->GetLevel() > 29)
            items = { 17057, 17058, 17030 };
        break;
    case CLASS_WARLOCK:
        regCount = 10;
        if (bot->GetLevel() > 9)
            items = { 6265 };
        if (bot->GetLevel() > 49)
            items = { 6265, 5565 };
        break;
    case CLASS_PRIEST:
        regCount = 3;
        if (bot->GetLevel() > 48)
            items = { 17028 };
        if (bot->GetLevel() > 55)
            items = { 17028, 17029 };
        break;
    case CLASS_ROGUE:
        regCount = 1;
        if (bot->GetLevel() > 21)
            items = { 5140 };
        if (bot->GetLevel() > 33)
            items = { 5140, 5530 };
        break;
    }

    for (std::list<uint32>::iterator i = items.begin(); i != items.end(); ++i)
    {
        ItemPrototype const* proto = sObjectMgr.GetItemPrototype(*i);
        if (!proto)
        {
            sLog.outError("No reagent (ItemId %d) found for bot %d (Class:%d)", *i, bot->GetGUIDLow(), bot->GetClass());
            continue;
        }

        uint32 maxCount = proto->GetMaxStackSize();
        uint32 want = maxCount * std::max<uint32>(1, regCount);

        QueryItemCountVisitor visitor(*i);
        ai->InventoryIterateItems(&visitor, IterateItemsMask::ITERATE_ITEMS_IN_BAGS);
        uint32 have = visitor.GetCount() > 0 ? (uint32)visitor.GetCount() : 0;
        if (have >= want) continue;

        uint32 randCount = urand(maxCount / 2, want);
        uint32 addCount = std::min(randCount, want - have);
        if (!addCount) continue;

        Item* newItem = bot->StoreNewItemInInventorySlot(*i, addCount);

        sLog.outDetail("Bot %d got reagent %s x%d", bot->GetGUIDLow(), proto->Name1, addCount);
    }

    for (PlayerSpellMap::iterator itr = bot->GetSpellMap().begin(); itr != bot->GetSpellMap().end(); ++itr)
    {
        uint32 spellId = itr->first;

        if (itr->second.state == PLAYERSPELL_REMOVED || itr->second.disabled || IsPassiveSpell(spellId))
            continue;

        const SpellEntry* pSpellInfo = sServerFacade.LookupSpellInfo(spellId);
        if (!pSpellInfo)
            continue;

        if (pSpellInfo->Effect[0] == SPELL_EFFECT_LEARN_SPELL)
            continue;

        for (const auto& totem : pSpellInfo->Totem)
        {
            if (totem && !bot->HasItemCount(totem, 1))
            {
                ItemPrototype const* proto = sObjectMgr.GetItemPrototype(totem);
                if (!proto)
                {
                    sLog.outError("No totem (ItemId %d) found for bot %d (Class:%d)", totem, bot->GetGUIDLow(), bot->GetClass());
                    continue;
                }

                Item* newItem = bot->StoreNewItemInInventorySlot(totem, 1);

                sLog.outDetail("Bot %d got totem %s x%d", bot->GetGUIDLow(), proto->Name1, 1);
            }
        }
    }
}

void PlayerbotFactory::InitInventorySkill()
{
    if (bot->HasSkill(SKILL_MINING)) {
        StoreItem(2901, 1); // Mining Pick
    }
    if (bot->HasSkill(SKILL_BLACKSMITHING) || bot->HasSkill(SKILL_ENGINEERING)) {
        StoreItem(5956, 1); // Blacksmith Hammer
    }
    if (bot->HasSkill(SKILL_ENGINEERING)) {
        StoreItem(6219, 1); // Arclight Spanner
    }
    if (bot->HasSkill(SKILL_ENCHANTING)) {
        StoreItem(16207, 1); // Runed Arcanite Rod
    }
    if (bot->HasSkill(SKILL_SKINNING)) {
        StoreItem(7005, 1); // Skinning Knife
    }
    // Fishing is impossible without a pole in hand (FishAction checks the
    // main-hand subclass); the donor skill list never granted one, so live
    // tool ownership is 0% in every band. StoreItem is already idempotent.
    if (bot->HasSkill(SKILL_FISHING)) {
        StoreItem(6256, 1); // Fishing Pole
    }
}

Item* PlayerbotFactory::StoreItem(uint32 itemId, uint32 count, bool ignoreCount)
{
    if (!ignoreCount)
    {
        if (bot->HasItemCount(itemId, count))
            return nullptr;
    }

    ItemPrototype const* proto = sObjectMgr.GetItemPrototype(itemId);
    ItemPosCountVec sDest;
    InventoryResult msg = bot->CanStoreNewItem(INVENTORY_SLOT_BAG_0, NULL_SLOT, sDest, itemId, count);
    if (msg != EQUIP_ERR_OK)
        return NULL;

    return bot->StoreNewItem(sDest, itemId, true, Item::GenerateItemRandomPropertyId(itemId));
}

void PlayerbotFactory::EnchantEquipment()
{
    if (bot->GetLevel() >= sPlayerbotAIConfig.minEnchantingBotLevel)
    {
        for (uint8 slot = 0; slot < SLOT_EMPTY; slot++)
        {
            Item* item = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
            if (item)
            {
                EnchantItem(item);
            }
        }
    }
}
