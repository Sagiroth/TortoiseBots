
#include "playerbot/playerbot.h"
#include "DpsTargetValue.h"
#include "RaidDpsTargetPolicy.h"
#include "playerbot/strategy/NefarianAddTargetPolicy.h"
#include "ViscidusGlobPolicy.h"
#include "playerbot/RaidDpsExclusionPolicy.h"
#include "playerbot/strategy/actions/NaxxramasDungeonActions.h"
#include "LeastHpTargetValue.h"
#include "AttackersValue.h"
#include "PossibleAttackTargetsValue.h"
#include "playerbot/policy/combat/DpsTargetPolicy.h"
#include "playerbot/FourHorsemenRotationPolicy.h"
#include "playerbot/GroupMembers.h"
#include "playerbot/policy/combat/RunnerFinishPolicy.h"

using namespace ai;

namespace
{
    // Four Horsemen mark rotation (FourHorsemenRotateAction): while the
    // bot's stored corner choice holds, its dps target is the horseman at
    // that corner. Without this "dps assist" resets the target to the tank's
    // horseman and the bot walks back into the mark it just left. A stay
    // order parks the bot, so the override stands down with it.
    Unit* FourHorsemenRotationTarget(PlayerbotAI* ai, Player* bot)
    {
        if (bot->GetMapId() != 533)
            return nullptr;
        AiObjectContext* context = ai->GetAiObjectContext();
        const int fromIdx = context->GetValue<int32>("manual int", kFourHorsemenFromKey)->Get() - 1;
        const int toIdx = context->GetValue<int32>("manual int", kFourHorsemenToKey)->Get() - 1;
        auto getStacks = [&](uint32 markSpell) -> uint32
        {
            Aura* aura = ai->GetAura(markSpell, bot);
            return aura ? aura->GetStackAmount() : 0;
        };
        auto findHorseman = [&](int idx) -> Unit*
        {
            for (ObjectGuid const& guid : context->GetValue<std::list<ObjectGuid>>("attackers")->Get())
            {
                Unit* unit = ai->GetUnit(guid);
                if (unit && unit->IsAlive() && unit->GetEntry() == kFourHorsemenEntries[idx])
                    return unit;
            }
            return nullptr;
        };
        return FourHorsemenRotationTargetRaw<Unit>(fromIdx, toIdx, getStacks, findHorseman,
            ai->HasStrategy("stay", BotState::BOT_STATE_COMBAT));
    }

    // Wasted DPS targets (mod-playerbots RaidMcStrategy::AppendTargetExclusions,
    // plus the vanilla Razuvious Understudies): Majordomo cannot die, Core
    // Ragers heal to full while Golemagg lives, and Understudies are the
    // priests' Mind Control tanks while Razuvious lives. Tanks still pick
    // them up through their own target selection.
    bool IsWastedDpsTarget(PlayerbotAI* ai, Unit* unit)
    {
        uint32 const entry = unit->GetEntry();
        uint32 boss = 0;
        if (entry == 12018)
            return true;
        if (entry == 11672)
            boss = 11988;
        else if (entry == 16803)
            boss = 16061;
        else
            return false;
        for (ObjectGuid const& guid : ai->GetAiObjectContext()->GetValue<std::list<ObjectGuid>>("attackers")->Get())
        {
            Unit* other = ai->GetUnit(guid);
            if (other && other->IsAlive() && other->GetEntry() == boss)
                return true;
        }
        return false;
    }


    // mod-playerbots DPS target tournament (LD-1/LD-4): lifetime buckets per
    // bot type instead of flat least-HP, CC-moon skip, skull snap. The
    // group-tank follow above stays: vanilla threat punishes independent
    // picks pulling aggro (LD-2 deliberately not ported).
    class DpsTournamentStrategy : public FindNonCcTargetStrategy
    {
    public:
        explicit DpsTournamentStrategy(PlayerbotAI* ai, float groupDps)
            : FindNonCcTargetStrategy(ai), groupDps(groupDps), foundHighPriority(false) {}
        using FindNonCcTargetStrategy::IsCcTarget;

