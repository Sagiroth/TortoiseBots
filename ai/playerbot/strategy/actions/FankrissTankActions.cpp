#include "playerbot/playerbot.h"
#include "FankrissTankActions.h"
#include "playerbot/FankrissTankPolicy.h"
#include "playerbot/EbonrocSwapPolicy.h"
#include "playerbot/GroupMembers.h"
#include "playerbot/strategy/values/PossibleAttackTargetsValue.h"
#include "../../../../runtime/PlayerbotAIStorage.h"
#include <map>
#include <mutex>

using namespace ai;
namespace
{
bool Assigned(PlayerbotAI* ai)
{
    Player* bot = ai ? ai->GetBot() : nullptr;
    return bot && bot->IsInWorld() && bot->IsAlive() && !bot->IsBeingTeleported() &&
        bot->GetMapId() == 531 && bot->GetGroup() && ai->IsTank(bot) &&
        ai->HasStrategy("fankriss tank", BotState::BOT_STATE_COMBAT);
}
bool OrderedElsewhere(PlayerbotAI* ai, Unit* boss)
{
    AiObjectContext* context = ai->GetAiObjectContext();
    for (Unit* target : {ai->GetUnit(AI_VALUE(ObjectGuid, "explicit attack target")),
                        AI_VALUE(Unit*, "rti target")})
        if (target && target != boss && PossibleAttackTargetsValue::IsValid(target,
            ai->GetBot(), sPlayerbotAIConfig.sightDistance, false, false))
            return true;
    return false;
}
unsigned Wounds(Unit* tank)
{
    Aura* wound = tank ? tank->GetAura(25646, EFFECT_INDEX_0) : nullptr;
    return wound ? wound->GetStackAmount() : 0;
}
char const* MissingStance(PlayerbotAI* ai)
{
    Player* bot = ai->GetBot();
    if (bot->GetClass() == CLASS_WARRIOR && bot->HasSpell(71) && !ai->HasAura("defensive stance", bot))
        return "defensive stance";
    if (bot->GetClass() == CLASS_DRUID && !ai->HasAura("dire bear form", bot) && !ai->HasAura("bear form", bot))
    {
        if (bot->HasSpell(9634)) return "dire bear form";
        if (bot->HasSpell(5487)) return "bear form";
    }
    return nullptr;
}
uint32 ReadyTaunt(PlayerbotAI* ai, Unit* boss)
{
    // Classic warrior/druid single-target taunts only. Validate the actual
    // learned spell, stance, cooldown, range, LOS and the boss's immunity.
    char const* name = ai->GetBot()->GetClass() == CLASS_WARRIOR ? "taunt" :
        ai->GetBot()->GetClass() == CLASS_DRUID ? "growl" : nullptr;
    if (!name || MissingStance(ai)) return 0;
    AiObjectContext* context = ai->GetAiObjectContext();
    uint32 const id = AI_VALUE2(uint32, "spell id", name);
    SpellEntry const* spell = sSpellTemplate.LookupEntry<SpellEntry>(id);
    if (!id || !spell || !ai->GetBot()->HasSpell(id)) return 0;
    bool taunt = false;
    for (int effect = EFFECT_INDEX_0; effect <= EFFECT_INDEX_2; ++effect)
        if (spell->Effect[effect] == SPELL_EFFECT_ATTACK_ME ||
            (spell->Effect[effect] == SPELL_EFFECT_APPLY_AURA && spell->EffectApplyAuraName[effect] == SPELL_AURA_MOD_TAUNT))
        {
            taunt = true;
            if (boss->IsImmuneToSpellEffect(spell, SpellEffectIndex(effect), false)) return 0;
        }
    return taunt && ai->CanCastSpell(id, boss) ? id : 0;
}
struct SwapRecord { FankrissSwapWindow window; uint32 lastSeen = 0; };
std::mutex swapMutex;
std::map<std::pair<uint32, uint64>, SwapRecord> swaps;
struct SwapRequest { Unit* boss = nullptr; uint32 spell = 0; };
SwapRequest RequestSwap(PlayerbotAI* ai, bool claim)
{
    Unit* boss = GetAssignedFankrissTankTarget(ai);
    Unit* victim = boss ? boss->GetVictim() : nullptr;
    if (!victim || victim == ai->GetBot()) return {};
    unsigned const stacks = Wounds(victim);
    unsigned const threshold = sPlayerbotAIConfig.fankrissSwapStacks;
    bool const tankVictim = victim->IsPlayer() && ai->IsTank(static_cast<Player*>(victim));
    if (!FankrissNeedsSwap(stacks, Wounds(ai->GetBot()), threshold, tankVictim)) return {};
    Player* chosen = nullptr;
    uint32 chosenSpell = 0;
    for (Player* member : LiveGroupMembers(ai->GetBot()->GetGroup()))
    {
        if (member == victim || !member->IsInMap(ai->GetBot()) ||
            !FankrissNeedsSwap(stacks, Wounds(member), threshold, tankVictim)) continue;
        PlayerbotAI* candidate = PlayerbotAIStorage::Instance().GetAI(member);
        if (!Assigned(candidate) || OrderedElsewhere(candidate, boss)) continue;
        uint32 const spell = ReadyTaunt(candidate, boss);
        if (spell && (!chosen || member->GetObjectGuid() < chosen->GetObjectGuid()))
        { chosen = member; chosenSpell = spell; }
    }
    if (chosen != ai->GetBot()) return {};
    uint32 const now = WorldTimer::getMSTime();
    std::lock_guard<std::mutex> lock(swapMutex);
    for (auto it = swaps.begin(); it != swaps.end();)
        if (now - it->second.lastSeen > 60000) it = swaps.erase(it);
        else ++it;
    auto& record = swaps[{boss->GetInstanceId(), boss->GetObjectGuid().GetRawValue()}];
    record.lastSeen = now;
    // Do not reset this guard on a victim change or an aura refresh: neither
    // may allow a second volunteer to taunt in the same handoff tick.
    bool const ready = claim ? record.window.Claim(ai->GetBot()->GetObjectGuid().GetRawValue(),
        chosen->GetObjectGuid().GetRawValue(), now) : record.window.Ready(now);
    return ready ? SwapRequest{boss, chosenSpell} : SwapRequest{};
}
class FankrissTauntMultiplier : public Multiplier
{
public:
    FankrissTauntMultiplier(PlayerbotAI* ai) : Multiplier(ai, "fankriss coordinated taunts") {}
    float GetValue(Action* action) override
    {
        if (!action || !EbonrocTauntAction(action->getName())) return 1.0f;
        Unit* boss = GetAssignedFankrissTankTarget(ai);
        if (!boss) return 1.0f;
        bool const aoe = action->getName() == "challenging shout" || action->getName() == "challenging roar";
        // The dedicated swap action alone chooses a volunteer; ordinary
        // rotation/loose-target taunts must not undo the handoff. Damage stays on.
        return (aoe || action->GetTarget() == boss ||
            (action->getName() == "righteous defense" && action->GetTarget() == boss->GetVictim())) ? 0.0f : 1.0f;
    }
};
}

