// pi-lens-ignore: clang:pp_file_not_found,clang:unknown_typename,clang:use_of_undeclared_identifier,clang:unknown_type_name
#include <cstdarg>
#include <cstdio>
#include "../ai/playerbot/BotLog.h"
BotLog& BotLog::Instance() { static BotLog i; return i; }
void BotLog::outError(const char* fmt, ...) {}
void BotLog::outDebug(const char* fmt, ...) {}
void BotLog::outDetail(const char* fmt, ...) {}
void BotLog::outString(const char* fmt, ...) { va_list ap; va_start(ap, fmt); vprintf(fmt, ap); printf("\n"); va_end(ap); }
std::vector<std::string>& split(const std::string &s, char delim, std::vector<std::string> &elems) { elems.clear(); size_t start = 0; size_t end = s.find(delim); while (end != std::string::npos) { elems.push_back(s.substr(start, end - start)); start = end + 1; end = s.find(delim, start); } elems.push_back(s.substr(start)); return elems; }
std::vector<std::string> split(const std::string &s, char delim) { std::vector<std::string> elems; split(s, delim, elems); return elems; }

// E2E: minimal stubs for Engine linkage
#include "../ai/playerbot/PlayerbotAIConfig.h"
#include "../ai/playerbot/RandomPlayerbotMgr.h"
#include "../ai/playerbot/BotActionLog.h"
#include "../ai/playerbot/PlayerbotAI.h"
#include "../ai/playerbot/ChatHelper.h"
#include "../ai/playerbot/ChatFilter.h"
#include "../ai/playerbot/PlayerbotSecurity.h"
#include "Policies/SingletonImp.h"
INSTANTIATE_SINGLETON_1(PlayerbotAIConfig)
INSTANTIATE_SINGLETON_1(RandomPlayerbotMgr)
PlayerbotAIConfig::PlayerbotAIConfig() {}
bool PlayerbotAIConfig::IsFreeAltBot(uint32 v) { return false; }
bool PlayerbotAIConfig::CanLogAction(PlayerbotAI* a, std::string n, bool b, std::string s) { return false; }
RandomPlayerbotMgr::RandomPlayerbotMgr() {}
RandomPlayerbotMgr::~RandomPlayerbotMgr() {}
bool RandomPlayerbotMgr::IsRandomBot(Player* p) { return false; }
namespace ai { namespace botdiag { void BotActionLog::Write(PlayerbotAI* a, const char* b, const char* c, ...) {} } }
// ChatHelper stub (real file disabled due to Penqle API)
namespace ai { ChatHelper::ChatHelper(PlayerbotAI* a) : PlayerbotAIAware(a) {} }
namespace ai { CompositeChatFilter::CompositeChatFilter(PlayerbotAI* a) : ChatFilter(a) {} }
PlayerbotSecurity::PlayerbotSecurity(Player* const b) : bot(b) {}

// RandomPlayerbotMgr virtuals (stubbed to avoid pulling 300-line mgr)
void RandomPlayerbotMgr::UpdateAIInternal(uint32 a, bool b) {}
void RandomPlayerbotMgr::MovePlayerBot(uint32 a, PlayerbotHolder* b) {}
void RandomPlayerbotMgr::OnBotLoginInternal(Player* p) {}
void RandomPlayerbotMgr::OnBotDeleted(uint32 a, uint32 b) {}
uint32 RandomPlayerbotMgr::GetOrCreateAccount(Player* master, std::string& error) { return 0; }
// TalentSpec vtable (stubbed, real Talentspec.cpp excluded)
#include "../ai/playerbot/Talentspec.h"
bool TalentSpec::CheckTalents(uint32 a, std::ostringstream* b) { return false; }
// GuidPosition vtable is now via real GuidPosition.cpp, no stub needed
// CompositeChatFilter dtor
namespace ai { CompositeChatFilter::~CompositeChatFilter() {} }
// PlayerbotHolder/botPID stubs for Random
PlayerbotHolder::PlayerbotHolder() {}
PlayerbotHolder::~PlayerbotHolder() {}
botPID::botPID(double a,double b,double c,double d,double e,double f) {}
botPID::~botPID() {}

namespace ai { std::string CompositeChatFilter::Filter(std::string s) { return s; } std::string ChatFilter::Filter(std::string a, std::string b) { return a; } }
void PlayerbotHolder::UpdateAIInternal(uint32 a, bool b) {}
void PlayerbotHolder::MovePlayerBot(uint32 a, PlayerbotHolder* b) {}
void PlayerbotHolder::OnBotDeleted(uint32 a, uint32 b) {}
uint32 PlayerbotHolder::GetOrCreateAccount(Player* p, std::string& s) { return 0; }
namespace ai { bool WorldPosition::isVmapLoaded(unsigned int a, int b, int c) { return false; } bool WorldPosition::loadVMap(unsigned int a, int b, int c) { return false; } }
// For Minimal's WorldPosition const methods

