#pragma once

#include <cstdint>
#include <string>
#include <vector>
class Group;
class ChatHandler;
class Player;

namespace TortoiseBots {
namespace BotCommands {

// The `.bot` subcommand handlers. The dispatch table in commands/BotCommands.cpp
// is the only caller; each handler lives in its domain file (Hire.cpp,
// Commander.cpp, ...).
bool HandleAhBot(ChatHandler* handler, char const* args);
bool HandleAction(ChatHandler* handler, char const* args);
bool HandleCommander(ChatHandler* handler, char const* args);
bool HandleCommanderOrder(ChatHandler* handler, char const* args);
bool HandleTrace(ChatHandler* handler, char const* args);
bool HandleDungeonRun(ChatHandler* handler, char const* args);
uint64 BindSystemParty(Player* lead, std::vector<Player*> const& joiners, std::string& code, std::string& message);
bool IsLfgWorldBot(Player* bot);                       // IsWorldBot(bot)
bool JoinBotToLeadGroup(Player* lead, Player* member); // JoinLeadGroup(lead, member)
bool IsBotBound(Player* bot);                          // bot && CommanderBindings().FindByBot(raw) != nullptr
uint64 GroupBindingKey(uint32 groupId);                // the binding's commanderGuid, or 0
// Item-leg skip glue (spec D): the crash ledger's login cleanup, called from
// host/BotPlayerAdapter.cpp (OnLogin) and host/BotHostAdapter.cpp (first world
// tick). The rest of the glue (party read, give/take, erase) is file-local to
// commands/Commander.cpp. The header never includes the policy header.
// The crash ledger's login cleanup (BotPlayerAdapter::OnLogin): destroy to the
// recorded count and delete the rows for bots whose given items have no live
// run slot. World thread only.
void CleanupRunGivenItems(Player* player);
void CleanupRunGivenItemsTick();
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
bool HandleCc(ChatHandler* handler, char const* args);
bool HandleList(ChatHandler* handler, char const* args);
bool HandleStats(ChatHandler* handler, char const* args);
bool HandleStatus(ChatHandler* handler, char const* args);
bool HandleLease(ChatHandler* handler, char const* args);
bool HandleVersion(ChatHandler* handler, char const* args);
bool HandleFeatures(ChatHandler* handler, char const* args);
bool HandlePullback(ChatHandler* handler, char const* args);
bool HandleRole(ChatHandler* handler, char const* args);
bool HandleSummon(ChatHandler* handler, char const* args);
bool HandleMaintenance(ChatHandler* handler, char const* args);
bool HandleAutogear(ChatHandler* handler, char const* args);
bool HandleHire(ChatHandler* handler, char const* args);
bool HandlePool(ChatHandler* handler, char const* args);
bool HandleLoot(ChatHandler* handler, char const* args);
bool HandleLootLog(ChatHandler* handler, char const* args);
bool HandleRepair(ChatHandler* handler, char const* args);
bool HandleSell(ChatHandler* handler, char const* args);
bool HandleDowntime(ChatHandler* handler, char const* args);
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