Unit* ai::GetAssignedFankrissTankTarget(PlayerbotAI* ai)
{
    if (!Assigned(ai)) return nullptr;
    Player* bot = ai->GetBot();
    AiObjectContext* context = ai->GetAiObjectContext();
    // Cached attackers only: no world scan and never a fresh pull.
    for (ObjectGuid const& guid : AI_VALUE(std::list<ObjectGuid>, "attackers"))
    {
        Unit* boss = ai->GetUnit(guid);
        if (!boss || boss->GetEntry() != 15510 || !boss->IsInWorld() || !boss->IsAlive() ||
            !boss->IsInCombat() || !boss->IsInMap(bot) || !boss->IsCreature() ||
            static_cast<Creature*>(boss)->IsInEvadeMode() || !bot->IsValidAttackTarget(boss)) continue;
        Unit* victim = boss->GetVictim();
        if (!victim || !victim->IsPlayer() || static_cast<Player*>(victim)->GetGroup() != bot->GetGroup() ||
            OrderedElsewhere(ai, boss)) continue;
        return boss;
    }
    return nullptr;
}
bool FankrissSwapAction::isUseful() { return RequestSwap(ai, false).boss != nullptr; }
bool FankrissSwapAction::Execute(Event& event)
{
    auto request = RequestSwap(ai, true);
    // Failed or resisted casts release no second simultaneous volunteer;
    // retry after two seconds, choosing a ready tank again.
    return request.boss && request.spell && ai->CastSpell(request.spell, request.boss);
}
bool FankrissTankEngageAction::isUseful()
{
    Unit* boss = GetAssignedFankrissTankTarget(ai);
    return boss && (MissingStance(ai) || AI_VALUE(Unit*, "current target") != boss ||
        bot->GetVictim() != boss || !bot->HasUnitState(UNIT_STAT_MELEE_ATTACKING));
}
bool FankrissTankEngageAction::Execute(Event& event)
{
    Unit* boss = GetAssignedFankrissTankTarget(ai);
    if (!boss || !isPossible()) return false;
    if (char const* stance = MissingStance(ai)) return ai->DoSpecificAction(stance, event, true);
    return Attack(GetMaster(), boss);
}
void FankrissTankStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode("fankriss tank engage needed",
        NextAction::array(0, new NextAction("fankriss tank engage", ACTION_HIGH + 3), NULL)));
}
void FankrissTankStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers) { InitCombatTriggers(triggers); }
void FankrissTankStrategy::InitReactionTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode("fankriss swap needed",
        NextAction::array(0, new NextAction("fankriss swap", ACTION_EMERGENCY + 4), NULL)));
}
void FankrissTankStrategy::InitCombatMultipliers(std::list<Multiplier*>& multipliers)
{ multipliers.push_back(new FankrissTauntMultiplier(ai)); }
void FankrissTankStrategy::InitReactionMultipliers(std::list<Multiplier*>& multipliers)
{ InitCombatMultipliers(multipliers); }
