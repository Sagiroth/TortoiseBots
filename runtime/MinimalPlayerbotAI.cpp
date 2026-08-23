// pi-lens-ignore: clang:pp_file_not_found,clang:unknown_typename,clang:use_of_undeclared_identifier,clang:unknown_type_name,clang:expected_namespace_name
// Minimal PlayerbotAI for E2E green checkpoint — ponytail stub.
#include "../ai/playerbot/PlayerbotAI.h"
#include "../ai/playerbot/AiFactory.h"

using namespace ai;

PlayerbotAI::PlayerbotAI() : PlayerbotAIBase(), bot(NULL), aiObjectContext(NULL),
    currentEngine(NULL), chatHelper(this), chatFilter(this), accountId(0), security(NULL), master(NULL), currentState(BotState::BOT_STATE_NON_COMBAT), faceTargetUpdateDelay(0), jumpTime(0), fallAfterJump(false)
{
    for (uint8 i = 0 ; i < (uint8)BotState::BOT_STATE_ALL; i++) engines[i] = NULL;
    for (int i = 0; i < MAX_ACTIVITY_TYPE; i++) { allowActiveCheckTimer[i] = time(nullptr); allowActive[i] = false; }
}

PlayerbotAI::PlayerbotAI(Player* bot_) :
    PlayerbotAIBase(), chatHelper(this), chatFilter(this), security(bot_), master(NULL), faceTargetUpdateDelay(0), jumpTime(0), fallAfterJump(false)
{
    this->bot = bot_;
    for (uint8 i = 0 ; i < (uint8)BotState::BOT_STATE_ALL; i++) engines[i] = NULL;
    for (int i = 0; i < MAX_ACTIVITY_TYPE; i++) { allowActiveCheckTimer[i] = time(nullptr); allowActive[i] = false; }
    accountId = sObjectMgr.GetPlayerAccountIdByGUID(bot->GetObjectGuid());
    aiObjectContext = AiFactory::createAiObjectContext(bot, this);
    engines[(uint8)BotState::BOT_STATE_COMBAT] = AiFactory::createCombatEngine(bot, this, aiObjectContext);
    engines[(uint8)BotState::BOT_STATE_NON_COMBAT] = AiFactory::createNonCombatEngine(bot, this, aiObjectContext);
    engines[(uint8)BotState::BOT_STATE_DEAD] = AiFactory::createDeadEngine(bot, this, aiObjectContext);
    engines[(uint8)BotState::BOT_STATE_REACTION] = reactionEngine = AiFactory::createReactionEngine(bot, this, aiObjectContext);
    for (uint8 e = 0; e < (uint8)BotState::BOT_STATE_ALL; e++) { engines[e]->initMode = false; engines[e]->Init(); }
    currentEngine = engines[(uint8)BotState::BOT_STATE_NON_COMBAT];
    currentState = BotState::BOT_STATE_NON_COMBAT;
    printf("TortoiseBots: MinimalPlayerbotAI created for %s\n", bot->GetName());
}

PlayerbotAI::~PlayerbotAI()
{
    for (uint8 i = 0 ; i < (uint8)BotState::BOT_STATE_ALL; i++) delete engines[i];
    delete aiObjectContext;
}

void PlayerbotAI::UpdateAI(uint32 elapsed, bool /*minimal*/)
{
    if (!bot || !bot->IsInWorld()) return;
    if (!currentEngine) return;
    if (reactionEngine) { bool rf=false; reactionEngine->Update(elapsed, false, false, rf); }
    bool acted = currentEngine->DoNextAction(NULL, 0, false, false);
    printf("TortoiseBots: Engine DoNextAction for %s state %u acted %u\n", bot->GetName(), (uint32)currentState, acted);
}

void PlayerbotAI::UpdateAIInternal(uint32 /*elapsed*/, bool /*minimal*/) {}
bool PlayerbotAI::CanMove() { return bot && !bot->HasUnitState(UNIT_STAT_STUNNED) && !bot->HasUnitState(UNIT_STAT_ROOT); }
bool PlayerbotAI::HasStrategy(const std::string& n, BotState s) { return false; }
bool PlayerbotAI::TellPlayer(Player* p, std::string t, PlayerbotSecurityLevel s, bool a, bool b) { return false; }
bool PlayerbotAI::TellPlayerNoFacing(Player* p, std::string t, PlayerbotSecurityLevel s, bool a, bool b, bool c) { return false; }
void PlayerbotAI::SetActionDuration(const ai::Action* a) {}
void PlayerbotAI::HandleCommands() {}
bool PlayerbotAI::DoSpecificAction(const std::string& n, ai::Event e, bool f) { return false; }
bool PlayerbotAI::HasAura(std::string n, Unit* u, bool a, bool b, int c, bool d, int e, int f) { return false; }
bool PlayerbotAI::HasAnyAuraOf(Unit* u, ...) { return false; }
bool PlayerbotAI::IsInterruptableSpellCasting(Unit* u, std::string n, uint8 f) { return false; }
bool PlayerbotAI::HasAuraToDispel(Unit* u, uint32 t) { return false; }
bool PlayerbotAI::CanCastSpell(std::string n, Unit* u, uint8 f, Item* i, bool a, bool b, bool c, SpellCastResult* r) { return false; }
bool PlayerbotAI::CastSpell(std::string n, Unit* u, Item* i, bool a, uint32* y) { return false; }
Unit* PlayerbotAI::GetUnit(ObjectGuid guid) { return nullptr; }
bool PlayerbotAI::HasAura(uint32 id, Unit* u, bool o) { return false; }
bool PlayerbotAI::CanCastSpell(uint32 id, Unit* u, uint8 f, bool c, Item* i, bool a, bool b, bool d, SpellCastResult* r) { return false; }
bool PlayerbotAI::CastSpell(uint32 id, Unit* u, Item* i, bool a, uint32* y) { return false; }

// Provide the PacketHandlingHelper stubs (required for linking, even though unused in minimal path)
void PacketHandlingHelper::AddHandler(uint16 /*opcode*/, std::string /*handler*/, bool /*shouldDelay*/) {}
void PacketHandlingHelper::Handle(ExternalEventHelper &/*helper*/) {}
void PacketHandlingHelper::AddPacket(const WorldPacket& /*packet*/) {}
uint32 PlayerbotChatHandler::extractQuestId(std::string /*str*/) { return 0; }
std::set<std::string> PlayerbotAI::unsecuredCommands;
