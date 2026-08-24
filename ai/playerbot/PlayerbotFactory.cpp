
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

#define PLAYER_SKILL_INDEX(x)       (PLAYER_SKILL_INFO_1_1 + ((x)*3))

uint32 PlayerbotFactory::tradeSkills[] =
{
    SKILL_ALCHEMY,
    SKILL_ENCHANTING,
    SKILL_SKINNING,
    SKILL_TAILORING,
    SKILL_LEATHERWORKING,
    SKILL_ENGINEERING,
    SKILL_HERBALISM,
    SKILL_MINING,
    SKILL_BLACKSMITHING,
    SKILL_COOKING,
    SKILL_FIRST_AID,
    SKILL_FISHING
};

std::list<uint32> PlayerbotFactory::classQuestIds;
std::list<uint32> PlayerbotFactory::specialQuestIds;

TaxiNodeLevelContainer PlayerbotFactory::overworldTaxiNodeLevelsA;
TaxiNodeLevelContainer PlayerbotFactory::overworldTaxiNodeLevelsH;

void PlayerbotFactory::Init()
{
    if (sPlayerbotAIConfig.randomBotPreQuests) {
        ObjectMgr::QuestMap const& questTemplates = sObjectMgr.GetQuestTemplates();
        for (ObjectMgr::QuestMap::const_iterator i = questTemplates.begin(); i != questTemplates.end(); ++i)
        {
            uint32 questId = i->first;
            Quest const* quest = i->second.get();

            if (!quest->GetRequiredClasses() || quest->IsRepeatable() || quest->GetMinLevel() < 10)
                continue;

            AddPrevQuests(questId, classQuestIds);
            classQuestIds.remove(questId);
            classQuestIds.push_back(questId);
        }
        for (std::list<uint32>::iterator i = sPlayerbotAIConfig.randomBotQuestIds.begin(); i != sPlayerbotAIConfig.randomBotQuestIds.end(); ++i)
        {
            uint32 questId = *i;
            AddPrevQuests(questId, specialQuestIds);
            specialQuestIds.remove(questId);
            specialQuestIds.push_back(questId);
        }
    }

    overworldTaxiNodeLevelsH.clear();
    overworldTaxiNodeLevelsA.clear();

    for (uint32 i = 1; i < sTaxiNodesStore.GetNumRows(); ++i)
    {
        TaxiNodesEntry const* taxiNode = sTaxiNodesStore.LookupEntry(i);

        if (!taxiNode)
            continue;

        // The taximask is eight words and AppendTaximaskTo puts exactly those
        // eight on the wire, so 256 is a protocol limit, not a tunable. Turtle
        // numbers its two Landing Pod nodes 508 and 509, which land on field 15
        // of an eight-field mask - seven words past it, straight into
        // m_TaxiDestinations. SetTaximaskNode bounds-checks that now, but there
        // is no point offering it a node it can never record.
        if (i > TaxiMaskSize * 32)
            continue;

        WorldPosition taxiPosition(taxiNode);

        if (!taxiPosition.isOverworld())
            continue;

        TaxiNodeLevel taxiNodeLevel = TaxiNodeLevel();

        taxiNodeLevel.Index = i;
        taxiNodeLevel.MapId = taxiNode->map_id;
        taxiNodeLevel.Level = taxiPosition.getAreaLevel();

        if (taxiNode->MountCreatureID[0])
            overworldTaxiNodeLevelsH.push_back(taxiNodeLevel);

        if (taxiNode->MountCreatureID[1])
            overworldTaxiNodeLevelsA.push_back(taxiNodeLevel);
    }
}

void PlayerbotFactory::Prepare()
{
    if (sServerFacade.UnitIsDead(bot))
    {
        bot->ResurrectPlayer(1.0f, false);
        bot->SpawnCorpseBones();
    }

    bot->CombatStop(true);
    // Was commented out - re-enabled so a manual .bot random/.bot init on a
    // still-fresh bot (below the configured starting level) brings it up to
    // randombotStartingLevel. GiveLevel (not SetLevel) so stats/HP/mana/talent
    // points come along too - raw SetLevel only touches the level field.
    if (sPlayerbotAIConfig.disableRandomLevels)
    {
        if (bot->GetLevel() < sPlayerbotAIConfig.randombotStartingLevel)
        {
            bot->GiveLevel(sPlayerbotAIConfig.randombotStartingLevel);
        }
    }

    if (!sPlayerbotAIConfig.disableRandomLevels)
    {
        bot->SetLevel(level);
        //Reset xp and xp for next level.
        bot->SetUInt32Value(PLAYER_XP, 0);
        bot->SetUInt32Value(PLAYER_NEXT_LEVEL_XP, sObjectMgr.getXPForLevel(level));
    }

    if (!sPlayerbotAIConfig.randomBotShowHelmet)
    {
       bot->SetFlag(PLAYER_FLAGS, PLAYER_FLAGS_HIDE_HELM);
    }

    if (!sPlayerbotAIConfig.randomBotShowCloak)
    {
       bot->SetFlag(PLAYER_FLAGS, PLAYER_FLAGS_HIDE_CLOAK);
    }
}

