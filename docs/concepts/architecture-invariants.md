---
id: concept-architecture-invariants
title: Architecture Invariants & Core Boundaries
category: concepts
summary: Non-negotiable architectural rules governing TortoiseBots modularity, headless sessions, and zero-core-coupling.
tags: [architecture, invariants, core, headless, modularity]
relates_to:
  - concept-strategy-engine
  - concept-donor-hierarchy
  - concept-known-limitations
---

# Architecture Invariants & Core Boundaries

TortoiseBots adheres to strict architectural rules to prevent the severe code coupling that crippled earlier PlayerBots implementations.

## The 5 Core Invariants

### 1. 100% Modular Native C++ Module
The core server ([tortoise-wow](https://github.com/tortoise-wow/tortoise-wow)) must compile cleanly without TortoiseBots enabled (`-DMODULES=disabled` or without `-DMODULE_TORTOISEBOTS=static`). TortoiseBots lives entirely inside `modules/TortoiseBots/`.

### 2. Zero Core Coupling (No `GetBot()` or `m_bot`)
Never reintroduce direct bot references into core engine code:
* ❌ No `WorldSession::GetBot()` or `Player::m_bot`.
* ❌ No scattered `if (IsBot())` checks in core combat, movement, or spell systems.
* ✅ All bot state, AI decision engines, strategies, and inventories are owned exclusively by the `TortoiseBots` module.

### 3. Generic Headless Sessions (`SessionTransport::Headless`)
Bot sessions are not special core subclasses. They are standard `WorldSession` instances backed by `SessionTransport::Headless`:
* One account can have at most one Network session + $N$ Headless sessions.
* **Human Reclaim Always Wins:** If a human logs into an account while a bot is active from that account, the bot is cleanly logged out and human control takes precedence immediately.
* Headless sessions never manipulate `LoginDatabase` online account status, preserving normal network authentication boundaries.

### 4. Narrow, Centralized Host Boundary
All interaction between the module and core server passes through explicit adapters in `host/`:
* `BotSessionAdapter`: Manages headless session allocation and termination via `World::StartHeadlessSession`.
* `BotPacketAdapter`: Handles packet routing and interception.
* `BotPlayerAdapter`: Interacts with standard `Player` objects through native server APIs.

### 5. Asynchronous LLM Isolation
LLM-based chat interactions are purely asynchronous and decoupled. If an LLM backend times out or fails, combat AI, movement, healing, interrupts, and crowd control continue running with zero interruption or frame hitching.
