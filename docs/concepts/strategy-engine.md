---
id: concept-strategy-engine
title: Strategy Engine & Action Scheduling
category: concepts
summary: Deep dive into the Playerbots AI cycle, action baskets, triggers, multipliers, reaction queues, and failure backoff.
tags: [architecture, engine, ai, triggers, actions, strategies]
relates_to:
  - concept-architecture-invariants
  - concept-donor-hierarchy
---

# Strategy Engine & Action Scheduling

TortoiseBots uses an action-scheduling engine derived from Playerbots. Rather than running a monolithic behavior tree or hardcoded FSM, bot behavior emerges from composable **Strategies**, **Triggers**, **Actions**, and **Multipliers**.

## The Execution Cycle

On every bot update tick (`PlayerbotAI::UpdateAI`), the engine executes the following loop:

```text
┌────────────────────────────────────────────────────────┐
│               Trigger Evaluation Phase                 │
│  Iterate active triggers across all attached strategies │
│  (e.g., TargetHealthBelow25, LowMana, CCMarkActive)   │
└──────────────────────────┬─────────────────────────────┘
                           │ Active Triggers Emit Actions
                           ▼
┌────────────────────────────────────────────────────────┐
│               Relevance Calculation                    │
│  Action base relevance (0.0 to 100.0)                  │
│  Multipliers adjust values based on context/role       │
└──────────────────────────┬─────────────────────────────┘
                           │ Sort & Insert
                           ▼
┌────────────────────────────────────────────────────────┐
│                   Action Basket                        │
│  Highest relevance action selected; ties keep older    │
└──────────────────────────┬─────────────────────────────┘
                           │ Execution Attempt
                           ▼
┌────────────────────────────────────────────────────────┐
│                Execution & Backoff                     │
│  Action::Execute() -> returns true / false             │
│  Failures trigger bounded backoff to prevent loops     │
└────────────────────────────────────────────────────────┘
```

## Core Building Blocks

### 1. Strategies
A strategy represents a high-level posture or intent (e.g., `frost mage combat`, `heal`, `stay`, `pullback`). Each strategy registers:
- Triggers to watch.
- Actions to invoke when those triggers fire.
- Default actions to fall back to when no trigger is active.

### 2. Triggers
Triggers test conditions on the bot, the environment, or party members:
- `CriticalHealthTrigger`: Party member health < 25%.
- `LowManaTrigger`: Bot mana < 20%.
- `EnemyCastTrigger`: Current target is casting an interruptible spell.

### 3. Actions
Actions perform discrete steps in the world:
- Cast spell (e.g. `CastFlashHealAction`, `CastFrostboltAction`).
- Movement (e.g. `FollowAction`, `FleeAction`, `ReachSpellAction`).
- Utility (e.g. `EatAction`, `DrinkAction`, `LootAction`).

### 4. Multipliers
Multipliers dynamically alter the relevance score of actions. For example, if a bot is assigned the `tank` role, threat-generating actions receive positive multipliers while defensive kiting actions receive suppression multipliers.

### 5. Failure Backoff
If an action fails (e.g., target out of line of sight, spell on cooldown, target invalid), the engine records failure in `ActionFailureBackoff`. This prevents bots from busy-looping and spamming identical failed cast packets every frame.