void PlayerbotFactory::Randomize(bool incremental, bool syncWithMaster)
{
    sLog.outDetail("Preparing to %s randomize...", (incremental ? "incremental" : "full"));
    Prepare();

    if (sPlayerbotAIConfig.disableRandomLevels)
    {
        return;
    }
    bool isRealRandomBot = sRandomBotFacade.IsRandomBot(bot);
    bool isRandomBot = sRandomBotFacade.IsRandomBot(bot) && PlayerbotAIStorage::Instance().GetAI(bot) && !PlayerbotAIStorage::Instance().GetAI(bot)->HasRealPlayerMaster() && !PlayerbotAIStorage::Instance().GetAI(bot)->IsInRealGuild();

    sLog.outDetail("Resetting player...");
    auto pmo = sPerformanceMonitor.start(PERF_MON_RNDBOT, "PlayerbotFactory_Reset");
    //ClearSkills();
    ClearSpells();

    if (!incremental && isRandomBot)
    {
        ClearInventory();
        ResetQuests();
        bot->resetTalents(true);
        CancelAuras();
    }
    if (isRealRandomBot)
    {
        if (bot->GetLevel() > level)
        {
            bot->SetLevel(level);
            //Reset xp and xp for next level.
            bot->SetUInt32Value(PLAYER_XP, 0);
            bot->SetUInt32Value(PLAYER_NEXT_LEVEL_XP, sObjectMgr.getXPForLevel(level));
        }

        InitQuests(specialQuestIds);
        bot->learnQuestRewardedSpells();

        // clear inventory and set level after getting xp and quest rewards
        ClearInventory();
        if (bot->GetLevel() > level)
        {
            bot->SetLevel(level);
            //Reset xp and xp for next level.
            bot->SetUInt32Value(PLAYER_XP, 0);
            bot->SetUInt32Value(PLAYER_NEXT_LEVEL_XP, sObjectMgr.getXPForLevel(level));
        }
    }
    pmo.reset();

    sLog.outDetail("Initializing bags...");
    InitBags();

    sLog.outDetail("Initializing spells (step 1)...");
    InitAvailableSpells();

    sLog.outDetail("Initializing skills (step 1)...");
    InitAllSkills();

    pmo = sPerformanceMonitor.start(PERF_MON_RNDBOT, "PlayerbotFactory_Talents");
    sLog.outDetail("Initializing talents...");
    // Assign a premade spec (specNo), then let the "auto talents" action apply the
    // matching premade build. If no premade spec is configured for the class, the
    // action falls back to its generic per-tree auto-selection.
    if (!incremental)
        SelectPremadeSpecNo();
    ai->DoSpecificAction("auto talents");

    if (!incremental && isRandomBot)
        sPlayerbotDbStore.Reset(ai);

    ai->ResetStrategies(incremental); // fix wrong stored strategy
    pmo.reset();

    pmo = sPerformanceMonitor.start(PERF_MON_RNDBOT, "PlayerbotFactory_Spells2");
    sLog.outDetail("Initializing spells (step 2)...");
    InitAvailableSpells();
    InitSpecialSpells();
    pmo.reset();

    if (isRealRandomBot)
    {
        sLog.outDetail("Initializing mounts...");
        InitMounts();
    }

    sLog.outDetail("Initializing skills (step 2)...");
    UpdateTradeSkills();

    if (isRealRandomBot)
    {
        sLog.outDetail("Initializing reputations...");
        InitReputations();

    }

    pmo = sPerformanceMonitor.start(PERF_MON_RNDBOT, "PlayerbotFactory_Equip");
    sLog.outDetail("Initializing equipmemt...");
    if (bot->GetLevel() >= sPlayerbotAIConfig.minEnchantingBotLevel)
    {
        sLog.outDetail("Initializing enchant templates...");
        LoadEnchantContainer();
    }

    InitEquipment(incremental, syncWithMaster);
    pmo.reset();

    if (isRandomBot)
    {
        sLog.outDetail("Initializing ammo...");
        InitAmmo();

        sLog.outDetail("Initializing food...");
        InitFood();

        sLog.outDetail("Initializing potions...");
        InitPotions();

        sLog.outDetail("Initializing reagents...");
        InitReagents();
    }

    if (!incremental)
    {
        sLog.outDetail("Initializing consumables...");
        AddConsumables();
    }

    if (!incremental && isRandomBot)
    {
        /*pmo = sPerformanceMonitor.start(PERF_MON_RNDBOT, "PlayerbotFactory_EqSets");
        sLog.outDetail("Initializing second equipment set...");
        InitSecondEquipmentSet();
        if (pmo) pmo->finish();*/

        sLog.outDetail("Initializing inventory...");
        InitInventory();
    }

    if (bot->GetLevel() >= 10 && bot->GetClass() == CLASS_HUNTER)
    {
        auto pmo_pet = sPerformanceMonitor.start(PERF_MON_RNDBOT, "PlayerbotFactory_Pet");
        sLog.outDetail("Initializing pet...");
        InitPet();
        InitPetSpells();
    }
    else if (bot->GetClass() == CLASS_WARLOCK)
    {
        auto pmo_pet = sPerformanceMonitor.start(PERF_MON_RNDBOT, "PlayerbotFactory_Pet");
        sLog.outDetail("Initializing pet...");
        InitPet();
        InitPetSpells();
    }

    if (isRandomBot)
    {
        if (incremental)
        {
            uint32 money = bot->GetMoney();
            bot->SetMoney(money + 1000 * urand(1, level * 5));
        }
        else
        {
            bot->SetMoney(10000 * urand(1, level * 5));
        }
    }

    if (isRandomBot)
    {
        sLog.outDetail("Initializing taxi...");
        InitTaxiNodes();
    }

    pmo = sPerformanceMonitor.start(PERF_MON_RNDBOT, "PlayerbotFactory_Save");
    sLog.outDetail("Saving to DB...");
    bot->SaveToDB();
    sLog.outDetail("Done.");
    pmo.reset();
}

