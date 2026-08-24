#pragma once
//
// SpecComposition — boilerplate-eliminating macros for per-spec strategy
// subclasses.
//
// The pattern across every class strategy file (BalanceDruidStrategy.cpp,
// ArmsWarriorStrategy.cpp, AfflictionWarlockStrategy.cpp, etc.) is:
//
//   class XSpecPveStrategy   inherits XSpecStrategy AND ClassPveStrategy
//   class XSpecPvpStrategy   inherits XSpecStrategy AND ClassPvpStrategy
//   class XSpecRaidStrategy  inherits XSpecStrategy AND ClassRaidStrategy
//
// And each Pve/Pvp/Raid subclass redefines all four trigger-init methods
// (InitCombatTriggers, InitNonCombatTriggers, InitReactionTriggers,
// InitDeadTriggers) to do nothing but call the same method on both parents.
//
// Without these macros, every spec file repeats 4 trivial methods × 3 spec
// variants × N mixin families (base / Aoe / Buff / Boost / Cc / Cure) ×
// 3 expansion ifdef blocks (MANGOSBOT_ZERO/ONE/TWO) — yielding ~600+ lines
// of identical pass-through boilerplate per file.
//
// With the macro: one line per spec.
//
// Example, before:
//
//   void BalanceDruidPveStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers) {
//       BalanceDruidStrategy::InitCombatTriggers(triggers);
//       DruidPveStrategy::InitCombatTriggers(triggers);
//   }
//   void BalanceDruidPveStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers) {
//       BalanceDruidStrategy::InitNonCombatTriggers(triggers);
//       DruidPveStrategy::InitNonCombatTriggers(triggers);
//   }
//   void BalanceDruidPveStrategy::InitReactionTriggers(std::list<TriggerNode*>& triggers) {
//       BalanceDruidStrategy::InitReactionTriggers(triggers);
//       DruidPveStrategy::InitReactionTriggers(triggers);
//   }
//   void BalanceDruidPveStrategy::InitDeadTriggers(std::list<TriggerNode*>& triggers) {
//       BalanceDruidStrategy::InitDeadTriggers(triggers);
//       DruidPveStrategy::InitDeadTriggers(triggers);
//   }
//
// After:
//
//   SPEC_COMPOSE_4(BalanceDruidPveStrategy, BalanceDruidStrategy, DruidPveStrategy)
//
// The "_4" suffix means "all four init methods". Some subclasses only need
// 2 (Combat + NonCombat) — use SPEC_COMPOSE_2 for those.

// All four init methods — Combat, NonCombat, Reaction, Dead
#define SPEC_COMPOSE_4(child, base1, base2)                                 \
    void child::InitCombatTriggers(std::list<TriggerNode*>& t) {            \
        base1::InitCombatTriggers(t);                                       \
        base2::InitCombatTriggers(t);                                       \
    }                                                                       \
    void child::InitNonCombatTriggers(std::list<TriggerNode*>& t) {         \
        base1::InitNonCombatTriggers(t);                                    \
        base2::InitNonCombatTriggers(t);                                    \
    }                                                                       \
    void child::InitReactionTriggers(std::list<TriggerNode*>& t) {          \
        base1::InitReactionTriggers(t);                                     \
        base2::InitReactionTriggers(t);                                     \
    }                                                                       \
    void child::InitDeadTriggers(std::list<TriggerNode*>& t) {              \
        base1::InitDeadTriggers(t);                                         \
        base2::InitDeadTriggers(t);                                         \
    }

// Combat + NonCombat only — used by Aoe / Buff / Boost / Cc / Cure family
// strategies that don't override Reaction or Dead.
#define SPEC_COMPOSE_2(child, base1, base2)                                 \
    void child::InitCombatTriggers(std::list<TriggerNode*>& t) {            \
        base1::InitCombatTriggers(t);                                       \
        base2::InitCombatTriggers(t);                                       \
    }                                                                       \
    void child::InitNonCombatTriggers(std::list<TriggerNode*>& t) {         \
        base1::InitNonCombatTriggers(t);                                    \
        base2::InitNonCombatTriggers(t);                                    \
    }