    protected:
        // LD-4 skull snap (donor FindTargetStrategy::IsHighPriority,
        // TargetValue.cpp:124-133): a skull marked mid-fight pulls DPS off
        // the current mob at once; the sticky flag holds it for the rest of
        // the tournament. The `prioritized targets` half has no equivalent
        // here — skull only.
        bool IsHighPriority(Unit* attacker)
        {
            Group* group = ai->GetBot()->GetGroup();
            if (!group)
                return false;
            ObjectGuid guid = group->GetTargetIcon(7);
            return guid && attacker->getObjectGuid() == ObjectGuid(guid);
        }

        bool CheckSkull(Unit* attacker)
        {
            if (foundHighPriority)
                return true;
            if (IsHighPriority(attacker))
            {
                result = attacker;
                foundHighPriority = true;
                return true;
            }
            return false;
        }

        float Lifetime(Unit* unit) const
        {
            return groupDps > 0.0f ? (float)unit->GetHealth() / groupDps : 1000000.0f;
        }

        bool InRange(Unit* unit) const
        {
            float range = ai->IsRanged(ai->GetBot())
                ? sPlayerbotAIConfig.spellDistance
                : sPlayerbotAIConfig.meleeDistance;
            return ai->GetBot()->GetDistance(unit) < range + 5.0f;
        }

        Unit* CurrentTarget() const
        {
            return ai->GetAiObjectContext()->GetValue<Unit*>("current target")->Get();
        }

        float groupDps;
        bool foundHighPriority;
    };

    // Caster: skip CC-moon, prefer 5-30 s in range (sub-5 s ranks second,
    // never skipped), don't switch when everything is nearly dead (donor).
    class CasterDpsStrategy : public DpsTournamentStrategy
    {
    public:
        CasterDpsStrategy(PlayerbotAI* ai, float groupDps) : DpsTournamentStrategy(ai, groupDps) {}

        void CheckAttacker(Unit* attacker, ThreatManager*) override
        {
            if (!attacker || attacker->IsPlayer() || !attacker->IsAlive() || IsCcTarget(attacker) ||
                (IsWastedDpsTarget(ai, attacker) || IsRaidDpsTargetExcluded(ai, attacker)))
                return;
            if (CheckSkull(attacker))
                return;
            if (!result || IsBetter(attacker, result))
                result = attacker;
        }

        bool IsBetter(Unit* newUnit, Unit* oldUnit)
        {
            int newLevel = CasterTargetBucket(Lifetime(newUnit), InRange(newUnit));
            int oldLevel = CasterTargetBucket(Lifetime(oldUnit), InRange(oldUnit));
            if (newLevel != oldLevel)
                return newLevel > oldLevel;

            float newTime = Lifetime(newUnit);
            float oldTime = Lifetime(oldUnit);
            if (newLevel % 10 == 2 || newLevel % 10 == 0)
                return newTime < oldTime;

            Unit* current = CurrentTarget();
            if (current == newUnit)
                return true;
            if (current == oldUnit)
                return false;
            return newTime > oldTime;
        }
    };

    // General: in range + lowest lifetime, else closest (donor lines 158-215).
    class GeneralDpsStrategy : public DpsTournamentStrategy
    {
    public:
        GeneralDpsStrategy(PlayerbotAI* ai, float groupDps) : DpsTournamentStrategy(ai, groupDps) {}

        void CheckAttacker(Unit* attacker, ThreatManager*) override
        {
            if (!attacker || attacker->IsPlayer() || !attacker->IsAlive() || IsCcTarget(attacker) ||
                (IsWastedDpsTarget(ai, attacker) || IsRaidDpsTargetExcluded(ai, attacker)))
                return;
            if (CheckSkull(attacker))
                return;
            if (!result || IsBetter(attacker, result))
                result = attacker;
        }

