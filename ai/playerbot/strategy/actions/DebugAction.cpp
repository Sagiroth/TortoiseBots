#include "playerbot/playerbot.h"
#include "DebugAction.h"
#include "playerbot/PlayerbotAIConfig.h"
#include <playerbot/TravelNode.h>
#include "ChooseTravelTargetAction.h"
#include "playerbot/strategy/values/SharedValueContext.h"
#include "playerbot/strategy/values/FreeMoveValues.h"
#include "playerbot/strategy/actions/RpgSubActions.h"
#include "playerbot/LootObjectStack.h"
#include "GameEventMgr.h"
#include "playerbot/TravelMgr.h"
#include "playerbot/PlayerbotHelpMgr.h"
#include "Transports/Transport.h"
#include "Maps/PathFinder.h"

#include "vmap/VMapFactory.h"

#include "Maps/GridNotifiers.h"
#include "Maps/GridNotifiersImpl.h"
#include "Maps/CellImpl.h"

#include <iomanip>
#include <typeindex>
#include "Maps/MoveMap.h"

using namespace ai;
using namespace MaNGOS;

bool DebugAction::Execute(Event& event)
{
    Player* requester = event.GetOwner() ? event.GetOwner() : GetMaster();
    bool isMod = event.GetSource() == ".bot" || (event.GetOwner() && event.GetOwner()->GetSession() && event.GetOwner()->GetSession()->GetSecurity() >= SEC_MODERATOR);

    if (!requester)
    {
        requester = bot;
    }

    std::string text = event.GetParam();

    if (text == "help" || text.find("help ") == 0)
        return HandleDebugHelp(event, requester, text, isMod);
    else if (text == "avoid scan" && isMod)
        return HandleAvoidScan(event, requester, text);
    else if (text.find("avoid add") == 0 && isMod)
        return HandleAvoidAdd(event, requester, text);
    else if (text.find("mount") == 0)
        return HandleMount(event, requester, text);
    else if (text.find("unmount") == 0)
        return HandleUnmount(event, requester, text);
    else if (text.find("area") == 0 && isMod)
        return HandleArea(event, requester, text);
    else if (text == "gy" && isMod)
        return HandleGY(event, requester, text);
    else if (text == "grid" && isMod)
        return HandleGrid(event, requester, text);
    else if (text.find("values") == 0)
        return HandleValues(event, requester, text);
    else if (text == "gps" && isMod)
        return HandleGPS(event, requester, text);
    else if (text.find("setvalueuin32 ") == 0)
        return HandleSetValueUInt32(event, requester, text);
    else if (text.find("do ") == 0)
        return HandleDo(event, requester, text);
    else if (text.find("trade ") == 0)
        return HandleTrade(event, requester, text);
    else if (text.find("trade") == 0)
        return HandleTrade(event, requester, text);
    else if (text.find("mail") == 0)
        return HandleMail(event, requester, text);
    else if (text.find("poi ") == 0)
        return HandlePOI(event, requester, text);
    else if (text.find("motion") == 0 && isMod)
        return HandleMotion(event, requester, text);
    else if (text.find("transport") == 0 && isMod)
        return HandleTransport(event, requester, text);
    else if (text.find("pointontrans") == 0 && isMod)
        return HandlePointOnTrans(event, requester, text);
    else if (text.find("dotrans") == 0 && isMod)
        return HandleDoTransport(event, requester, text);
    else if (text.find("ontrans") == 0 && isMod)
        return HandleOnTrans(event, requester, text);
    else if (text.find("offtrans") == 0 && isMod)
        return HandleOffTrans(event, requester, text);
    else if (text.find("pathable") == 0 && isMod)
        return HandlePathable(event, requester, text);
    else if (text.find("randomspot") == 0 && isMod)
        return HandleRandomSpot(event, requester, text);
    else if (text.find("printmap") == 0 && isMod)
        return HandlePrintMap(event, requester, text);
    else if (text.find("corpse") == 0)
        return HandleCorpse(event, requester, text);
    else if (text.find("logouttime") == 0)
        return HandleLogoutTime(event, requester, text);
    else if (text.find("level") == 0)
        return HandleLevel(event, requester, text);
    else if (text.find("quest") == 0)
        return HandleQuest(event, requester, text);
    else if (text.find("position") == 0)
        return HandlePosition(event, requester, text);
    else if (text.find("npc") == 0)
        return HandleNPC(event, requester, text);
    else if (text.find("go ") == 0)
        return HandleGO(event, requester, text);
    else if (text.find("item ") == 0)
        return HandleItem(event, requester, text);
    else if (text.find("find ") == 0)
        return HandleFind(event, requester, text);
    else if (text.find("rpg ") == 0)
        return HandleRPG(event, requester, text);
    else if (text.find("rpgtargets") == 0)
        return HandleRPGTargets(event, requester, text);
    else if (text.find("travel ") == 0)
        return HandleTravel(event, requester, text);
    else if (text.find("print travel") == 0)
        return HandlePrintTravel(event, requester, text);
    else if (text.find("values ") == 0)
        return HandleValues(event, requester, text);
    else if (text.find("loot ") == 0)
        return HandleLoot(event, requester, text);
    else if (text.find("loot") == 0)
        return HandleLoot(event, requester, text);
    else if (text.find("drops ") == 0)
        return HandleDrops(event, requester, text);
    else if (text.find("taxi") == 0)
        return HandleTaxi(event, requester, text);
    else if (text.find("price ") == 0)
        return HandlePrice(event, requester, text);
    else if (text.find("nc") == 0)
        return HandleNC(event, requester, text);
    else if (text.find("save node") == 0 && isMod)
        return HandleSaveNode(event, requester, text);
    else if (text.find("show node") == 0 && isMod)
        return HandleShowNode(event, requester, text);
    else if (text.find("dspell ") == 0 && isMod)
        return HandleDSpell(event, requester, text);
    else if (text.find("vspell ") == 0 && isMod)
        return HandleVSpell(event, requester, text);
    else if (text.find("aspell ") == 0 && isMod)
        return HandleASpell(event, requester, text);
    else if (text.find("cspell ") == 0 && isMod)
        return HandleCSpell(event, requester, text);
    else if (text.find("fspell ") == 0 && isMod)
        return HandleFSpell(event, requester, text);
    else if (text.find("spell ") == 0 && isMod)
        return HandleSpell(event, requester, text);
    else if (text.find("tspellmap") == 0 && isMod)
        return HandleTSpellMap(event, requester, text);
    else if (text.find("uspellmap") == 0 && isMod)
        return HandleUSpellMap(event, requester, text);
    else if (text.find("dspellmap") == 0 && isMod)
        return HandleDSpellMap(event, requester, text);
    else if (text.find("vspellmap") == 0 && isMod)
        return HandleVSpellMap(event, requester, text);
    else if (text.find("ispellmap") == 0 && isMod)
        return HandleISpellMap(event, requester, text);
    else if (text.find("cspellmap") == 0 && isMod)
        return HandleCSpellMap(event, requester, text);
    else if (text.find("aspellmap") == 0 && isMod)
        return HandleASpellMap(event, requester, text);
    else if (text.find("gspellmap") == 0 && isMod)
        return HandleGSpellMap(event, requester, text);
    else if (text.find("mspellmap") == 0 && isMod)
        return HandleMSpellMap(event, requester, text);
    else if (text.find("soundmap") == 0 && isMod)
        return HandleSoundMap(event, requester, text);
    else if (text.find("sounds") == 0 && isMod)
        return HandleSounds(event, requester, text);
    else if (text.find("dsound") == 0 && isMod)
        return HandleDSound(event, requester, text);
    else if (text.find("sound") == 0 && isMod)
        return HandleSound(event, requester, text);
    else if (text.find("stuck") == 0)
        return HandleStuck(event, requester, text);
    else if (text.find("combat") == 0)
        return HandleCombat(event, requester, text);
    else if (text.find("nodes") == 0)
        return HandleNodes(event, requester, text);
    else if (text.find("activity") == 0)
        return HandleActivity(event, requester, text);
    else if (text.find("transanal") == 0)
        return HandleTransanal(event, requester, text);
    else if (text.find("updownspace") == 0 && isMod)
        return HandleUpdownspace(event, requester, text);
    else if (text.find("patharound") == 0 && isMod)
        return HandlePatharound(event, requester, text);
    else if (text.find("heightforlos") == 0 && isMod)
        return HandleHeightForLos(event, requester, text);

    // Fallback/default behavior
    std::string response = ai->HandleRemoteCommand(text);
    ai->TellPlayer(requester, response);
    return true;
}

