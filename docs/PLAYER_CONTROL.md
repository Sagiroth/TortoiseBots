# Player-owned control catalog

**Status:** design and delivery contract

This document defines the public control surface for a human who owns a
TortoiseBot. It is intentionally smaller than the inherited PlayerbotAI chat
parser. The parser remains the implementation of mature AI behaviour; this
catalog decides what is safe, discoverable and stable enough to present in
the native `.bot` command and the companion addon.

## Product rule

Player controls must make an owned bot convenient to manage in world content.
They are not a second bot AI, a raw debug console, a GM replacement, or a way
to mutate core-owned session/group/queue state.

All controls require the existing account-owner or GM authorization. A request
may be accepted asynchronously; the UI must report *queued* or *rejected*, not
claim a gameplay action succeeded before the mature AI accepts it. New gameplay
requests resolve scope from the requester's normal WoW target; roster checkbox
selection is never consulted by gameplay.

## Implementation rule

The control module has one interface for native commands and the addon:

```text
validated player intent + owned live bot + optional selected target
    -> accepted / rejected / queued result
    -> existing PlayerbotAI action or strategy
```

It reuses `PlayerbotAI::HandleCommand` or a named existing action only after
validating a fixed catalog entry. It must not copy the inherited chat parser,
reimplement strategies, or grow gameplay state inside `BotManager`.

`BotManager` continues to own bot records, Headless lifecycle and durable
master binding. `PlayerConvenience` owns only the short-lived summon
transition. Pullback is dispatched to the existing PlayerbotAI pull/return
strategy, rather than reproducing movement or combat state in the control
layer.

| Control family | Public intent | Existing behaviour | Delivery notes |
| --- | --- | --- | --- |
| Movement | follow, stay, come, stop | Existing follow/stay shortcuts, native combat stop | Dynamic scope: targeted owned bot or all controllable party bots. |
| Combat | attack | `attack my target` | Uses the requester's current enemy target and server-side party fan-out. |
| Pull | pull | `PullStrategy` / `pull my target` | Selects a tank executor and disables the existing `pull back` strategy for an ordinary pull. |
| Pullback | pullback | `PullStrategy` + `PullBackStrategy` / `pull my target` | Selects a tank executor and enables the existing return-to-pull-position behavior. |
| RTI | focus skull | `rti` value + `attack rti target` | Uses the existing Skull default and group raid-target icon. |
| Crowd control | cc moon | Existing `rti cc` value and class CC strategies/triggers | Server selects a capable executor, or honors an explicitly targeted owned bot. |
| Policy | aoe on/off | Existing `dps aoe` strategy | Scope-resolved strategy toggle; CC/RTI avoidance remains mature-AI-owned. |
| Lifecycle | login, logout, invite, kick, summon | Headless lifecycle and native group/convenience handlers | Roster-only; multi-select is filtered to eligible server-owned rows. |
| Visibility | roster snapshot, status | Module roster storage and diagnostics | `.bot roster` is authoritative for offline and online owned rows. |

The native action shell emits one structured `TBM:ACTION_ACK` or
`TBM:ACTION_ERR` result for addon requests. It suppresses incidental mature-AI
chat where the existing `silent` strategy permits; legacy CLI commands retain
their existing human-readable responses.

The authoritative roster is stored server-side in the module-owned character
database table. SavedVariables may retain only addon presentation preferences.

### Deferred support controls

`focus heal`, `buff target`, and `revive target` are deliberately not aliases
yet. The inherited implementation treats them as value-setting commands with
their own target-list/spell parameter grammar; they are not equivalent to
“use the player’s current selection.” A player-facing version needs a small,
explicit target-list contract and a real-client acceptance case before it is
added. Until then, the native shell must not guess parameters or invoke those
actions with an empty event.

The addon should first display a selected owned bot, its lifecycle/health/group
state, and only actions applicable to that state. It should not create another
transport or ask the core whether a player is a bot.

## Explicitly not public in the first release

The inherited parser also accepts powerful or highly contextual operations.
They stay hidden from normal players until a separate product decision,
security review and acceptance case exists:

* `debug`, `cdebug`, `cheat`, `set value`, custom strategy editing and remote
  diagnostics;
* raw combat/non-combat/dead/reaction strategy strings;
* direct travel, taxi, teleport, auction, bank, mail, trade, craft, guild and
  LFT controls;
* random-population, AH-market and battleground service operations;
* any command requiring free-form text whose valid Turtle/data contract is not
  represented in the public interface.

`.bot command <bot> <text>` is a transitional advanced path, not the player
product interface. Restricting it to GM use is a separate compatibility and
security change; do not make that change until its current user workflows have
been audited against the merged core.

## Delivery and acceptance order

1. Merge/rebase #411 then #416; build module-on and module-off with legacy
   PlayerBots disabled.
2. Prove the real-client owned-bot journey, including incoming command packet
   delivery and reclaim.
3. Prove summon and pullback acceptance scenarios in `PLAN.md`.
4. Add one catalog family at a time to the shared native/addon control path.
5. For every family, add a deterministic server-side scenario where possible
   and a concise manual-client step where client packet semantics matter.

The first release does not compete by exposing the longest command list. It
competes by making the useful owned-bot controls obvious, safe and dependable.