        bool IsBetter(Unit* newUnit, Unit* oldUnit)
        {
            int newLevel = GeneralTargetBucket(InRange(newUnit));
            int oldLevel = GeneralTargetBucket(InRange(oldUnit));
            if (newLevel != oldLevel)
                return newLevel > oldLevel;
            if (newLevel == 10)
                return Lifetime(newUnit) < Lifetime(oldUnit);
            return ai->GetBot()->GetDistance(newUnit) < ai->GetBot()->GetDistance(oldUnit);
        }
    };

    // Combo (rogue / cat): stick the combo target, else lowest in range
    // (donor lines 218-279).
    class ComboDpsStrategy : public DpsTournamentStrategy
    {
    public:
        ComboDpsStrategy(PlayerbotAI* ai, float groupDps) : DpsTournamentStrategy(ai, groupDps) {}

        void CheckAttacker(Unit* attacker, ThreatManager*) override
        {
            if (!attacker || attacker->IsPlayer() || !attacker->IsAlive() || IsCcTarget(attacker) ||
                (IsWastedDpsTarget(ai, attacker) || IsRaidDpsTargetExcluded(ai, attacker)))
                return;
            if (CheckSkull(attacker))
                return;
            if (!result || IsBetter(attacker, result))
                result = attacker;
        }

        bool IsBetter(Unit* newUnit, Unit* oldUnit)
        {
            int newLevel = GeneralTargetBucket(InRange(newUnit));
            int oldLevel = GeneralTargetBucket(InRange(oldUnit));
            if (newLevel != oldLevel)
                return newLevel > oldLevel;

            Player* bot = ai->GetBot();
            if (newLevel == 10)
            {
                Unit* comboUnit = ai->GetUnit(bot->GetComboTargetGuid());
                if (newUnit == comboUnit)
                    return true;
                return Lifetime(newUnit) < Lifetime(oldUnit);
            }
            return bot->GetDistance(newUnit) < bot->GetDistance(oldUnit);
        }
    };

    // Encounter-specific selectors retain their least-health ordering.
    class DpsTargetStrategy : public FindLeastHpTargetStrategy
    {
    public:
        explicit DpsTargetStrategy(PlayerbotAI* ai) : FindLeastHpTargetStrategy(ai) {}
        using FindNonCcTargetStrategy::IsCcTarget;
        void CheckAttacker(Unit* attacker, ThreatManager* threatManager) override
        {
            if (!IsRaidDpsTargetExcluded(ai, attacker))
                FindLeastHpTargetStrategy::CheckAttacker(attacker, threatManager);
        }
    };

    bool IsComboBot(PlayerbotAI* ai, Player* bot)
    {
        if (bot->GetClass() == CLASS_ROGUE)
            return true;
        return bot->GetClass() == CLASS_DRUID && ai->HasAura("cat form", bot);
    }

    bool IsCasterBot(PlayerbotAI* ai, Player* bot)
    {
        return ai->IsRanged(bot) && bot->GetClass() != CLASS_HUNTER;
    }

    unsigned NearGroupCount(PlayerbotAI* ai, Player* bot)
    {
        unsigned count = 1;
        if (Group* group = bot->GetGroup())
        {
            for (Player* member : LiveGroupMembers(group))
            {
                if (!member || member == bot || !member->IsInWorld())
                    continue;
                if (member->GetMapId() != bot->GetMapId())
                    continue;
                if (member->GetDistance(bot) > sPlayerbotAIConfig.sightDistance)
                    continue;
                ++count;
            }
        }
        return count;
    }

