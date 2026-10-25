#include "playerbot/playerbot.h"
#include "BlackwingLairDungeonActions.h"
#include "playerbot/EbonrocSwapPolicy.h"
#include "playerbot/GroupMembers.h"
#include "playerbot/strategy/values/PositionValue.h"
#include "playerbot/strategy/values/PossibleAttackTargetsValue.h"
#include "../../../../runtime/PlayerbotAIStorage.h"
#include <map>
#include <mutex>

using namespace ai;

namespace
{
bool EbonrocHoldOrder(PlayerbotAI* ai)
{
    AiObjectContext* context = ai->GetAiObjectContext();
    PositionMap& positions = AI_VALUE(PositionMap&, "position");
    auto hold = positions.find("pull hold");
    return ai->HasStrategy("stay", BotState::BOT_STATE_COMBAT) ||
        ai->HasStrategy("follow", BotState::BOT_STATE_COMBAT) ||
        ai->HasStrategy("wait for attack", BotState::BOT_STATE_COMBAT) ||
        ai->HasStrategy("grind", BotState::BOT_STATE_COMBAT) ||
        (hold != positions.end() && hold->second.isSet());
}

bool EbonrocOtherTargetOrder(PlayerbotAI* ai, Unit* boss)
{
    AiObjectContext* context = ai->GetAiObjectContext();
    Unit* target = ai->GetUnit(AI_VALUE(ObjectGuid, "explicit attack target"));
    if (!target || !PossibleAttackTargetsValue::IsValid(target, ai->GetBot(),
        sPlayerbotAIConfig.sightDistance, false, false))
        target = AI_VALUE(Unit*, "rti target");
    return target && target != boss && PossibleAttackTargetsValue::IsValid(target, ai->GetBot(),
        sPlayerbotAIConfig.sightDistance, false, false);
}

uint32 ReadyEbonrocTaunt(PlayerbotAI* ai, Unit* boss)
{
    AiObjectContext* context = ai->GetAiObjectContext();
    for (char const* name : { "taunt", "growl", "hand of reckoning" })
    {
        uint32 const id = AI_VALUE2(uint32, "spell id", name);
        SpellEntry const* spell = sSpellTemplate.LookupEntry<SpellEntry>(id);
        if (!id || !spell || !ai->GetBot()->HasSpell(id))
            continue;
        bool taunt = false;
        bool immune = false;
        for (int effect = EFFECT_INDEX_0; effect <= EFFECT_INDEX_2; ++effect)
            if (spell->Effect[effect] == SPELL_EFFECT_ATTACK_ME ||
                (spell->Effect[effect] == SPELL_EFFECT_APPLY_AURA && spell->EffectApplyAuraName[effect] == SPELL_AURA_MOD_TAUNT))
            {
                taunt = true;
                immune = immune || boss->IsImmuneToSpellEffect(spell, SpellEffectIndex(effect), false);
            }
        if (taunt && !immune && ai->CanCastSpell(id, boss))
            return id;
    }
    return 0;
}

struct EbonrocSwapRecord
{
    EbonrocSwapWindow window;
    uint32 lastSeen = 0;
};
std::mutex swapMutex;
std::map<std::pair<uint32, uint64>, EbonrocSwapRecord> swaps;

struct EbonrocSwapRequest
{
    Unit* boss = nullptr;
    uint32 spell = 0;
};

EbonrocSwapRequest GetEbonrocSwapRequest(PlayerbotAI* ai, bool claim)
{
    Unit* boss = GetActiveEbonrocSwapBoss(ai);
    Unit* victim = boss ? boss->GetVictim() : nullptr;
    if (!boss || !victim || !victim->IsAlive())
        return {};
    Aura* shadow = ai->GetAura(23340, victim);
    Player* chosen = nullptr;
    uint32 chosenSpell = 0;
    if (shadow)
        for (Player* member : LiveGroupMembers(ai->GetBot()->GetGroup()))
        {
            if (!member || member == victim || !member->IsAlive() || !member->IsInWorld() ||
                member->IsBeingTeleported() || !member->IsInMap(boss) || !member->IsInCombat() || member->HasAura(23340))
                continue;
            PlayerbotAI* candidate = PlayerbotAIStorage::Instance().GetAI(member);
            if (!candidate || !candidate->IsTank(member) ||
                !candidate->HasStrategy("ebonroc swap", BotState::BOT_STATE_COMBAT) ||
                EbonrocHoldOrder(candidate) || EbonrocOtherTargetOrder(candidate, boss))
                continue;
            uint32 spell = ReadyEbonrocTaunt(candidate, boss);
            if (spell && (!chosen || member->GetObjectGuid() < chosen->GetObjectGuid()))
            {
                chosen = member;
                chosenSpell = spell;
            }
        }
    uint32 const now = WorldTimer::getMSTime();
    std::lock_guard<std::mutex> lock(swapMutex);
    // One record per live instance/boss, not per bot or debuff application.
    // Expired encounter records are pruned on subsequent coordinator calls.
    for (auto it = swaps.begin(); it != swaps.end();)
        if (now - it->second.lastSeen > 60000)
            it = swaps.erase(it);
        else
            ++it;
    auto& record = swaps[{ boss->GetInstanceId(), boss->GetObjectGuid().GetRawValue() }];
    record.lastSeen = now;
    record.window.ObserveVictim(victim->GetObjectGuid().GetRawValue());
    if (!shadow)
        return {};
    record.window.Begin(victim->GetObjectGuid().GetRawValue(), shadow->GetAuraApplyTime());
    if (!chosen || chosen != ai->GetBot())
        return {};
    bool const ready = claim ? record.window.Claim(ai->GetBot()->GetObjectGuid().GetRawValue(),
        chosen->GetObjectGuid().GetRawValue(), now) : record.window.Ready(now);
    return ready ? EbonrocSwapRequest{ boss, chosenSpell } : EbonrocSwapRequest{};
}
}

Unit* ai::GetActiveEbonrocSwapBoss(PlayerbotAI* ai)
{
    Player* bot = ai->GetBot();
    if (bot->GetMapId() != 469 || !bot->IsInWorld() || !bot->IsAlive() || bot->IsBeingTeleported() ||
        !bot->IsInCombat() || !ai->IsTank(bot) || !ai->HasStrategy("ebonroc swap", BotState::BOT_STATE_COMBAT) ||
        EbonrocHoldOrder(ai))
        return nullptr;
    AiObjectContext* context = ai->GetAiObjectContext();
    for (ObjectGuid const& guid : AI_VALUE(std::list<ObjectGuid>, "attackers"))
    {
        Unit* boss = ai->GetUnit(guid);
        if (boss && boss->GetEntry() == 14601 && boss->IsAlive() && boss->IsInCombat() && boss->IsInMap(bot) &&
            !EbonrocOtherTargetOrder(ai, boss))
            return boss;
    }
    return nullptr;
}

bool EbonrocShadowSwapAction::isUseful()
{
    return GetEbonrocSwapRequest(ai, false).boss != nullptr;
}

bool EbonrocShadowSwapAction::Execute(Event& event)
{
    auto const request = GetEbonrocSwapRequest(ai, true);
    if (!request.boss || !request.spell)
        return false;
    // Claim before casting: another bot cannot race this unconfirmed attempt.
    // A cast return value is not confirmation; the next observed victim is.
    return ai->CastSpell(request.spell, request.boss);
}