void PlayerbotFactory::Refresh()
{
    //Prepare();
    if (!ai->HasCheat(BotCheatMask::item))
        return;

    InitAmmo();
    InitFood();
    InitPotions();
    InitReagents();
    AddConsumables();
    bot->SaveToDB();
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
         if (level >= 5 && level < 20) {
            StoreItem(CONSUM_ID_MINOR_WIZARD_OIL, 5);
            }
         if (level >= 20 && level < 40) {
            StoreItem(CONSUM_ID_MINOR_MANA_OIL, 5);
            StoreItem(CONSUM_ID_MINOR_WIZARD_OIL, 5);
         }
         if (level >= 40 && level < 45) {
             StoreItem(CONSUM_ID_MINOR_MANA_OIL, 5);
             StoreItem(CONSUM_ID_WIZARD_OIL, 5);
         }
         if (level >= 45) {
             StoreItem(CONSUM_ID_BRILLIANT_MANA_OIL, 5);
             StoreItem(CONSUM_ID_BRILLIANT_WIZARD_OIL, 5);
         }
   }
      break;
      case CLASS_PALADIN:
      case CLASS_WARRIOR:
      case CLASS_HUNTER:
       {
         if (level >= 1 && level < 5) {
            StoreItem(CONSUM_ID_ROUGH_SHARPENING_STONE, 5);
            StoreItem(CONSUM_ID_ROUGH_WEIGHTSTONE, 5);
        }
         if (level >= 5 && level < 15) {
            StoreItem(CONSUM_ID_COARSE_WEIGHTSTONE, 5);
            StoreItem(CONSUM_ID_COARSE_SHARPENING_STONE, 5);
         }
         if (level >= 15 && level < 25) {
            StoreItem(CONSUM_ID_HEAVY_WEIGHTSTONE, 5);
            StoreItem(CONSUM_ID_HEAVY_SHARPENING_STONE, 5);
         }
         if (level >= 25 && level < 35) {
            StoreItem(CONSUM_ID_SOL_SHARPENING_STONE, 5);
            StoreItem(CONSUM_ID_SOLID_WEIGHTSTONE, 5);
         }
         if (level >= 35) {
             StoreItem(CONSUM_ID_DENSE_WEIGHTSTONE, 5);
             StoreItem(CONSUM_ID_DENSE_SHARPENING_STONE, 5);
         }
   }
       break;
       case CLASS_ROGUE:
      {
         if (level >= 20 && level < 28) {
            StoreItem(CONSUM_ID_INSTANT_POISON, 5);
            StoreItem(CONSUM_ID_CRIPPLING_POISON, 5);
         }
         if (level >= 28 && level < 30) {
            StoreItem(CONSUM_ID_INSTANT_POISON_II, 5);
            StoreItem(CONSUM_ID_CRIPPLING_POISON, 5);
            StoreItem(CONSUM_ID_MIND_POISON, 5);
         }
         if (level >= 30 && level < 36) {
            StoreItem(CONSUM_ID_DEADLY_POISON, 5);
            StoreItem(CONSUM_ID_INSTANT_POISON_II, 5);
            StoreItem(CONSUM_ID_CRIPPLING_POISON, 5);
            StoreItem(CONSUM_ID_MIND_POISON, 5);
         }
         if (level >= 36 && level < 38) {
             StoreItem(CONSUM_ID_DEADLY_POISON, 5);
             StoreItem(CONSUM_ID_INSTANT_POISON_III, 5);
             StoreItem(CONSUM_ID_CRIPPLING_POISON, 5);
             StoreItem(CONSUM_ID_MIND_POISON, 5);
         }
         if (level >= 38 && level < 44) {
             StoreItem(CONSUM_ID_DEADLY_POISON_II, 5);
             StoreItem(CONSUM_ID_INSTANT_POISON_III, 5);
             StoreItem(CONSUM_ID_CRIPPLING_POISON, 5);
             StoreItem(CONSUM_ID_MIND_POISON_II, 5);
         }
         if (level >= 44 && level < 46) {
             StoreItem(CONSUM_ID_DEADLY_POISON_II, 5);
            StoreItem(CONSUM_ID_INSTANT_POISON_IV, 5);
            StoreItem(CONSUM_ID_CRIPPLING_POISON, 5);
            StoreItem(CONSUM_ID_MIND_POISON_II, 5);
         }
         if (level >= 46 && level < 52) {
             StoreItem(CONSUM_ID_DEADLY_POISON_III, 5);
             StoreItem(CONSUM_ID_INSTANT_POISON_IV, 5);
             StoreItem(CONSUM_ID_CRIPPLING_POISON, 5);
             StoreItem(CONSUM_ID_MIND_POISON_II, 5);
         }
         if (level >= 52 && level < 54) {
             StoreItem(CONSUM_ID_DEADLY_POISON_III, 5);
            StoreItem(CONSUM_ID_INSTANT_POISON_V, 5);
            StoreItem(CONSUM_ID_CRIPPLING_POISON_II, 5);
            StoreItem(CONSUM_ID_MIND_POISON_III, 5);
         }
         if (level >= 54 && level < 60) {
             StoreItem(CONSUM_ID_DEADLY_POISON_IV, 5);
             StoreItem(CONSUM_ID_INSTANT_POISON_V, 5);
             StoreItem(CONSUM_ID_CRIPPLING_POISON_II, 5);
             StoreItem(CONSUM_ID_MIND_POISON_III, 5);
         }
         if (level >= 60) {
            StoreItem(CONSUM_ID_DEADLY_POISON_V, 5);
            StoreItem(CONSUM_ID_INSTANT_POISON_VI, 5);
            StoreItem(CONSUM_ID_CRIPPLING_POISON_II, 5);
            StoreItem(CONSUM_ID_MIND_POISON_III, 5);
         }
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

            if ((int)co->MinLevel > (int)bot->GetLevel())
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
            pet->setFaction(bot->GetFaction());
            pet->SetLevel(bot->GetLevel());
            pet->InitStatsForLevel(bot->GetLevel());
            pet->SetLoyaltyLevel(BEST_FRIEND);
            pet->SetPower(POWER_HAPPINESS, HAPPINESS_LEVEL_SIZE * 2);
            pet->GetCharmInfo()->SetPetNumber(pet->getObjectGuid().GetEntry(), true);
            pet->GetMap()->Add((Creature*)pet);
            pet->AIM_Initialize();
            pet->AI()->SetReactState(REACT_DEFENSIVE);
            pet->InitPetCreateSpells();
            pet->LearnPetPassives();
            pet->CastPetAuras(true);
            pet->CastOwnerTalentAuras();
            pet->UpdateAllStats();
            bot->SetPet(pet);
            bot->SetPetGuid(pet->getObjectGuid());

            sLog.outDebug(  "Bot %s: assign pet %d (%d level)", bot->GetName(), co->Entry, bot->GetLevel());
            pet->SavePetToDB(PET_SAVE_AS_CURRENT, bot);
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
        pet->AI()->SetReactState(REACT_DEFENSIVE);
    }
    else
    {
        sLog.outError("Cannot create pet for bot %s", bot->GetName());
        return;
    }

    for (PetSpellMap::const_iterator itr = pet->m_spells.begin(); itr != pet->m_spells.end(); ++itr)
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

            switch (ci->Family)
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
                        pet->learnSpell(spellID);
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
            pet->learnSpell(growlSpellId);
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
            pet->learnSpell(naturalArmorSpellId);
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
            pet->learnSpell(greatStaminaSpellId);
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
                    pet->learnSpell(res.spellId);
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
                    pet->learnSpell(spellID);
                }
            }
        }
    }
}

void PlayerbotFactory::ClearSkills()
{
    for (int i = 0; i < sizeof(tradeSkills) / sizeof(uint32); ++i)
    {
        bot->SetSkill(tradeSkills[i], 0, 0, 0);
    }
    bot->SetUInt32Value(PLAYER_SKILL_INDEX(0), 0);
    bot->SetUInt32Value(PLAYER_SKILL_INDEX(1), 0);
}

void PlayerbotFactory::ClearSpells()
{
#ifdef MANGOS
    std::list<uint32> spells;
    for(PlayerSpellMap::iterator itr = bot->GetSpellMap().begin(); itr != bot->GetSpellMap().end(); ++itr)
    {
        uint32 spellId = itr->first;
		if (itr->second.state == PLAYERSPELL_REMOVED || itr->second.disabled || IsPassiveSpell(spellId))
			continue;

        spells.push_back(spellId);
    }

    for (std::list<uint32>::iterator i = spells.begin(); i != spells.end(); ++i)
    {
        bot->removeSpell(*i, false, false);
    }
#endif
#ifdef CMANGOS
    bot->resetSpells();
#endif
}

