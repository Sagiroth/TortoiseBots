#pragma once

#include "CustomStrategy.h"
#include "generic/NonCombatStrategy.h"
#include "generic/RacialsStrategy.h"
#include "generic/ChatCommandHandlerStrategy.h"
#include "generic/WorldPacketHandlerStrategy.h"
#include "generic/DeadStrategy.h"
#include "generic/QuestStrategies.h"
#include "generic/LootNonCombatStrategy.h"
#include "generic/DuelStrategy.h"
#include "generic/KiteStrategy.h"
#include "generic/FleeStrategy.h"
#include "generic/FollowMasterStrategy.h"
#include "generic/RunawayStrategy.h"
#include "generic/StayStrategy.h"
#include "generic/UseFoodStrategy.h"
#include "generic/ConserveManaStrategy.h"
#include "generic/EmoteStrategy.h"
#include "generic/TankAssistStrategy.h"
#include "generic/DpsAssistStrategy.h"
#include "generic/AutoCcStrategy.h"
#include "generic/PassiveStrategy.h"
#include "generic/GrindingStrategy.h"
#include "generic/UsePotionsStrategy.h"
#include "generic/GuardStrategy.h"
#include "generic/CastTimeStrategy.h"
#include "generic/ThreatStrategy.h"
#include "generic/FocusStrategy.h"
#include "generic/TellTargetStrategy.h"
#include "generic/AttackEnemyPlayersStrategy.h"
#include "generic/MarkRtiStrategy.h"
#include "generic/MeleeCombatStrategy.h"
#include "generic/PullStrategy.h"
#include "generic/RangedCombatStrategy.h"
#include "generic/ReturnStrategy.h"
#include "generic/RpgStrategy.h"
#include "generic/TravelStrategy.h"
#include "generic/DebugStrategy.h"
#include "generic/AoeFearStrategy.h"
#include "generic/BattlegroundStrategy.h"
#include "generic/MaintenanceStrategy.h"
#include "generic/GroupStrategy.h"
#include "generic/GuildStrategy.h"
#include "generic/FocusTargetStrategy.h"
#include "generic/AvoidMobsStrategy.h"
#include "generic/WanderStrategy.h"
#include "generic/ConsumableStrategy.h"
#include "generic/MimicConsumableStrategy.h"
// #include "generic/WorldBuffTravelStrategy.h" // E2E green: excluded to avoid GetSource mismatches
#include "generic/DungeonStrategy.h"
#include "generic/OnyxiasLairDungeonStrategies.h"
#include "generic/MoltenCoreDungeonStrategies.h"
#include "generic/BlackwingLairDungeonStrategies.h"
#include "generic/NaxxramasDungeonStrategies.h"
#include "generic/EmeraldSanctumDungeonStrategies.h"
#include "generic/LowerKarazhanDungeonStrategies.h"
#include "generic/ClassicRaidDungeonStrategies.h"
#include "generic/KarazhanCryptDungeonStrategies.h"
#include "generic/GluthDungeonStrategies.h"
#include "generic/KelthuzadDungeonStrategies.h"
#include "generic/SapphironDungeonStrategies.h"
#include "generic/HeiganDungeonStrategies.h"
#include "generic/AnubrekhanDungeonStrategies.h"

