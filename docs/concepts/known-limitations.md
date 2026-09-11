---
id: concept-known-limitations
title: Known Limitations & Non-Goals
category: concepts
summary: Authoritative list of current engine boundaries, blocked features, deferred work, and explicit non-goals.
tags: [architecture, limitations, non-goals, engine]
relates_to:
  - concept-architecture-invariants
  - concept-strategy-engine
---

# Known Limitations & Non-Goals

This document provides an honest, authoritative record of current architectural boundaries, pending runtime verifications, and deliberate non-goals in TortoiseBots.

## 1. Blocked & Incomplete Features

1. **Self-Bot (Controlling Your Own Character as a Bot):**
   - No Network-session attach seam exists in the core server.
   - Native PlayerAI is not a control seam; converting a Network session into a Headless session mid-connection would violate session integrity.
2. **Synthetic Auction Market (Empty Market Population):**
   - Personal bot auction posting and bidding works using real bot inventories.
   - Full synthetic market seeding (server-generated trade goods out of thin air) requires a dedicated core snapshot seam (`MarketPost`/`Bid`/`Expire`).
3. **Early Loot Admission Check:**
   - The core currently lacks a pre-movement `CanLoot` query, so bots may occasionally approach a corpse before discovering it has no valid personal loot slot.

## 2. Design Decisions & Architectural Warts

- **Summon Sequencing:** Re-binding the master occurs before follow movement is resumed to guarantee ownership cannot be hijacked during a teleport transition.
- **Out-of-AH Inventory Queries:** Bots check their inventory values using cached vendor and pricing models rather than sending world-wide auction queries.
- **Totem Range Churn:** Shamans occasionally refresh totems when moving between combat anchors.

## 3. Explicit Non-Goals (What We Deliberately Do Not Do)

- **Autonomous Dungeon Clear (`mod-dungeon-clear` / `.dc` commands):**
  - Autonomous bot pathfinding that clears entire dungeons without a human leader is explicitly excluded. Dungeons are designed for a human player leading companion bots.
- **Expansion Mechanics (TBC / WotLK):**
  - No Death Knights, glyphs, vehicles, Arena ratings, or expansion zones (e.g. Outland, Northrend).
  - All classes are strictly Vanilla 1.12 with Turtle WoW 1.18.1 additions.
- **Synchronous LLM in Combat or Movement:**
  - LLM integration is experimental and strictly asynchronous for chat. Combat, movement, healing, threat, and interrupts never wait on or depend on LLM generation.
- **Guild Vaults:**
  - Turtle WoW 1.18.1 does not feature multi-tab WotLK-style guild vaults; all guild bank actions fail closed gracefully.