void PlayerbotFactory::ResetQuests()
{
    ObjectMgr::QuestMap const& questTemplates = sObjectMgr.GetQuestTemplates();
    for (ObjectMgr::QuestMap::const_iterator i = questTemplates.begin(); i != questTemplates.end(); ++i)
    {
        Quest const* quest = i->second.get();
        uint32 entry = quest->GetQuestId();

        // remove all quest entries for 'entry' from quest log
        for (uint8 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
        {
            uint32 quest = bot->GetQuestSlotQuestId(slot);
            if (quest == entry)
            {
                bot->SetQuestSlot(slot, 0);
            }
        }

        // reset rewarded for restart repeatable quest
        bot->getQuestStatusMap().erase(entry);
        //bot->GetQuestStatusMap()[entry].m_rewarded = false;
        //bot->GetQuestStatusMap()[entry].m_status = QUEST_STATUS_NONE;
    }
    //bot->UpdateForQuestWorldObjects();
    CharacterDatabase.PExecute("DELETE FROM character_queststatus WHERE guid = '%u'", bot->GetGUIDLow());
}

void PlayerbotFactory::InitReputations()
{
    auto pmo = sPerformanceMonitor.start(PERF_MON_RNDBOT, "PlayerbotFactory_Reputations");
    // list of factions
    std::list<uint32> factions;

    // neutral
    if (level >= 60)
    {
        factions.push_back(910); // nozdormu
        factions.push_back(749); // hydraxian waterlords
        factions.push_back(529); // argent dawn
    }

    // pvp factions
    if (level >= 60)
    {
        if (bot->GetTeam() == ALLIANCE)
        {
            factions.push_back(890); // Silverwing Sentinels
            factions.push_back(730); // Stormpike Guard
            factions.push_back(509); // The League of Arathor
        }
        else
        {
            factions.push_back(729); // Frostwolf Clan
            factions.push_back(510); // The Defilers
            factions.push_back(889); // Warsong Outriders
        }
    }


    for (auto faction : factions)
    {
        FactionEntry const* factionEntry = sFactionStore.LookupEntry(faction);

        if (!factionEntry || !factionEntry->HasReputation())
            continue;

        bot->GetReputationMgr().SetReputation(factionEntry, 42000);
    }
}

void PlayerbotFactory::InitSpells()
{
    for (int i = 0; i < 15; i++)
        InitAvailableSpells();
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

    // Check spec before wiping gear — a specId==0 result after DestroyItemsVisitor would
    // leave the bot naked.
    uint32 specId = sRandomItemMgr.GetPlayerSpecId(bot);
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

        // Item availability is derived from the active Turtle item cache.

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
                for (uint32 q = quality; q < ITEM_QUALITY_ARTIFACT; ++q)
                {
                    // quality selected from command
                    if (setQuality && q != quality)
                        continue;

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
                            if(proto)
                            {
                                if (proto->ItemLevel > maxItemLevel)
                                    continue;

                                hasProperLevel = true;
                                break;
                            }
                        }

                        if (!hasProperLevel)
                        {
                            ids.clear();
                            currSearchLevel--;
                        }
                    }

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

                sLog.outDetail("Bot #%d %s:%d <%s>: %u possible items for slot %d", bot->GetGUIDLow(), bot->GetTeam() == ALLIANCE ? "A" : "H", bot->GetLevel(), bot->GetName(), uint32(ids.size()), slot);

                if (incremental || !progressiveGear)
                {
                    // sort items based on stat value, ilvl or quality
                    std::sort(ids.begin(), ids.end(), [specId](int a, int b)
                        {
                            uint32 baseCompareA = (sRandomItemMgr.GetStatWeight(a, specId) + sRandomItemMgr.GetBestRandomEnchantStatWeight(a, specId)) * 1000;
                            uint32 baseCompareB = (sRandomItemMgr.GetStatWeight(b, specId) + sRandomItemMgr.GetBestRandomEnchantStatWeight(b, specId)) * 1000;
                            if (baseCompareA < baseCompareB)
                                return true;

                            ItemPrototype const* proto1 = sObjectMgr.GetItemPrototype(a);
                            ItemPrototype const* proto2 = sObjectMgr.GetItemPrototype(b);

                            baseCompareA += proto1->Quality * proto1->ItemLevel;
                            baseCompareB += proto2->Quality * proto2->ItemLevel;

                            return baseCompareA < baseCompareB;
                        });

                    if (!progressiveGear)
                        std::reverse(ids.begin(), ids.end());
                }
                else if (!ids.empty())
                {
                    Shuffle(ids);
                }

                for (uint32 index = 0; index < ids.size(); ++index)
                {
                    uint32 newItemId = ids[index];

                    // filter item level
                    ItemPrototype const* proto = sObjectMgr.GetItemPrototype(newItemId);
                    if (!proto)
                        continue;

                    if (std::find(lockedItems.begin(), lockedItems.end(), proto->ItemId) != lockedItems.end())
                        continue;

                    // blacklist
                    if (std::find(sPlayerbotAIConfig.randomGearBlacklist.begin(), sPlayerbotAIConfig.randomGearBlacklist.end(), proto->ItemId) != sPlayerbotAIConfig.randomGearBlacklist.end())
                        continue;

                    // skip unique-equippable items if already have one in inventory
                    if (proto->Flags & ITEM_FLAG_UNIQUE_EQUIPPABLE && bot->HasItemCount(proto->ItemId, 1))
                        continue;

                    if (proto->MaxCount && bot->HasItemCount(proto->ItemId, proto->MaxCount))
                        continue;

                    if (proto->ItemLevel > maxItemLevel)
                        continue;

                    // do not use items that required level is too low compared to bot's level
                    uint32 reqLevel = sRandomItemMgr.GetMinLevelFromCache(newItemId);
                    if (reqLevel && proto->Quality < ITEM_QUALITY_LEGENDARY && abs((int)bot->GetLevel() - (int)reqLevel) > (int)sPlayerbotAIConfig.randomGearMaxDiff)
                        continue;

                    // filter tank weapons
                    if (slot == EQUIPMENT_SLOT_OFFHAND && (specId == 3 || specId == 5) && !(proto->Class == ITEM_CLASS_ARMOR && proto->SubClass == ITEM_SUBCLASS_ARMOR_SHIELD))
                        continue;

                    if (slot == EQUIPMENT_SLOT_MAINHAND && proto->Class == ITEM_CLASS_ARMOR && proto->SubClass == ITEM_SUBCLASS_ARMOR_SHIELD)
                        continue;

                    if (slot == EQUIPMENT_SLOT_MAINHAND && proto->InventoryType == INVTYPE_HOLDABLE)
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

                    Item* oldItem = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
                    ItemPrototype const* oldProto = oldItem ? oldItem->GetProto() : nullptr;
                    //uint32 oldStatValue = oldItem ? sRandomItemMgr.GetStatWeight(oldProto->ItemId, specId) : 0;
                    uint32 oldStatValue = oldItem ? sRandomItemMgr.GetLiveStatWeight(bot, oldProto->ItemId, specId) : 0;

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

                    // skip off hand if main hand is worse
                    if (proto->IsWeapon() && slot == EQUIPMENT_SLOT_OFFHAND && (bot->GetClass() == CLASS_ROGUE || specId == 2))
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
                                // overwrite random generated property
                                pItem->SetItemRandomProperties(randomEnchBestId);
                                // update for inspect
                                bot->SetVisibleItemSlot(pItem->GetSlot(), pItem);
                            }
                            pItem->SetOwnerGuid(bot->getObjectGuid());
                            EnchantItem(pItem);
                            found = true;
                        }
                    }
                    if (found)
                    {
                        if (incremental)
                        {
                            if (oldItem)
                                sLog.outDetail("Bot #%d %s:%d <%s>: Old Item: slot: %u, id: %u, value: %u (%s)", bot->GetGUIDLow(), bot->GetTeam() == ALLIANCE ? "A" : "H", bot->GetLevel(), bot->GetName(), slot, oldProto->ItemId, oldStatValue, oldProto->Name1);
                            sLog.outDetail("Bot #%d %s:%d <%s>: New Item: slot: %u, id: %u, value: %u (%s)", bot->GetGUIDLow(), bot->GetTeam() == ALLIANCE ? "A" : "H", bot->GetLevel(), bot->GetName(), slot, proto->ItemId, newStatValue, proto->Name1);
                        }
                        break;
                    }
                }
            }

            if (!found && quality > ITEM_QUALITY_NORMAL)
            {
                quality--;
            }

            attempts++;
        } while (!found && attempts < 3 && quality != ITEM_QUALITY_POOR);
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

