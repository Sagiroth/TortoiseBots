#pragma once

class ChatHandler;

namespace TortoiseBots {
namespace BotCommands {

// The `.bot` subcommand handlers. The dispatch table in commands/BotCommands.cpp
// is the only caller; each handler lives in its domain file (Hire.cpp,
// Commander.cpp, ...).
bool HandleAhBot(ChatHandler* handler, char const* args);
bool HandleAction(ChatHandler* handler, char const* args);
bool HandleCommander(ChatHandler* handler, char const* args);
bool HandleCommanderOrder(ChatHandler* handler, char const* args);
bool HandleAdd(ChatHandler* handler, char const* args);
bool HandleRemove(ChatHandler* handler, char const* args);
bool HandleLogout(ChatHandler* handler, char const* args);
bool HandleRoster(ChatHandler* handler, char const* args);
bool HandleFollow(ChatHandler* handler, char const* args);
bool HandleInvite(ChatHandler* handler, char const* args);
bool HandleUninvite(ChatHandler* handler, char const* args);
bool HandleStay(ChatHandler* handler, char const* args);
bool HandleGuard(ChatHandler* handler, char const* args);
bool HandleFree(ChatHandler* handler, char const* args);
bool HandleReady(ChatHandler* handler, char const* args);
bool HandleAttack(ChatHandler* handler, char const* args);
bool HandleInterrupt(ChatHandler* handler, char const* args);
bool HandleFormation(ChatHandler* handler, char const* args);
bool HandleList(ChatHandler* handler, char const* args);
bool HandleStats(ChatHandler* handler, char const* args);
bool HandleStatus(ChatHandler* handler, char const* args);
bool HandleLease(ChatHandler* handler, char const* args);
bool HandleVersion(ChatHandler* handler, char const* args);
bool HandlePullback(ChatHandler* handler, char const* args);
bool HandleRole(ChatHandler* handler, char const* args);
bool HandleGearInspect(ChatHandler* handler, char const* args);
bool HandleGearBis(ChatHandler* handler, char const* args);
bool HandleSummon(ChatHandler* handler, char const* args);
bool HandleMaintenance(ChatHandler* handler, char const* args);
bool HandleAutogear(ChatHandler* handler, char const* args);
bool HandleHire(ChatHandler* handler, char const* args);
bool HandlePool(ChatHandler* handler, char const* args);
bool HandleLoot(ChatHandler* handler, char const* args);
bool HandleRepair(ChatHandler* handler, char const* args);
bool HandleSell(ChatHandler* handler, char const* args);
bool HandleRest(ChatHandler* handler, char const* args);
bool HandleRelease(ChatHandler* handler, char const* args);
bool HandleCorpseRun(ChatHandler* handler, char const* args);
bool HandleCorpseShortcut(ChatHandler* handler, char const* args);
bool HandleLearn(ChatHandler* handler, char const* args);
bool HandleTrade(ChatHandler* handler, char const* args);
bool HandleMatureCommand(ChatHandler* handler, char const* args);
bool HandleInventory(ChatHandler* handler, char const* args);
bool HandleItem(ChatHandler* handler, char const* args);
bool HandleBehavior(ChatHandler* handler, char const* args);
bool HandleTotem(ChatHandler* handler, char const* args);
bool HandleStrategy(ChatHandler* handler, char const* args);
bool HandleHelp(ChatHandler* handler, char const* args);

} // namespace BotCommands
} // namespace TortoiseBots
