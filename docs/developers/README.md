---
id: developers
title: Developer Documentation
category: developers
summary: Architecture, strategy engine internals, host API boundary, donor lineage, and development guides.
tags: [developer, architecture, host-api, internals]
---

# Developer Documentation

Technical documentation, architecture invariants, strategy engine details, host integration seams, and contributor resources for the TortoiseBots module.

## Core Documentation

| Document | Topic |
| :--- | :--- |
| [Architecture & Invariants](architecture.md) | Module boundaries, core isolation invariants, and design rules. |
| [Strategy Engine](strategy-engine.md) | Autonomous decision loop, triggers, actions, multipliers, and tick scheduling. |
| [Bot Mechanics & Internals](bot-mechanics.md) | Deep bot mechanics, targeting rules, threat handling, and internal technical edge cases. |
| [Donor Hierarchy & Lineage](donor-hierarchy.md) | Codebase lineage, porting priorities, and behavior harvesting principles. |
| [Host API & Integration Boundary](host-api.md) | Host seam contracts, headless sessions, lifecycle hooks, and packet bridge. |
| [Licensing Audit & Provenance](licensing-audit.md) | Donor licensing verification, GPL compatibility, and release gates. |
| [Guild Supply Chain](guild-supply.md) | How guild orders use the guild bank as the crafting store. |

## Contribution Guidelines & Tools

- [Contributing Guidelines](../../CONTRIBUTING.md): Workflow, branching conventions, and submission requirements.
- [Agentic Guidelines & Invariants](../../AGENTS.md): Coding rules, validation expectations, and donor lookup rules.
- [Hire Gear Generator](../../tools/hire_gear/README.md): Offline gear and item level scaling tools.
- [Gear Enchants Generator](../../tools/hire_gear/enchants/README.md): Tier-scaled enchantment generator tools.
- [Observability Daemon](../../tools/observability/README.md): Telemetry pipeline, Prometheus metrics, and live monitoring.
