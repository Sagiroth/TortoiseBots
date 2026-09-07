# Migration evidence log (plan §6 A01–A16, §15 P01–P26)

Rule: compilation, source presence and command ACKs never close a runtime gate.
Every scenario below needs fixture + revision/data/config + steps + observed results.
Status values: `pending` | `pass` | `fail` | `blocked(reason)`.

## M0 source-verification evidence (2026-09-07, no client, no server run)

| Check | Anchor | Result |
| --- | --- | --- |
| Pins recorded | T `e0085780`, CORE `c12bb16e`, M `5397110c`, S `49d183a0` | done |
| Engine dual hooks | T `strategy/Engine.cpp:112-134` | confirmed mixed interface |
| Queue name identity | T `strategy/Queue.cpp:10-33` | confirmed |
| SpellId numeric branch | T `strategy/values/SpellIdValue.cpp:169-181` | confirmed |
| GetValues empty | T `PlayerbotAIConfig.cpp:23-26` | confirmed gap |
| Auction mirror clear-only | T `runtime/PlayerbotRuntimeFacade.cpp:271-278` | confirmed gap |
| GuildBank false ZERO | T `strategy/actions/GuildBankAction.cpp:11-39` | confirmed |
| Turtle Karazhan native | CORE `ScriptLoader.cpp:291,305`, `src/scripts/dungeons/{karazhan_crypt,lower_karazhan_halls,upper_karazhan_halls}` | confirmed native scope |
| Guard scripts | `tools/verify_turtle_surface.sh`, `tools/verify_penqle_host_contract.sh --core ../tortoise-wow` | re-run pending in M0 PR validation |

## Shared acceptance scenarios A01–A16 (all pending)

| ID | Setup and trigger | Required outcome | Status | Evidence |
| --- | --- | --- | --- | --- |
| A01 | Owned bot login, repeated add/remove, owner reconnect/reclaim | One AI owner; durable owner; saved build unchanged | pending | — |
| A02 | Follow → Stay → Come → Follow in/out of combat | All engines agree; no reaction fighting next command | pending | — |
| A03 | Explicit ranged attack before threat, then target death/evade | Requested target survives pre-threat; stale clears, assist resumes | pending | — |
| A04 | Reach-then-cast; target dies/moves/maps or command changes | Valid prerequisite survives; invalid cancels bounded | pending | — |
| A05 | Legal + deliberately rejected unit/ground/item/pet casts | Acceptance matches native preparation; later failure distinguished | pending | — |
| A06 | Multi-mob tank + healer/DPS | Pickup, threat rules, no default-action starvation | pending | — |
| A07 | Tank emergency, injured DPS, incoming heal, remote member | Correct urgency/target/rank; no unsigned-health error | pending | — |
| A08 | Interruptible cast, immune target, cooldown/range variants | Eligible executor interrupts in window; expired request never fires late | pending | — |
| A09 | Assigned mark + CC with AoE on/off, pets, cleave | CC lands/reapplies; group damage respects protected target | pending | — |
| A10 | Pull and pullback; blocked path/timeout/owner disappears | One target; pullback returns to anchor; invalid cancels | pending | — |
| A11 | Kill/loot quest, tapped/empty/skinnable/full-bag corpses | Native rights/rolls respected; no crouch/reopen loop | pending | — |
| A12 | Single death, out-of-range corpse, total wipe | Correct rez/corpse-run; regroup; dead engine exits cleanly | pending | — |
| A13 | Dungeon portal, far/near teleport, exit, transport, rejected summon | Native transition; AI pauses/resumes; rejection not arrival | pending | — |
| A14 | Consecutive pulls deplete mana/ammo/reagents/durability | Recovery/restock or reported inability; no unusable rotation | pending | — |
| A15 | Talent/role change, saved preset, relog, no-op talent query | Only real topology rebuilds; owned build preserved | pending | — |
| A16 | Optional service selects bot then human assumes control | Service releases ownership; no unwanted side effects | pending | — |

## Feature scenarios P01–P26 (all pending; fixtures per F-packet during M5–M9)

P01 owned→autonomous→recall; P02 quest sync directions; P03 autonomous quest journey;
P04 blocked objective/full log/restart; P05 taxi/boat/vendor route; P06 crowded RPG hub + avoidance;
P07 gather→craft→mail/sell; P08 random creation→progression→restart; P09 AH price fixtures;
P10 sell/bid/outbid/buyout/cancel; P11 empty-market supplier+buyer; P12 stale auction/reclaim;
P13 market reload/override/rebuild; P14 population timers/pins/restart; P15 recruit/release/world group;
P16 guild flow; P17 LFT missing role/timeout; P18 BG demand + match; P19 bulk mixed-authority;
P20 preset migration; P21 self-bot/free-alt/always-online; P22 world-buff config reload;
P23 clean install + data import; P24 dungeon/raid without DungeonClear; P25 simultaneous services + takeover;
P26 multi-party soak with budgets. All `pending`.

## M1 decision-trail evidence (2026-09-07, branch migration/m1-diagnostics)

### Emitter grammar (implemented, uncompiled — no docker builds per user constraint)

- `Engine::DoNextAction`: TICK carries `state=` + `strats=`; T/PUSH/A lines carry
  `src=` (+ `base=`/`eff=` on A lines); non-1.0 multiplier factors logged as MULT lines.
- `PlayerbotAI::CastSpell` unit/GO/coordinate overloads: CAST_START + CAST_OK/FAIL
  `phase=PREPARE*`; 7 CAST_GATE early-exit reasons (self-harmful, pet-redirect,
  flying, stand-or-facing-delay, moving-jump-fall, moving-no-master, loot-impossible).
- Coordinate overload result still ignored by design (M3 owns the fix); now logged
  as `phase=PREPARE-coord-ignored` so rejected ground casts are visible.
- All behavior paths preserved (diff review: every early-return intact; the one
  dropped `return false` introduced mid-edit was restored before commit).

### Checker

- `tools/check_decision_trail.py --self-test`: PASS (grammar + UNKNOWN, stale-retry?,
  multiplier-zero, cast-fail/gate signatures + malformed-line detection).
- Production-log run pending server execution; `--strict` gates M1 runtime closure.

### Findings

- No module-visible EFFECT-phase completion signal exists (free-function hooks in
  BotActionLog.cpp have no callers; core owns spell effects). Generic core
  completion/failure proposal deferred to M3/M10 with this evidence — not dropped.
- Disabled cost: `Open()` returns null when `EnableActionLog=0` (BotActionLog.cpp:95);
  disabled Write = mutex + map miss + flag branch (+ one spell-map lookup for LogCast*).
  Measurement pending server run.
- A log label never converts an attempt into a successful heal/interrupt; A05/A07/A08
  remain pending real-client observation.
