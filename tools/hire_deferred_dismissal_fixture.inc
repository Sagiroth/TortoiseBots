#include <cassert>
#include <cstdint>
#include <ctime>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <map>
#include <list>
#include <cstdio>
#define private public
#include "runtime/HireLifecycle.h"
#undef private

struct Session { bool headless = false; bool IsHeadless() const { return headless; } };
class Group;
class Player {
public:
 ObjectGuid guid;
 Group* group = nullptr;
 Group* original = nullptr;
 bool inWorld = true;
 Session session;
 Group* GetGroup() { return group; }
 Group* GetOriginalGroup() { return original; }
 bool IsInWorld() const { return inWorld; }
 Session* GetSession() { return &session; }
 bool IsInSameRaidWith(Player* p) { return p == this || (group && group == p->group); }
};
std::map<unsigned, Player> players;
struct Accessor {
 Player* FindPlayer(ObjectGuid id) {
  auto it = players.find(id.GetCounter());
  return it == players.end() ? nullptr : &it->second;
 }
} sObjectAccessor;
struct Logger { template<class... T> void outError(T...) {} } sLog;
#define TB_LOG_BASIC(...) do {} while (0)
using TortoiseBots::HireLifecycle;
using TortoiseBots::HireCommandState;
HireLifecycle lifecycle;
unsigned callbackDepth = 0, stopped = 0;
Group* bgRaid = nullptr;
ObjectGuid enqueueOnLogout;
class Group {
public:
 bool bg = false;
 std::list<ObjectGuid> members;
 bool isBGGroup() const { return bg; }
 bool IsMember(ObjectGuid id) const {
  for (auto const& member : members) if (member == id) return true;
  return false;
 }
 unsigned RemoveMember(ObjectGuid id, unsigned) {
  if (members.size() > (bg ? 1u : 2u)) {
   members.remove(id);
   if (auto* player = sObjectAccessor.FindPlayer(id)) player->group = nullptr;
   ++callbackDepth;
   lifecycle.OnGroupMemberRemoved(this, id);
   --callbackDepth;
  } else Disband();
  return members.size();
 }
 void Disband() {
  // Core calls module hooks BEFORE traversing and clearing member slots.
  ++callbackDepth;
  lifecycle.OnGroupDisband(this);
  --callbackDepth;
  for (auto const& id : members)
   if (auto* player = sObjectAccessor.FindPlayer(id)) player->group = nullptr;
  members.clear();
 }
};
namespace TortoiseBots {
struct BotActivityLeaseManager {
 static BotActivityLeaseManager& Instance() { static BotActivityLeaseManager value; return value; }
 void ReleaseMaster(unsigned) {}
};
struct BotManager {
 static BotManager& Instance() { static BotManager value; return value; }
 void ClearBotMaster(ObjectGuid) {}
 void RemoveBot(ObjectGuid id, bool) {
  // Logout -> LeaveBattleground -> RemoveMember -> delete empty BG raid.
  // This assertion fails BEFORE the old reentrant delete causes a UAF.
  assert(callbackDepth == 0 && "logout must wait for core group callbacks to unwind");
  if (Group* group = bgRaid) {
   if (!group->RemoveMember(id, 0)) { bgRaid = nullptr; delete group; }
  }
  players.erase(id.GetCounter());
  ++stopped;
#if HAS_QUEUE
  if (!enqueueOnLogout.IsEmpty()) {
   auto record = lifecycle.m_hired.at(enqueueOnLogout.GetCounter());
   lifecycle.QueueDismissal(record, HireLifecycle::DismissalReason::Removed);
   enqueueOnLogout = {};
  }
#endif
 }
};
bool HireLifecycle::LedgerRow(ObjectGuid, uint32_t& id) const { id = 1; return true; }
void HireLifecycle::QueueDeletion(uint32_t, uint32_t, char const*) {}
// PRODUCTION_METHODS
}
void drain() {
#if HAS_QUEUE
 lifecycle.DrainDismissals();
#endif
}
void reset() {
 assert(!bgRaid);
 players.clear();
 lifecycle.m_hired.clear();
#if HAS_QUEUE
 lifecycle.m_pendingDismissals.clear();
#endif
 stopped = 0;
}
void hire(unsigned id, Group* group, unsigned master = 1000) {
 Player player;
 player.guid = {id}; player.group = group; player.session.headless = true;
 players[id] = player;
 if (group) group->members.push_back({id});
 HireLifecycle::HiredRecord record;
 record.botGuid = {id}; record.masterGuid = {master}; record.everGrouped = true;
 lifecycle.m_hired[id] = record;
}
void mass() {
 for (unsigned count : {2u, 9u, 38u}) {
  reset(); bgRaid = new Group; bgRaid->bg = true;
  for (unsigned id = 1; id <= count; ++id) hire(id, bgRaid);
  // Actual grace sweep owns record copies; its explicit RemoveMember is
  // followed by LogoutPlayer's second BG removal at the final two members.
  for (unsigned id = count; id; --id) {
   if (lifecycle.m_hired.count(id)) {
    auto record = lifecycle.m_hired.at(id);
    lifecycle.Dismiss(record, "grace expired");
   }
  }
  drain();
  assert(!bgRaid && players.empty() && lifecycle.m_hired.empty() && stopped == count);
  // Ordinary-party disband must also defer, even without a BG raid.
  reset(); Group party;
  for (unsigned id = 1; id <= count; ++id) hire(id, &party);
  party.Disband();
  assert(stopped == 0 && lifecycle.m_hired.size() == count);
  drain(); drain();
  assert(stopped == count && lifecycle.m_hired.empty());
 }
}
void battleground() {
 reset(); Group party, other, bg; bg.bg = true;
 players[1000].group = &party; // ordinary game session, not headless
 hire(1, &party);
 auto const record = lifecycle.m_hired.at(1);
 auto& master = players.at(1000); auto& bot = players.at(1);
 assert(!lifecycle.MasterLeftGroup(record));
 master.original = &party; master.group = &bg;
 assert(!lifecycle.MasterLeftGroup(record)); // master enters first
 bot.original = &party; bot.group = &bg;
 assert(!lifecycle.MasterLeftGroup(record));
 master.group = &party; master.original = nullptr;
 assert(!lifecycle.MasterLeftGroup(record)); // bot enters first / master leaves first
 bot.group = &party; bot.original = nullptr;
 assert(!lifecycle.MasterLeftGroup(record));
 master.group = &bg; master.original = &other;
 bot.group = &bg; bot.original = &party;
 assert(lifecycle.MasterLeftGroup(record)); // same BG is NOT same hire party
 bg.members.push_back({1});
 lifecycle.OnGroupMemberRemoved(&bg, {1});
 lifecycle.OnGroupDisband(&bg); drain();
 assert(stopped == 0 && lifecycle.m_hired.size() == 1);
 master.group = nullptr; master.original = nullptr;
 assert(lifecycle.MasterLeftGroup(record));
 bot.inWorld = false;
 assert(!lifecycle.MasterLeftGroup(record));
 bot.inWorld = true;
 auto offline = record; offline.masterOfflineSince = 1;
 assert(!lifecycle.MasterLeftGroup(offline));
 auto bound = record; bound.commandState = HireCommandState::Bound;
 assert(!lifecycle.MasterLeftGroup(bound));
}
void queue() {
 // The real removal hook must defer BOTH a hire kick and a master leaving
 // a surviving party. Neither callback may stop a session on its stack.
 reset(); Group removalParty;
 players[1000].group = &removalParty;
 for (unsigned id = 1; id <= 4; ++id) hire(id, &removalParty);
 removalParty.RemoveMember({1}, 0);
 assert(stopped == 0);
 drain(); assert(stopped == 1);
 players[1000].group = nullptr;
 ++callbackDepth;
 lifecycle.OnGroupMemberRemoved(&removalParty, {1000});
 --callbackDepth;
 assert(stopped == 1);
 drain(); drain();
 assert(stopped == 4 && lifecycle.m_hired.empty());
#if HAS_QUEUE
 using Reason = HireLifecycle::DismissalReason;
 reset(); Group party;
 players[1000].group = &party;
 for (unsigned id = 1; id <= 7; ++id) hire(id, &party);
 lifecycle.QueueDismissal(lifecycle.m_hired.at(1), Reason::Disbanded);
 lifecycle.QueueDismissal(lifecycle.m_hired.at(1), Reason::Removed);
 lifecycle.QueueDismissal(lifecycle.m_hired.at(1), Reason::Disbanded);
 lifecycle.m_hired.at(1).masterOfflineSince = 1; // explicit kick still ends hire
 lifecycle.QueueDismissal(lifecycle.m_hired.at(2), Reason::Disbanded);
 lifecycle.m_hired.at(2).masterOfflineSince = 1; // disconnect grace still owns it
 lifecycle.QueueDismissal(lifecycle.m_hired.at(3), Reason::Removed);
 lifecycle.m_hired.at(3).commandState = HireCommandState::Transition;
 lifecycle.QueueDismissal(lifecycle.m_hired.at(4), Reason::Removed);
 lifecycle.m_hired.at(4).masterGuid = {2000}; // stale ownership intent
 lifecycle.QueueDismissal(lifecycle.m_hired.at(5), Reason::Removed);
 lifecycle.m_hired.erase(5); // already dismissed/forgotten
 lifecycle.QueueDismissal(lifecycle.m_hired.at(6), Reason::MasterLeft);
 enqueueOnLogout = {7};
 drain();
 assert(stopped == 1 && lifecycle.m_hired.count(6)); // master rejoined
 assert(lifecycle.m_hired.count(7)); // nested work waits for NEXT update
 drain(); drain();
 assert(stopped == 2 && !lifecycle.m_hired.count(7));
 lifecycle.OnGroupMemberRemoved(&party, {9999}); // non-hire unaffected
 drain(); assert(stopped == 2);
#else
 assert(false && "group dismissals need a deferred world-update queue");
#endif
}
int main(int argc, char** argv) {
 assert(argc == 2);
 std::string mode = argv[1];
 if (mode == "mass") mass(); else if (mode == "bg") battleground(); else queue();
 std::puts("PASS hire lifecycle regression");
}
