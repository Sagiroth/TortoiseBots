# TortoiseBots — optional PlayerBots module for Tortoise WoW 1.18.1 (Penqle/tortoise-wow)
cmake_minimum_required(VERSION 3.10)
project(TortoiseBots)
if(POLICY CMP0079)
  cmake_policy(SET CMP0079 NEW)
endif()
set(TORTOISEBOTS_SOURCES
    host/Module.cpp
    host/BotHostAdapter.cpp
    host/BotSessionAdapter.cpp
    host/BotChatAdapter.cpp
    runtime/BotManager.cpp
    runtime/BotController.cpp
    runtime/PlayerbotAIStorage.cpp
    runtime/PlayerbotAIAdapter.cpp
    behavior/Movement.cpp
    commands/BotCommands.cpp
)
set(TORTOISEBOTS_HEADERS
    host/Module.h
    host/BotHostAdapter.h
    host/BotSessionAdapter.h
    host/BotChatAdapter.h
    runtime/BotManager.h
    runtime/BotController.h
    behavior/Config.h
    behavior/Movement.h
    commands/BotCommands.h
)
# === Foundational PlayerBots runtime - REAL code, not stubs ===
# Shyalya Tortoise baseline (playerbots-integration-gh @ 1f9497e) is the
# Vanilla/Tortoise translation reference; mod-playerbots (5397110) is the
# modernization donor. This batch brings the full Engine/AiObjectContext/
# Strategy/Trigger/Action/Value/ReactionEngine/AiFactory/shared-contexts
# stack plus the generic Base and class-factory foundation.
set(BOT_CORE_SRC
    ai/playerbot/PlayerbotAIAware.h
    ai/playerbot/PlayerbotAIBase.cpp
    ai/playerbot/PlayerbotAIBase.h
    ai/playerbot/BotState.h
    ai/playerbot/PlayerbotAI.cpp
    ai/playerbot/PlayerbotAI.h
    ai/playerbot/PlayerbotAIConfig.cpp
    ai/playerbot/PlayerbotAIConfig.h
    ai/playerbot/AiFactory.cpp
    ai/playerbot/AiFactory.h
    ai/playerbot/ServerFacade.cpp
    ai/playerbot/ServerFacade.h
    ai/playerbot/PlayerbotSecurity.cpp
    ai/playerbot/PlayerbotSecurity.h
    ai/playerbot/BotLog.cpp
    ai/playerbot/BotLog.h
    ai/playerbot/PerformanceMonitor.cpp
    ai/playerbot/PerformanceMonitor.h
    ai/playerbot/ChatHelper.cpp
    ai/playerbot/ChatHelper.h
    ai/playerbot/WorldPosition.cpp
    ai/playerbot/WorldPosition.h
    ai/playerbot/GuidPosition.cpp
    ai/playerbot/GuidPosition.h
    ai/playerbot/TravelMgr.cpp
    ai/playerbot/TravelMgr.h
)
set(BOT_STRATEGY_SRC
    ai/playerbot/strategy/Engine.cpp
    ai/playerbot/strategy/Engine.h
    ai/playerbot/strategy/AiObjectContext.cpp
    ai/playerbot/strategy/AiObjectContext.h
    ai/playerbot/strategy/AiObject.cpp
    ai/playerbot/strategy/AiObject.h
    ai/playerbot/strategy/Event.cpp
    ai/playerbot/strategy/Event.h
    ai/playerbot/strategy/Queue.cpp
    ai/playerbot/strategy/Queue.h
    ai/playerbot/strategy/Trigger.cpp
    ai/playerbot/strategy/Trigger.h
    ai/playerbot/strategy/Value.cpp
    ai/playerbot/strategy/Value.h
    ai/playerbot/strategy/Action.cpp
    ai/playerbot/strategy/Action.h
    ai/playerbot/strategy/Strategy.cpp
    ai/playerbot/strategy/Strategy.h
    ai/playerbot/strategy/NamedObjectContext.h
    ai/playerbot/strategy/Multiplier.cpp
    ai/playerbot/strategy/Multiplier.h
    ai/playerbot/strategy/PassiveMultiplier.cpp
    ai/playerbot/strategy/PassiveMultiplier.h
    ai/playerbot/strategy/ReactionEngine.cpp
    ai/playerbot/strategy/ReactionEngine.h
    ai/playerbot/strategy/CustomStrategy.cpp
    ai/playerbot/strategy/CustomStrategy.h
    ai/playerbot/strategy/StrategyContext.h
    ai/playerbot/strategy/ActionBasket.cpp
    ai/playerbot/strategy/ActionBasket.h
    ai/playerbot/strategy/ExternalEventHelper.h
)
# Shared contexts - the glue that makes Strategy/Trigger/Action/Value discoverable
# In Shyalya's Tortoise tree these are header-only (no .cpp), so we glob for existing files
file(GLOB BOT_CONTEXT_SRC
    ai/playerbot/strategy/StrategyContext.h
    ai/playerbot/strategy/actions/ActionContext.h
    ai/playerbot/strategy/triggers/TriggerContext.h
    ai/playerbot/strategy/values/ValueContext.h
    ai/playerbot/strategy/actions/ChatActionContext.h
    ai/playerbot/strategy/triggers/ChatTriggerContext.h
)
# Generic Base behavior - shared strategies that all bots use (follow, combat, etc)
# We bring the full generic folder from Shyalya (Tortoise-translated) and will
# layer mod-playerbots modernizations on top. For this batch we include the
# Shyalya generic set; WotLK-only generic (e.g., deathknight) is excluded via filter.
file(GLOB BOT_GENERIC_SRC
    ai/playerbot/strategy/generic/*.cpp
    ai/playerbot/strategy/generic/*.h
)
# Class integration - AiObjectContexts for Vanilla classes (DK excluded)
file(GLOB BOT_CLASS_CONTEXT_SRC
    ai/playerbot/strategy/druid/DruidAiObjectContext.cpp
    ai/playerbot/strategy/druid/DruidAiObjectContext.h
    ai/playerbot/strategy/hunter/HunterAiObjectContext.cpp
    ai/playerbot/strategy/hunter/HunterAiObjectContext.h
    ai/playerbot/strategy/mage/MageAiObjectContext.cpp
    ai/playerbot/strategy/mage/MageAiObjectContext.h
    ai/playerbot/strategy/paladin/PaladinAiObjectContext.cpp
    ai/playerbot/strategy/paladin/PaladinAiObjectContext.h
    ai/playerbot/strategy/priest/PriestAiObjectContext.cpp
    ai/playerbot/strategy/priest/PriestAiObjectContext.h
    ai/playerbot/strategy/rogue/RogueAiObjectContext.cpp
    ai/playerbot/strategy/rogue/RogueAiObjectContext.h
    ai/playerbot/strategy/shaman/ShamanAiObjectContext.cpp
    ai/playerbot/strategy/shaman/ShamanAiObjectContext.h
    ai/playerbot/strategy/warlock/WarlockAiObjectContext.cpp
    ai/playerbot/strategy/warlock/WarlockAiObjectContext.h
    ai/playerbot/strategy/warrior/WarriorAiObjectContext.cpp
    ai/playerbot/strategy/warrior/WarriorAiObjectContext.h
)
# For this checkpoint we keep actions/values/triggers/class-strategies out of the
# default build to avoid pulling the full 800-file dep tree. They are present in
# the tree (ai/playerbot/strategy/{actions,values,triggers} and class folders)
# and will be enabled in the next batch after the core Engine/Context is stable.
# This keeps the first stabilization build tractable while still having the real
# foundational code present.

list(FILTER BOT_GENERIC_SRC EXCLUDE REGEX ".*Test.*")
list(FILTER BOT_CORE_SRC EXCLUDE REGEX ".*Test.*")

# Value/Trigger/Action - real modern families (mod-playerbots donor, Shyalya Tortoise translation)
# These are header-heavy and bring the full Strategy/Trigger/Action/Value engine.
# For this checkpoint we include the Value/Trigger/Action *contexts* and the
# concrete Value/Trigger/Action implementations as a coherent batch.
file(GLOB BOT_VALUE_SRC
    ai/playerbot/strategy/values/*.cpp
    ai/playerbot/strategy/values/*.h
)
file(GLOB BOT_TRIGGER_SRC
    ai/playerbot/strategy/triggers/*.cpp
    ai/playerbot/strategy/triggers/*.h
)
file(GLOB BOT_ACTION_SRC
    ai/playerbot/strategy/actions/*.cpp
    ai/playerbot/strategy/actions/*.h
)
list(FILTER BOT_VALUE_SRC EXCLUDE REGEX ".*Test.*")
list(FILTER BOT_TRIGGER_SRC EXCLUDE REGEX ".*Test.*")
list(FILTER BOT_ACTION_SRC EXCLUDE REGEX ".*Test.*")

set(TORTOISEBOTS_AI_SOURCES
    ${BOT_CORE_SRC}
    ${BOT_STRATEGY_SRC}
    ${BOT_CONTEXT_SRC}
    ${BOT_CLASS_CONTEXT_SRC}
    ${BOT_VALUE_SRC}
    ${BOT_TRIGGER_SRC}
    ${BOT_ACTION_SRC}
    # Travel/WorldPosition/GuidPosition - real Tortoise-translated code + modern TravelMgr
    ai/playerbot/TravelMgr.cpp
    ai/playerbot/TravelMgr.h
    ai/playerbot/TravelNode.cpp
    ai/playerbot/TravelNode.h
    ai/playerbot/WorldPosition.cpp
    ai/playerbot/WorldPosition.h
    ai/playerbot/GuidPosition.cpp
    ai/playerbot/GuidPosition.h
    # Module-local AI registry (replaces Player::GetPlayerbotAI host coupling)
    runtime/PlayerbotAIStorage.cpp
    runtime/PlayerbotAIStorage.h
    ai/botpch.cpp
)
# For this checkpoint we still keep the bulk generic/class actions out of the default build
# to avoid the 800-file dep tree; they are present in ai/ and will be enabled in the
# next batch after Value/Trigger/Action + Travel/WorldPosition are stable.
# file(GLOB BOT_GENERIC_SRC ...) and file(GLOB BOT_CLASS_CONTEXT_SRC ...) are
# defined above and will be added in the next batch.
# Exclude WotLK-only and test scaffolding
list(FILTER TORTOISEBOTS_AI_SOURCES EXCLUDE REGEX ".*deathknight.*")
list(FILTER TORTOISEBOTS_AI_SOURCES EXCLUDE REGEX ".*DeathKnight.*")
list(FILTER TORTOISEBOTS_AI_SOURCES EXCLUDE REGEX ".*DK.*")
list(FILTER TORTOISEBOTS_AI_SOURCES EXCLUDE REGEX ".*Test.*")
list(FILTER TORTOISEBOTS_AI_SOURCES EXCLUDE REGEX ".*ahbot.*")
list(FILTER TORTOISEBOTS_AI_SOURCES EXCLUDE REGEX ".*AhBot.*")

add_library(tortoise_bots STATIC
    ${TORTOISEBOTS_SOURCES}
    ${TORTOISEBOTS_HEADERS}
    ${TORTOISEBOTS_AI_SOURCES}
)
set_property(TARGET tortoise_bots PROPERTY CXX_STANDARD 17)
set_property(TARGET tortoise_bots PROPERTY CXX_STANDARD_REQUIRED ON)
target_compile_definitions(tortoise_bots PRIVATE MANGOSBOT_ZERO=1 BUILD_PLAYERBOTS=1 CMANGOS=1 ENABLE_PLAYERBOTS=1)
target_include_directories(tortoise_bots
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/ai
        ${CMAKE_CURRENT_SOURCE_DIR}/ai/playerbot
        ${CMAKE_CURRENT_SOURCE_DIR}/ai/playerbot/strategy
        ${CMAKE_CURRENT_SOURCE_DIR}/ai/playerbot/strategy/actions
        ${CMAKE_CURRENT_SOURCE_DIR}/ai/playerbot/strategy/values
        ${CMAKE_CURRENT_SOURCE_DIR}/ai/playerbot/strategy/triggers
        ${CMAKE_CURRENT_SOURCE_DIR}/host
        ${CMAKE_CURRENT_SOURCE_DIR}/runtime
        ${CMAKE_SOURCE_DIR}/src/game
        ${CMAKE_SOURCE_DIR}/src/game/AI
        ${CMAKE_SOURCE_DIR}/src/game/Anticheat
        ${CMAKE_SOURCE_DIR}/src/game/AuctionHouse
        ${CMAKE_SOURCE_DIR}/src/game/Battlegrounds
        ${CMAKE_SOURCE_DIR}/src/game/Chat
        ${CMAKE_SOURCE_DIR}/src/game/Commands
        ${CMAKE_SOURCE_DIR}/src/game/Database
        ${CMAKE_SOURCE_DIR}/src/game/Group
        ${CMAKE_SOURCE_DIR}/src/game/Guild
        ${CMAKE_SOURCE_DIR}/src/game/Handlers
        ${CMAKE_SOURCE_DIR}/src/game/LFG
        ${CMAKE_SOURCE_DIR}/src/game/Mail
        ${CMAKE_SOURCE_DIR}/src/game/MapNodes
        ${CMAKE_SOURCE_DIR}/src/game/Maps
        ${CMAKE_SOURCE_DIR}/src/game/Maps/Pool
        ${CMAKE_SOURCE_DIR}/src/game/Movement
        ${CMAKE_SOURCE_DIR}/src/game/Movement/spline
        ${CMAKE_SOURCE_DIR}/src/game/Objects
        ${CMAKE_SOURCE_DIR}/src/game/OutdoorPvP
        ${CMAKE_SOURCE_DIR}/src/game/Protocol
        ${CMAKE_SOURCE_DIR}/src/game/Spells
        ${CMAKE_SOURCE_DIR}/src/game/Threat
        ${CMAKE_SOURCE_DIR}/src/game/Transports
        ${CMAKE_SOURCE_DIR}/src/game/vmap
        ${CMAKE_SOURCE_DIR}/src/shared
        ${CMAKE_SOURCE_DIR}/src/framework
        ${CMAKE_SOURCE_DIR}/src/framework/Network
        ${CMAKE_SOURCE_DIR}/dep/include
        ${CMAKE_SOURCE_DIR}/dep/include/g3dlite
        ${CMAKE_SOURCE_DIR}/dep/recastnavigation
        ${CMAKE_SOURCE_DIR}/dep/recastnavigation/Detour
        ${CMAKE_SOURCE_DIR}/dep/libdeflate
        ${CMAKE_BINARY_DIR}
        ${CMAKE_BINARY_DIR}/src/shared
        ${MYSQL_INCLUDE_DIR}
        ${ACE_INCLUDE_DIR}
        ${OPENSSL_INCLUDE_DIR}
        ${TBB_INCLUDE_DIRS}
)
if(TARGET game)
    target_link_libraries(tortoise_bots PRIVATE game shared)
endif()
if(TARGET mangosd)
    target_link_libraries(mangosd tortoise_bots)
    if(UNIX AND NOT APPLE)
        target_link_options(mangosd PRIVATE "LINKER:--whole-archive,$<TARGET_FILE:tortoise_bots>,--no-whole-archive")
    endif()
    message(STATUS "TortoiseBots: linked into mangosd (BUILD_PLAYERBOTS=ON)")
else()
    message(WARNING "TortoiseBots: mangosd target not found — module will not be linked (check src/CMakeLists.txt ordering)")
endif()
if(COMMAND target_precompile_headers)
    target_precompile_headers(tortoise_bots PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/ai/botpch.h")
endif()
if(DEFINED CONF_DIR)
    install(FILES config/tortoise_bots.conf.dist DESTINATION ${CONF_DIR} OPTIONAL)
endif()
message(STATUS "TortoiseBots module configured — sources: ${TORTOISEBOTS_SOURCES}")
