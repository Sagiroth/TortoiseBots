#include "playerbot/playerbot.h"
#include "playerbot/AiFactory.h"
#include "playerbot/PlayerbotAI.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "runtime/PlayerbotAIStorage.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "Spells/SpellMgr.h"
#include "Spells/Spell.h"
#include "Spells/SpellAuras.h"
#include "Spells/SpellEntry.h"
#include "vmap/VMapFactory.h"
#include "playerbot/BotActionLog.h"
#include "playerbot/Talentspec.h"

using namespace ai;

std::set<std::string> PlayerbotAI::unsecuredCommands;

PlayerbotAI::PlayerbotAI() : PlayerbotAIBase(), bot(nullptr), aiObjectContext(nullptr), currentEngine(nullptr),
    chatHelper(this), chatFilter(this), accountId(0), security(nullptr), master(nullptr),
    currentState(BotState::BOT_STATE_NON_COMBAT), faceTargetUpdateDelay(0), jumpTime(0), fallAfterJump(false)
{
    std::fill(std::begin(engines), std::end(engines), nullptr);
    std::fill(std::begin(allowActive), std::end(allowActive), true);
    std::fill(std::begin(allowActiveCheckTimer), std::end(allowActiveCheckTimer), time(nullptr));
}

PlayerbotAI::PlayerbotAI(Player* player) : PlayerbotAIBase(), bot(player), aiObjectContext(nullptr), currentEngine(nullptr),
    chatHelper(this), chatFilter(this), accountId(0), security(player), master(nullptr),
    currentState(BotState::BOT_STATE_NON_COMBAT), faceTargetUpdateDelay(0), jumpTime(0), fallAfterJump(false)
{
    std::fill(std::begin(engines), std::end(engines), nullptr);
    std::fill(std::begin(allowActive), std::end(allowActive), true);
    std::fill(std::begin(allowActiveCheckTimer), std::end(allowActiveCheckTimer), time(nullptr));
    accountId = sObjectMgr.GetPlayerAccountIdByGUID(bot->GetObjectGuid());
    aiObjectContext = AiFactory::createAiObjectContext(bot, this);
    engines[static_cast<uint8>(BotState::BOT_STATE_COMBAT)] = AiFactory::createCombatEngine(bot, this, aiObjectContext);
    engines[static_cast<uint8>(BotState::BOT_STATE_NON_COMBAT)] = AiFactory::createNonCombatEngine(bot, this, aiObjectContext);
    engines[static_cast<uint8>(BotState::BOT_STATE_DEAD)] = AiFactory::createDeadEngine(bot, this, aiObjectContext);
    engines[static_cast<uint8>(BotState::BOT_STATE_REACTION)] = reactionEngine = AiFactory::createReactionEngine(bot, this, aiObjectContext);
    for (uint8 i = 0; i < static_cast<uint8>(BotState::BOT_STATE_ALL); ++i)
    {
        engines[i]->initMode = false;
        engines[i]->Init();
    }
    currentEngine = engines[static_cast<uint8>(BotState::BOT_STATE_NON_COMBAT)];
    sLog.outString("TortoiseBots AI: mature Engine/Value/Trigger/Action stack created for %s", bot->GetName());
}

PlayerbotAI::~PlayerbotAI()
{
    for (Engine* engine : engines) delete engine;
    delete aiObjectContext;
}

void PlayerbotAI::UpdateAI(uint32 elapsed, bool minimal)
{
    if (!bot || !bot->IsInWorld() || !currentEngine) return;
    bool reactionFound = false;
    reactionEngine->Update(elapsed, minimal, false, reactionFound);
    currentEngine->DoNextAction(nullptr, 0, minimal, false);
}

void PlayerbotAI::UpdateAIInternal(uint32, bool) { DoNextAction(false); }

void PlayerbotAI::DoNextAction(bool minimal)
{
    if (currentEngine) currentEngine->DoNextAction(nullptr, 0, minimal, false);
}

void PlayerbotAI::ChangeEngine(BotState state)
{
    Engine* next = engines[static_cast<uint8>(state)];
    if (!next || next == currentEngine) return;
    currentEngine = next;
    currentState = state;
    currentEngine->Init();
    sLog.outString("TortoiseBots AI: %s engine state -> %u", bot->GetName(), static_cast<uint32>(state));
}

void PlayerbotAI::OnCombatStarted()
{
    if (currentState == BotState::BOT_STATE_COMBAT) return;
    aiObjectContext->GetValue<time_t>("combat start time")->Set(time(nullptr));
    ChangeEngine(BotState::BOT_STATE_COMBAT);
}

