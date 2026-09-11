---
id: concept-donor-hierarchy
title: Donor Hierarchy & Porting Rules
category: concepts
summary: Evidence priority and architectural guidelines when referencing shyalya-tortoise-wow, mod-playerbots, and upstream core.
tags: [donors, shyalya-tortoise-wow, mod-playerbots, porting, rules]
relates_to:
  - concept-architecture-invariants
  - concept-strategy-engine
---

# Donor Hierarchy & Porting Rules

When implementing or diagnosing behavior in TortoiseBots, code must not be blindly copied. Different donor repositories serve strictly different purposes.

## Evidence Hierarchy

When investigating bugs or porting features, evaluate evidence in this order:

1. **Actual Tortoise WoW Client / Server Runtime Behavior** (Ground Truth).
2. **shyalya-tortoise-wow Implementation** ([Shyalya/tortoise-wow](https://github.com/Shyalya/tortoise-wow)):
   - **Primary runtime oracle for Turtle WoW 1.18.1 PlayerBots semantics.**
   - Consult first for: engine lifecycle, action queue behavior, movement/`MotionMaster` interactions, Turtle-specific DBC/spell adaptations, and packet hook ordering.
   - *Never copy its core coupling (`m_bot`, `WorldSession::GetBot()`).*
3. **Current Core Server Semantics** ([tortoise-wow](https://github.com/Penqle/tortoise-wow)).
4. **mod-playerbots** ([mod-playerbots/mod-playerbots](https://github.com/mod-playerbots/mod-playerbots)):
   - **Primary mature behavior donor.**
   - Consult first for: combat rotations, healing prioritization, interrupt selections, CC logic, dungeon behavior, and rich tactics.
   - Strip all TBC/WotLK assumptions before porting.

## Porting Rules

```text
Turtle runtime mechanics  → shyalya-tortoise-wow first
Mature gameplay behavior  → mod-playerbots first
Architecture ownership    → TortoiseBots rules always win
```

### Harvest Behavior, Never Coupling
- Never copy global bot managers, baked-in player pointers, or core-level bot checks.
- Keep all state and logic inside `modules/TortoiseBots/`.
- Record all donor inspirations, file paths, and commit hashes in `docs/reference/donor-map.tsv` and `docs/PROVENANCE.md`.
