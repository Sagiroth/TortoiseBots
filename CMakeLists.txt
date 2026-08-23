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
# First playable Warrior vertical slice: mature Engine plus focused real
# Strategy/Trigger/Action/Value contexts. Expansion, travel, random-bot, AH,
# raid and other class families remain physically available but are not built.
set(TORTOISEBOTS_AI_SOURCES
    ai/playerbot/PlayerbotAIAware.h
    ai/playerbot/PlayerbotAIBase.cpp
    ai/playerbot/PlayerbotAIBase.h
    ai/playerbot/BotState.h
    ai/playerbot/PlayerbotAI.cpp
    ai/playerbot/PlayerbotAI.h
    ai/playerbot/PlayerbotAIConfig.h
    ai/playerbot/AiFactory.cpp
    ai/playerbot/AiFactory.h
    ai/playerbot/ServerFacade.cpp
    ai/playerbot/ServerFacade.h
    ai/playerbot/BotLog.cpp
    ai/playerbot/BotLog.h
    ai/playerbot/ChatHelper.h
    ai/playerbot/ChatFilter.h
    ai/playerbot/PlayerbotSecurity.h
    ai/playerbot/GuidPosition.h
    ai/playerbot/WorldPosition.h
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
    ai/playerbot/strategy/Value.h
    ai/playerbot/strategy/Action.cpp
    ai/playerbot/strategy/Action.h
    ai/playerbot/strategy/Strategy.cpp
    ai/playerbot/strategy/Strategy.h
    ai/playerbot/strategy/Multiplier.cpp
    ai/playerbot/strategy/Multiplier.h
    ai/playerbot/strategy/PassiveMultiplier.cpp
    ai/playerbot/strategy/PassiveMultiplier.h
    ai/playerbot/strategy/ReactionEngine.cpp
    ai/playerbot/strategy/ReactionEngine.h
    ai/playerbot/strategy/ActionBasket.cpp
    ai/playerbot/strategy/ActionBasket.h
    ai/playerbot/strategy/generic/CombatStrategy.cpp
    ai/playerbot/strategy/generic/CombatStrategy.h
    ai/playerbot/strategy/generic/NonCombatStrategy.cpp
    ai/playerbot/strategy/generic/NonCombatStrategy.h
    ai/playerbot/strategy/generic/ClassStrategy.cpp
    ai/playerbot/strategy/generic/ClassStrategy.h
    ai/playerbot/strategy/generic/DeadStrategy.cpp
    ai/playerbot/strategy/generic/DeadStrategy.h
    ai/playerbot/strategy/generic/DpsAssistStrategy.cpp
    ai/playerbot/strategy/generic/DpsAssistStrategy.h
    ai/playerbot/strategy/generic/FollowMasterStrategy.cpp
    ai/playerbot/strategy/generic/FollowMasterStrategy.h
    ai/playerbot/strategy/generic/MeleeCombatStrategy.cpp
    ai/playerbot/strategy/generic/MeleeCombatStrategy.h
    ai/playerbot/strategy/warrior/WarriorStrategy.cpp
    ai/playerbot/strategy/warrior/WarriorStrategy.h
    ai/playerbot/strategy/warrior/ArmsWarriorStrategy.cpp
    ai/playerbot/strategy/warrior/ArmsWarriorStrategy.h
    ai/playerbot/strategy/warrior/FuryWarriorStrategy.cpp
    ai/playerbot/strategy/warrior/FuryWarriorStrategy.h
    ai/playerbot/strategy/warrior/ProtectionWarriorStrategy.cpp
    ai/playerbot/strategy/warrior/ProtectionWarriorStrategy.h
    ai/playerbot/strategy/warrior/WarriorAiObjectContext.cpp
    ai/playerbot/strategy/warrior/WarriorAiObjectContext.h
    runtime/VerticalSliceObjects.cpp
    runtime/VerticalSliceObjects.h
    runtime/VerticalPlayerbotAIConfig.cpp
    ai/botpch.cpp
)

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
