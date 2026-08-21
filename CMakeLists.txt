# TortoiseBots — optional PlayerBots module for Tortoise WoW 1.18.1 (Penqle/tortoise-wow)
# Repo root IS the module root. When testing against Penqle, this repo should appear
# as source/src/modules/TortoiseBots via a symlink or ephemeral copy (not committed).
# No FetchContent — single source of truth is this repo.
cmake_minimum_required(VERSION 3.10)

# This file is only evaluated when the core configures with -DBUILD_PLAYERBOTS=ON
# and src/CMakeLists.txt has done add_subdirectory(modules/TortoiseBots).
# It builds a static helper library that is linked into mangosd (the only
# executable that needs bot logic). The core's generic seams (SessionTransport,
# IWorldUpdateListener) remain unconditional; this module registers itself
# explicitly without weak symbols.

project(TortoiseBots)

# Allow linking to mangosd from this subdirectory (module is added after mangosd).
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

add_library(tortoise_bots STATIC
    ${TORTOISEBOTS_SOURCES}
    ${TORTOISEBOTS_HEADERS}
)

# Require C++17 (core uses 17)
set_property(TARGET tortoise_bots PROPERTY CXX_STANDARD 17)
set_property(TARGET tortoise_bots PROPERTY CXX_STANDARD_REQUIRED ON)

# Include paths — mirror src/game's include_directories so #include "World.h" etc works.
# CMAKE_SOURCE_DIR here is the core's source root when included via add_subdirectory(modules/TortoiseBots)
# from src/CMakeLists.txt, so we can reference core headers relative to it.
target_include_directories(tortoise_bots
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
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

# The module's code lives in the game/mangosd process. It needs the game and shared
# static libraries at compile/link time.
if(TARGET game)
    target_link_libraries(tortoise_bots PRIVATE game shared)
    # Ensure game headers see the module when needed (e.g. for pending-listener registration)
    # but game does NOT link tortoise_bots — the executable does. See below.
endif()

# Link into mangosd so the module's code is part of the final executable.
# Mangosd's CMakeLists uses plain (non-keyword) target_link_libraries, so we must
# also use the plain form here to avoid "plain vs keyword" CMake error.
if(TARGET mangosd)
    # Plain signature — consistent with mangosd's existing calls.
    target_link_libraries(mangosd tortoise_bots)
    # On Linux, ensure the factory static object is not discarded by --as-needed.
    # We do this via LINKER flag rather than embedding -Wl,... in link_libraries.
    if(UNIX AND NOT APPLE)
        target_link_options(mangosd PRIVATE "LINKER:--whole-archive,$<TARGET_FILE:tortoise_bots>,--no-whole-archive")
    endif()
    message(STATUS "TortoiseBots: linked into mangosd (BUILD_PLAYERBOTS=ON)")
else()
    message(WARNING "TortoiseBots: mangosd target not found — module will not be linked (check src/CMakeLists.txt ordering)")
endif()

# PCH — reuse game PCH if available, otherwise none. Keeping this minimal avoids
# coupling to the core's PCHSupport detection.
if(COMMAND target_precompile_headers)
    # No PCH for the module in MVP; game PCH is heavy and bot-specific headers
    # don't benefit. Enable later if build times need it.
endif()

# Install the example config alongside the core's configs.
install(FILES config/tortoise_bots.conf.dist DESTINATION ${CONF_DIR} OPTIONAL)

message(STATUS "TortoiseBots module configured — sources: ${TORTOISEBOTS_SOURCES}")
