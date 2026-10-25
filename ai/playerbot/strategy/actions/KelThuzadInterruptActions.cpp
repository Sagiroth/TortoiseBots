#include "playerbot/playerbot.h"
#include "KelThuzadInterruptActions.h"
#include "playerbot/KelThuzadInterruptPolicy.h"
#include "playerbot/GroupMembers.h"
#include "playerbot/strategy/values/PositionValue.h"
#include "playerbot/strategy/values/PossibleAttackTargetsValue.h"
#include "../../../../runtime/PlayerbotAIStorage.h"
#include <map>
#include <mutex>

using namespace ai;
namespace
{
bool Eligible(PlayerbotAI* ai, Unit* boss)
{
    Player* bot = ai->GetBot();
    if (!bot->IsInWorld() || !bot->IsAlive() || bot->IsBeingTeleported() ||
        !bot->IsInCombat() || !bot->IsInMap(boss) ||
        !ai->HasStrategy("kt interrupt", BotState::BOT_STATE_COMBAT))
        return false;
    AiObjectContext* context = ai->GetAiObjectContext();
    PositionMap& positions = AI_VALUE(PositionMap&, "position");
    auto hold = positions.find("pull hold");
    if (ai->HasStrategy("stay", BotState::BOT_STATE_COMBAT) ||
        ai->HasStrategy("follow", BotState::BOT_STATE_COMBAT) ||
        ai->HasStrategy("wait for attack", BotState::BOT_STATE_COMBAT) ||
        ai->HasStrategy("grind", BotState::BOT_STATE_COMBAT) ||
        (hold != positions.end() && hold->second.isSet()))
        return false;
    Unit* order = ai->GetUnit(AI_VALUE(ObjectGuid, "explicit attack target"));
    if (!order || !PossibleAttackTargetsValue::IsValid(order, bot,
        sPlayerbotAIConfig.sightDistance, false, false))
        order = AI_VALUE(Unit*, "rti target");
    return !order || order == boss || !PossibleAttackTargetsValue::IsValid(order, bot,
        sPlayerbotAIConfig.sightDistance, false, false);
}

bool DangerousCast(Unit* boss)
{
    Spell* cast = boss->GetCurrentSpell(CURRENT_GENERIC_SPELL);
    if (!cast || cast->m_spellInfo->Id != 28478 || cast->getState() != SPELL_STATE_PREPARING ||
        cast->GetCastTime() <= 0 || cast->GetCastTime() > 2000 || cast->GetCastedTime() < 150)
        return false;
    // Match the core's EffectInterruptCast requirements, not spell names.
    return cast->m_spellInfo->PreventionType == SPELL_PREVENTION_TYPE_SILENCE &&
        (cast->m_spellInfo->InterruptFlags & SPELL_INTERRUPT_FLAG_DAMAGE);
}

uint32 ReadyInterrupt(PlayerbotAI* ai, Unit* boss)
{
    AiObjectContext* context = ai->GetAiObjectContext();
    if (!ai->GetBot()->IsWithinLOSInMap(boss))
        return 0;
    for (char const* name : { "kick", "pummel", "shield bash", "earth shock", "counterspell" })
    {
        uint32 id = AI_VALUE2(uint32, "spell id", name);
        SpellEntry const* spell = sSpellTemplate.LookupEntry<SpellEntry>(id);
        if (!id || !spell || !ai->GetBot()->HasSpell(id))
            continue;
        bool interrupt = false;
        for (int i = EFFECT_INDEX_0; i <= EFFECT_INDEX_2; ++i)
            if (spell->Effect[i] == SPELL_EFFECT_INTERRUPT_CAST &&
                !boss->IsImmuneToSpellEffect(spell, SpellEffectIndex(i), false))
                interrupt = true;
        SpellCastResult result = SPELL_FAILED_NOT_READY;
        if (interrupt && ai->CanCastSpell(id, boss, 0, true, nullptr, false, false, false, &result) &&
            result == SPELL_CAST_OK)
            return id;
    }
    return 0;
}

struct Record { KelThuzadInterruptWindow window; uint32 seen = 0; };
std::mutex interruptMutex;
std::map<std::pair<uint32,uint64>, Record> records;
struct Request { Unit* boss = nullptr; uint32 spell = 0; };
Request GetRequest(PlayerbotAI* ai, bool claim)
{
    Unit* boss = GetKelThuzadInterruptBoss(ai);
    if (!boss || !DangerousCast(boss))
        return {};
    std::map<uint64, uint32> ready;
    for (Player* member : LiveGroupMembers(ai->GetBot()->GetGroup()))
    {
        PlayerbotAI* candidate = member ? PlayerbotAIStorage::Instance().GetAI(member) : nullptr;
        if (!candidate || !Eligible(candidate, boss))
            continue;
        uint32 spell = ReadyInterrupt(candidate, boss);
        if (spell)
            ready[member->GetObjectGuid().GetRawValue()] = spell;
    }
    uint32 now = WorldTimer::getMSTime();
    uint64 caller = ai->GetBot()->GetObjectGuid().GetRawValue();
    std::lock_guard<std::mutex> lock(interruptMutex);
    for (auto it = records.begin(); it != records.end();)
        if (now - it->second.seen > 60000)
            it = records.erase(it);
        else
            ++it;
    auto& record = records[{boss->GetInstanceId(), boss->GetObjectGuid().GetRawValue()}];
    record.seen = now;
    record.window.Observe(now);
    for (auto const& candidate : ready)
        if (record.window.Ready(candidate.first, now))
        {
            if (candidate.first != caller ||
                (claim && !record.window.Claim(caller, candidate.first, now)))
                return {};
            return {boss, candidate.second};
        }
    return {};
}
}

