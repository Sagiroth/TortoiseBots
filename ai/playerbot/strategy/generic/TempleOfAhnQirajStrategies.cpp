#include "playerbot/playerbot.h"
#include "TempleOfAhnQirajStrategies.h"
#include "playerbot/strategy/actions/ViscidusActions.h"
#include "playerbot/strategy/actions/GenericSpellActions.h"
#include "playerbot/strategy/actions/ReachTargetActions.h"

using namespace ai;
namespace
{
    class ViscidusMultiplier : public Multiplier
    {
    public:
        explicit ViscidusMultiplier(PlayerbotAI* ai) : Multiplier(ai, "viscidus cycle") {}
        float GetValue(Action* action) override
        {
            if (!action || !ViscidusEnabled(ai)) return 1.0f;
            auto const& name = action->getName();
            if (name == "viscidus toxin escape" || name == "move away from hazard" || name == "avoid aoe") return 1.0f;
            if (InsideViscidusToxin(ai) && (dynamic_cast<MovementAction*>(action) ||
                dynamic_cast<CastReachTargetSpellAction*>(action))) return 0.0f;
            if (name == "viscidus keep distance") return 1.0f;
            if (NeedsViscidusDistance(ai) && (dynamic_cast<MovementAction*>(action) ||
                dynamic_cast<CastSpellAction*>(action))) return 0.0f;
            auto* encounter = dynamic_cast<ViscidusCombatAction*>(ai->GetAiObjectContext()->GetAction("viscidus combat"));
            WorldPosition ring;
            Unit* focus = GetViscidusCombatTarget(ai);
            bool ownMovement = encounter && (encounter->RingPoint(ring) ||
                (focus && focus->GetEntry() == 15667 && ai->GetBot()->GetClass() == CLASS_MAGE));
            if (ownMovement && name != "viscidus combat" && (dynamic_cast<MovementAction*>(action) ||
                dynamic_cast<CastReachTargetSpellAction*>(action))) return 0.0f;
            auto* cast = dynamic_cast<CastSpellAction*>(action);
            if (!cast) return 1.0f;
            Unit* target = GetViscidusCombatTarget(ai);
            if (!target) return 1.0f;
            if (target->GetEntry() == 15667 && ai->GetBot()->GetClass() == CLASS_MAGE)
            {
                SpellEntry const* spell = sSpellTemplate.LookupEntry<SpellEntry>(cast->GetSpellID());
                if (spell && !IsPositiveSpell(spell) &&
                    (IsChanneledSpell(spell) || GetSpellCastTime(spell, ai->GetBot()))) return 0.0f;
            }
            if (ViscidusBlocksSpell(ai, cast->GetSpellID(), target)) return 0.0f;
            // Rank-one Frostbolt owns the non-frozen boss damage slot, but
            // never suppress buffs, heals, mana recovery or damage to globs.
            if (target->GetEntry() == 15299 && !target->HasAura(25937) &&
                ai->GetBot()->GetClass() == CLASS_MAGE && ai->GetBot()->HasSpell(116))
            {
                SpellEntry const* spell = sSpellTemplate.LookupEntry<SpellEntry>(cast->GetSpellID());
                if (spell && !IsPositiveSpell(spell) && cast->getThreatType() != ActionThreatType::ACTION_THREAT_NONE) return 0.0f;
            }
            return 1.0f;
        }
    };
}
void ViscidusGlobStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode("viscidus melee danger",
        NextAction::array(0, new NextAction("viscidus keep distance", ACTION_EMERGENCY + 7), nullptr)));
    triggers.push_back(new TriggerNode("viscidus toxin nearby",
        NextAction::array(0, new NextAction("viscidus toxin escape", ACTION_EMERGENCY + 8), nullptr)));
    triggers.push_back(new TriggerNode("viscidus combat needed",
        NextAction::array(0, new NextAction("viscidus combat", ACTION_HIGH + 8), nullptr)));
}
void ViscidusGlobStrategy::InitReactionTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode("viscidus melee danger",
        NextAction::array(0, new NextAction("viscidus keep distance", ACTION_EMERGENCY + 7), nullptr)));
    triggers.push_back(new TriggerNode("viscidus toxin nearby",
        NextAction::array(0, new NextAction("viscidus toxin escape", ACTION_EMERGENCY + 8), nullptr)));
}
void ViscidusGlobStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers) { InitCombatTriggers(triggers); }
void ViscidusGlobStrategy::InitCombatMultipliers(std::list<Multiplier*>& multipliers) { multipliers.push_back(new ViscidusMultiplier(ai)); }
void ViscidusGlobStrategy::InitReactionMultipliers(std::list<Multiplier*>& multipliers) { InitCombatMultipliers(multipliers); }
void ViscidusGlobStrategy::InitNonCombatMultipliers(std::list<Multiplier*>& multipliers) { InitCombatMultipliers(multipliers); }

bool ViscidusCombatTrigger::IsActive()
{
    if (!ViscidusEnabled(ai)) return false;
    Action* action = ai->GetAiObjectContext()->GetAction("viscidus combat");
    return action && action->isUseful();
}