bool PlayerbotFactory::IsDesiredReplacement(uint32 itemId)
{
    if (!itemId)
        return true;

    ItemPrototype const* proto = sObjectMgr.GetItemPrototype(itemId);
    if (!proto)
        return false;

    uint32 requiredLevel = proto->RequiredLevel;
    if (!requiredLevel)
    {
        requiredLevel = sRandomItemMgr.GetMinLevelFromCache(proto->ItemId);
    }
    if (!requiredLevel)
        return false;

    int delta = sPlayerbotAIConfig.randomGearMaxDiff + (DEFAULT_MAX_LEVEL - bot->GetLevel()) / 10;
    return (int)bot->GetLevel() - (int)requiredLevel > delta;
}

void PlayerbotFactory::InitSecondEquipmentSet()
{
    if (bot->GetClass() == CLASS_MAGE || bot->GetClass() == CLASS_WARLOCK || bot->GetClass() == CLASS_PRIEST)
        return;

    std::map<uint32, std::vector<uint32> > items;

    uint32 desiredQuality = ITEM_QUALITY_NORMAL;
    if (level < 10)
        desiredQuality = urand(ITEM_QUALITY_POOR, ITEM_QUALITY_UNCOMMON);
    if (level < 20)
        desiredQuality = urand(ITEM_QUALITY_NORMAL, ITEM_QUALITY_UNCOMMON);
    else if (level < 40)
        desiredQuality = urand(ITEM_QUALITY_UNCOMMON, ITEM_QUALITY_RARE);
    else if (level < 60)
        desiredQuality = urand(ITEM_QUALITY_UNCOMMON, ITEM_QUALITY_RARE);
    else
        desiredQuality = urand(ITEM_QUALITY_RARE, ITEM_QUALITY_EPIC);

    while (urand(0, 100) < 100 * sPlayerbotAIConfig.randomGearLoweringChance && desiredQuality > ITEM_QUALITY_NORMAL) {
        desiredQuality--;
    }

    do
    {
        for (uint32 itemId = 0; itemId < sItemStorage.GetMaxEntry(); ++itemId)
        {
            ItemPrototype const* proto = sObjectMgr.GetItemPrototype(itemId);
            if (!proto)
                continue;

            // filter item level
            if (proto->ItemLevel > sPlayerbotAIConfig.randomGearMaxLevel)
                continue;

            // do not use items that required level is too low compared to bot's level
            uint32 reqLevel = sRandomItemMgr.GetMinLevelFromCache(itemId);
            if (reqLevel && proto->Quality < ITEM_QUALITY_LEGENDARY && abs((int)bot->GetLevel() - (int)reqLevel) > (int)sPlayerbotAIConfig.randomGearMaxDiff)
                continue;

            if (!CanEquipItem(proto, desiredQuality))
                continue;

            if (proto->Class == ITEM_CLASS_WEAPON)
            {
                if (!CanEquipWeapon(proto))
                    continue;

                Item* existingItem = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_MAINHAND);
                if (existingItem)
                {
                    switch (existingItem->GetProto()->SubClass)
                    {
                    case ITEM_SUBCLASS_WEAPON_AXE:
                    case ITEM_SUBCLASS_WEAPON_DAGGER:
                    case ITEM_SUBCLASS_WEAPON_FIST:
                    case ITEM_SUBCLASS_WEAPON_MACE:
                    case ITEM_SUBCLASS_WEAPON_SWORD:
                        if (proto->SubClass == ITEM_SUBCLASS_WEAPON_AXE || proto->SubClass == ITEM_SUBCLASS_WEAPON_DAGGER ||
                            proto->SubClass == ITEM_SUBCLASS_WEAPON_FIST || proto->SubClass == ITEM_SUBCLASS_WEAPON_MACE ||
                            proto->SubClass == ITEM_SUBCLASS_WEAPON_SWORD)
                            continue;
                        break;
                    default:
                        if (proto->SubClass != ITEM_SUBCLASS_WEAPON_AXE && proto->SubClass != ITEM_SUBCLASS_WEAPON_DAGGER &&
                            proto->SubClass != ITEM_SUBCLASS_WEAPON_FIST && proto->SubClass != ITEM_SUBCLASS_WEAPON_MACE &&
                            proto->SubClass != ITEM_SUBCLASS_WEAPON_SWORD)
                            continue;
                        break;
                    }
                }
            }
            else if (proto->Class == ITEM_CLASS_ARMOR && proto->SubClass == ITEM_SUBCLASS_ARMOR_SHIELD)
            {
                Item* existingItem = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_OFFHAND);
                if (existingItem && existingItem->GetProto()->SubClass == ITEM_SUBCLASS_ARMOR_SHIELD)
                    continue;
            }
            else
                continue;

            items[proto->Class].push_back(itemId);
        }
    } while (items[ITEM_CLASS_ARMOR].empty() && items[ITEM_CLASS_WEAPON].empty() && desiredQuality++ != ITEM_QUALITY_ARTIFACT);

    int maxCount = urand(0, 5);
    int count = 0;
    for (std::map<uint32, std::vector<uint32> >::iterator i = items.begin(); i != items.end(); ++i)
    {
        if (count++ >= maxCount)
            break;

        std::vector<uint32>& ids = i->second;
        if (ids.empty())
        {
            sLog.outDebug(  "%s: no items to make second equipment set for slot %d", bot->GetName(), i->first);
            continue;
        }
        for (int attempts = 0; attempts < 15; attempts++)
        {
            uint32 index = urand(0, ids.size() - 1);
            uint32 newItemId = ids[index];
            Item* newItem = StoreItem(newItemId, 1);
            if (newItem)
            {
                count++;
                break;
            }
        }
    }
}

void PlayerbotFactory::InitBags()
{
    auto pmo = sPerformanceMonitor.start(PERF_MON_RNDBOT, "PlayerbotFactory_Bags");
    for (uint8 slot = INVENTORY_SLOT_BAG_START; slot < INVENTORY_SLOT_BAG_END; ++slot)
    {
        Bag* pBag = (Bag*)bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
        if (!pBag)
        {
            bot->StoreNewItemInBestSlots(4500, 1); // add Traveler's Backpack if no bag in slot
        }
    }
}