    Unit* GetArlokkPantherTarget(PlayerbotAI* ai)
    {
        Player* bot = ai ? ai->GetBot() : nullptr;
        if (!bot || bot->GetMapId() != 309 || !bot->IsAlive() || !bot->IsInCombat() || ai->IsTank(bot) ||
            !ai->HasStrategy("arlokk panther focus", BotState::BOT_STATE_COMBAT))
            return nullptr;

        AiObjectContext* context = ai->GetAiObjectContext();
        std::list<ObjectGuid> const possible = AI_VALUE(std::list<ObjectGuid>, "possible attack targets");
        // Prowlers remain engaged while Arlokk vanishes out of the target list.
        DpsTargetStrategy strategy(ai);
        for (ObjectGuid const& guid : possible)
        {
            Unit* candidate = ai->GetUnit(guid);
            if (!candidate || candidate->GetEntry() != 15101 || !candidate->IsAlive() || !candidate->IsInCombat() ||
                !candidate->IsInMap(bot) || !PossibleAttackTargetsValue::IsValid(candidate, bot,
                    sPlayerbotAIConfig.sightDistance, false, false) || candidate == AI_VALUE(Unit*, "cc target"))
                continue;
            ThreatManager& threat = sServerFacade.GetThreatManager(candidate);
            strategy.CheckAttacker(candidate, &threat);
        }
        return strategy.GetResult();
    }

    Unit* GetMarliSpawnTarget(PlayerbotAI* ai)
    {
        Player* bot = ai ? ai->GetBot() : nullptr;
        if (!bot || bot->GetMapId() != 309 || !bot->IsAlive() || !bot->IsInCombat() || ai->IsTank(bot) ||
            !ai->HasStrategy("marli spawn focus", BotState::BOT_STATE_COMBAT))
            return nullptr;

        AiObjectContext* context = ai->GetAiObjectContext();
        std::list<ObjectGuid> const possible = AI_VALUE(std::list<ObjectGuid>, "possible attack targets");
        bool active = false;
        for (ObjectGuid const& guid : possible)
        {
            Unit* candidate = ai->GetUnit(guid);
            if (candidate && candidate->GetEntry() == 14510 && candidate->IsAlive() &&
                candidate->IsInCombat() && candidate->IsInMap(bot))
            {
                active = true;
                break;
            }
        }
        if (!active)
            return nullptr;

        DpsTargetStrategy strategy(ai);
        for (ObjectGuid const& guid : possible)
        {
            Unit* candidate = ai->GetUnit(guid);
            if (!candidate || candidate->GetEntry() != 15041 || !candidate->IsAlive() || !candidate->IsInCombat() ||
                !candidate->IsInMap(bot) || !PossibleAttackTargetsValue::IsValid(candidate, bot,
                    sPlayerbotAIConfig.sightDistance, false, false) || candidate == AI_VALUE(Unit*, "cc target"))
                continue;
            ThreatManager& threat = sServerFacade.GetThreatManager(candidate);
            strategy.CheckAttacker(candidate, &threat);
        }
        return strategy.GetResult();
    }

    Unit* GetThekalBalanceTarget(PlayerbotAI* ai)
    {
        Player* bot = ai ? ai->GetBot() : nullptr;
        if (!bot || bot->GetMapId() != 309 || !bot->IsAlive() || !bot->IsInCombat() || ai->IsTank(bot) ||
            !ai->HasStrategy("thekal balance", BotState::BOT_STATE_COMBAT))
            return nullptr;

        AiObjectContext* context = ai->GetAiObjectContext();
        GeneralDpsStrategy strategy(ai, 0.0f);
        Unit* chosen = nullptr;
        for (ObjectGuid const& guid : AI_VALUE(std::list<ObjectGuid>, "possible attack targets"))
        {
            Unit* candidate = ai->GetUnit(guid);
            // The core uses 14599 in phase one and changes to 14509 for the
            // tiger phase. Do not balance against the phase-two boss/tigers.
            if (!candidate || (candidate->GetEntry() != 14599 && candidate->GetEntry() != 11347 &&
                candidate->GetEntry() != 11348) || !candidate->IsAlive() || !candidate->IsInCombat() ||
                !candidate->IsInMap(bot) || !PossibleAttackTargetsValue::IsValid(candidate, bot,
                    sPlayerbotAIConfig.sightDistance, false, false) ||
                candidate == AI_VALUE(Unit*, "cc target") || strategy.IsCcTarget(candidate) ||
                IsRaidDpsTargetExcluded(ai, candidate))
                continue;
            if (!chosen || candidate->GetHealth() > chosen->GetHealth() ||
                (candidate->GetHealth() == chosen->GetHealth() && candidate->GetObjectGuid() < chosen->GetObjectGuid()))
                chosen = candidate;
        }
        return chosen;
    }