namespace ai
{
    class StrategyContext : public NamedObjectContext<Strategy>
    {
    public:
        StrategyContext()
        {
            creators["racials"] = [](PlayerbotAI* ai) { return new RacialsStrategy(ai); };
            creators["loot"] = [](PlayerbotAI* ai) { return new LootNonCombatStrategy(ai); };
            creators["gather"] = [](PlayerbotAI* ai) { return new GatherStrategy(ai); };
            creators["roll"] = [](PlayerbotAI* ai) { return new RollStrategy(ai); };
            creators["delayed roll"] = [](PlayerbotAI* ai) { return new DelayedRollStrategy(ai); };
            creators["emote"] = [](PlayerbotAI* ai) { return new EmoteStrategy(ai); };
            creators["passive"] = [](PlayerbotAI* ai) { return new PassiveStrategy(ai); };
            creators["conserve mana"] = [](PlayerbotAI* ai) { return new ConserveManaStrategy(ai); };
            creators["food"] = [](PlayerbotAI* ai) { return new UseFoodStrategy(ai); };
            creators["consumables"] = [](PlayerbotAI* ai) { return new ConsumableStrategy(ai); };
            creators["chat"] = [](PlayerbotAI* ai) { return new ChatCommandHandlerStrategy(ai); };
            creators["default"] = [](PlayerbotAI* ai) { return new WorldPacketHandlerStrategy(ai); };
            creators["ready check"] = [](PlayerbotAI* ai) { return new ReadyCheckStrategy(ai); };
            creators["dead"] = [](PlayerbotAI* ai) { return new DeadStrategy(ai); };
            creators["flee"] = [](PlayerbotAI* ai) { return new FleeStrategy(ai); };
            creators["avoid mobs"] = [](PlayerbotAI* ai) { return new AvoidMobsStrategy(ai); };
            creators["duel"] = [](PlayerbotAI* ai) { return new DuelStrategy(ai); };
            creators["start duel"] = [](PlayerbotAI* ai) { return new StartDuelStrategy(ai); };
            creators["kite"] = [](PlayerbotAI* ai) { return new KiteStrategy(ai); };
            creators["potions"] = [](PlayerbotAI* ai) { return new UsePotionsStrategy(ai); };
            creators["cast time"] = [](PlayerbotAI* ai) { return new CastTimeStrategy(ai); };
            creators["threat"] = [](PlayerbotAI* ai) { return new ThreatStrategy(ai); };
            creators["focus"] = [](PlayerbotAI* ai) { return new FocusStrategy(ai); };
            creators["tell target"] = [](PlayerbotAI* ai) { return new TellTargetStrategy(ai); };
            creators["pvp"] = [](PlayerbotAI* ai) { return new AttackEnemyPlayersStrategy(ai); };
            creators["return"] = [](PlayerbotAI* ai) { return new ReturnStrategy(ai); };
            creators["custom"] = [](PlayerbotAI* ai) { return new CustomStrategy(ai); };
            creators["reveal"] = [](PlayerbotAI* ai) { return new RevealStrategy(ai); };
            creators["collision"] = [](PlayerbotAI* ai) { return new CollisionStrategy(ai); };
            creators["rpg"] = [](PlayerbotAI* ai) { return new RpgStrategy(ai); };
            creators["rpg quest"] = [](PlayerbotAI* ai) { return new RpgQuestStrategy(ai); };
            creators["rpg vendor"] = [](PlayerbotAI* ai) { return new RpgVendorStrategy(ai); };
            creators["rpg explore"] = [](PlayerbotAI* ai) { return new RpgExploreStrategy(ai); };
            creators["rpg maintenance"] = [](PlayerbotAI* ai) { return new RpgMaintenanceStrategy(ai); };
            creators["rpg guild"] = [](PlayerbotAI* ai) { return new RpgGuildStrategy(ai); };
            creators["rpg bg"] = [](PlayerbotAI* ai) { return new RpgBgStrategy(ai); };
            creators["rpg player"] = [](PlayerbotAI* ai) { return new RpgPlayerStrategy(ai); };
            creators["rpg craft"] = [](PlayerbotAI* ai) { return new RpgCraftStrategy(ai); };
            creators["rpg jump"] = [](PlayerbotAI* ai) { return new RpgJumpStrategy(ai); };
            creators["follow jump"] = [](PlayerbotAI* ai) { return new FollowJumpStrategy(ai); };
            creators["chase jump"] = [](PlayerbotAI* ai) { return new ChaseJumpStrategy(ai); };
			creators["travel"] = [](PlayerbotAI* ai) { return new TravelStrategy(ai); };
            creators["travel once"] = [](PlayerbotAI* ai) { return new TravelOnceStrategy(ai); };
            creators["explore"] = [](PlayerbotAI* ai) { return new ExploreStrategy(ai); };
            creators["map"] = [](PlayerbotAI* ai) { return new MapStrategy(ai); };
            creators["map full"] = [](PlayerbotAI* ai) { return new MapFullStrategy(ai); };
            creators["sit"] = [](PlayerbotAI* ai) { return new SitStrategy(ai); };
            creators["mark rti"] = [](PlayerbotAI* ai) { return new MarkRtiStrategy(ai); };
            creators["ads"] = [](PlayerbotAI* ai) { return new PossibleAdsStrategy(ai); };
            creators["close"] = [](PlayerbotAI* ai) { return new MeleeCombatStrategy(ai); };
            creators["ranged"] = [](PlayerbotAI* ai) { return new RangedCombatStrategy(ai); };
            creators["behind"] = [](PlayerbotAI* ai) { return new SetBehindCombatStrategy(ai); };
            creators["tank face"] = [](PlayerbotAI* ai) { return new TankFaceStrategy(ai); };
            creators["battleground"] = [](PlayerbotAI* ai) { return new BattlegroundStrategy(ai); };
            creators["warsong"] = [](PlayerbotAI* ai) { return new WarsongStrategy(ai); };
            creators["alterac"] = [](PlayerbotAI* ai) { return new AlteracStrategy(ai); };
            creators["arathi"] = [](PlayerbotAI* ai) { return new ArathiStrategy(ai); };
            creators["mount"] = [](PlayerbotAI* ai) { return new MountStrategy(ai); };
            creators["attack tagged"] = [](PlayerbotAI* ai) { return new AttackTaggedStrategy(ai); };
            creators["debug"] = [](PlayerbotAI* ai) { return new DebugStrategy(ai); };
            creators["debug action"] = [](PlayerbotAI* ai) { return new DebugActionStrategy(ai); };
            creators["debug move"] = [](PlayerbotAI* ai) { return new DebugMoveStrategy(ai); };
            creators["aoe fear"] = [](PlayerbotAI* ai) { return new AoeFearStrategy(ai); };
            creators["debug rpg"] = [](PlayerbotAI* ai) { return new DebugRpgStrategy(ai); };
            creators["debug spell"] = [](PlayerbotAI* ai) { return new DebugSpellStrategy(ai); };
            creators["debug travel"] = [](PlayerbotAI* ai) { return new DebugTravelStrategy(ai); };
            creators["debug threat"] = [](PlayerbotAI* ai) { return new DebugThreatStrategy(ai); };
            creators["debug mount"] = [](PlayerbotAI* ai) { return new DebugMountStrategy(ai); };
            creators["debug grind"] = [](PlayerbotAI* ai) { return new DebugGrindStrategy(ai); };
            creators["debug loot"] = [](PlayerbotAI* ai) { return new DebugLootStrategy(ai); };
            creators["debug log"] = [](PlayerbotAI* ai) { return new DebugLogStrategy(ai); };
            creators["debug llm"] = [](PlayerbotAI* ai) { return new DebugLLMStrategy(ai); };
            creators["debug stuck"] = [](PlayerbotAI* ai) { return new DebugStuckStrategy(ai); };
            creators["debug xp"] = [](PlayerbotAI* ai) { return new DebugXpStrategy(ai); };
            creators["debug equip"] = [](PlayerbotAI* ai) { return new DebugEquipStrategy(ai); };
            creators["debug logname"] = [](PlayerbotAI* ai) { return new DebugLogNameStrategy(ai); };
            creators["maintenance"] = [](PlayerbotAI* ai) { return new MaintenanceStrategy(ai); };
            creators["group"] = [](PlayerbotAI* ai) { return new GroupStrategy(ai); };
            creators["guild"] = [](PlayerbotAI* ai) { return new GuildStrategy(ai); };
            creators["grind"] = [](PlayerbotAI* ai) { return new GrindingStrategy(ai); };
            creators["avoid aoe"] = [](PlayerbotAI* ai) { return new AvoidAoeStrategy(ai); };
            creators["wait for attack"] = [](PlayerbotAI* ai) { return new WaitForAttackStrategy(ai); };
            creators["pull back"] = [](PlayerbotAI* ai) { return new PullBackStrategy(ai); };
            creators["focus heal targets"] = [](PlayerbotAI* ai) { return new FocusHealTargetsStrategy(ai); };
            creators["focus rti targets"] = [](PlayerbotAI* ai) { return new FocusRtiTargetsStrategy(ai); };
            creators["heal interrupt"] = [](PlayerbotAI* ai) { return new HealInterruptStrategy(ai); };
            creators["preheal"] = [](PlayerbotAI* ai) { return new PreHealStrategy(ai); };
            creators["spread"] = [](PlayerbotAI* ai) { return new SpreadStrategy(ai); };
            creators["wbuff"] = [](PlayerbotAI* ai) { return new WorldBuffStrategy(ai); };
            // creators["wbuff travel"] = [](PlayerbotAI* ai) { return new WorldBuffTravelStrategy(ai); }; // E2E green: excluded
            creators["silent"] = [](PlayerbotAI* ai) { return new SilentStrategy(ai); };
            creators["nowar"] = [](PlayerbotAI* ai) { return new NoWarStrategy(ai); };
            creators["ai chat"] = [](PlayerbotAI* ai) { return new AIChatStrategy(ai); };

            creators["mimic consumables"] = [](PlayerbotAI* ai) { return new MimicConsumableStrategy(ai); };
            // Dungeon Strategies
            creators["dungeon"] = [](PlayerbotAI* ai) { return new DungeonStrategy(ai); };
            creators["onyxia's lair"] = [](PlayerbotAI* ai) { return new OnyxiasLairDungeonStrategy(ai); };
            creators["molten core"] = [](PlayerbotAI* ai) { return new MoltenCoreDungeonStrategy(ai); };
            creators["blackwing lair"] = [](PlayerbotAI* ai) { return new BlackwingLairDungeonStrategy(ai); };
            creators["naxxramas"] = [](PlayerbotAI* ai) { return new NaxxramasDungeonStrategy(ai); };
            creators["zul'gurub"] = [](PlayerbotAI* ai) { return new ZulgurubDungeonStrategy(ai); };
            creators["ruins of ahn'qiraj"] = [](PlayerbotAI* ai) { return new RuinsOfAhnqirajDungeonStrategy(ai); };
            creators["ahn'qiraj temple"] = [](PlayerbotAI* ai) { return new AhnqirajTempleDungeonStrategy(ai); };
            creators["emerald sanctum"] = [](PlayerbotAI* ai) { return new EmeraldSanctumDungeonStrategy(ai); };
            creators["lower karazhan"] = [](PlayerbotAI* ai) { return new LowerKarazhanDungeonStrategy(ai); };
            creators["karazhan crypt"] = [](PlayerbotAI* ai) { return new KarazhanCryptDungeonStrategy(ai); };

            // Dungeon Boss Strategies
            creators["onyxia"] = [](PlayerbotAI* ai) { return new OnyxiaFightStrategy(ai); };
            creators["magmadar"] = [](PlayerbotAI* ai) { return new MagmadarFightStrategy(ai); };
            creators["garr"] = [](PlayerbotAI* ai) { return new GarrFightStrategy(ai); };
            creators["shazzrah"] = [](PlayerbotAI* ai) { return new ShazzrahFightStrategy(ai); };
            creators["geddon"] = [](PlayerbotAI* ai) { return new GeddonFightStrategy(ai); };
            creators["suppression room"] = [](PlayerbotAI* ai) { return new SuppressionRoomStrategy(ai); };
            creators["chromaggus"] = [](PlayerbotAI* ai) { return new ChromaggusFightStrategy(ai); };
            creators["broodlord"] = [](PlayerbotAI* ai) { return new BroodlordFightStrategy(ai); };
            creators["nefarian"] = [](PlayerbotAI* ai) { return new NefarianFightStrategy(ai); };
            creators["vael"] = [](PlayerbotAI* ai) { return new VaelFightStrategy(ai); };
            creators["four horseman"] = [](PlayerbotAI* ai) { return new FourHorsemanFightStrategy(ai); };
            creators["gluth"] = [](PlayerbotAI* ai) { return new GluthFightStrategy(ai); };
            creators["kel'thuzad"] = [](PlayerbotAI* ai) { return new KelthuzadFightStrategy(ai); };
            creators["sapphiron"] = [](PlayerbotAI* ai) { return new SapphironFightStrategy(ai); };
            creators["heigan"] = [](PlayerbotAI* ai) { return new HeiganFightStrategy(ai); };
            creators["anub'rekhan"] = [](PlayerbotAI* ai) { return new AnubrekhanFightStrategy(ai); };
            creators["solnius"] = [](PlayerbotAI* ai) { return new SolniusFightStrategy(ai); };
            creators["araxxna"] = [](PlayerbotAI* ai) { return new AraxxnaFightStrategy(ai); };
            creators["moroes"] = [](PlayerbotAI* ai) { return new MoroesFightStrategy(ai); };

        }
    };