void PlayerbotAI::OnCombatEnded()
{
    if (currentState == BotState::BOT_STATE_NON_COMBAT) return;
    aiObjectContext->GetValue<time_t>("combat start time")->Set(0);
    aiObjectContext->GetValue<Unit*>("current target")->Set(nullptr);
    bot->AttackStop();
    ChangeEngine(BotState::BOT_STATE_NON_COMBAT);
    DoSpecificAction("follow", Event("combat end"), true);
    sLog.outString("TortoiseBots AI: %s combat ended; PlayerBots follow resumed", bot->GetName());
}

void PlayerbotAI::OnDeath() { ChangeEngine(BotState::BOT_STATE_DEAD); }
void PlayerbotAI::OnResurrected() { ChangeEngine(BotState::BOT_STATE_NON_COMBAT); }

void PlayerbotAI::ChangeStrategy(const std::string& names, BotState state)
{
    Engine* engine = engines[static_cast<uint8>(state)];
    if (engine) engine->ChangeStrategy(names);
}

bool PlayerbotAI::HasStrategy(const std::string& name, BotState state)
{
    Engine* engine = engines[static_cast<uint8>(state)];
    return engine && engine->HasStrategy(name);
}

std::list<std::string_view> PlayerbotAI::GetStrategies(BotState state)
{
    Engine* engine = engines[static_cast<uint8>(state)];
    return engine ? engine->GetStrategies() : std::list<std::string_view>();
}

bool PlayerbotAI::DoSpecificAction(const std::string& name, Event event, bool)
{
    return currentEngine && currentEngine->ExecuteAction(name, event) == ACTION_RESULT_OK;
}

bool PlayerbotAI::CanMove()
{
    return bot && bot->IsAlive() && !bot->HasUnitState(UNIT_STAT_STUNNED | UNIT_STAT_ROOT | UNIT_STAT_FLEEING | UNIT_STAT_CONFUSED);
}

Unit* PlayerbotAI::GetUnit(ObjectGuid guid)
{
    return bot && guid ? sObjectAccessor.GetUnit(*bot, guid) : nullptr;
}

bool PlayerbotAI::CanCastSpell(std::string name, Unit* target, uint8, Item*, bool, bool, bool, SpellCastResult* checkResult)
{
    uint32 spell = aiObjectContext->GetValue<uint32>("spell id", name)->Get();
    return CanCastSpell(spell, target, 0, true, nullptr, true, false, false, checkResult);
}

bool PlayerbotAI::CanCastSpell(uint32 spell, Unit* target, uint8, bool checkHasSpell, Item*, bool, bool, bool, SpellCastResult* checkResult)
{
    SpellCastResult result = SPELL_CAST_OK;
    if (!spell || !target || !target->IsAlive()) result = SPELL_FAILED_BAD_TARGETS;
    else if (checkHasSpell && !bot->HasSpell(spell)) result = SPELL_FAILED_NOT_KNOWN;
    else if (bot->HasSpellCooldown(spell)) result = SPELL_FAILED_NOT_READY;
    else if (!bot->CanReachWithMeleeAutoAttack(target)) result = SPELL_FAILED_OUT_OF_RANGE;
    if (checkResult) *checkResult = result;
    return result == SPELL_CAST_OK;
}

bool PlayerbotAI::CastSpell(std::string name, Unit* target, Item* item, bool wait, uint32* duration)
{
    return CastSpell(aiObjectContext->GetValue<uint32>("spell id", name)->Get(), target, item, wait, duration);
}

bool PlayerbotAI::CastSpell(uint32 spell, Unit* target, Item* item, bool, uint32* duration)
{
    if (!CanCastSpell(spell, target, 0, true, item, true)) return false;
    SpellCastResult result = bot->CastSpell(target, spell, false, item);
    if (result == SPELL_CAST_OK)
        bot->AddSpellCooldown(spell, 0, time(nullptr) + 1);
    if (duration) *duration = sPlayerbotAIConfig.globalCoolDown;
    return result == SPELL_CAST_OK;
}

bool PlayerbotAI::HasAura(std::string name, Unit* unit, bool, bool, int, bool, int, int)
{
    uint32 spell = aiObjectContext->GetValue<uint32>("spell id", name)->Get();
    return spell && unit && unit->HasAura(spell);
}

