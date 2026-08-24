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
      DESTINATION "${CMAKE_INSTALL_PREFIX}/modules/TortoiseBots/data/sql/World")
  endif()
  if(EXISTS "${TORTOISEBOTS_ROOT}/data/sql/char")
    install(DIRECTORY "${TORTOISEBOTS_ROOT}/data/sql/char/"
      DESTINATION "${CMAKE_INSTALL_PREFIX}/modules/TortoiseBots/data/sql/Char")
  endif()

  set(TORTOISEBOTS_HOST_SRC
    "${TORTOISEBOTS_ROOT}/host/Module.cpp"
    "${TORTOISEBOTS_ROOT}/host/BotHostAdapter.cpp"
    "${TORTOISEBOTS_ROOT}/host/BotSessionAdapter.cpp"
    "${TORTOISEBOTS_ROOT}/host/BotChatAdapter.cpp"
    "${TORTOISEBOTS_ROOT}/host/BotPacketAdapter.cpp"
    "${TORTOISEBOTS_ROOT}/host/BotPlayerAdapter.cpp"
    "${TORTOISEBOTS_ROOT}/runtime/BotManager.cpp"
    "${TORTOISEBOTS_ROOT}/runtime/BotController.cpp"
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

  file(GLOB TORTOISEBOTS_GENERIC_SRC CONFIGURE_DEPENDS
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/generic/*.cpp")

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

  file(GLOB TORTOISEBOTS_CLASS_SRC CONFIGURE_DEPENDS
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/druid/*.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/hunter/*.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/mage/*.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/paladin/*.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/priest/*.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/rogue/*.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/shaman/*.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/warlock/*.cpp"
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/warrior/*.cpp")

  file(GLOB TORTOISEBOTS_VALUE_SRC CONFIGURE_DEPENDS
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/values/*.cpp")
  file(GLOB TORTOISEBOTS_TRIGGER_SRC CONFIGURE_DEPENDS
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/triggers/*.cpp")
  file(GLOB TORTOISEBOTS_ACTION_SRC CONFIGURE_DEPENDS
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/*.cpp")

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