void PlayerbotFactory::EnchantItem(Item* item)
{
    if (!item)
        return;

    if (bot->GetLevel() < sPlayerbotAIConfig.minEnchantingBotLevel)
        return;

    int tab = AiFactory::GetPlayerSpecTab(bot);
    uint32 tempId = uint32((uint32)bot->GetClass() * (uint32)10);
    ApplyEnchantTemplate(tempId += (uint32)tab, item);
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
            switch (urand(0, 6))
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

        if (co->TrainerType != TRAINER_TYPE_TRADESKILLS)
            continue;

        uint32 trainerId = co->TrainerTemplateId;
        if (!trainerId)
            trainerId = co->Entry;

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

            uint32 reqLevel = 0;
            reqLevel = tSpell->isProvidedReqLevel ? tSpell->reqLevel : std::max(reqLevel, tSpell->reqLevel);
            TrainerSpellState state = bot->GetTrainerSpellState(tSpell, reqLevel);
            if (state != TRAINER_SPELL_GREEN)
                continue;

            SpellEntry const* proto = sServerFacade.LookupSpellInfo(tSpell->spell);
            if (!proto)
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

            if (tSpell->learnedSpell)
            {
                bool learned = false;
                for (int j = 0; j < 3; ++j)
                {
                    if (proto->Effect[j] == SPELL_EFFECT_LEARN_SPELL)
                    {
                        uint32 learnedSpell = proto->EffectTriggerSpell[j];
                        bot->learnSpell(learnedSpell, false);
                        learned = true;
                    }
                }
                if (!learned) bot->learnSpell(tSpell->learnedSpell, false);
            }
            else
                ai->CastSpell(tSpell->spell, bot);
        }
    }
}

void PlayerbotFactory::UpdateTradeSkills()
{
    auto pmo = sPerformanceMonitor.start(PERF_MON_RNDBOT, "PlayerbotFactory_Skills2");
    for (int i = 0; i < sizeof(tradeSkills) / sizeof(uint32); ++i)
    {
        if (bot->GetSkillValue(tradeSkills[i]) == 1)
            bot->SetSkill(tradeSkills[i], 0, 0, 0);
    }
}

void PlayerbotFactory::InitSkills()
{
    bot->UpdateSkillsForLevel(true);

// Riding skills requirements are different
    if (bot->GetLevel() >= 60)
        bot->SetSkill(SKILL_RIDING, 150, 150);
    else if (bot->GetLevel() >= 40)
        bot->SetSkill(SKILL_RIDING, 75, 75);
    else
        bot->SetSkill(SKILL_RIDING, 0, 0);

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

void PlayerbotFactory::InitAvailableSpells()
{
    auto pmo = sPerformanceMonitor.start(PERF_MON_RNDBOT, "PlayerbotFactory_Spells1");
    bot->learnDefaultSpells();
    bot->learnClassLevelSpells(true);

    if (bot->GetClass() == CLASS_PALADIN)
    {
        // judgement missing
        if(!bot->HasSpell(20271))
        {
            bot->learnSpell(20271, false);
        }
    }

    // add polymorph pig/turtle
    if (bot->GetClass() == CLASS_MAGE && bot->GetLevel() >= 60)
    {
        bot->learnSpell(28271, false);
        bot->learnSpell(28272, false);
    }

    // add inferno
    if (bot->GetClass() == CLASS_WARLOCK && !bot->HasSpell(1122) && bot->GetLevel() >= 50)
        bot->learnSpell(1122, false);

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
            bot->learnSpell(5487, false);   // Bear Form
        if (bot->GetLevel() >= 16 && !bot->HasSpell(1066))
            bot->learnSpell(1066, false);   // Aquatic Form
        if (bot->GetLevel() >= 40 && !bot->HasSpell(9634))
            bot->learnSpell(9634, false);   // Dire Bear Form
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
                bot->learnSpell(spellId, false);
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
            bot->learnSpell(spellId, false);
    }
}

void PlayerbotFactory::AddPrevQuests(uint32 questId, std::list<uint32>& questIds)
{
    Quest const *quest = sObjectMgr.GetQuestTemplate(questId);
    for (Quest::PrevQuests::const_iterator iter = quest->prevQuests.begin(); iter != quest->prevQuests.end(); ++iter)
    {
        uint32 prevId = abs(*iter);
        AddPrevQuests(prevId, questIds);
        questIds.remove(prevId);
        questIds.push_back(prevId);
    }
}

void PlayerbotFactory::InitQuests(std::list<uint32>& questMap)
{
    int count = 0;
    for (std::list<uint32>::iterator i = questMap.begin(); i != questMap.end(); ++i)
    {
        uint32 questId = *i;
        Quest const *quest = sObjectMgr.GetQuestTemplate(questId);

        if (!bot->SatisfyQuestClass(quest, false) ||
                quest->GetMinLevel() > bot->GetLevel() ||
                !bot->SatisfyQuestRace(quest, false))
            continue;

        bot->SetQuestStatus(questId, QUEST_STATUS_COMPLETE);
        bot->RewardQuest(quest, 0, bot, false);
        sLog.outDetail("Bot %s (%d level) rewarded quest %d: %s (MinLevel=%d, QuestLevel=%d)",
                bot->GetName(), bot->GetLevel(), questId, quest->GetTitle().c_str(),
                quest->GetMinLevel(), quest->GetQuestLevel());
        /*if (!(count++ % 10))
            ClearInventory();*/
    }
}

void PlayerbotFactory::ClearInventory()
{
    DestroyItemsVisitor visitor(bot);
    IterateItemsMask mask = IterateItemsMask((uint8)IterateItemsMask::ITERATE_ITEMS_IN_BAGS | (uint8)IterateItemsMask::ITERATE_ITEMS_IN_EQUIP);
    ai->InventoryIterateItems(&visitor, mask);
}