bool PlayerbotAI::HasAura(uint32 spell, Unit* unit, bool) { return spell && unit && unit->HasAura(spell); }
bool PlayerbotAI::HasAnyAuraOf(Unit* unit, ...)
{
    if (!unit) return false;
    va_list args;
    va_start(args, unit);
    for (const char* name = va_arg(args, const char*); name; name = va_arg(args, const char*))
    {
        uint32 spell = aiObjectContext->GetValue<uint32>("spell id", name)->Get();
        if (spell && unit->HasAura(spell)) { va_end(args); return true; }
    }
    va_end(args);
    return false;
}
bool PlayerbotAI::IsInterruptableSpellCasting(Unit* unit, std::string, uint8)
{
    Spell* spell = unit ? unit->GetCurrentSpell(CURRENT_GENERIC_SPELL) : nullptr;
    return spell && spell->getState() == SPELL_STATE_CASTING && spell->m_spellInfo &&
        !(spell->m_spellInfo->InterruptFlags & SPELL_INTERRUPT_FLAG_INTERRUPT);
}
bool PlayerbotAI::HasAuraToDispel(Unit* unit, uint32 dispelType)
{
    if (!unit) return false;
    uint32 mask = GetDispellMask(DispelType(dispelType));
    for (auto const& entry : unit->GetSpellAuraHolderMap())
    {
        SpellAuraHolder* holder = entry.second;
        if (holder && (1u << holder->GetSpellProto()->Dispel) & mask) return true;
    }
    return false;
}

void PlayerbotAI::StopMoving()
{
    if (!bot) return;
    bot->StopMoving();
    bot->GetMotionMaster()->Clear();
}

void PlayerbotAI::SetActionDuration(const Action* action)
{
    if (action) SetAIInternalUpdateDelay(action->GetDuration());
}

bool PlayerbotAI::TellPlayer(Player* player, std::string text, PlayerbotSecurityLevel, bool, bool)
{
    if (!player || !player->GetSession()) return false;
    player->GetSession()->SendNotification("%s", text.c_str());
    return true;
}
bool PlayerbotAI::TellPlayerNoFacing(Player* player, std::string text, PlayerbotSecurityLevel level, bool a, bool b, bool c)
{
    return TellPlayer(player, std::move(text), level, a, b);
}
void PlayerbotAI::HandleCommands() {}

uint32 PlayerbotChatHandler::extractQuestId(std::string) { return 0; }
void PacketHandlingHelper::AddHandler(uint16 opcode, std::string handler, bool delayHandler) { handlers[opcode] = std::move(handler); delay[opcode] = delayHandler; }
void PacketHandlingHelper::Handle(ExternalEventHelper&) {}
void PacketHandlingHelper::AddPacket(const WorldPacket&) {}

namespace ai
{
ChatHelper::ChatHelper(PlayerbotAI* ai) : PlayerbotAIAware(ai) {}
CompositeChatFilter::CompositeChatFilter(PlayerbotAI* ai) : ChatFilter(ai) {}
CompositeChatFilter::~CompositeChatFilter() = default;
std::string CompositeChatFilter::Filter(std::string value) { return value; }
std::string ChatFilter::Filter(std::string value, std::string) { return value; }
}

PlayerbotSecurity::PlayerbotSecurity(Player* const player) : bot(player) {}

std::vector<std::string>& split(std::string const& value, char delimiter, std::vector<std::string>& result)
{
    result.clear();
    std::stringstream stream(value);
    std::string part;
    while (std::getline(stream, part, delimiter)) result.push_back(part);
    return result;
}

std::vector<std::string> split(std::string const& value, char delimiter)
{
    std::vector<std::string> result;
    return split(value, delimiter, result);
}

bool TalentSpec::CheckTalents(uint32 freeTalentPoints, std::ostringstream* out)
{
    uint32 spent = 0;
    for (auto const& talent : talents) spent += talent.rank;
    if (spent > freeTalentPoints)
    {
        if (out) *out << "spec requires " << spent << " talent points";
        return false;
    }
    return true;
}
namespace ai
{
bool WorldPosition::isVmapLoaded(unsigned int mapId, int x, int y)
{
    return VMAP::VMapFactory::createOrGetVMapManager()->existsMap(sWorld.GetDataPath().c_str(), mapId, x, y);
}
bool WorldPosition::loadVMap(unsigned int mapId, int x, int y)
{
    if (isVmapLoaded(mapId, x, y)) return true;
    return VMAP::VMapFactory::createOrGetVMapManager()->loadMap(sWorld.GetDataPath().c_str(), mapId, x, y) == VMAP::VMAP_LOAD_RESULT_OK;
}
namespace botdiag { void BotActionLog::Write(PlayerbotAI*, char const*, char const*, ...) {} }
}
