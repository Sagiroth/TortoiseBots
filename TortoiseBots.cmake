# Native Tortoise module integration for TortoiseBots.
#
# Penqle's module loader recursively collects every C/C++ file below src/.
# Keep only the tiny loader entrypoint there; the donor tree stays in the
# repository root and is added here as an explicit, MANGOSBOT_ZERO-filtered
# source set.

if(TORTOISE_MODULE_CMAKE_PHASE STREQUAL "DISCOVERY")
  set(TORTOISEBOTS_ROOT "${CMAKE_CURRENT_LIST_DIR}")

  # PlayerbotAIConfig deliberately keeps its mature, large configuration
  # template beside the behavior source. Native packaging installs the
  # runtime file beside mangosd.conf; deployments can copy/edit it explicitly.
  set(TORTOISEBOTS_AI_CONFIG "${CMAKE_CURRENT_BINARY_DIR}/aiplayerbot.conf")
  configure_file(
    "${TORTOISEBOTS_ROOT}/ai/playerbot/aiplayerbot.conf.dist.in"
    "${TORTOISEBOTS_AI_CONFIG}"
    COPYONLY)
  # PlayerbotAIConfig resolves this file beside mangosd.conf, not through the
  # module INI aggregator used by tortoise_bots.conf.
  install(FILES "${TORTOISEBOTS_AI_CONFIG}" DESTINATION "${CONF_DIR}")

  configure_file(
    "${TORTOISEBOTS_ROOT}/conf/tortoise_bots.conf.dist"
    "${CMAKE_CURRENT_BINARY_DIR}/tortoise_bots.conf"
    COPYONLY)
  CopyModuleConfig("${CMAKE_CURRENT_BINARY_DIR}/tortoise_bots.conf")

  # Static modules do not have a shared-library payload to carry their data.
  # Install the native migrations beside the runtime module path so the core
  # AutoUpdater can apply them only when this optional module is deployed.
  if(EXISTS "${TORTOISEBOTS_ROOT}/data/sql/world")
    install(DIRECTORY "${TORTOISEBOTS_ROOT}/data/sql/world/"
      DESTINATION "${CMAKE_INSTALL_PREFIX}/modules/TortoiseBots/data/sql/world")
  endif()
  if(EXISTS "${TORTOISEBOTS_ROOT}/data/sql/char")
    install(DIRECTORY "${TORTOISEBOTS_ROOT}/data/sql/char/"
      DESTINATION "${CMAKE_INSTALL_PREFIX}/modules/TortoiseBots/data/sql/character")
  endif()

  set(TORTOISEBOTS_HOST_SRC
    "${TORTOISEBOTS_ROOT}/host/Module.cpp"
    "${TORTOISEBOTS_ROOT}/host/BotHostAdapter.cpp"
    "${TORTOISEBOTS_ROOT}/host/BotSessionAdapter.cpp"
    "${TORTOISEBOTS_ROOT}/host/BotChatAdapter.cpp"
    "${TORTOISEBOTS_ROOT}/host/BotPacketAdapter.cpp"
    "${TORTOISEBOTS_ROOT}/host/BotPlayerAdapter.cpp"
    "${TORTOISEBOTS_ROOT}/runtime/BotManager.cpp"
    "${TORTOISEBOTS_ROOT}/runtime/RandomBotService.cpp"
    "${TORTOISEBOTS_ROOT}/runtime/PlayerbotAIStorage.cpp"
    "${TORTOISEBOTS_ROOT}/runtime/PlayerbotAIAdapter.cpp"
    "${TORTOISEBOTS_ROOT}/runtime/PlayerbotRuntimeFacade.cpp"
    "${TORTOISEBOTS_ROOT}/behavior/Movement.cpp"
    "${TORTOISEBOTS_ROOT}/commands/BotCommands.cpp"
  )

  # Real PlayerbotAI/Engine/Strategy foundations. Deliberately do not add
  # PlayerbotMgr.cpp, RandomPlayerbotMgr.cpp, or their donor login managers:
  # those classes own a second session lifecycle and are not the module's
  # Headless/BotManager ownership model.
  set(TORTOISEBOTS_AI_SRC
    "${TORTOISEBOTS_ROOT}/ai/playerbot/PlayerbotAIBase.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/PlayerbotAI.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/PlayerbotAIConfig.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/AiFactory.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/ServerFacade.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/PlayerbotSecurity.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/BotLog.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/BotActionLog.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/BotDiagnostics.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/BroadcastHelper.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/ChatFilter.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/FleeManager.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/Helpers.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/LootObjectStack.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/MemoryMonitor.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/PerformanceMonitor.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/PlayerbotDbStore.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/PlayerbotFactory.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/PlayerbotHelpMgr.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/PlayerbotLLMInterface.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/PlayerbotTextMgr.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/RandomItemMgr.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/Talentspec.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/ChatHelper.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/WorldPosition.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/WorldSquare.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/GuidPosition.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/TravelMgr.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/TravelNode.cpp"
    "${TORTOISEBOTS_ROOT}/ai/botpch.cpp"
  )

  set(TORTOISEBOTS_STRATEGY_SRC
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/Engine.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/AiObjectContext.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/AiObject.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/Event.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/Queue.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/Trigger.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/Value.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/Action.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/Strategy.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/Multiplier.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/PassiveMultiplier.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/ReactionEngine.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/CustomStrategy.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/ActionBasket.cpp"
  )

  set(TORTOISEBOTS_GENERIC_SRC
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/generic/AttackEnemyPlayersStrategy.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/generic/AvoidMobsStrategy.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/generic/BattlegroundStrategy.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/generic/BlackwingLairDungeonStrategies.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/generic/CastTimeStrategy.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/generic/ChatCommandHandlerStrategy.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/generic/ClassStrategy.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/generic/CombatStrategy.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/generic/ConserveManaStrategy.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/generic/ConsumableStrategy.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/generic/DeadStrategy.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/generic/DebugStrategy.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/generic/DpsAssistStrategy.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/generic/DuelStrategy.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/generic/DungeonMultipliers.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/generic/DungeonStrategy.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/generic/EmoteStrategy.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/generic/FleeStrategy.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/generic/FocusTargetStrategy.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/generic/FollowMasterStrategy.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/generic/GrindingStrategy.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/generic/GroupStrategy.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/generic/GuardStrategy.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/generic/GuildStrategy.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/generic/KarazhanDungeonStrategies.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/generic/KiteStrategy.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/generic/LfgStrategy.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/generic/LootNonCombatStrategy.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/generic/MaintenanceStrategy.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/generic/MarkRtiStrategy.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/generic/MeleeCombatStrategy.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/generic/MoltenCoreDungeonStrategies.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/generic/NaxxramasDungeonStrategies.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/generic/NonCombatStrategy.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/generic/OnyxiasLairDungeonStrategies.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/generic/PassiveStrategy.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/generic/PullStrategy.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/generic/QuestStrategies.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/generic/RTSCStrategy.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/generic/RacialsStrategy.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/generic/RangedCombatStrategy.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/generic/ReactionStrategy.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/generic/ReturnStrategy.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/generic/RpgStrategy.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/generic/RunawayStrategy.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/generic/SayStrategy.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/generic/StayStrategy.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/generic/TankAssistStrategy.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/generic/TellTargetStrategy.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/generic/ThreatStrategy.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/generic/TravelStrategy.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/generic/UseFoodStrategy.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/generic/UsePotionsStrategy.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/generic/WanderStrategy.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/generic/WorldBuffTravelStrategy.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/generic/WorldPacketHandlerStrategy.cpp"
  )

  set(TORTOISEBOTS_CLASS_CONTEXT_SRC
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/druid/DruidAiObjectContext.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/hunter/HunterAiObjectContext.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/mage/MageAiObjectContext.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/paladin/PaladinAiObjectContext.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/priest/PriestAiObjectContext.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/rogue/RogueAiObjectContext.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/shaman/ShamanAiObjectContext.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/warlock/WarlockAiObjectContext.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/warrior/WarriorAiObjectContext.cpp")

  set(TORTOISEBOTS_CLASS_SRC
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/druid/BalanceDruidStrategy.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/druid/BearDruidStrategy.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/druid/CatDruidStrategy.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/druid/DpsFeralDruidStrategy.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/druid/DruidActions.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/druid/DruidAiObjectContext.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/druid/DruidMultipliers.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/druid/DruidPullStrategy.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/druid/DruidStrategy.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/druid/DruidTriggers.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/druid/DruidValues.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/druid/FeralDruidStrategy.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/druid/GenericDruidNonCombatStrategy.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/druid/GenericDruidStrategy.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/druid/LevelingDruidStrategy.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/druid/RestoDruidStrategy.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/druid/RestorationDruidStrategy.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/druid/TankFeralDruidStrategy.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/hunter/BeastMasteryHunterStrategy.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/hunter/GenericHunterNonCombatStrategy.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/hunter/GenericHunterStrategy.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/hunter/HunterActions.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/hunter/HunterAiObjectContext.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/hunter/HunterBuffStrategies.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/hunter/HunterMultipliers.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/hunter/HunterStrategy.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/hunter/HunterTriggers.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/hunter/MarksmanshipHunterStrategy.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/hunter/SurvivalHunterStrategy.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/mage/ArcaneMageStrategy.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/mage/FireMageStrategy.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/mage/FrostFireMageStrategy.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/mage/FrostMageStrategy.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/mage/GenericMageNonCombatStrategy.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/mage/GenericMageStrategy.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/mage/MageActions.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/mage/MageAiObjectContext.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/mage/MageMultipliers.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/mage/MageStrategy.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/mage/MageTriggers.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/paladin/DpsPaladinStrategy.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/paladin/GenericPaladinNonCombatStrategy.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/paladin/GenericPaladinStrategy.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/paladin/HealPaladinStrategy.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/paladin/HolyPaladinStrategy.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/paladin/OffhealRetPaladinStrategy.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/paladin/PaladinActions.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/paladin/PaladinAiObjectContext.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/paladin/PaladinBuffStrategies.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/paladin/PaladinMultipliers.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/paladin/PaladinPullStrategy.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/paladin/PaladinStrategy.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/paladin/PaladinTriggers.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/paladin/ProtectionPaladinStrategy.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/paladin/RetributionPaladinStrategy.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/paladin/TankPaladinStrategy.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/priest/DisciplinePriestStrategy.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/priest/GenericPriestStrategy.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/priest/HealPriestStrategy.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/priest/HolyPriestStrategy.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/priest/PriestActions.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/priest/PriestAiObjectContext.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/priest/PriestMultipliers.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/priest/PriestNonCombatStrategy.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/priest/PriestStrategy.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/priest/PriestTriggers.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/priest/ShadowPriestStrategy.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/rogue/AssassinationRogueStrategy.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/rogue/CombatRogueStrategy.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/rogue/DpsRogueStrategy.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/rogue/GenericRogueNonCombatStrategy.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/rogue/RogueActions.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/rogue/RogueAiObjectContext.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/rogue/RogueMultipliers.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/rogue/RogueStrategy.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/rogue/RogueTriggers.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/rogue/SubtletyRogueStrategy.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/shaman/ElementalShamanStrategy.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/shaman/EnhancementShamanStrategy.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/shaman/GenericShamanStrategy.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/shaman/RestoShamanStrategy.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/shaman/RestorationShamanStrategy.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/shaman/ShamanActions.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/shaman/ShamanAiObjectContext.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/shaman/ShamanMultipliers.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/shaman/ShamanNonCombatStrategy.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/shaman/ShamanStrategy.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/shaman/ShamanTriggers.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/shaman/TotemsShamanStrategy.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/warlock/AfflictionWarlockStrategy.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/warlock/DemonologyWarlockStrategy.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/warlock/DestructionWarlockStrategy.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/warlock/GenericWarlockNonCombatStrategy.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/warlock/GenericWarlockStrategy.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/warlock/TankWarlockStrategy.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/warlock/WarlockActions.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/warlock/WarlockAiObjectContext.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/warlock/WarlockMultipliers.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/warlock/WarlockStrategy.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/warlock/WarlockTriggers.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/warrior/ArmsWarriorStrategy.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/warrior/FuryWarriorStrategy.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/warrior/GenericWarriorNonCombatStrategy.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/warrior/GenericWarriorStrategy.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/warrior/ProtectionWarriorStrategy.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/warrior/TankWarriorStrategy.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/warrior/WarriorActions.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/warrior/WarriorAiObjectContext.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/warrior/WarriorMultipliers.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/warrior/WarriorPullStrategy.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/warrior/WarriorStrategy.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/warrior/WarriorTriggers.cpp"
  )

  set(TORTOISEBOTS_VALUE_SRC
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/values/ActiveSpellValue.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/values/AggressiveTargetValue.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/values/AlwaysLootListValue.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/values/AoeHealValues.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/values/AoeValues.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/values/Arrow.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/values/AttackerCountValues.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/values/AttackerWithoutAuraTargetValue.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/values/AttackersValue.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/values/AvailableLootValue.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/values/BudgetValues.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/values/CcTargetValue.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/values/CollisionValue.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/values/CraftValues.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/values/CurrentCcTargetValue.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/values/CurrentTargetValue.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/values/DeadValues.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/values/DistanceValue.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/values/DpsTargetValue.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/values/DuelTargetValue.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/values/EnemyHealerTargetValue.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/values/EnemyPlayerValue.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/values/EngineValues.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/values/EntryValues.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/values/EstimatedLifetimeValue.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/values/FishValues.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/values/Formations.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/values/FreeMoveValues.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/values/GlyphValues.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/values/GrindTargetValue.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/values/GroupLeaderValue.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/values/GroupValues.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/values/GuidPositionValues.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/values/GuildValues.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/values/HasAvailableLootValue.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/values/HasTotemValue.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/values/HazardsValue.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/values/InvalidTargetValue.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/values/IsBehindValue.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/values/IsFacingValue.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/values/IsMovingValue.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/values/ItemCountValue.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/values/ItemForSpellValue.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/values/ItemUsageValue.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/values/LastMovementValue.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/values/LastSpellCastValue.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/values/LeastHpTargetValue.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/values/LineTargetValue.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/values/LootStrategyValue.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/values/LootValues.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/values/MaintenanceValues.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/values/MountValues.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/values/MoveStyleValue.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/values/NearestAdsValue.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/values/NearestCorpsesValue.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/values/NearestFriendlyPlayersValue.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/values/NearestGameObjects.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/values/NearestNonBotPlayersValue.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/values/NearestNpcsValue.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/values/NearestUnitsValue.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/values/NewPlayerNearbyValue.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/values/OperatorValues.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/values/OutfitListValue.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/values/PartyMemberSnaredTargetValue.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/values/PartyMemberToDispel.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/values/PartyMemberToHeal.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/values/PartyMemberToResurrect.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/values/PartyMemberToSoulstone.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/values/PartyMemberValue.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/values/PartyMemberWithoutAuraValue.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/values/PartyMemberWithoutItemValue.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/values/PetTargetValue.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/values/PositionValue.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/values/PossibleAttackTargetsValue.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/values/PossibleRpgTargetsValue.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/values/PossibleTargetsValue.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/values/PvpValues.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/values/QuestValues.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/values/RTSCValues.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/values/RangeValues.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/values/RpgValues.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/values/RtiTargetValue.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/values/RtiValue.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/values/RuneForgeValues.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/values/SelfTargetValue.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/values/SkipSpellsListValue.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/values/SnareTargetValue.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/values/SpellCastUsefulValue.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/values/SpellIdValue.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/values/Stances.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/values/StatsValues.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/values/StuckValues.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/values/SubStrategyValue.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/values/TankTargetValue.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/values/TargetValue.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/values/ThreatValues.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/values/TradeValues.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/values/TrainerValues.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/values/TravelValues.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/values/VendorValues.cpp"
  )
  set(TORTOISEBOTS_TRIGGER_SRC
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/triggers/BossAuraTriggers.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/triggers/BotStateTriggers.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/triggers/ChatCommandTrigger.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/triggers/CureTriggers.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/triggers/DungeonTriggers.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/triggers/FishingTriggers.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/triggers/GenericTriggers.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/triggers/GuildMeetingTrigger.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/triggers/GuildTriggers.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/triggers/HealthTriggers.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/triggers/KarazhanDungeonTriggers.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/triggers/LfgTriggers.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/triggers/LootTriggers.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/triggers/MoltenCoreDungeonTriggers.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/triggers/NaxxramasDungeonTriggers.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/triggers/OnyxiasLairDungeonTriggers.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/triggers/PullTriggers.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/triggers/PvpTriggers.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/triggers/RangeTriggers.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/triggers/RpgTriggers.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/triggers/RtiTriggers.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/triggers/StuckTriggers.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/triggers/TravelTriggers.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/triggers/WithinAreaTrigger.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/triggers/WorldBuffTravelTriggers.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/triggers/WorldPacketTrigger.cpp"
  )
  set(TORTOISEBOTS_ACTION_SRC
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/AcceptBattlegroundInvitationAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/AcceptDuelAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/AcceptInvitationAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/AcceptQuestAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/AcceptResurrectAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/AddLootAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/AhAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/AreaTriggerAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/ArenaTeamActions.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/AttackAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/AutoCompleteQuestAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/AutoLearnSpellAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/AutoMaintenanceOnLevelupAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/BankAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/BattleGroundJoinAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/BattleGroundTactics.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/BattleGroundTacticsAV.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/BossAuraActions.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/BotStateActions.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/BuffAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/BuyAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/CancelChannelAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/CastCustomSpellAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/ChangeChatAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/ChangeStrategyAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/ChangeTalentsAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/ChatShortcutActions.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/CheatAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/CheckMailAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/CheckMountStateAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/CheckValuesAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/ChooseRpgTargetAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/ChooseTargetActions.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/ChooseTravelTargetAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/CombatActions.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/CustomStrategyEditAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/DebugAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/DelayAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/DestroyItemAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/DropQuestAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/DungeonActions.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/EmoteAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/EquipAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/EquipGlyphsAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/FactionAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/FishAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/FishingAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/FlagAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/FollowActions.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/GenericActions.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/GenericSpellActions.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/GiveItemAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/GlyphAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/GoAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/GossipHelloAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/GreetAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/GuildAcceptAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/GuildAcceptQuestOrderAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/GuildBankAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/GuildCraftOrderAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/GuildCreateActions.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/GuildManagementActions.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/GuildShareAhBuyAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/GuildShareItemAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/HelpAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/HireAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/HonorGainAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/ImbueAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/InventoryAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/InventoryChangeFailureAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/InviteToGroupAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/KarazhanDungeonActions.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/KeepItemAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/LeaveGroupAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/LfgActions.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/ListQuestsActions.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/ListSpellsAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/LogLevelAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/LootAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/LootRollAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/LootStrategyAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/MailAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/MoltenCoreDungeonActions.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/MoveStyleAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/MoveToRpgTargetAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/MoveToTravelTargetAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/MovementActions.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/NaxxramasDungeonActions.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/NonCombatActions.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/OnyxiasLairDungeonActions.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/OpenItemAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/OutfitAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/PassLeadershipToMasterAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/PetitionSignAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/PetsAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/PositionAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/PullActions.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/QueryItemUsageAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/QueryQuestAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/QuestAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/QuestConfirmAcceptAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/QuestRewardActions.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/RandomBotUpdateAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/RangeAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/ReachTargetActions.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/ReadyCheckAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/ReleaseSpiritAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/RememberTaxiAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/RemoveAuraAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/RepairAllAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/ResetAiAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/ResetInstancesAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/RevealGatheringItemAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/ReviveFromCorpseAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/RewardAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/RpgAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/RpgSubActions.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/RtiAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/RtscAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/SaveManaAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/SayAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/SecurityCheckAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/SeeSpellAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/SellAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/SendMailAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/SetAvoidAreaAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/SetCraftAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/SetFocusHealTargetsAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/SetHomeAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/SetValueAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/ShareQuestAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/SkillAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/SkipSpellsListAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/StatsAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/StayActions.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/SuggestWhatToDoAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/TalkToQuestGiverAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/TameAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/TaxiAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/TeleportAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/TellCastFailedAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/TellEmblemsAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/TellGlyphsAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/TellItemCountAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/TellLosAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/TellMasterAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/TellPvpStatsAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/TellReputationAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/TellTargetAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/TradeAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/TradeStatusAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/TradeStatusExtendedAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/TradeValues.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/TrainerAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/TravelAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/UnequipAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/UnlockItemAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/UnlockTradedItemAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/UnstuckAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/UpdateGearAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/UseConsumableAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/UseItemAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/UseMeetingStoneAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/UseTrinketAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/ValueActions.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/VehicleActions.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/WaitForAttackAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/WhoAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/WipeAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/WorldBuffAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/WorldBuffTravelActions.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/WtsAction.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/XpGainAction.cpp"
  )

  set(TORTOISEBOTS_SOURCES
    ${TORTOISEBOTS_HOST_SRC}
    ${TORTOISEBOTS_AI_SRC}
    ${TORTOISEBOTS_STRATEGY_SRC}
    ${TORTOISEBOTS_GENERIC_SRC}
    ${TORTOISEBOTS_CLASS_CONTEXT_SRC}
    ${TORTOISEBOTS_CLASS_SRC}
    ${TORTOISEBOTS_VALUE_SRC}
    ${TORTOISEBOTS_TRIGGER_SRC}
    ${TORTOISEBOTS_ACTION_SRC})

  # Vanilla/Turtle policy. Keep the source physically available for later
  # ports, but do not let native module discovery compile it accidentally.
  foreach(TORTOISEBOTS_SOURCE_LIST
      TORTOISEBOTS_AI_SRC
      TORTOISEBOTS_STRATEGY_SRC
      TORTOISEBOTS_GENERIC_SRC
      TORTOISEBOTS_CLASS_CONTEXT_SRC
      TORTOISEBOTS_CLASS_SRC
      TORTOISEBOTS_VALUE_SRC
      TORTOISEBOTS_TRIGGER_SRC
      TORTOISEBOTS_ACTION_SRC)
    list(FILTER ${TORTOISEBOTS_SOURCE_LIST} EXCLUDE REGEX ".*[Dd]eath[Kk]night.*")
    list(FILTER ${TORTOISEBOTS_SOURCE_LIST} EXCLUDE REGEX ".*[Dd][Kk].*")
    list(FILTER ${TORTOISEBOTS_SOURCE_LIST} EXCLUDE REGEX ".*[Tt]est.*")
    list(FILTER ${TORTOISEBOTS_SOURCE_LIST} EXCLUDE REGEX ".*[Aa][Hh][Bb]ot.*")
    list(FILTER ${TORTOISEBOTS_SOURCE_LIST} EXCLUDE REGEX ".*[Ll][Ff][Gg].*")
    list(FILTER ${TORTOISEBOTS_SOURCE_LIST} EXCLUDE REGEX ".*[Gg]lyph.*")
    list(FILTER ${TORTOISEBOTS_SOURCE_LIST} EXCLUDE REGEX ".*[Vv]ehicle.*")
    list(FILTER ${TORTOISEBOTS_SOURCE_LIST} EXCLUDE REGEX ".*[Aa]rena.*")
    list(FILTER ${TORTOISEBOTS_SOURCE_LIST} EXCLUDE REGEX ".*[Kk]arazhan.*")
  endforeach()

  # This donor action is an AzerothCore/WotLK random-bot maintenance path. Its
  # talent-tree, gear-score, and teleport managers do not exist in the
  # Vanilla/Turtle module boundary; keep the source for a later native port
  # instead of compiling a false no-op replacement.
  list(FILTER TORTOISEBOTS_ACTION_SRC EXCLUDE REGEX ".*AutoMaintenanceOnLevelupAction\\.cpp$")
  # The donor fishing action targets WotLK liquid/phase/packet APIs.  Fishing
  # remains a documented follow-up port; do not register a source that cannot
  # execute on the Vanilla/Turtle host.
  list(FILTER TORTOISEBOTS_ACTION_SRC EXCLUDE REGEX ".*/FishingAction\\.cpp$")
  # InventoryAction is an AzerothCore-specific reporting helper built around
  # ItemTemplate/visitor types that are not part of the Vanilla/Turtle host.
  # Native inventory operations remain in the module's PlayerbotAI/actions.
  list(FILTER TORTOISEBOTS_ACTION_SRC EXCLUDE REGEX ".*/InventoryAction\\.cpp$")
  list(FILTER TORTOISEBOTS_ACTION_SRC EXCLUDE REGEX ".*/TellEmblemsAction\\.cpp$")
  # The old WotLK food/drink wrappers collide with the native UseItemAction
  # implementations already registered by ActionContext.
  list(FILTER TORTOISEBOTS_ACTION_SRC EXCLUDE REGEX ".*/NonCombatActions\\.cpp$")
  list(FILTER TORTOISEBOTS_ACTION_SRC EXCLUDE REGEX ".*/PetsAction\\.cpp$")
  # ArenaTeam/ArenaTeamMgr are not part of the Vanilla/Turtle host and this
  # reporting action is not registered by the native ActionContext yet.
  list(FILTER TORTOISEBOTS_ACTION_SRC EXCLUDE REGEX ".*/TellPvpStatsAction\\.cpp$")
  # TradeStatusExtendedAction is an AzerothCore packet/TradeData extension;
  # the native TradeStatusAction remains the supported Vanilla/Turtle path.
  list(FILTER TORTOISEBOTS_ACTION_SRC EXCLUDE REGEX ".*/TradeStatusExtendedAction\\.cpp$")
  # A second donor TradeValues.cpp declares the same value with a different
  # container/API. The native strategy/values/TradeValues.cpp is the supported
  # implementation and is already part of TORTOISEBOTS_VALUE_SRC.
  list(FILTER TORTOISEBOTS_ACTION_SRC EXCLUDE REGEX ".*/TradeValues\\.cpp$")
  # ValueActions.cpp is the native implementation of focus-heal target
  # commands; the separately forward-ported file defines the same action
  # under a different base class and must not be compiled alongside it.
  list(FILTER TORTOISEBOTS_ACTION_SRC EXCLUDE REGEX ".*/SetFocusHealTargetsAction\\.cpp$")

  set(TORTOISEBOTS_SOURCES
    ${TORTOISEBOTS_HOST_SRC}
    ${TORTOISEBOTS_AI_SRC}
    ${TORTOISEBOTS_STRATEGY_SRC}
    ${TORTOISEBOTS_GENERIC_SRC}
    ${TORTOISEBOTS_CLASS_CONTEXT_SRC}
    ${TORTOISEBOTS_CLASS_SRC}
    ${TORTOISEBOTS_VALUE_SRC}
    ${TORTOISEBOTS_TRIGGER_SRC}
    ${TORTOISEBOTS_ACTION_SRC})
  list(REMOVE_DUPLICATES TORTOISEBOTS_SOURCES)

  foreach(TORTOISEBOTS_SOURCE ${TORTOISEBOTS_SOURCES})
    if(NOT EXISTS "${TORTOISEBOTS_SOURCE}")
      message(FATAL_ERROR "TortoiseBots source listed but missing: ${TORTOISEBOTS_SOURCE}")
    endif()
    TW_ADD_SCRIPT("${TORTOISEBOTS_SOURCE}")
  endforeach()
endif()

if(TORTOISE_MODULE_CMAKE_PHASE STREQUAL "POST_TARGETS")
  if(DEFINED TORTOISE_CURRENT_MODULE_TARGET
     AND NOT "${TORTOISE_CURRENT_MODULE_TARGET}" STREQUAL "")
    # Static modules are compiled in their own OBJECT target by Penqle's
    # module system. This keeps the native module's definitions, include
    # paths, and PCH local even though its objects are folded into `modules`.
    set(TORTOISEBOTS_TARGET "${TORTOISE_CURRENT_MODULE_TARGET}")
  elseif(TORTOISE_CURRENT_MODULE_LINKAGE STREQUAL "static")
    set(TORTOISEBOTS_TARGET modules)
  else()
    GetModuleProjectName("${TORTOISE_CURRENT_MODULE}" TORTOISEBOTS_TARGET)
  endif()

  if(TARGET "${TORTOISEBOTS_TARGET}")
    set(TORTOISEBOTS_ROOT "${CMAKE_CURRENT_LIST_DIR}")
    target_compile_definitions("${TORTOISEBOTS_TARGET}" PRIVATE
      BUILD_PLAYERBOTS=1
      ENABLE_PLAYERBOTS=1
      MANGOSBOT_ZERO=1
      CMANGOS=1)
    target_include_directories("${TORTOISEBOTS_TARGET}" PRIVATE
      "${TORTOISEBOTS_ROOT}"
      "${TORTOISEBOTS_ROOT}/ai"
      "${TORTOISEBOTS_ROOT}/ai/playerbot"
      "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy"
      "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions"
      "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/triggers"
      "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/values"
      "${TORTOISEBOTS_ROOT}/host"
      "${TORTOISEBOTS_ROOT}/runtime"
      "${CMAKE_SOURCE_DIR}/src/game/MapNodes"
      "${CMAKE_SOURCE_DIR}/src/game/Protocol"
      "${CMAKE_SOURCE_DIR}/src/game/Objects"
      "${CMAKE_SOURCE_DIR}/src/game/Maps"
      "${CMAKE_SOURCE_DIR}/src/game/Maps/Pool"
      "${CMAKE_SOURCE_DIR}/src/game/Movement"
      "${CMAKE_SOURCE_DIR}/src/game/Movement/spline"
      "${CMAKE_SOURCE_DIR}/src/game/Transports"
      "${CMAKE_SOURCE_DIR}/src/game/vmap")
    if(COMMAND target_precompile_headers)
      target_precompile_headers("${TORTOISEBOTS_TARGET}" PRIVATE
        "${TORTOISEBOTS_ROOT}/ai/botpch.h")
    endif()
  endif()
endif()