void PlayerbotFactory::ClearAllItems()
{
    DestroyItemsVisitor visitor(bot);
    ai->InventoryIterateItems(&visitor, IterateItemsMask::ITERATE_ALL_ITEMS);
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

    // TurtleWoW: thrown weapons are single repairable items (Stackable=1), not 200-stack ammo.
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
    uint32 count = bot->GetItemCount(entry) / 200;
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

void PlayerbotFactory::InitMounts()
{
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
        // Turtle carries races this switch never knew - Goblin (9) and High
        // Elf (10) - and on a vanilla build the Draenei and Blood Elf cases
        // below are preprocessed away as well. Any race that falls through
        // leaves every list empty, and the draw further down then reads past
        // the end of an empty vector, because size() - 1 on an unsigned type
        // is not -1 but the largest value there is. What comes back is either
        // a spell id out of thin air - which learnSpell duly reports as not
        // existing - or a segfault.
        //
        // Their own mounts cannot be named here: Turtle gives nearly every
        // mount item the same generic spell and restricts it by faction mask
        // instead, so there is no per-race spell to list. The faction's
        // ordinary mounts are the honest fallback. Not lore, but a bot that
        // rides rather than walks to sixty.
        if (bot->GetTeam() == ALLIANCE)
        {
            slow = { 470, 6648, 458, 472 };
            fast = { 23228, 23227, 23229 };
        }
        else
        {
            slow = { 6654, 6653, 580 };
            fast = { 23250, 23252, 23251 };
        }
        break;
    }

    // Turtle collection mounts are represented by a generic item spell and a
    // core-owned collection_mount mapping. Include eligible mapped spells in
    // the factory pool so randomized characters do not lose custom mounts
    // merely because their item has no classic per-item mount spell. Factory
    // initialization intentionally teaches the mapped spell, matching the
    // existing classic factory mount path; real item use remains owned by the
    // core collection spell script.
    if (QueryResult* result = WorldDatabase.Query("SELECT itemId, spellId FROM collection_mount"))
    {
        do
        {
            Field* fields = result->Fetch();
            uint32 itemId = fields[0].GetUInt32();
            uint32 spellId = fields[1].GetUInt32();
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
        } while (result->NextRow());
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
            bot->learnSpell(spell, false);
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

        if (effect == SPELL_EFFECT_ENERGIZE && !bot->HasMana()) //Do not give manapots to non-mana users.
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

        if (category == 59 && !bot->HasMana()) //Do not give drinks to non-mana users.
            continue;

        FindFoodVisitor visitor(bot, category);
        ai->InventoryIterateItems(&visitor, IterateItemsMask::ITERATE_ITEMS_IN_BAGS);
        if (!visitor.GetResult().empty()) continue;

        uint32 itemId = sRandomItemMgr.GetFood(level, category);
        if (!itemId)
        {
            sLog.outDetail("No food (category %d) available for bot %s (%d level)", category, bot->GetName(), bot->GetLevel());
            continue;
        }
        ItemPrototype const* proto = sObjectMgr.GetItemPrototype(itemId);
        if (!proto) continue;

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
        if (bot->GetLevel() > 50)
            items = { 21177 };
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

        QueryItemCountVisitor visitor(*i);
        ai->InventoryIterateItems(&visitor, IterateItemsMask::ITERATE_ITEMS_IN_BAGS);
        if ((uint32)visitor.GetCount() > maxCount) continue;

        uint32 randCount = urand(maxCount / 2, maxCount * regCount);

        Item* newItem = bot->StoreNewItemInInventorySlot(*i, randCount);

        sLog.outDetail("Bot %d got reagent %s x%d", bot->GetGUIDLow(), proto->Name1, randCount);
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

void PlayerbotFactory::CancelAuras()
{
    bot->RemoveAllAuras();
}

void PlayerbotFactory::InitInventory()
{
    auto pmo = sPerformanceMonitor.start(PERF_MON_RNDBOT, "PlayerbotFactory_Inventory");
    //InitInventoryTrade();
    //InitInventoryEquip();
    InitInventorySkill();
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

void PlayerbotFactory::InitInventoryTrade()
{
    uint32 itemId = sRandomItemMgr.GetRandomTrade(level);
    if (!itemId)
    {
        sLog.outError("No trade items available for bot %s (%d level)", bot->GetName(), bot->GetLevel());
        return;
    }

    ItemPrototype const* proto = sObjectMgr.GetItemPrototype(itemId);
    if (!proto)
        return;

    uint32 count = 1, stacks = 1;
    switch (proto->Quality)
    {
    case ITEM_QUALITY_NORMAL:
        count = proto->GetMaxStackSize();
        stacks = urand(1, 3);
        break;
    case ITEM_QUALITY_UNCOMMON:
        stacks = 1;
        count = urand(1, proto->GetMaxStackSize() / 2);
        break;
    }

    for (uint32 i = 0; i < stacks; i++)
        StoreItem(itemId, count);
}

void PlayerbotFactory::InitInventoryEquip()
{
    std::vector<uint32> ids;

    uint32 desiredQuality = ITEM_QUALITY_NORMAL;
    if (level < 10)
        desiredQuality = urand(ITEM_QUALITY_POOR, ITEM_QUALITY_UNCOMMON);
    if (level < 20)
        desiredQuality = urand(ITEM_QUALITY_NORMAL, ITEM_QUALITY_UNCOMMON);
    else if (level < 40)
        desiredQuality = urand(ITEM_QUALITY_UNCOMMON, ITEM_QUALITY_RARE);
    else if (level < 60)
        desiredQuality = urand(ITEM_QUALITY_UNCOMMON, ITEM_QUALITY_RARE);
    else
        desiredQuality = urand(ITEM_QUALITY_RARE, ITEM_QUALITY_EPIC);

    if (urand(0, 100) < 100 * sPlayerbotAIConfig.randomGearLoweringChance && desiredQuality > ITEM_QUALITY_NORMAL) {
        desiredQuality--;
    }

    for (uint32 itemId = 0; itemId < sItemStorage.GetMaxEntry(); ++itemId)
    {
        ItemPrototype const* proto = sObjectMgr.GetItemPrototype(itemId);
        if (!proto)
            continue;

        if (proto->ItemLevel > sPlayerbotAIConfig.randomGearMaxLevel)
            continue;

        // do not use items that required level is too low compared to bot's level
        uint32 reqLevel = sRandomItemMgr.GetMinLevelFromCache(itemId);
        if (reqLevel && proto->Quality < ITEM_QUALITY_LEGENDARY && abs((int)bot->GetLevel() - (int)reqLevel) > (int)sPlayerbotAIConfig.randomGearMaxDiff)
            continue;

        if ((proto->Class != ITEM_CLASS_ARMOR && proto->Class != ITEM_CLASS_WEAPON) || (proto->Bonding == BIND_WHEN_PICKED_UP ||
                proto->Bonding == BIND_WHEN_USE))
            continue;


        if (proto->Class == ITEM_CLASS_ARMOR && !CanEquipArmor(proto))
            continue;

        if (proto->Class == ITEM_CLASS_WEAPON && !CanEquipWeapon(proto))
            continue;

        ids.push_back(itemId);
    }

    int maxCount = urand(0, 5);
    int count = 0;
    for (int attempts = 0; attempts < 15; attempts++)
    {
        uint32 index = urand(0, ids.size() - 1);
        uint32 itemId = ids[index];
        Item* newItem = StoreItem(itemId, 1);
        if (newItem && count++ >= maxCount)
            break;
   }
}

void PlayerbotFactory::InitImmersive()
{
    uint32 owner = bot->getObjectGuid().GetCounter();
    std::map<Stats, int32> percentMap;

    bool initialized = false;
    for (int i = STAT_STRENGTH; i < MAX_STATS; ++i)
    {
        Stats type = (Stats)i;
        std::ostringstream name; name << "immersive_stat_" << i;
        uint32 value = sRandomBotFacade.GetValue(owner, name.str());
        if (value) initialized = true;
        percentMap[type] = value;
    }

    if (!initialized)
    {
        switch (bot->GetClass())
        {
        case CLASS_DRUID:
        case CLASS_SHAMAN:
            percentMap[STAT_STRENGTH] = 15;
            percentMap[STAT_INTELLECT] = 10;
            percentMap[STAT_SPIRIT] = 5;
            percentMap[STAT_AGILITY] = 35;
            percentMap[STAT_STAMINA] = 35;
            break;
        case CLASS_PALADIN:
            percentMap[STAT_STRENGTH] = 35;
            percentMap[STAT_INTELLECT] = 10;
            percentMap[STAT_SPIRIT] = 5;
            percentMap[STAT_AGILITY] = 15;
            percentMap[STAT_STAMINA] = 35;
            break;
        case CLASS_WARRIOR:
            percentMap[STAT_STRENGTH] = 30;
            percentMap[STAT_SPIRIT] = 10;
            percentMap[STAT_AGILITY] = 20;
            percentMap[STAT_STAMINA] = 40;
            break;
        case CLASS_ROGUE:
        case CLASS_HUNTER:
            percentMap[STAT_STRENGTH] = 15;
            percentMap[STAT_SPIRIT] = 5;
            percentMap[STAT_AGILITY] = 40;
            percentMap[STAT_STAMINA] = 40;
            break;
        case CLASS_MAGE:
            percentMap[STAT_INTELLECT] = 65;
            percentMap[STAT_SPIRIT] = 5;
            percentMap[STAT_STAMINA] = 30;
            break;
        case CLASS_PRIEST:
            percentMap[STAT_INTELLECT] = 15;
            percentMap[STAT_SPIRIT] = 55;
            percentMap[STAT_STAMINA] = 30;
            break;
        case CLASS_WARLOCK:
            percentMap[STAT_INTELLECT] = 30;
            percentMap[STAT_SPIRIT] = 15;
            percentMap[STAT_STAMINA] = 55;
            break;
        }

        for (int i = 0; i < 5; i++)
        {
            Stats from = (Stats)urand(STAT_STRENGTH, MAX_STATS - 1);
            Stats to = (Stats)urand(STAT_STRENGTH, MAX_STATS - 1);
            int32 delta = urand(0, 5 + bot->GetLevel() / 3);
            if (from != to && percentMap[to] + delta <= 100 && percentMap[from] - delta >= 0)
            {
                percentMap[to] += delta;
                percentMap[from] -= delta;
            }
        }

        for (int i = STAT_STRENGTH; i < MAX_STATS; ++i)
        {
            Stats type = (Stats)i;
            std::ostringstream name; name << "immersive_stat_" << i;
            sRandomBotFacade.SetValue(owner, name.str(), percentMap[type]);
        }
    }
    bot->InitStatsForLevel(true);
    bot->UpdateAllStats();
}


void PlayerbotFactory::EnchantEquipment()
{
    if (bot->GetLevel() >= sPlayerbotAIConfig.minEnchantingBotLevel)
    {
        if (m_EnchantContainer.empty())
        {
            LoadEnchantContainer();
        }

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

void PlayerbotFactory::ApplyEnchantTemplate()
{
   int tab = AiFactory::GetPlayerSpecTab(bot);

   switch (bot->GetClass())
   {
   case CLASS_WARRIOR:
      if (tab == 2)
          ApplyEnchantTemplate(12);
      else if (tab == 1)
          ApplyEnchantTemplate(11);
      else
          ApplyEnchantTemplate(10);
      break;
   case CLASS_DRUID:
      if (tab == 2)
          ApplyEnchantTemplate(112);
      else if (tab == 0)
          ApplyEnchantTemplate(110);
      else
          ApplyEnchantTemplate(111);
      break;
   case CLASS_SHAMAN:
      if (tab == 0)
         ApplyEnchantTemplate(70);
      else if (tab == 2)
         ApplyEnchantTemplate(71);
      else
         ApplyEnchantTemplate(72);
      break;
   case CLASS_PALADIN:
      if (tab == 0)
         ApplyEnchantTemplate(20);
      else if (tab == 2)
         ApplyEnchantTemplate(22);
      else if (tab == 1)
         ApplyEnchantTemplate(21);
      break;
   case CLASS_HUNTER:
      ApplyEnchantTemplate(30);
      break;
   case CLASS_ROGUE:
      ApplyEnchantTemplate(40);
      break;
   case CLASS_MAGE:
      ApplyEnchantTemplate(80);
      break;
   case CLASS_WARLOCK:
      ApplyEnchantTemplate(90);
      break;
   case CLASS_PRIEST:
       ApplyEnchantTemplate(50);
       break;
   }
}

void PlayerbotFactory::ApplyEnchantTemplate(uint8 spec, Item* item)
{
   for (EnchantContainer::const_iterator itr = GetEnchantContainerBegin(); itr != GetEnchantContainerEnd(); ++itr)
      if ((*itr)->ClassId == bot->GetClass() && (*itr)->SpecId == spec)
         ai->EnchantItemT((*itr)->SpellId, (*itr)->SlotId, item);
}

void PlayerbotFactory::LoadEnchantContainer()
{
   for (EnchantContainer::const_iterator itr = m_EnchantContainer.begin(); itr != m_EnchantContainer.end(); ++itr)
      delete *itr;

   m_EnchantContainer.clear();

   uint32 count = 0;

   auto result = WorldDatabase.PQuery("SELECT class, spec, spellid, slotid FROM ai_playerbot_enchants");
   if (result)
   {
      do
      {
         Field* fields = result->Fetch();

         EnchantTemplate* pEnchant = new EnchantTemplate;

         pEnchant->ClassId = fields[0].GetUInt8();
         pEnchant->SpecId = fields[1].GetUInt8();
         pEnchant->SpellId = fields[2].GetUInt32();
         pEnchant->SlotId = fields[3].GetUInt8();

         m_EnchantContainer.push_back(pEnchant);
         ++count;
      } while (result->NextRow());
   }
}

void PlayerbotFactory::InitTaxiNodes()
{
    auto pmo = sPerformanceMonitor.start(PERF_MON_RNDBOT, "PlayerbotFactory_TaxiNodes");
    uint32 startMap = bot->GetMapId();

    TaxiNodeLevelContainer const& overworldTaxiNodeLevels = bot->GetTeam() == ALLIANCE ? overworldTaxiNodeLevelsA : overworldTaxiNodeLevelsH;

    for (TaxiNodeLevelContainer::const_iterator itr = overworldTaxiNodeLevels.begin(); itr != overworldTaxiNodeLevels.end(); ++itr)
    {
        TaxiNodeLevel const& taxiNodeLevel = *itr;

        if (taxiNodeLevel.Level > bot->GetLevel() && urand(0, 20)) //Limit nodes in high level area's.
            continue;

        if (taxiNodeLevel.MapId != startMap && taxiNodeLevel.Level + 20 > bot->GetLevel() && urand(0, 4)) //Limit nodes on other map.
            continue;

        bot->m_taxi.SetTaximaskNode(taxiNodeLevel.Index);
    }
}