    Unit* GetViscidusGlobTarget(PlayerbotAI* ai)
    {
        Player* bot = ai ? ai->GetBot() : nullptr;
        if (!bot || !ShouldFocusViscidusGlob(bot->GetMapId(),
                ai->HasStrategy("viscidus glob focus", BotState::BOT_STATE_COMBAT),
                bot->IsAlive(), bot->IsInCombat(), ai->IsTank(bot), VISCIDUS_GLOB_ENTRY))
            return nullptr;

        AiObjectContext* context = ai->GetAiObjectContext();
        if (!context)
            return nullptr;

        // Glob AIs intentionally never engage attackers, so they are absent from
        // possible attack targets. During Viscidus's exploded phase use the
        // nearby hostile scan and validate attackability without an aggro gate.
        std::list<ObjectGuid> const nearbyTargets = AI_VALUE(std::list<ObjectGuid>, "possible targets no los");
        DpsTargetStrategy strategy(ai);
        Unit* chosen = nullptr;
        Unit* ccTarget = AI_VALUE(Unit*, "cc target");
        for (ObjectGuid const& guid : nearbyTargets)
        {
            Unit* candidate = ai->GetUnit(guid);
            if (!candidate ||
                !CanUseViscidusGlobTarget(bot->GetMapId(), true, bot->IsAlive(),
                    bot->IsInCombat(), ai->IsTank(bot), candidate->GetEntry(), [&]()
                    {
                        return sServerFacade.IsAlive(candidate) &&
                            PossibleAttackTargetsValue::IsPossibleTarget(candidate, bot,
                                sPlayerbotAIConfig.sightDistance, false) &&
                            !strategy.IsCcTarget(candidate) && candidate != ccTarget &&
                            !IsRaidDpsTargetExcluded(ai, candidate);
                    }))
                continue;

            if (!chosen || candidate->GetHealth() < chosen->GetHealth())
                chosen = candidate;
        }

        return chosen;
    }

    Unit* GetNefarianBoneConstructTarget(PlayerbotAI* ai)
    {
        Player* bot = ai ? ai->GetBot() : nullptr;
        if (!bot || bot->GetMapId() != 469 || !bot->IsAlive() || !bot->IsInCombat() || ai->IsTank(bot) ||
            !ai->HasStrategy("nefarian phase3", BotState::BOT_STATE_COMBAT))
            return nullptr;

        AiObjectContext* context = ai->GetAiObjectContext();
        Unit* nefarian = nullptr;
        std::list<ObjectGuid> const possibleTargets = AI_VALUE(std::list<ObjectGuid>, "possible attack targets");
        for (ObjectGuid const& guid : possibleTargets)
        {
            Unit* candidate = ai->GetUnit(guid);
            if (candidate && candidate->GetEntry() == 11583 && candidate->IsAlive() && candidate->IsInCombat())
            {
                nefarian = candidate;
                break;
            }
        }
        if (!nefarian)
            return nullptr;

        GeneralDpsStrategy strategy(ai, 0.0f);
        Unit* chosen = nullptr;
        for (ObjectGuid const& guid : possibleTargets)
        {
            Unit* candidate = ai->GetUnit(guid);
            if (!candidate || candidate->GetEntry() != 14605 || !candidate->IsAlive() ||
                !PossibleAttackTargetsValue::IsValid(candidate, bot, sPlayerbotAIConfig.sightDistance, false, false) ||
                candidate == AI_VALUE(Unit*, "cc target") || strategy.IsCcTarget(candidate))
                continue;
            if (!chosen || candidate->GetObjectGuid() < chosen->GetObjectGuid())
                chosen = candidate;
        }

        return NefarianBoneFocusActive(ai->HasStrategy("nefarian phase3", BotState::BOT_STATE_COMBAT),
            true, bot->IsAlive(), bot->IsInCombat(), ai->IsTank(bot), nefarian->IsAlive(), nefarian->IsInCombat(),
            nefarian->GetHealthPercent(), chosen != nullptr) ? chosen : nullptr;
    }