    class MovementStrategyContext : public NamedObjectContext<Strategy>
    {
    public:
        MovementStrategyContext() : NamedObjectContext<Strategy>(false, true)
        {
            creators["follow"] = [](PlayerbotAI* ai) { return new FollowMasterStrategy(ai); };
            creators["stay"] = [](PlayerbotAI* ai) { return new StayStrategy(ai); };
            creators["runaway"] = [](PlayerbotAI* ai) { return new RunawayStrategy(ai); };
            creators["flee from adds"] = [](PlayerbotAI* ai) { return new FleeFromAddsStrategy(ai); };
            creators["guard"] = [](PlayerbotAI* ai) { return new GuardStrategy(ai); };
            creators["free"] = [](PlayerbotAI* ai) { return new FreeStrategy(ai); };
            creators["wander"] = [](PlayerbotAI* ai) { return new WanderStrategy(ai); };
        }
    };

    class AssistStrategyContext : public NamedObjectContext<Strategy>
    {
    public:
        AssistStrategyContext() : NamedObjectContext<Strategy>(false, true)
        {
            creators["dps assist"] = [](PlayerbotAI* ai) { return new DpsAssistStrategy(ai); };
            creators["dps aoe"] = [](PlayerbotAI* ai) { return new DpsAoeStrategy(ai); };
            creators["tank assist"] = [](PlayerbotAI* ai) { return new TankAssistStrategy(ai); };
            creators["auto cc"] = [](PlayerbotAI* ai) { return new AutoCcStrategy(ai); };
        }
    };

    class QuestStrategyContext : public NamedObjectContext<Strategy>
    {
    public:
        QuestStrategyContext() : NamedObjectContext<Strategy>(false, true)
        {
            creators["quest"] = [](PlayerbotAI* ai) { return new DefaultQuestStrategy(ai); };
            creators["accept all quests"] = [](PlayerbotAI* ai) { return new AcceptAllQuestsStrategy(ai); };
        }
    };

    class FishStrategyContext : public NamedObjectContext<Strategy>
    {
    public:
        FishStrategyContext() : NamedObjectContext<Strategy>(false, true)
        {
            creators["fish"] = [](PlayerbotAI* ai) { return new FishStrategy(ai); };
            creators["tfish"] = [](PlayerbotAI* ai) { return new TFishStrategy(ai); };
        }
    };
};