Unit* ai::GetKelThuzadInterruptBoss(PlayerbotAI* ai)
{
    Player* bot = ai->GetBot();
    if (bot->GetMapId() != 533 || !bot->IsInWorld() || !bot->IsAlive() ||
        !bot->IsInCombat() || !ai->HasStrategy("kt interrupt", BotState::BOT_STATE_COMBAT))
        return nullptr;
    AiObjectContext* context = ai->GetAiObjectContext();
    for (ObjectGuid const& guid : AI_VALUE(std::list<ObjectGuid>, "attackers"))
    {
        Unit* boss = ai->GetUnit(guid);
        if (boss && boss->GetEntry() == 15990 && boss->IsAlive() && boss->IsInCombat() &&
            boss->IsInMap(bot) && Eligible(ai, boss))
            return boss;
    }
    return nullptr;
}

bool ai::IsKelThuzadReservedInterrupt(PlayerbotAI* ai, uint32 id)
{
    if (!id)
        return false;
    AiObjectContext* context = ai->GetAiObjectContext();
    bool assignedAbility = false;
    for (char const* name : { "kick", "pummel", "shield bash", "earth shock", "counterspell" })
        if (AI_VALUE2(uint32, "spell id", name) == id)
            assignedAbility = true;
    SpellEntry const* spell = sSpellTemplate.LookupEntry<SpellEntry>(id);
    if (assignedAbility && spell)
        for (int i = EFFECT_INDEX_0; i <= EFFECT_INDEX_2; ++i)
            if (spell->Effect[i] == SPELL_EFFECT_INTERRUPT_CAST)
                return true;
    return false;
}

bool KelThuzadInterruptAction::isUseful() { return GetRequest(ai, false).boss != nullptr; }
bool KelThuzadInterruptAction::Execute(Event& event)
{
    auto request = GetRequest(ai, true);
    // Reserve before sending a cast; success is not assumed from this return value.
    return request.boss && DangerousCast(request.boss) && ai->CastSpell(request.spell, request.boss);
}
