// pi-lens-ignore: clang:pp_file_not_found,clang:unknown_typename,clang:undeclared_var_use,clang:use_of_undeclared_identifier,clang:unknown_type_name
//add here most rarely modified headers to speed up debug build compilation
#include "Protocol/WorldSocket.h"
#include "Common.h"

// Core game systems
#include "Maps/MapManager.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "ObjectGuid.h"
#include "Database/SQLStorages.h"
#include "Protocol/Opcodes.h"
#include "SharedDefines.h"
#include "Guild/GuildMgr.h"
#include "ObjectMgr.h"
#include "ScriptMgr.h"

// Heavy game headers (frequently used, rarely modified)
#include "Spells/Spell.h"
#include "Spells/SpellMgr.h"
#include "Spells/SpellAuras.h"
#include "WorldPacket.h"
#include "LootMgr.h"
#include "GossipDef.h"
#include "Chat/Chat.h"
#include "World.h"
#include "Objects/Unit.h"
#include "Movement/MotionMaster.h"
#include "Guild/Guild.h"
#include "Objects/Player.h"
#include "Group/Group.h"
#include "Database/DatabaseEnv.h"

// Grid system (used in many playerbot files)
#include "Maps/GridNotifiers.h"
#include "Maps/GridNotifiersImpl.h"
#include "Maps/CellImpl.h"

// Additional core headers needed before the shim (shim's inline proxies reference these)
#include "Spells/SpellEntry.h"
#include "Objects/ItemPrototype.h"
#include "Database/DBCStructure.h"
#include "Database/DBCStores.h"
#include "Group/Group.h"
#include "Guild/Guild.h"
#include "Chat/Chat.h"
#include "LootMgr.h"

// cmangos -> Penqle compatibility shim. Must come AFTER Penqle's core headers
// (so the shim's proxy methods can inline-call sSpellMgr.GetSpellEntry(...) etc.)
// and BEFORE the bot module's own headers (which reference the shim's typedefs
// like GuidSet, AreaTableEntry, GenericTransport).
#include "cmangos-compat-shim.h"

// Boost headers - removed for minimal TortoiseBots build (Penqle builder has no Boost).
// Original Shyalya botpch.h required these for some AHBot/RPG logic, but the
// foundational Engine/AiObjectContext/Strategy core does not need them.
// If a future subsystem needs Boost, install libboost-all-dev in the builder
// or add the specific header back here.
// #include <boost/algorithm/string.hpp>
// #include <boost/functional/hash.hpp>
// #include <boost/bimap.hpp>
// #include <boost/bimap/multiset_of.hpp>
// #include <boost/filesystem.hpp>

// STL headers
#include <stack>
#include <iomanip>
#include <algorithm>
#include <functional>
#include <memory>
#include <regex>
#include <numeric>

// Playerbot core - minimal for foundational checkpoint: do NOT pull full playerbot.h
// (which drags in RandomPlayerbotMgr/WorldPosition with many cmangos->Penqle
// mismatches). The foundational Engine/AiObjectContext/Strategy core does not
// need the full playerbot.h. Include only what the PCH itself needs.
// #include "playerbot/playerbot.h"

// Playerbot AI framework - minimal subset for Engine
#include "playerbot/PlayerbotAIAware.h"
#include "playerbot/BotState.h"
#include "runtime/PlayerbotAIStorage.h"
// The remaining AI framework headers are included per-TU via their own #includes,
// not via the PCH, to keep the PCH tractable for the first checkpoint.
// #include "playerbot/strategy/AiObject.h"
// #include "playerbot/strategy/Value.h"
// #include "playerbot/strategy/Action.h"
// #include "playerbot/strategy/Trigger.h"
// #include "playerbot/strategy/Strategy.h"
// #include "playerbot/strategy/NamedObjectContext.h"
// #include "playerbot/strategy/AiObjectContext.h"
