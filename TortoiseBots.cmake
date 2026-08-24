# Native Vanilla/Turtle 1.18.1 module integration for TortoiseBots.
#
# Penqle's module loader recursively collects every C/C++ file below src/.
# Keep that tree limited to the loader entrypoint and describe the actual
# PlayerBots implementation here. Expansion-only donor families are removed
# from the tree rather than hidden behind a subtraction list.

if(TORTOISE_MODULE_CMAKE_PHASE STREQUAL "DISCOVERY")
  set(TORTOISEBOTS_ROOT "${CMAKE_CURRENT_LIST_DIR}")

  # Make source selection observable. The Penqle development builder mounts
  # this checkout over the core's optional module path; a direct core build
  # must not silently fall back to an unrelated stale copy.
  set(TORTOISEBOTS_SOURCE_COMMIT "unknown")
  execute_process(
    COMMAND git -c "safe.directory=${TORTOISEBOTS_ROOT}" -C "${TORTOISEBOTS_ROOT}" rev-parse HEAD
    RESULT_VARIABLE TORTOISEBOTS_GIT_RESULT
    OUTPUT_VARIABLE TORTOISEBOTS_SOURCE_COMMIT
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET)
  if(NOT TORTOISEBOTS_GIT_RESULT EQUAL 0)
    set(TORTOISEBOTS_SOURCE_COMMIT "unknown")
  endif()
  message(STATUS "TortoiseBots source: ${TORTOISEBOTS_ROOT} commit ${TORTOISEBOTS_SOURCE_COMMIT}")

  # PlayerbotAIConfig reads its mature configuration beside mangosd.conf.
  set(TORTOISEBOTS_AI_CONFIG "${CMAKE_CURRENT_BINARY_DIR}/aiplayerbot.conf")
  configure_file(
    "${TORTOISEBOTS_ROOT}/ai/playerbot/aiplayerbot.conf.dist.in"
    "${TORTOISEBOTS_AI_CONFIG}"
    COPYONLY)
  install(FILES "${TORTOISEBOTS_AI_CONFIG}" DESTINATION "${CONF_DIR}")

  configure_file(
    "${TORTOISEBOTS_ROOT}/conf/tortoise_bots.conf.dist"
    "${CMAKE_CURRENT_BINARY_DIR}/tortoise_bots.conf"
    COPYONLY)
  CopyModuleConfig("${CMAKE_CURRENT_BINARY_DIR}/tortoise_bots.conf")

  # Optional module migrations are installed only with the module. The core
  # AutoUpdater applies them on startup without making bots a core dependency.
  # AutoUpdater's module contract is case-sensitive on Linux. Penqle's
  # effective mangosd.conf.dist names these configured folders `world` and
  # `character`; the uppercase names in AutoUpdater.cpp are only fallback
  # defaults when those settings are absent. Keep the module aligned with the
  # shipped core configuration so a fresh install cannot silently skip SQL.
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
    "${TORTOISEBOTS_ROOT}/commands/BotCommands.cpp")

  # These are the module-owned runtime and mature AI foundations. The donor
  # manager/login sources are intentionally absent from this list and are
  # removed from the physical tree as part of the Vanilla/Turtle cleanup.
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
    "${TORTOISEBOTS_ROOT}/ai/botpch.cpp")

  set(TORTOISEBOTS_STRATEGY_SRC)
  file(GLOB TORTOISEBOTS_STRATEGY_SRC CONFIGURE_DEPENDS
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/*.cpp")

  # The source tree contains only the nine Vanilla class directories after
  # the cleanup. Keep the directory list explicit so a future donor family
  # cannot enter the native module by accident.
  set(TORTOISEBOTS_CLASS_DIRS
    druid hunter mage paladin priest rogue shaman warlock warrior)

  set(TORTOISEBOTS_GENERIC_SRC)
  file(GLOB TORTOISEBOTS_GENERIC_SRC CONFIGURE_DEPENDS
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/generic/*.cpp")

  set(TORTOISEBOTS_CLASS_SRC)
  foreach(TORTOISEBOTS_CLASS_DIR IN LISTS TORTOISEBOTS_CLASS_DIRS)
    file(GLOB TORTOISEBOTS_CLASS_FILES CONFIGURE_DEPENDS
      "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/${TORTOISEBOTS_CLASS_DIR}/*.cpp")
    list(APPEND TORTOISEBOTS_CLASS_SRC ${TORTOISEBOTS_CLASS_FILES})
  endforeach()

  set(TORTOISEBOTS_VALUE_SRC)
  file(GLOB TORTOISEBOTS_VALUE_SRC CONFIGURE_DEPENDS
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/values/*.cpp")

  set(TORTOISEBOTS_TRIGGER_SRC)
  file(GLOB TORTOISEBOTS_TRIGGER_SRC CONFIGURE_DEPENDS
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/triggers/*.cpp")

  set(TORTOISEBOTS_ACTION_SRC)
  file(GLOB TORTOISEBOTS_ACTION_SRC CONFIGURE_DEPENDS
    "${TORTOISEBOTS_ROOT}/ai/playerbot/strategy/actions/*.cpp")

  set(TORTOISEBOTS_SOURCES
    ${TORTOISEBOTS_HOST_SRC}
    ${TORTOISEBOTS_AI_SRC}
    ${TORTOISEBOTS_STRATEGY_SRC}
    ${TORTOISEBOTS_GENERIC_SRC}
    ${TORTOISEBOTS_CLASS_SRC}
    ${TORTOISEBOTS_VALUE_SRC}
    ${TORTOISEBOTS_TRIGGER_SRC}
    ${TORTOISEBOTS_ACTION_SRC})
  list(REMOVE_DUPLICATES TORTOISEBOTS_SOURCES)

  foreach(TORTOISEBOTS_SOURCE IN LISTS TORTOISEBOTS_SOURCES)
    if(NOT EXISTS "${TORTOISEBOTS_SOURCE}")
      message(FATAL_ERROR "TortoiseBots source listed but missing: ${TORTOISEBOTS_SOURCE}")
    endif()

    # Keep the positive source graph mechanically hostile to donor expansion
    # families. A future file with one of these names must be reviewed before
    # it can enter the module through a directory glob.
    string(TOUPPER "${TORTOISEBOTS_SOURCE}" TORTOISEBOTS_SOURCE_UPPER)
    if(TORTOISEBOTS_SOURCE_UPPER MATCHES "DEATHKNIGHT|GLYPH|VEHICLE|KARAZHAN|ARENA|RTSC|BOSSAURA")
      message(FATAL_ERROR "Expansion/test family is not allowed in TortoiseBots source graph: ${TORTOISEBOTS_SOURCE}")
    endif()

    TW_ADD_SCRIPT("${TORTOISEBOTS_SOURCE}")
  endforeach()
endif()

if(TORTOISE_MODULE_CMAKE_PHASE STREQUAL "POST_TARGETS")
  if(DEFINED TORTOISE_CURRENT_MODULE_TARGET
     AND NOT "${TORTOISE_CURRENT_MODULE_TARGET}" STREQUAL "")
    set(TORTOISEBOTS_TARGET "${TORTOISE_CURRENT_MODULE_TARGET}")
  elseif(TORTOISE_CURRENT_MODULE_LINKAGE STREQUAL "static")
    set(TORTOISEBOTS_TARGET modules)
  else()
    GetModuleProjectName("${TORTOISE_CURRENT_MODULE}" TORTOISEBOTS_TARGET)
  endif()

  if(TARGET "${TORTOISEBOTS_TARGET}")
    set(TORTOISEBOTS_ROOT "${CMAKE_CURRENT_LIST_DIR}")
    # BUILD_PLAYERBOTS is the core's legacy-vendor escape hatch. Native module
    # selection is controlled by MODULE_TORTOISEBOTS, so do not force the
    # legacy option on from inside this module.
    target_compile_definitions("${TORTOISEBOTS_TARGET}" PRIVATE
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
