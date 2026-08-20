# PLAN.md — Decoupled PlayerBots Module for Tortoise WoW 1.18.1

**Status:** Proposed / experimental  
**Target core:** `Penqle/tortoise-wow` after the legacy PlayerBots cleanup (PR #396)  
**Primary goal:** Build useful PlayerBots for Turtle WoW 1.18.1 while keeping the Tortoise core as clean and bot-agnostic as reasonably possible.  
**Initial player scenario:** A small number of human players can use bots to complete normal world content and 5-player dungeons.  
**Long-term scenario:** Dungeons, raids, travel/questing, battlegrounds, optional random bots, and contextual conversation.

---

# 1. Why this project exists

The old PlayerBots implementation was tightly coupled to the core:

- `WorldSession::GetBot()`
- `m_bot`
- `sPlayerBotMgr`
- PlayerBots-specific branches inside normal gameplay code
- PlayerBots-specific socket/session exceptions
- CMake/config/SQL wiring spread across the core

PR #396 removes that legacy implementation and gives us a clean starting point.

Do **not** recreate the same architecture.

The objective is to build a PlayerBots system that behaves much more like a module:

```text
Penqle/tortoise-wow
        |
        | small, stable host interface
        v
tortoise-wow-playerbots
        |
        +-- bot runtime
        +-- movement
        +-- combat
        +-- group coordination
        +-- class behavior
        +-- Turtle 1.18.1 data
        +-- optional LLM/chat layer
```

The core should not need to understand bot strategies, classes, commands, rotations, personalities, travel logic, dungeon logic, or LLM behavior.

---

# 2. Non-negotiable architecture rules

These rules take priority over feature speed.

## Rule 1 — No scattered bot checks in core

Never rebuild patterns such as:

```cpp
if (player->GetSession()->GetBot())
```

or:

```cpp
if (isBot)
```

throughout `Player`, `Unit`, `Spell`, `WorldSession`, movement, groups, etc.

If normal core code starts accumulating bot-specific branches, stop and redesign the boundary.

---

## Rule 2 — Bots OFF must be a first-class build

The normal Tortoise core must build and run without the PlayerBots module.

Desired configuration:

```text
BUILD_PLAYERBOTS=OFF
```

by default.

The PlayerBots module must not be required for an ordinary server build.

Do not promise bit-for-bit identical binaries unless this is actually verified. The practical requirement is:

- no runtime PlayerBots dependency
- no PlayerBots config requirement
- no PlayerBots SQL requirement
- no module repository required
- normal server behavior remains unchanged

---

## Rule 3 — Prefer existing extension hooks

Before adding any core hook, inspect the current Tortoise script/event infrastructure.

Prefer existing mechanisms such as:

- world lifecycle hooks
- player login/logout hooks
- chat hooks
- command registration
- group events
- map/world updates
- ScriptMgr-style extension points

Do **not** add a custom PlayerBots hook if an existing general-purpose extension point already provides the required event.

---

## Rule 4 — New core integration must be centralized

If the core genuinely lacks a required extension point, add the smallest possible host seam.

All PlayerBots-specific integration must be concentrated in a very small number of clearly named bridge/factory files.

Target:

```text
core files directly aware of PlayerBots integration: <= 5 if practical
```

Treat exceeding roughly 8–10 host files before the MVP works as an architectural warning.

Do not create dozens of host-hook modifications like the previous CMaNGOS graft.

---

## Rule 5 — Session differences are transport differences, not gameplay differences

Bots will probably require a player/session without a normal network socket.

This is the hardest integration seam and must be handled deliberately.

Do **not** reintroduce:

```cpp
WorldSession::GetBot()
m_bot
"<BOT>"
bot-specific socket exceptions scattered through WorldSession
```

Instead investigate a centralized abstraction such as:

```text
normal network session
headless/synthetic session
```

or an explicit factory:

```text
CreateNetworkSession(...)
CreateHeadlessSession(...)
```

Core logic should ask about the capability it actually needs, for example:

```text
HasNetworkTransport()
CanReceiveClientPackets()
```

rather than asking:

```text
IsBot()
```

Any behavior that is truly bot-specific belongs in the module.

---

## Rule 6 — Bot behavior belongs in the module

The module owns:

- bot identity/runtime ownership
- master/owner relationship
- commands
- strategies
- actions
- triggers
- class behavior
- combat decisions
- follow/formation logic
- loot decisions
- travel
- quest behavior
- dungeon/raid encounter behavior
- random bot systems
- LLM conversation
- bot-specific diagnostics

The core must not own these concepts.

---

## Rule 7 — LLMs never participate in the real-time combat tick

Combat must work with:

```text
zero internet
zero LLM
zero external service
```

LLM functionality is optional and asynchronous.

Good LLM responsibilities:

- conversational responses
- personality
- party banter
- natural-language command interpretation
- explaining quests/items
- contextual comments

Bad LLM responsibilities:

- deciding whether to heal this tick
- interrupt timing
- threat calculation
- movement every frame
- blocking a map/world thread while waiting for HTTP

---

# 3. Source material and how to use it

We have several valuable sources, but they serve different purposes.

## A. TortoiseWoWKnowledgeBase — behavior/specification oracle

Repository:

```text
https://github.com/tortoise-wow-stack/TortoiseWoWKnowledgeBase
```

Agent guide:

```text
https://github.com/tortoise-wow-stack/TortoiseWoWKnowledgeBase/blob/main/AGENTS.md
```

PlayerBots knowledge:

```text
https://github.com/tortoise-wow-stack/TortoiseWoWKnowledgeBase/tree/main/playerbots
```

The knowledge base currently states that its PlayerBots capability baseline is pinned to:

```text
172ee948e591f8bf1b53ea6389e3102186339f6e
```

and routes bot work through:

```text
playerbots/index.md
playerbots/capability-map.md
```

with focused documentation for:

- console commands
- chat commands
- audience filters
- addon transport
- actions / strategies
- configuration
- ownership / security / failures
- lifecycle

### Important

Treat the knowledge base as:

```text
behavioral specification
capability inventory
test oracle
research index
```

Do **not** treat it as an instruction to reproduce the old internal architecture.

Its distinction between public behavior and internal implementation is valuable.

For each feature we build, use the knowledge base to answer:

```text
What should the user be able to do?
What is the expected observable behavior?
What permissions/ownership rules exist?
What failure behavior is expected?
```

Then implement that behavior through the new module architecture.

The knowledge base must remain a development/research dependency, not a runtime dependency.

---

## B. CMaNGOS PlayerBots — mature behavior reference

Use:

```text
https://github.com/cmangos/playerbots
```

Primarily as:

- accumulated behavior knowledge
- class AI reference
- movement reference
- dungeon/raid behavior reference
- source of bug fixes and edge cases

Do not vendor the whole repository into Tortoise again.

Do not assume its host architecture is appropriate for Tortoise.

---

## C. Shyalya / r-o-sh Turtle port — Turtle compatibility reference

Use the Turtle PlayerBots forks to understand:

- API incompatibilities encountered during the port
- Turtle-specific spell/talent differences
- required compatibility shims
- bugs already discovered in 1.18.1
- bot session problems
- group/loot/movement issues
- Turtle-specific fixes

Use these repositories as evidence of what Turtle requires.

Do **not** automatically copy their ~80 host-hook architecture.

---

## D. MangosZero Bots — lifecycle / MaNGOS-Zero reference

Use MangosZero particularly for:

- player/session lifecycle ideas
- alt-character bot ownership
- random-bot lifecycle
- bot creation/login
- group handling
- MaNGOS-Zero-compatible patterns

Do not assume `src/modules/Bots` is already cleanly decoupled.

It is a source pool and architectural reference, not the final architecture.

---

# 4. Selective harvesting rule

We want to reuse years of learned behavior without inheriting years of coupling.

The rule is:

```text
harvest behavior, not architecture
```

For every feature imported from another bot implementation:

1. Identify the user-visible behavior.
2. Find the relevant knowledge-base description if available.
3. Inspect the newest relevant upstream implementation.
4. Inspect Turtle/Shyalya differences where relevant.
5. Write or define an acceptance test.
6. Port/reimplement the minimum behavior inside the new module.
7. Do not add a new core hook unless absolutely necessary.
8. Record provenance.

Do not blindly cherry-pick incompatible commits.

A literal `git cherry-pick` is acceptable only when:

- the commit is isolated
- it applies entirely inside the module or module-owned files
- it does not bring core coupling with it
- its license/provenance is understood
- tests verify the intended behavior

Otherwise:

```text
study -> extract intent -> reimplement/port -> test
```

is preferred.

---

# 5. Provenance / licensing discipline

Because this may eventually be useful to other people:

For every substantial port, create a lightweight provenance record.

Suggested file:

```text
docs/PROVENANCE.md
```

Record:

```text
Feature:
Source project:
Source commit:
Source files:
Ported/reimplemented:
Reason:
Local tests:
```

Preserve upstream copyright/license notices where required.

Before distributing copied code, verify the licenses of all source projects being harvested and comply with their requirements.

Do not let an AI silently copy large bodies of code without attribution.

---

# 6. Proposed repository/module layout

Preferred long-term model:

```text
Penqle/tortoise-wow
└── src/modules/
    └── PlayerBots/     <-- separate repository checkout
```

Possible separate repository name:

```text
tortoise-wow-playerbots
```

The core should be able to exist without this directory.

Conceptual CMake behavior:

```text
BUILD_PLAYERBOTS=OFF
    -> module is ignored/not required

BUILD_PLAYERBOTS=ON
    -> require src/modules/TortoiseBots/CMakeLists.txt
    -> build/register module
```

Do not make the module repository a mandatory dependency of Tortoise.

Whether this is implemented as:

- optional git submodule
- separate clone into `src/modules/PlayerBots`
- another supported module-loading mechanism

should be decided after inspecting the existing build system.

Prefer the least intrusive option.

---

# 7. Proposed internal module architecture

Do not build everything immediately.

Use this as direction, not mandatory boilerplate.

```text
src/modules/TortoiseBots/
│
├── CMakeLists.txt
├── README.md
├── config/
│   └── playerbots.conf.dist
│
├── host/
│   ├── Module.cpp
│   ├── BotHostAdapter.*
│   └── BotSessionAdapter.*
│
├── runtime/
│   ├── BotManager.*
│   ├── BotController.*
│   ├── BotContext.*
│   └── BotOwnership.*
│
├── perception/
│   ├── CombatState.*
│   ├── PartyState.*
│   ├── ThreatState.*
│   └── NearbyObjects.*
│
├── behavior/
│   ├── movement/
│   ├── combat/
│   ├── group/
│   ├── loot/
│   └── interaction/
│
├── classes/
│   ├── warrior/
│   ├── priest/
│   ├── mage/
│   └── ...
│
├── turtle/
│   ├── spells/
│   ├── talents/
│   └── content/
│
├── commands/
│
├── diagnostics/
│
└── llm/
    └── optional async integration
```

Avoid building an enormous generic framework before a real bot works.

Prefer small modules with clear responsibilities.

---

# 8. Phase 0 — Establish the clean baseline

Do this only after PR #396 is merged or after working from an equivalent clean commit.

## Tasks

1. Record exact Penqle core commit.
2. Confirm legacy PlayerBots code is gone.
3. Build the normal server with PlayerBots absent.
4. Run existing project tests/checks.
5. Record baseline build commands and results.

Suggested checks:

```bash
rg -n -i \
  'sPlayerBotMgr|PlayerBotEntry|PB_STATE_|GetBot\(\)|SetBot\(|\bm_bot\b' \
  src .
```

Expected:

```text
no legacy PlayerBots implementation
```

Legitimate `PlayerAI` / `PlayerControlledAI` must remain.

## Deliverable

```text
docs/BASELINE.md
```

Containing:

- core commit
- build command
- test command
- baseline result

## Stop condition

Do not start module implementation if the clean core does not build.

---

# 9. Phase 1 — Host-boundary discovery

This phase is intentionally design-first.

Do **not** begin by porting bot AI.

## Goal

Find the minimum events/capabilities the module actually needs.

## Inspect existing core for

- ScriptMgr / script registration
- player login/logout events
- player AddToWorld/RemoveFromWorld events
- world update event
- player update event
- chat hooks
- command registration
- group events
- movement notifications
- map events
- packet hooks
- object lookup APIs
- character DB loading APIs
- normal session creation/login path

## Produce a host capability table

Create:

```text
docs/HOST_API.md
```

Example:

| Need | Existing hook? | New core seam required? | Why |
|---|---|---|---|
| World tick | yes/no | yes/no | |
| Bot login | yes/no | yes/no | |
| Bot logout | yes/no | yes/no | |
| Chat command | yes/no | yes/no | |
| Group join | yes/no | yes/no | |
| Headless session | yes/no | likely | |
| Movement | yes/no | yes/no | |
| Loot | normal API | no | |

## Key objective

Try to get the required core integration down to:

```text
existing general hooks
+
one centralized headless-session integration
+
only genuinely missing lifecycle hooks
```

## Stop condition

If the proposed design requires patching 20+ core files before a bot can even log in:

```text
STOP
REDESIGN
```

---

# 10. Phase 2 — Build the empty module

Create the PlayerBots module with no meaningful AI yet.

## Requirements

- `BUILD_PLAYERBOTS=OFF` default
- Tortoise builds without the module directory
- Tortoise builds with module present but OFF
- module builds with `BUILD_PLAYERBOTS=ON`
- module registers/unregisters cleanly
- optional config loads only when enabled

First diagnostic behavior can simply log:

```text
PlayerBots module loaded
PlayerBots module unloaded
```

Do not add player logic yet.

## CI/build matrix

At minimum:

```text
Core + module absent + OFF
Core + module present + OFF
Core + module present + ON
```

Later add Windows/macOS if supported by the project.

---

# 11. Phase 3 — Solve the headless session properly

This is the most important technical spike.

## Goal

Create one bot-controlled Player through the normal character-loading machinery without a real client socket.

## Requirements

The solution must not require normal gameplay code to ask whether the player is a bot.

Centralize all special behavior in:

```text
BotSessionAdapter
headless session factory
or equivalent
```

## Investigate carefully

- authentication assumptions
- account ownership
- socket lifetime
- packet queue assumptions
- anticheat initialization
- disconnect behavior
- player saving
- account online state
- character online state
- duplicate-login protection
- logout cleanup
- reconnect behavior
- shutdown cleanup

## Preferred semantic model

The core understands:

```text
network-backed session
headless session
```

The module understands:

```text
this headless session is a bot
```

## Acceptance test

One configured character can:

1. load through the server
2. enter the world
3. remain alive for several minutes
4. save
5. leave world
6. log out
7. be loaded again
8. server shutdown cleanly

No combat required yet.

---

# 12. Phase 4 — First playable vertical slice

Do not build random world bots.

Do not build raids.

Do not build every class.

Goal:

```text
one human
+
one owned bot
```

## MVP capabilities

Implement:

```text
.bot add <character>
.bot remove <character>
follow
stay
assist
attack
stop
basic loot
save/logout
```

If module command registration exists, keep commands entirely in the module.

## Bot behavior

Start with one simple class.

Choose whichever class gives the fastest useful test, for example:

```text
Warrior DPS
or
Mage DPS
```

Implement only:

- follow owner
- face target
- maintain usable distance
- auto attack / basic rotation
- stop when target dies
- return to owner
- loot under simple rules

## Acceptance scenario

For at least 30 minutes:

- summon bot
- kill normal world mobs
- move through terrain
- loot
- dismiss bot
- resummon bot
- logout/relogin human
- no crash
- no stuck session
- no database corruption

---

# 13. Phase 5 — Small-party dungeon MVP

This is the first major product milestone.

Target scenario:

```text
2 human players
+
bots filling remaining 5-player party roles
```

Do not optimize for 1000 bots.

## Add

- role assignment
- tank target selection
- healer health thresholds
- DPS assist
- threat awareness
- group follow
- simple formation
- combat resurrection rules if applicable
- drink/eat/rest
- basic buffing
- dispel/interrupt framework
- loot rules
- wipe recovery
- regroup

## Initial supported role set

Prefer one known-good composition before class breadth:

```text
1 tank implementation
1 healer implementation
1–2 DPS implementations
```

Example:

```text
Warrior tank
Priest healer
Mage DPS
```

## Milestone

Complete a representative low/mid-level 5-player dungeon with humans + bots.

The bot code does not need to be clever yet.

It needs to be:

```text
predictable
safe
not embarrassing
recoverable
```

---

# 14. Phase 6 — Create the behavior harvesting pipeline

Only after the new architecture works should we aggressively mine mature PlayerBots behavior.

Create:

```text
docs/BEHAVIOR_BACKLOG.md
```

Suggested table:

| Capability | Priority | KB reference | Upstream source | Turtle source | Status | Tests |
|---|---:|---|---|---|---|---|
| Heal critical ally | P0 | ... | cmangos | Shyalya | todo | |
| Polymorph marked target | P1 | ... | cmangos | Shyalya | todo | |
| Interrupt caster | P0 | ... | cmangos | Shyalya | todo | |
| Avoid reflect target | P2 | ... | cmangos | ... | todo | |
| Buff party | P1 | ... | cmangos | ... | todo | |

## AI workflow for each capability

The local AI should:

1. Read `TortoiseWoWKnowledgeBase/AGENTS.md`.
2. Read the relevant `playerbots/` documentation.
3. Identify the observable behavior.
4. Inspect the pinned Shyalya source if required.
5. Inspect the newest CMaNGOS implementation for later fixes.
6. Inspect MangosZero if it provides a cleaner compatible solution.
7. Summarize the behavior before editing.
8. Identify whether any new host dependency is required.
9. Prefer a module-only implementation.
10. Add test/diagnostic coverage.
11. Record provenance.
12. Implement one capability per focused commit where practical.

This is where AI assistance becomes extremely valuable.

---

# 15. Phase 7 — Turtle-native spell and talent layer

Do not assume vanilla 1.12 class behavior equals Turtle 1.18.1.

Create a Turtle-specific data/compatibility layer.

## Goals

Bot decisions should be based on actual server data where practical:

- known spells
- DBC/server spell data
- Turtle talent trees
- player level
- learned ranks
- forms/stances
- custom Turtle abilities

Avoid blindly hardcoding old Wowhead/Vanilla spell lists.

## Structure

Conceptually:

```text
turtle/
├── SpellCapabilities
├── TalentProfiles
├── ClassRoles
└── Overrides
```

Use generated/data-driven rules where it reduces duplication, but keep explicit overrides for weird Turtle behavior.

## AI use

AI can help:

- compare Turtle spell/talent data with Vanilla
- generate candidate class profiles
- identify missing ranks
- translate old strategy logic
- propose tests

Do not automatically accept generated rotations without in-game validation.

---

# 16. Phase 8 — Capability compatibility with the knowledge base

The knowledge base documents an existing public PlayerBots surface.

We do **not** need to clone it all immediately.

However, maintaining familiar commands can make adoption much easier.

Prioritize compatibility where cheap:

```text
.bot add
.bot remove
.bot command surfaces
chat-directed commands
ownership rules
basic strategy controls
```

Use the knowledge base capability map to define:

```text
implemented
compatible
partially compatible
not planned
```

Create:

```text
docs/COMPATIBILITY.md
```

Do not expose internal implementation names as public commands just because old PlayerBots did internally.

Keep the new public API intentional.

---

# 17. Phase 9 — Diagnostics before complexity

Build diagnostics early.

We will need to understand:

```text
why did this bot choose this target?
why did it not heal?
why is it stuck?
what action won?
what movement goal is active?
what role does it think it has?
```

Add an opt-in action log / decision trace in the module.

Example fields:

```text
bot guid/name
time
state
target
role
decision
reason
action
result
duration
```

Keep it disabled by default.

Diagnostics must not cause heavy per-tick database writes.

Prefer in-memory/ring-buffer logging with optional file output.

---

# 18. Phase 10 — Contextual conversation / LLM

Only after normal bot gameplay is stable.

## Architecture

```text
game thread
   |
   | enqueue event
   v
LLM conversation queue
   |
   | async response
   v
safe chat output
```

No blocking game thread.

## LLM receives structured context

Examples:

```text
party members
current zone
current quest
recent deaths
recent loot
current target
bot personality
recent conversation
```

Do not dump arbitrary server memory.

## Natural-language commands

Eventually allow:

```text
"wait here"
"heal me instead"
"focus the caster"
"don't pull anything"
"let's go back to town"
```

The LLM should translate these into stable internal command intents.

The LLM must not directly manipulate server objects.

Use:

```text
LLM -> validated intent -> normal BotController command
```

## Failure model

If LLM is unavailable:

```text
combat still works
movement still works
commands still work
only conversational extras disappear
```

---

# 19. Later roadmap

Only after the 5-player dungeon MVP is reliable.

Suggested order:

1. More classes/specs
2. Better CC / interrupts / dispels
3. Dungeon encounter behaviors
4. Travel
5. Questing
6. Party automation
7. Raid roles
8. Raid encounter behaviors
9. Battlegrounds
10. Random world population
11. Auction/economy bots if desired
12. Addon integration / remote state query compatibility
13. Advanced LLM personality/memory

Do not start with random 1000-bot population.

That solves a different scaling problem from:

```text
two humans want competent party/raid companions
```

---

# 20. Performance rules

From day one:

- no database query every bot tick
- no full-world scans every bot tick
- no synchronous network calls
- no rebuilding huge strategy graphs every tick
- cache immutable spell/talent metadata
- use event-driven invalidation where practical
- put expensive diagnostics behind flags
- profile before adding large random-bot populations

Track:

```text
bot update time
total bot CPU
DB query rate
movement/path requests
AI decisions/sec
memory per bot
```

---

# 21. Security / ownership rules

Owned bots must not become a path for controlling another player's characters.

Define ownership before public commands expand.

At minimum:

- character belongs to requesting account, OR explicit supported policy
- duplicate login is rejected safely
- human login takes precedence over its bot session
- bot commands verify owner/group authority
- guild/group chat must not become arbitrary remote control
- debug/admin operations require explicit privilege

Use the knowledge base's security/failure documentation as a behavior reference.

Do not copy old permissive behavior without reviewing it.

---

# 22. Tests and verification

## Build tests

Always retain:

```text
Bots OFF
Bots ON
```

## Module-removal test

A strong decoupling test:

1. remove/rename `src/modules/PlayerBots`
2. set `BUILD_PLAYERBOTS=OFF`
3. build Tortoise

Expected:

```text
clean build
```

## Core-coupling audit

Regularly run a search such as:

```bash
rg -n -i \
  'PlayerBot|BotService|BotSession|HeadlessSession' \
  src/game
```

Every match must be explainable as part of the small approved host boundary.

A growing number of unrelated matches is a design regression.

Explicitly reject reintroduction of:

```text
GetBot()
SetBot()
m_bot
sPlayerBotMgr
PlayerBotEntry in core gameplay code
```

## Behavior tests

Prefer deterministic test/debug scenarios for:

- login/logout
- follow
- target selection
- heal selection
- threat
- interrupt
- CC
- death
- wipe recovery
- dungeon transitions
- teleport
- human reconnect
- server shutdown

---

# 23. Definition of Done — architecture MVP

The architecture MVP is complete when:

- [ ] Legacy PlayerBots remain removed from Penqle core.
- [ ] `BUILD_PLAYERBOTS=OFF` is the default.
- [ ] Core builds with no module checkout present.
- [ ] Module builds only when explicitly enabled.
- [ ] Existing generic hooks are used wherever possible.
- [ ] New host integration is centralized and documented.
- [ ] No `WorldSession::GetBot()`-style API exists.
- [ ] No generic gameplay subsystem needs to know whether a Player is a bot.
- [ ] Headless session lifecycle is centralized.
- [ ] One owned bot can log in and log out safely.
- [ ] One owned bot can follow and fight normal mobs.
- [ ] Bot can save/reload correctly.
- [ ] Server shuts down without leaked bot sessions.
- [ ] Diagnostics explain basic bot decisions.
- [ ] Provenance process exists before upstream code is harvested.

---

# 24. Definition of Done — first useful release

The first genuinely useful release is complete when:

- [ ] 2 human players can fill a 5-player group with bots.
- [ ] At least one tank, one healer, and one DPS implementation are usable.
- [ ] Bots follow reliably through a dungeon.
- [ ] Bots perform basic tank/heal/DPS responsibilities.
- [ ] Bots recover from combat and regroup.
- [ ] Core remains cleanly buildable with bots disabled.
- [ ] No significant PlayerBots logic has leaked into normal core gameplay code.
- [ ] Important behaviors have tests/diagnostic scenarios.
- [ ] Imported behavior has provenance.
- [ ] Installation is documented for another user.

---

# 25. Instructions for the local AI agent

When implementing this plan:

## Before editing

1. Read this entire `PLAN.md`.
2. Read repository `AGENTS.md` / contributor instructions.
3. If using the knowledge base, read its `AGENTS.md` first.
4. Inspect existing Tortoise extension hooks.
5. Inspect the relevant source before proposing a new core hook.
6. Preserve unrelated local changes.

## During implementation

Prefer:

```text
small vertical slices
module-only changes
tests/diagnostics
explicit architecture decisions
```

Avoid:

```text
large speculative frameworks
bulk vending upstream PlayerBots
dozens of host patches
bot checks scattered through core
blind cherry-picks
silent AI code copying
```

## Before each core modification ask

```text
Can this be done entirely inside the module?

Does an existing generic hook already exist?

Can the required core capability be generalized as an actual core concept
(e.g. headless session) rather than "bot special case"?

Will this create a future need for GetBot()/IsBot() checks elsewhere?
```

If the answer to the last question is yes, redesign.

## At the end of each phase report

- files changed
- core files changed
- module files changed
- new host hooks
- why each host hook is necessary
- tests/build commands run
- behavior demonstrated
- remaining problems
- architecture concerns
- provenance for imported logic

Never claim a phase is complete without showing the verification result.

---

# 26. First task to execute

Do **not** start writing combat AI.

The first implementation task is:

```text
PHASE 1 — Host-boundary discovery
```

Specifically:

1. Inspect current Penqle/Tortoise extension infrastructure after PR #396.
2. List every existing lifecycle/script hook relevant to PlayerBots.
3. Trace normal WorldSession creation/login/logout.
4. Determine the minimum clean design for a headless session.
5. Produce `docs/HOST_API.md`.
6. Propose the minimum required core edits.
7. Stop for architecture review before implementing the PlayerBots module.

The desired output is a design based on the actual current source tree, not assumptions from CMaNGOS, MangosZero, AzerothCore, or Shyalya.

---

# 27. Guiding principle

The long-term goal is not:

```text
"port PlayerBots to Tortoise"
```

It is:

```text
"give Tortoise a clean optional bot platform,
then selectively teach it the best behavior we can recover from years
of PlayerBots implementations and Turtle-specific knowledge."
```

That difference should guide every architectural decision.