    class NefarianInfernalTargetStrategy : public FindLeastHpTargetStrategy
    {
    public:
        explicit NefarianInfernalTargetStrategy(PlayerbotAI* ai) : FindLeastHpTargetStrategy(ai) {}

        void CheckAttacker(Unit* attacker, ThreatManager* threatManager) override
        {
            AiObjectContext* context = ai->GetAiObjectContext();
            if (attacker && attacker->GetEntry() == NEFARIAN_CORRUPTED_INFERNAL_ENTRY &&
                attacker != AI_VALUE(Unit*, "cc target") && !IsRaidDpsTargetExcluded(ai, attacker))
                FindLeastHpTargetStrategy::CheckAttacker(attacker, threatManager);
        }
    };

    bool IsAttackedByParty(Unit* target, Group* group)
    {
        for (Player* member : LiveGroupMembers(group))
        {
            if (member->GetVictim() == target)
                return true;

            Unit* pet = member->GetPet();
            if (pet && pet->GetVictim() == target)
                return true;
        }

        return false;
    }

    Unit* GetGroupTankTarget(PlayerbotAI* ai)
    {
        Player* bot = ai ? ai->GetBot() : nullptr;
        Group* group = bot ? bot->GetGroup() : nullptr;
        if (!bot || !group)
            return nullptr;

        PlayerbotAIStorage& storage = PlayerbotAIStorage::Instance();
        Player* master = ai->GetMaster();
        bool useHumanMasterTarget = master && master != bot && master->GetGroup() == group &&
            !storage.GetAI(master) && PlayerbotAI::IsTank(master);
        Player* tank = useHumanMasterTarget ? master : nullptr;

        if (!tank)
        {
            for (Player* member : LiveGroupMembers(group))
            {
                if (!storage.GetAI(member) || !PlayerbotAI::IsTank(member))
                    continue;

                tank = member;
                break;
            }
        }

        if (!tank)
            return nullptr;

        Unit* target = useHumanMasterTarget
            ? ai->GetUnit(master->GetSelectionGuid())
            : tank->GetVictim();
        if (!target || !sServerFacade.IsAlive(target) ||
            !PossibleAttackTargetsValue::IsValid(target, bot, sPlayerbotAIConfig.sightDistance, false, false) ||
            !bot->IsWithinLOSInMap(target) ||
            (!target->IsInCombat() && !IsAttackedByParty(target, group)))
            return nullptr;

        // The tank's victim skips the attackers list, so a mob this bot set
        // aside in a creature aura keep-out came straight back here (the
        // tank holds it in the ring): hold 3 s, set aside, re-pick, all
        // fight. Refuse it until the set-aside runs out.
        if (AttackersValue::IsKeepoutSetAside(target, bot))
            return nullptr;

        return target;
    }

    // A mob running from the fight: the fleeing state (fear-style flee, the
    // timed flee) or the core's flee-for-assistance walk and its pause at the
    // helper (Creature::DoFleeToGetAssistance), which carries no fleeing state.
    bool IsRunningMob(Unit* unit)
    {
        if (unit->HasUnitState(UNIT_STAT_FLEEING))
            return true;
        switch (unit->GetMotionMaster()->GetCurrentMovementGeneratorType())
        {
            case FLEEING_MOTION_TYPE:
            case TIMED_FLEEING_MOTION_TYPE:
            case ASSISTANCE_MOTION_TYPE:
            case ASSISTANCE_DISTRACT_MOTION_TYPE:
                return true;
            default:
                return false;
        }
    }

    // A gap closer castable on this mob right now. In 1.12 only Intercept
    // counts: Charge is out-of-combat only, and Feral Charge needs bear form
    // (bears tank, and tanks never chase runners). Intercept still needs
    // Berserker Stance, rage and its cooldown, which CanCastSpell checks; its
    // range is the runner leash, so range is left to the policy.
    bool GapCloserReady(PlayerbotAI* ai, Player* bot, Unit* unit)
    {
        return bot->GetClass() == CLASS_WARRIOR &&
            ai->CanCastSpell("intercept", unit, 0, nullptr, true);
    }

