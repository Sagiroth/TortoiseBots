# TortoiseBots migration — known limitations (M10, honest list)

Status: source-audited, NOT gameplay-certified. Every runtime gate (A01–A16,
P01–P26) remains pending real-client/server observation. Compilation alone
never closes a gate.

## Blocked (incomplete, not excluded)

1. **F18 self-bot**: no Network-session attach seam exists; native PlayerAI is
   not a control seam. Headless conversion would be false implementation.
2. **F10 synthetic market**: needs companion core seam (paged snapshot + Guard
   + MarketPost/Bid/Expire). Personal AH works; empty-market supply does not.
3. **F06-D1 admission permission**: Penqle has no `CanLoot`; late per-slot skip
   holds correctness, wasted movement remains. Needs compilable env.
4. **M3-SHIM-D2 TARGET_FLAG_LOCKED**: needs Spell DBC Targets-mask evidence.
5. **Runtime client proof**: code compiles and links in the Docker build
   matrix (ON + DISABLED verified), but live client/server gameplay
   observation (A01–A16, P01–P26) remains pending. Compilation proves
   build-level correctness, not gameplay acceptance.

## Deferred to compilable environment (specified, not started)

- F11/G1-G2 first-init pipeline; G5 distribution weights; L1-L12 event
  machine/clock-shift/dynamic-target/revive-scheduling/brackets (M9 scope).
- F04 Shyalya corrections (crowd tally, RNG source, patrol pause).
- F24 void-zone creators; suppression auto-enable; SV/BR tactics; per-boss
  competence beyond the 4 object/hazard behaviors; Onyxia fight body.
- F17 `.bot` preset ops + migration diagnostics; F16 bulk selectors + per-target
  auth; mail send/COD/return; train-pet executor; feed-real-food (enhancement).
- F22 dataset import workflow (operator task); KARAZHAN guard narrowing spec.
- F25 engine backoff; service restart-orphan cleanup; arbitration of
  solo-idle triple-eligibility.

## Design warts (noted, unchanged — need runtime harm proof)

- Summon binding-before-follow ordering; `shouldQueryAHListingsOutsideOfAH`
  out-of-AH knowledge policy; destroy-ladder SKILL expendability; H1-H7 loot
  hazards; totem range-mismatch churn; aimed-autoshot weave (no guard).

## Explicit non-goals (excluded with reason)

- Autonomous dungeon guide/clear (`mod-dungeon-clear` + `.dc` surface).
- Death Knight, glyphs, vehicles, Arena mechanics, Eye/Isle maps (expansion).
- Working LLM generation (disabled in both donors; non-LLM chat required).
- WotLK/TBC-only spells and mechanics (per-class audit lists).
- Guild vaults (no native Tortoise storage; ZERO-false both sides).