bool DebugAction::HandleDebugHelp(Event& event, Player* requester, const std::string& text, bool isMod)
{
    std::string cmd = text;
    if (cmd.find("help ") == 0)
        cmd = cmd.substr(5);
    else
        cmd = "";

    while (cmd.size() > 0 && cmd[0] == ' ')
        cmd = cmd.substr(1);

    if (cmd.empty())
    {
        ai->TellPlayer(requester, "=== Debug Commands ===");
        ai->TellPlayer(requester, "Usage: debug help <command>");
        ai->TellPlayer(requester, "");
        ai->TellPlayer(requester, "General: position, quest, values, level, who, stats, spells");
        ai->TellPlayer(requester, "Movement: route, path, distance, teleport, zone");
        ai->TellPlayer(requester, "Info: target, movement, corpse, logouttime, taxi");
        ai->TellPlayer(requester, "Interaction: npc, go, rpg, travel, loot, trade, mail");
        ai->TellPlayer(requester, "Quest: quest list/complete/drop/add/travel");
        ai->TellPlayer(requester, "Nodes: nodes");

        if (isMod)
        {
            ai->TellPlayer(requester, "Mod Only: area, avoid, motion, transport, nc, gps");
        }

        ai->TellPlayer(requester, "");
        ai->TellPlayer(requester, "Type 'debug help <command>' for detailed help.");
        return true;
    }

    std::transform(cmd.begin(), cmd.end(), cmd.begin(), ::tolower);

    if (cmd == "position" || cmd == "pos")
    {
        ai->TellPlayer(requester, "=== debug position ===");
        ai->TellPlayer(requester, "Get position info and manipulate position-related data.");
        ai->TellPlayer(requester, "");
        ai->TellPlayer(requester, "Usage:");
        ai->TellPlayer(requester, "  debug position                    - Bot's GPS coordinates");
        ai->TellPlayer(requester, "  debug position zone [loc]          - Zone info (bot or location)");
        ai->TellPlayer(requester, "  debug position teleport [loc]     - Teleport to master or location");
        ai->TellPlayer(requester, "  debug position distance [loc]    - Distance to master or location");
        ai->TellPlayer(requester, "  debug position path <from>/<to>  - Full path with movement breakdown");
        ai->TellPlayer(requester, "  debug position path <loc> full   - Show all path points");
        ai->TellPlayer(requester, "  debug position route <from>/<to> - Route nodes with map/area");
        ai->TellPlayer(requester, "");
        ai->TellPlayer(requester, "Location formats:");
        ai->TellPlayer(requester, "  x y                    - Coordinates (uses bot's map/z)");
        ai->TellPlayer(requester, "  map x y                - 3 params");
        ai->TellPlayer(requester, "  map x y z              - 4 params");
        ai->TellPlayer(requester, "  LocationName           - Named location (case sensitive)");
        ai->TellPlayer(requester, "  from/to                - Slash separates start and end");
        ai->TellPlayer(requester, "");
        ai->TellPlayer(requester, "Examples:");
        ai->TellPlayer(requester, "  debug position");
        ai->TellPlayer(requester, "  debug position teleport Stormwind");
        ai->TellPlayer(requester, "  debug position distance Orgrimmar");
        ai->TellPlayer(requester, "  debug position path Orgrimmar/Stormwind");
        ai->TellPlayer(requester, "  debug position route Orgrimmar/Stormwind full");
    }
    else if (cmd == "quest")
    {
        ai->TellPlayer(requester, "=== debug quest ===");
        ai->TellPlayer(requester, "Manage and view quest information.");
        ai->TellPlayer(requester, "");
        ai->TellPlayer(requester, "Usage:");
        ai->TellPlayer(requester, "  debug quest                   - List all quests in bot's log");
        ai->TellPlayer(requester, "  debug quest <id>              - Show quest details");
        ai->TellPlayer(requester, "  debug quest complete <id>     - Complete a quest");
        ai->TellPlayer(requester, "  debug quest drop <id>         - Abandon a quest");
        ai->TellPlayer(requester, "  debug quest add <id>          - Add quest to log");
        ai->TellPlayer(requester, "  debug quest travel <id>       - Show quest objectives");
        ai->TellPlayer(requester, "");
        ai->TellPlayer(requester, "Examples:");
        ai->TellPlayer(requester, "  debug quest");
        ai->TellPlayer(requester, "  debug quest 1");
        ai->TellPlayer(requester, "  debug quest complete 1");
    }
    else if (cmd == "route")
    {
        ai->TellPlayer(requester, "=== debug position route ===");
        ai->TellPlayer(requester, "Show travel nodes between two locations.");
        ai->TellPlayer(requester, "");
        ai->TellPlayer(requester, "Usage: debug position route <from>/<to>");
        ai->TellPlayer(requester, "");
        ai->TellPlayer(requester, "Output:");
        ai->TellPlayer(requester, "  - List of travel nodes with names, zone, map");
        ai->TellPlayer(requester, "  - Distance between each node");
        ai->TellPlayer(requester, "  - Total route distance");
        ai->TellPlayer(requester, "");
        ai->TellPlayer(requester, "Examples:");
        ai->TellPlayer(requester, "  debug position route Orgrimmar/Stormwind");
        ai->TellPlayer(requester, "  debug position route ThunderBluff/Stormwind");
    }
    else if (cmd == "path")
    {
        ai->TellPlayer(requester, "=== debug position path ===");
        ai->TellPlayer(requester, "Show full pathfinding route with movement breakdown.");
        ai->TellPlayer(requester, "");
        ai->TellPlayer(requester, "Usage:");
        ai->TellPlayer(requester, "  debug position path <loc>              - Summary only");
        ai->TellPlayer(requester, "  debug position path <loc> full          - All points");
        ai->TellPlayer(requester, "  debug position path <from>/<to>        - Between two locations");
        ai->TellPlayer(requester, "");
        ai->TellPlayer(requester, "Output:");
        ai->TellPlayer(requester, "  - Total distance");
        ai->TellPlayer(requester, "  - Walk/Fly/Transport/Teleport breakdown");
        ai->TellPlayer(requester, "  - Map transitions");
        ai->TellPlayer(requester, "  - Next waypoint coordinates");
        ai->TellPlayer(requester, "");
        ai->TellPlayer(requester, "Node Types: 0=prepath, 1=walk, 4=transport, 5=flight, 6=teleport");
        ai->TellPlayer(requester, "");
        ai->TellPlayer(requester, "Examples:");
        ai->TellPlayer(requester, "  debug position path Stormwind");
        ai->TellPlayer(requester, "  debug position path Orgrimmar/Stormwind full");
    }
    else if (cmd == "distance" || cmd == "dist")
    {
        ai->TellPlayer(requester, "=== debug position distance ===");
        ai->TellPlayer(requester, "Calculate distance to a location.");
        ai->TellPlayer(requester, "");
        ai->TellPlayer(requester, "Usage: debug position distance [location]");
        ai->TellPlayer(requester, "  Without location: distance to master");
        ai->TellPlayer(requester, "  With location: distance to that point");
        ai->TellPlayer(requester, "");
        ai->TellPlayer(requester, "Examples:");
        ai->TellPlayer(requester, "  debug position distance");
        ai->TellPlayer(requester, "  debug position distance Orgrimmar");
    }
    else if (cmd == "teleport")
    {
        ai->TellPlayer(requester, "=== debug position teleport ===");
        ai->TellPlayer(requester, "Teleport bot to a location.");
        ai->TellPlayer(requester, "");
        ai->TellPlayer(requester, "Usage: debug position teleport [location/coords]");
        ai->TellPlayer(requester, "  Without params: teleport to master");
        ai->TellPlayer(requester, "  With location: teleport to named location");
        ai->TellPlayer(requester, "");
        ai->TellPlayer(requester, "Examples:");
        ai->TellPlayer(requester, "  debug position teleport");
        ai->TellPlayer(requester, "  debug position teleport Stormwind");
        ai->TellPlayer(requester, "  debug position teleport -8944 0.5");
    }
    else if (cmd == "zone")
    {
        ai->TellPlayer(requester, "=== debug position zone ===");
        ai->TellPlayer(requester, "Get zone information for a location.");
        ai->TellPlayer(requester, "");
        ai->TellPlayer(requester, "Usage: debug position zone [location]");
        ai->TellPlayer(requester, "  Without params: bot's current zone");
        ai->TellPlayer(requester, "  With location: zone at that location");
        ai->TellPlayer(requester, "");
        ai->TellPlayer(requester, "Examples:");
        ai->TellPlayer(requester, "  debug position zone");
        ai->TellPlayer(requester, "  debug position zone Stormwind");
    }
    else if (cmd == "values")
    {
        ai->TellPlayer(requester, "=== debug values ===");
        ai->TellPlayer(requester, "List and get bot AI values.");
        ai->TellPlayer(requester, "");
        ai->TellPlayer(requester, "Usage:");
        ai->TellPlayer(requester, "  debug values                    - List all values");
        ai->TellPlayer(requester, "  debug values <name>            - Get value by name");
        ai->TellPlayer(requester, "  debug setvalueuin32 <name> <v> - Set uint32 value");
        ai->TellPlayer(requester, "");
        ai->TellPlayer(requester, "Examples:");
        ai->TellPlayer(requester, "  debug values");
        ai->TellPlayer(requester, "  debug values last movement");
    }
    else if (cmd == "gps")
    {
        ai->TellPlayer(requester, "=== debug gps ===");
        ai->TellPlayer(requester, "Get detailed position information (mod only).");
        ai->TellPlayer(requester, "");
        ai->TellPlayer(requester, "Usage: debug gps");
        ai->TellPlayer(requester, "  Shows: X, Y, Z, Map, Area, Zone, Orientation");
    }
    else if (cmd == "level")
    {
        ai->TellPlayer(requester, "=== debug level ===");
        ai->TellPlayer(requester, "Get or set bot level.");
        ai->TellPlayer(requester, "");
        ai->TellPlayer(requester, "Usage:");
        ai->TellPlayer(requester, "  debug level          - Show current level");
        ai->TellPlayer(requester, "  debug level <n>     - Set level to n");
    }
    else if (cmd == "who")
    {
        ai->TellPlayer(requester, "=== debug who ===");
        ai->TellPlayer(requester, "Show detailed bot information.");
        ai->TellPlayer(requester, "");
        ai->TellPlayer(requester, "Usage: debug who");
        ai->TellPlayer(requester, "  Shows: Name, Level, Class, HP, Mana, Gold, Location");
    }
    else if (cmd == "stats")
    {
        ai->TellPlayer(requester, "=== debug stats ===");
        ai->TellPlayer(requester, "Show bot statistics.");
        ai->TellPlayer(requester, "");
        ai->TellPlayer(requester, "Usage: debug stats");
        ai->TellPlayer(requester, "  Shows: Gold, Bag slots, Durability, XP, Mana");
    }
    else if (cmd == "spells")
    {
        ai->TellPlayer(requester, "=== debug spells ===");
        ai->TellPlayer(requester, "List bot's known spells.");
        ai->TellPlayer(requester, "");
        ai->TellPlayer(requester, "Usage: debug spells");
    }
    else if (cmd == "target")
    {
        ai->TellPlayer(requester, "=== debug target ===");
        ai->TellPlayer(requester, "Show bot's current target.");
        ai->TellPlayer(requester, "");
        ai->TellPlayer(requester, "Usage: debug target");
        ai->TellPlayer(requester, "  Shows: Target name, GUID, type");
    }
    else if (cmd == "movement")
    {
        ai->TellPlayer(requester, "=== debug movement ===");
        ai->TellPlayer(requester, "Show last movement coordinates.");
        ai->TellPlayer(requester, "");
        ai->TellPlayer(requester, "Usage: debug movement");
    }
    else if (cmd == "mount")
    {
        ai->TellPlayer(requester, "=== debug mount ===");
        ai->TellPlayer(requester, "Mount the bot.");
        ai->TellPlayer(requester, "");
        ai->TellPlayer(requester, "Usage: debug mount");
    }
    else if (cmd == "unmount")
    {
        ai->TellPlayer(requester, "=== debug unmount ===");
        ai->TellPlayer(requester, "Dismount the bot.");
        ai->TellPlayer(requester, "");
        ai->TellPlayer(requester, "Usage: debug unmount");
    }
    else if (cmd == "who")
    {
        ai->TellPlayer(requester, "=== debug who ===");
        ai->TellPlayer(requester, "Show detailed bot information.");
        ai->TellPlayer(requester, "");
        ai->TellPlayer(requester, "Usage: debug who");
        ai->TellPlayer(requester, "  Shows: Name, Level, Class, HP, Mana, Gold, Location");
    }
    else if (cmd == "stats")
    {
        ai->TellPlayer(requester, "=== debug stats ===");
        ai->TellPlayer(requester, "Show bot statistics.");
        ai->TellPlayer(requester, "");
        ai->TellPlayer(requester, "Usage: debug stats");
        ai->TellPlayer(requester, "  Shows: Gold, Bag slots, Durability, XP, Mana");
    }
    else if (cmd == "spells")
    {
        ai->TellPlayer(requester, "=== debug spells ===");
        ai->TellPlayer(requester, "List bot's known spells.");
        ai->TellPlayer(requester, "");
        ai->TellPlayer(requester, "Usage: debug spells");
    }
    else if (cmd == "gps" && isMod)
    {
        ai->TellPlayer(requester, "=== debug gps ===");
        ai->TellPlayer(requester, "Get detailed position information (mod only).");
        ai->TellPlayer(requester, "");
        ai->TellPlayer(requester, "Usage: debug gps");
        ai->TellPlayer(requester, "  Shows: X, Y, Z, Map, Area, Zone, Orientation");
    }
    else if (cmd == "area" && isMod)
    {
        ai->TellPlayer(requester, "=== debug area ===");
        ai->TellPlayer(requester, "Area debugging (mod only).");
        ai->TellPlayer(requester, "");
        ai->TellPlayer(requester, "Usage: debug area");
    }
    else if (cmd == "avoid" && isMod)
    {
        ai->TellPlayer(requester, "=== debug avoid ===");
        ai->TellPlayer(requester, "Avoidance management (mod only).");
        ai->TellPlayer(requester, "");
        ai->TellPlayer(requester, "Usage:");
        ai->TellPlayer(requester, "  debug avoid scan");
        ai->TellPlayer(requester, "  debug avoid add <spellId>");
    }
    else if (cmd == "trade")
    {
        ai->TellPlayer(requester, "=== debug trade ===");
        ai->TellPlayer(requester, "Manage trade window with bot.");
        ai->TellPlayer(requester, "");
        ai->TellPlayer(requester, "Usage:");
        ai->TellPlayer(requester, "  debug trade       - Show trade status");
        ai->TellPlayer(requester, "  debug trade <item> - Use item in trade");
    }
    else if (cmd == "mail")
    {
        ai->TellPlayer(requester, "=== debug mail ===");
        ai->TellPlayer(requester, "Send mail to bot.");
        ai->TellPlayer(requester, "");
        ai->TellPlayer(requester, "Usage: debug mail <subject>");
    }
    else if (cmd == "taxi")
    {
        ai->TellPlayer(requester, "=== debug taxi ===");
        ai->TellPlayer(requester, "Show taxi/fly routes.");
        ai->TellPlayer(requester, "");
        ai->TellPlayer(requester, "Usage: debug taxi");
    }
    else if (cmd == "rpg")
    {
        ai->TellPlayer(requester, "=== debug rpg ===");
        ai->TellPlayer(requester, "Roleplay interaction.");
        ai->TellPlayer(requester, "");
        ai->TellPlayer(requester, "Usage: debug rpg <action>");
    }
    else if (cmd == "travel")
    {
        ai->TellPlayer(requester, "=== debug travel ===");
        ai->TellPlayer(requester, "Travel to a location.");
        ai->TellPlayer(requester, "");
        ai->TellPlayer(requester, "Usage: debug travel <location>");
    }
    else if (cmd == "loot")
    {
        ai->TellPlayer(requester, "=== debug loot ===");
        ai->TellPlayer(requester, "Loot information.");
        ai->TellPlayer(requester, "");
        ai->TellPlayer(requester, "Usage:");
        ai->TellPlayer(requester, "  debug loot");
        ai->TellPlayer(requester, "  debug loot <target>");
    }
    else if (cmd == "npc")
    {
        ai->TellPlayer(requester, "=== debug npc ===");
        ai->TellPlayer(requester, "Interact with NPC.");
        ai->TellPlayer(requester, "");
        ai->TellPlayer(requester, "Usage: debug npc <npc>");
    }
    else if (cmd == "go" || cmd == "goto")
    {
        ai->TellPlayer(requester, "=== debug go ===");
        ai->TellPlayer(requester, "Go to location or object.");
        ai->TellPlayer(requester, "");
        ai->TellPlayer(requester, "Usage:");
        ai->TellPlayer(requester, "  debug go <coords>");
        ai->TellPlayer(requester, "  debug go <gameobject>");
    }
    else if (cmd == "corpse")
    {
        ai->TellPlayer(requester, "=== debug corpse ===");
        ai->TellPlayer(requester, "Show corpse location.");
        ai->TellPlayer(requester, "");
        ai->TellPlayer(requester, "Usage: debug corpse");
    }
    else if (cmd == "nc" || cmd == "nocombat")
    {
        ai->TellPlayer(requester, "=== debug nc ===");
        ai->TellPlayer(requester, "Toggle non-combat mode.");
        ai->TellPlayer(requester, "");
        ai->TellPlayer(requester, "Usage: debug nc");
    }
    else if (cmd == "motion" && isMod)
    {
        ai->TellPlayer(requester, "=== debug motion ===");
        ai->TellPlayer(requester, "Motion controller (mod only).");
        ai->TellPlayer(requester, "");
        ai->TellPlayer(requester, "Usage: debug motion <params>");
    }
    else if (cmd == "transport" && isMod)
    {
        ai->TellPlayer(requester, "=== debug transport ===");
        ai->TellPlayer(requester, "Transport debugging (mod only).");
        ai->TellPlayer(requester, "");
        ai->TellPlayer(requester, "Usage: debug transport");
    }
    else if (cmd == "do")
    {
        ai->TellPlayer(requester, "=== debug do ===");
        ai->TellPlayer(requester, "Execute bot action directly.");
        ai->TellPlayer(requester, "");
        ai->TellPlayer(requester, "Usage: debug do <action>");
    }
    else if (cmd == "item")
    {
        ai->TellPlayer(requester, "=== debug item ===");
        ai->TellPlayer(requester, "Use or manage items.");
        ai->TellPlayer(requester, "");
        ai->TellPlayer(requester, "Usage: debug item <item>");
    }
    else if (cmd == "find")
    {
        ai->TellPlayer(requester, "=== debug find ===");
        ai->TellPlayer(requester, "Find objects or NPCs.");
        ai->TellPlayer(requester, "");
        ai->TellPlayer(requester, "Usage: debug find <name>");
    }
    else if (cmd == "drops")
    {
        ai->TellPlayer(requester, "=== debug drops ===");
        ai->TellPlayer(requester, "Show loot drops from creatures.");
        ai->TellPlayer(requester, "");
        ai->TellPlayer(requester, "Usage: debug drops <creature>");
    }
    else if (cmd == "price")
    {
        ai->TellPlayer(requester, "=== debug price ===");
        ai->TellPlayer(requester, "Show item vendor price.");
        ai->TellPlayer(requester, "");
        ai->TellPlayer(requester, "Usage: debug price <item>");
    }
    else if (cmd == "logouttime")
    {
        ai->TellPlayer(requester, "=== debug logouttime ===");
        ai->TellPlayer(requester, "Show logout timer.");
        ai->TellPlayer(requester, "");
        ai->TellPlayer(requester, "Usage: debug logouttime");
    }
    else if (cmd == "rpgtargets")
    {
        ai->TellPlayer(requester, "=== debug rpgtargets ===");
        ai->TellPlayer(requester, "Show RPG interaction targets.");
        ai->TellPlayer(requester, "");
        ai->TellPlayer(requester, "Usage: debug rpgtargets");
    }
    else if (cmd == "poi")
    {
        ai->TellPlayer(requester, "=== debug poi ===");
        ai->TellPlayer(requester, "Show point of interest.");
        ai->TellPlayer(requester, "");
        ai->TellPlayer(requester, "Usage: debug poi <poiId>");
    }
    else if (cmd == "setvalue" || cmd == "setvalueuin32")
    {
        ai->TellPlayer(requester, "=== debug setvalue ===");
        ai->TellPlayer(requester, "Set bot AI value.");
        ai->TellPlayer(requester, "");
        ai->TellPlayer(requester, "Usage: debug setvalueuin32 <name> <value>");
    }
    // MOD ONLY - avoid
    else if (cmd == "avoid scan" && isMod)
    {
        ai->TellPlayer(requester, "=== debug avoid scan ===");
        ai->TellPlayer(requester, "Scan for avoidance targets (mod only).");
        ai->TellPlayer(requester, "");
        ai->TellPlayer(requester, "Usage: debug avoid scan");
    }
    else if (cmd == "avoid add" && isMod)
    {
        ai->TellPlayer(requester, "=== debug avoid add ===");
        ai->TellPlayer(requester, "Add avoidance target (mod only).");
        ai->TellPlayer(requester, "");
        ai->TellPlayer(requester, "Usage: debug avoid add <spellId>");
    }
    // MOD ONLY - travel nodes
    else if (cmd == "save node" && isMod)
    {
        ai->TellPlayer(requester, "=== debug save node ===");
        ai->TellPlayer(requester, "Save travel node (mod only).");
        ai->TellPlayer(requester, "");
        ai->TellPlayer(requester, "Usage: debug save node");
    }
    else if (cmd == "show node" && isMod)
    {
        ai->TellPlayer(requester, "=== debug show node ===");
        ai->TellPlayer(requester, "Show travel nodes (mod only).");
        ai->TellPlayer(requester, "");
        ai->TellPlayer(requester, "Usage: debug show node");
    }
    else if (cmd == "print travel" && isMod)
    {
        ai->TellPlayer(requester, "=== debug print travel ===");
        ai->TellPlayer(requester, "Print travel info (mod only).");
        ai->TellPlayer(requester, "");
        ai->TellPlayer(requester, "Usage: debug print travel");
    }
    // MOD ONLY - transport
    else if (cmd == "pointontrans" && isMod)
    {
        ai->TellPlayer(requester, "=== debug pointontrans ===");
        ai->TellPlayer(requester, "Point on transport (mod only).");
        ai->TellPlayer(requester, "");
        ai->TellPlayer(requester, "Usage: debug pointontrans");
    }
    else if (cmd == "dotrans" && isMod)
    {
        ai->TellPlayer(requester, "=== debug dotrans ===");
        ai->TellPlayer(requester, "Do transport action (mod only).");
        ai->TellPlayer(requester, "");
        ai->TellPlayer(requester, "Usage: debug dotrans");
    }
    else if (cmd == "ontrans" && isMod)
    {
        ai->TellPlayer(requester, "=== debug ontrans ===");
        ai->TellPlayer(requester, "On transport (mod only).");
        ai->TellPlayer(requester, "");
        ai->TellPlayer(requester, "Usage: debug ontrans");
    }
    else if (cmd == "offtrans" && isMod)
    {
        ai->TellPlayer(requester, "=== debug offtrans ===");
        ai->TellPlayer(requester, "Off transport (mod only).");
        ai->TellPlayer(requester, "");
        ai->TellPlayer(requester, "Usage: debug offtrans");
    }
    // MOD ONLY - map
    else if (cmd == "pathable" && isMod)
    {
        ai->TellPlayer(requester, "=== debug pathable ===");
        ai->TellPlayer(requester, "Check if location is pathable (mod only).");
        ai->TellPlayer(requester, "");
        ai->TellPlayer(requester, "Usage: debug pathable");
    }
    else if (cmd == "randomspot" && isMod)
    {
        ai->TellPlayer(requester, "=== debug randomspot ===");
        ai->TellPlayer(requester, "Show random spot (mod only).");
        ai->TellPlayer(requester, "");
        ai->TellPlayer(requester, "Usage: debug randomspot");
    }
    else if (cmd == "printmap" && isMod)
    {
        ai->TellPlayer(requester, "=== debug printmap ===");
        ai->TellPlayer(requester, "Print map info (mod only).");
        ai->TellPlayer(requester, "");
        ai->TellPlayer(requester, "Usage: debug printmap");
    }
    // MOD ONLY - spells
    else if (cmd == "dspell" && isMod)
    {
        ai->TellPlayer(requester, "=== debug dspell ===");
        ai->TellPlayer(requester, "Debug spell (mod only).");
        ai->TellPlayer(requester, "");
        ai->TellPlayer(requester, "Usage: debug dspell <spellId>");
    }
    else if (cmd == "vspell" && isMod)
    {
        ai->TellPlayer(requester, "=== debug vspell ===");
        ai->TellPlayer(requester, "Validate spell (mod only).");
        ai->TellPlayer(requester, "");
        ai->TellPlayer(requester, "Usage: debug vspell <spellId>");
    }
    else if (cmd == "aspell" && isMod)
    {
        ai->TellPlayer(requester, "=== debug aspell ===");
        ai->TellPlayer(requester, "Add spell (mod only).");
        ai->TellPlayer(requester, "");
        ai->TellPlayer(requester, "Usage: debug aspell <spellId>");
    }
    else if (cmd == "cspell" && isMod)
    {
        ai->TellPlayer(requester, "=== debug cspell ===");
        ai->TellPlayer(requester, "Cast spell (mod only).");
        ai->TellPlayer(requester, "");
        ai->TellPlayer(requester, "Usage: debug cspell <spellId>");
    }
    else if (cmd == "fspell" && isMod)
    {
        ai->TellPlayer(requester, "=== debug fspell ===");
        ai->TellPlayer(requester, "Fake spell (mod only).");
        ai->TellPlayer(requester, "");
        ai->TellPlayer(requester, "Usage: debug fspell <spellId>");
    }
    else if (cmd == "spell" && isMod)
    {
        ai->TellPlayer(requester, "=== debug spell ===");
        ai->TellPlayer(requester, "Spell action (mod only).");
        ai->TellPlayer(requester, "");
        ai->TellPlayer(requester, "Usage: debug spell <spellId>");
    }
    // MOD ONLY - spell maps
    else if (cmd == "tspellmap" && isMod)
    {
        ai->TellPlayer(requester, "=== debug tspellmap ===");
        ai->TellPlayer(requester, "Test spell map (mod only).");
        ai->TellPlayer(requester, "");
        ai->TellPlayer(requester, "Usage: debug tspellmap");
    }
    else if (cmd == "uspellmap" && isMod)
    {
        ai->TellPlayer(requester, "=== debug uspellmap ===");
        ai->TellPlayer(requester, "Use spell map (mod only).");
        ai->TellPlayer(requester, "");
        ai->TellPlayer(requester, "Usage: debug uspellmap");
    }
    else if (cmd == "dspellmap" && isMod)
    {
        ai->TellPlayer(requester, "=== debug dspellmap ===");
        ai->TellPlayer(requester, "Debug spell map (mod only).");
        ai->TellPlayer(requester, "");
        ai->TellPlayer(requester, "Usage: debug dspellmap");
    }
    else if (cmd == "vspellmap" && isMod)
    {
        ai->TellPlayer(requester, "=== debug vspellmap ===");
        ai->TellPlayer(requester, "Validate spell map (mod only).");
        ai->TellPlayer(requester, "");
        ai->TellPlayer(requester, "Usage: debug vspellmap");
    }
    else if (cmd == "ispellmap" && isMod)
    {
        ai->TellPlayer(requester, "=== debug ispellmap ===");
        ai->TellPlayer(requester, "Info spell map (mod only).");
        ai->TellPlayer(requester, "");
        ai->TellPlayer(requester, "Usage: debug ispellmap");
    }
    else if (cmd == "cspellmap" && isMod)
    {
        ai->TellPlayer(requester, "=== debug cspellmap ===");
        ai->TellPlayer(requester, "Cast spell map (mod only).");
        ai->TellPlayer(requester, "");
        ai->TellPlayer(requester, "Usage: debug cspellmap");
    }
    else if (cmd == "aspellmap" && isMod)
    {
        ai->TellPlayer(requester, "=== debug aspellmap ===");
        ai->TellPlayer(requester, "Add spell map (mod only).");
        ai->TellPlayer(requester, "");
        ai->TellPlayer(requester, "Usage: debug aspellmap");
    }
    else if (cmd == "gspellmap" && isMod)
    {
        ai->TellPlayer(requester, "=== debug gspellmap ===");
        ai->TellPlayer(requester, "Get spell map (mod only).");
        ai->TellPlayer(requester, "");
        ai->TellPlayer(requester, "Usage: debug gspellmap");
    }
    else if (cmd == "mspellmap" && isMod)
    {
        ai->TellPlayer(requester, "=== debug mspellmap ===");
        ai->TellPlayer(requester, "Modify spell map (mod only).");
        ai->TellPlayer(requester, "");
        ai->TellPlayer(requester, "Usage: debug mspellmap");
    }
    // MOD ONLY - sounds
    else if (cmd == "soundmap" && isMod)
    {
        ai->TellPlayer(requester, "=== debug soundmap ===");
        ai->TellPlayer(requester, "Sound map (mod only).");
        ai->TellPlayer(requester, "");
        ai->TellPlayer(requester, "Usage: debug soundmap");
    }
    else if (cmd == "sounds" && isMod)
    {
        ai->TellPlayer(requester, "=== debug sounds ===");
        ai->TellPlayer(requester, "List sounds (mod only).");
        ai->TellPlayer(requester, "");
        ai->TellPlayer(requester, "Usage: debug sounds");
    }
    else if (cmd == "dsound" && isMod)
    {
        ai->TellPlayer(requester, "=== debug dsound ===");
        ai->TellPlayer(requester, "Debug sound (mod only).");
        ai->TellPlayer(requester, "");
        ai->TellPlayer(requester, "Usage: debug dsound");
    }
    else if (cmd == "sound" && isMod)
    {
        ai->TellPlayer(requester, "=== debug sound ===");
        ai->TellPlayer(requester, "Play sound (mod only).");
        ai->TellPlayer(requester, "");
        ai->TellPlayer(requester, "Usage: debug sound <soundId>");
    }
    // MOD ONLY - other
    else if (cmd == "gy" && isMod)
    {
        ai->TellPlayer(requester, "=== debug gy ===");
        ai->TellPlayer(requester, "Graveyard info (mod only).");
        ai->TellPlayer(requester, "");
        ai->TellPlayer(requester, "Usage: debug gy");
    }
    else if (cmd == "grid" && isMod)
    {
        ai->TellPlayer(requester, "=== debug grid ===");
        ai->TellPlayer(requester, "Grid info (mod only).");
        ai->TellPlayer(requester, "");
        ai->TellPlayer(requester, "Usage: debug grid");
    }
    else if (cmd == "test" && isMod)
    {
        ai->TellPlayer(requester, "=== debug test ===");
        ai->TellPlayer(requester, "Testing command (mod only).");
        ai->TellPlayer(requester, "");
        ai->TellPlayer(requester, "Usage: debug test");
    }
    else if (cmd == "nodes")
    {
        ai->TellPlayer(requester, "=== debug nodes ===");
        ai->TellPlayer(requester, "Debug travel node connection - why bots get stuck.");
        ai->TellPlayer(requester, "");
        ai->TellPlayer(requester, "Usage: debug nodes");
        ai->TellPlayer(requester, "");
        ai->TellPlayer(requester, "This command checks:");
        ai->TellPlayer(requester, "  - Nodes within 100y of bot");
        ai->TellPlayer(requester, "  - If pathfinding works to each node");
        ai->TellPlayer(requester, "  - What getNode() returns (the actual function used)");
        ai->TellPlayer(requester, "  - Why pathfinding fails if it does");
        ai->TellPlayer(requester, "");
        ai->TellPlayer(requester, "Output:");
        ai->TellPlayer(requester, "  - Step-by-step pathfinding results");
        ai->TellPlayer(requester, "  - Path type and point count");
        ai->TellPlayer(requester, "  - Distance reached vs total distance");
        ai->TellPlayer(requester, "  - Suggested fixes");
    }
    else
    {
        ai->TellPlayer(requester, "Unknown command: " + cmd);
        ai->TellPlayer(requester, "Type 'debug help' for list of commands.");
    }

    return true;
}