    // Finish a mob of the party's fight that runs away at low health
    // (RunnerFinishPolicy): ranged in range, melee with a gap closer on a short
    // leash; during a hold ("stay") only what the bot reaches from its spot.
    // The current target wins a tie, so a runner is never dropped for a fresh
    // mob. Reads the cached attackers list only.
    Unit* FinishRunnerTarget(PlayerbotAI* ai, Player* bot, Unit* current, GeneralDpsStrategy& ccProbe)
    {
        ai::RunnerChaser chaser;
        chaser.healer = PlayerbotAI::IsHeal(bot);
        chaser.tank = PlayerbotAI::IsTank(bot);
        if (chaser.healer || chaser.tank)
            return nullptr;
        chaser.ranged = ai->IsRanged(bot);
        chaser.holding = ai->HasStrategy("stay", BotState::BOT_STATE_COMBAT);
        chaser.spellRange = ai->GetRange("spell");
        chaser.meleeReach = sPlayerbotAIConfig.meleeDistance + 5.0f;

        Unit* const ccTarget = ai->GetAiObjectContext()->GetValue<Unit*>("cc target")->Get();
        Unit* best = nullptr;
        ai::RunnerFacts bestFacts;
        for (ObjectGuid const& guid : ai->GetAiObjectContext()->GetValue<std::list<ObjectGuid>>("attackers")->Get())
        {
            Unit* unit = ai->GetUnit(guid);
            if (!unit || unit->IsPlayer() || !unit->IsInWorld() || unit->GetMapId() != bot->GetMapId())
                continue;
            ai::RunnerFacts facts;
            facts.alive = unit->IsAlive();
            facts.inPartyFight = true;
            facts.running = IsRunningMob(unit);
            facts.feared = unit->HasAuraType(SPELL_AURA_MOD_FEAR);
            facts.healthPct = unit->GetHealthPercent();
            facts.distance = bot->GetDistance(unit);
            facts.ccTarget = unit == ccTarget || ccProbe.IsCcTarget(unit);
            if (!ai::IsFinishableRunner(facts))
                continue;
            // The gap closer is judged per mob, and only for a melee bot.
            ai::RunnerChaser mobChaser = chaser;
            mobChaser.hasGapCloser = !chaser.ranged && GapCloserReady(ai, bot, unit);
            if (!ai::ShouldFinishRunner(facts, mobChaser))
                continue;
            if (IsWastedDpsTarget(ai, unit) || AttackersValue::IsKeepoutSetAside(unit, bot))
                continue;
            if (chaser.ranged && !bot->IsWithinLOSInMap(unit))
                continue;
            if (!best || ai::RunnerBetter(facts, unit == current, bestFacts, best == current))
            {
                best = unit;
                bestFacts = facts;
            }
        }
        return best;
    }
}



Unit* DpsTargetValue::Calculate()
{
    // Explicit orders and raid marks remain ahead of tank assistance.
    if (Unit* explicitTarget = GetExplicitAttackTarget())
        return explicitTarget;

    // A Four Horsemen mark carrier fights the horseman at its new corner
    // (a raid mark on another corner would walk it back into the mark).
    if (Unit* horseman = FourHorsemenRotationTarget(ai, bot))
        return horseman;

    // A raid mark set aside in a creature aura keep-out falls through to
    // the next pick like the tank's victim below.
    Unit* rti = RtiTargetValue::Calculate();
    if (rti && !AttackersValue::IsKeepoutSetAside(rti, bot)) return rti;

    if (Unit* panther = GetArlokkPantherTarget(ai))
        return panther;

    if (Unit* spawn = GetMarliSpawnTarget(ai))
        return spawn;

    if (Unit* priest = GetThekalBalanceTarget(ai))
        return priest;

    if (Unit* construct = GetNefarianBoneConstructTarget(ai))
        return construct;

    if (Unit* glob = GetViscidusGlobTarget(ai))
        return glob;

    if (Unit* wrap = GetMaexxnaRescueTarget(ai))
        return wrap;

    if (Unit* whelp = GetOnyxiaWhelpTarget(ai))
        return whelp;

    GeneralDpsStrategy ccProbe(ai, 0.0f);

    // A nearly dead mob running from the fight comes before the tank's victim:
    // left alone it fetches help or leaves an event wave unfinished.
    if (Unit* runner = FinishRunnerTarget(ai, bot, AI_VALUE(Unit*, "current target"), ccProbe))
        return runner;

    if (Unit* tankTarget = GetGroupTankTarget(ai))
    {
        if (!ai->IsTank(bot) && ai->HasStrategy("nefarian infernal focus", BotState::BOT_STATE_COMBAT) &&
            IsNefarianClassCallAdd(bot->GetMapId(), tankTarget->GetEntry(), NEFARIAN_CORRUPTED_INFERNAL_ENTRY))
        {
            NefarianInfernalTargetStrategy infernalStrategy(ai);
            if (Unit* infernal = TargetValue::FindTarget(&infernalStrategy))
                return infernal;
        }

        if (tankTarget != AI_VALUE(Unit*, "cc target") && !ccProbe.IsCcTarget(tankTarget) &&
            !IsWastedDpsTarget(ai, tankTarget) && !IsRaidDpsTargetExcluded(ai, tankTarget))
            return tankTarget;
    }

    // mod-playerbots picks the tournament by bot type in groups of 4+
    // (DpsTargetValue.cpp:281-295); small groups always run the general pick.
    unsigned nearCount = NearGroupCount(ai, bot);
    float groupDps = AI_VALUE(float, "estimated group dps");
    // Every tournament skips the CC marks; when they are all that is left,
    // the marked mob is killed last instead of never (FindLastCcTarget).
    if (nearCount > 3 && IsCasterBot(ai, bot))
    {
        CasterDpsStrategy strategy(ai, groupDps);
        if (Unit* target = TargetValue::FindTarget(&strategy))
            return target;
        return FindLastCcTarget();
    }
    if (nearCount > 3 && IsComboBot(ai, bot))
    {
        ComboDpsStrategy strategy(ai, groupDps);
        if (Unit* target = TargetValue::FindTarget(&strategy))
            return target;
        return FindLastCcTarget();
    }

    GeneralDpsStrategy strategy(ai, groupDps);
    if (Unit* target = TargetValue::FindTarget(&strategy))
        return target;
    return FindLastCcTarget();
}

class FindMaxHpTargetStrategy : public FindNonCcTargetStrategy
{
public:
    FindMaxHpTargetStrategy(PlayerbotAI* ai) : FindNonCcTargetStrategy(ai)
    {
        maxHealth = 0;
    }

public:
    virtual void CheckAttacker(Unit* attacker, ThreatManager* threatManager) override
    {
        // Same CC-mark rule as every other picker: a protected mark is
        // skipped, a free one is a normal enemy.
        if (IsCcTarget(attacker))
            return;
        if (IsRaidDpsTargetExcluded(ai, attacker))
            return;
        if (!result || result->GetHealth() < attacker->GetHealth())
            result = attacker;
    }

protected:
    float maxHealth;
};

Unit* DpsAoeTargetValue::Calculate()
{
    if (Unit* explicitTarget = GetExplicitAttackTarget())
        return explicitTarget;

    Unit* rti = RtiTargetValue::Calculate();
    if (rti && !AttackersValue::IsKeepoutSetAside(rti, bot)) return rti;

    if (Unit* construct = GetNefarianBoneConstructTarget(ai))
        return construct;

    if (Unit* wrap = GetMaexxnaRescueTarget(ai))
        return wrap;

    if (Unit* whelp = GetOnyxiaWhelpTarget(ai))
        return whelp;

    FindMaxHpTargetStrategy strategy(ai);
    return TargetValue::FindTarget(&strategy);
}
