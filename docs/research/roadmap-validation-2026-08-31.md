# TortoiseBots roadmap validation

**Checked:** 2026-08-31
**Repository snapshot:** local TortoiseBots `88afd3b3` (working tree had no
production-code edits)
**Scope:** validate the active roadmap in [`PLAN.md`](../PLAN.md) and the
status/feature claims in [`README.md`](../../README.md) against local source
and official GitHub source metadata. No reference repository was cloned. The
Shyalya and mod-playerbots checks below use the pinned GitHub commits directly.

## Executive conclusion

The direction is coherent if it is treated as an acceptance-and-delivery
roadmap, not as a plan to import another complete PlayerBots tree.

The corrected ordering is:

1. Deliver and rebase on the actual merged Penqle core PRs (#411, then #416),
   and rebuild both module-enabled and module-disabled configurations.
2. Complete a real-client owned-bot acceptance slice, including reconnect,
   reclaim, teleport, death and cleanup.
3. Complete the human + four-bot 5-player dungeon journey with the first
   tank/healer/DPS roles.
4. Fix only observed defects, then broaden Turtle/class/spec coverage from
   those failures.
5. Validate the already-present opt-in random/LFT/AH/BG services one at a time
   under bounded population and demand scenarios.
6. Productize the existing command/addon and operator surfaces, then measure
   larger-population performance.
7. Consider deliberately excluded donor helpers, new generic core seams, or
   optional asynchronous LLM only after a scope decision and acceptance data.

This is partly a prioritization inference. The factual basis is that the
current plan already names owned-bot and dungeon acceptance as the milestone,
while the source graph already contains nine Vanilla class families and the
random, LFT, AH and BG services. Presence in source is not gameplay proof.
See [`PLAN.md` §6–8](../PLAN.md#6-current-milestone--gameplay-acceptance),
[`TortoiseBots.cmake`](../../TortoiseBots.cmake#L55-L180), and the current
status checklist in [`README.md`](../../README.md#L20-L84).

## What was checked

* **Local architecture:** `PLAN.md`, `HOST_API.md`, `PROVENANCE.md`, the
  active CMake source graph, host adapters, runtime services and command
  registration.
* **Penqle core:** official PR pages and metadata for [#411](https://github.com/Penqle/tortoise-wow/pull/411)
  and [#416](https://github.com/Penqle/tortoise-wow/pull/416), checked on the
  date above. The GitHub API reports both as `open` and `clean`; current heads
  were `8037fc8cc4c8c5734aafb9dc43858205bcf9051f` (#411) and
  `e63161c2da7f13ab25687ea389026aa2e3c97647` (#416), both based on
  `Penqle/tortoise-wow:main` at `05912a49f7cd8f12afff04b3c37e6f852f981268`.
* **Shyalya:** [repository tree at
  `1f9497e0f42bfc1055841bb6ebdc7caa3515de0b`](https://github.com/Shyalya/tortoise-wow/tree/1f9497e0f42bfc1055841bb6ebdc7caa3515de0b),
  especially its README and `src/modules/PlayerBots/playerbot` sources.
* **mod-playerbots:** [pinned README](https://github.com/mod-playerbots/mod-playerbots/blob/5397110cba484a9b7209bc9f632652e9d4bd6a70/README.md)
  and representative pinned strategy sources, including
  [`FollowMasterStrategy.cpp`](https://github.com/mod-playerbots/mod-playerbots/blob/5397110cba484a9b7209bc9f632652e9d4bd6a70/src/Ai/Base/Strategy/FollowMasterStrategy.cpp)
  and [`HealPriestStrategy.cpp`](https://github.com/mod-playerbots/mod-playerbots/blob/5397110cba484a9b7209bc9f632652e9d4bd6a70/src/Ai/Class/Priest/Strategy/HealPriestStrategy.cpp).

Statements labelled **Fact** are directly stated by or observable in those
sources. Statements labelled **Inference** are recommendations derived from
the facts; they are not claims about an unobserved runtime.

## Architecture validation

### The module boundary is the right direction

**Fact:** The active contract assigns session lifetime to Tortoise `World`,
bot-record and AI lifetime to TortoiseBots, gameplay decisions to
`PlayerbotAI`, and movement semantics to the imported strategy/action layer.
The contract explicitly forbids `GetBot`, `SetBot`, `m_bot`,
`sPlayerBotMgr`, `PlayerBotEntry`, and scattered bot checks in normal gameplay
code ([`HOST_API.md` §1 and §9](../HOST_API.md#1-boundary-rule)).

**Fact:** The module's host integration is concentrated in adapters for host
startup/update, Headless session lifecycle, player lifecycle, chat commands,
packets and LFT fill. `BotHostAdapter` drives `BotManager`, `RandomBotService`,
AH and BG updates from the world listener; it does not add a second world
loop ([`host/BotHostAdapter.cpp`](../../host/BotHostAdapter.cpp#L61-L125)).
`BotSessionAdapter` constructs a Headless transport and queues it through
`World::AddHeadlessSession` ([`host/BotSessionAdapter.cpp`](../../host/BotSessionAdapter.cpp#L20-L68)).

**Inference:** Preserve this boundary. A roadmap item that introduces a
module-owned session manager, a second queue, bot fields in `Player` or
`WorldSession`, or per-system `IsBot()` branches would duplicate ownership and
create the coupling the project is explicitly avoiding.

### The positive source graph is already broad

**Fact:** `TortoiseBots.cmake` explicitly compiles the runtime, AI, strategy,
generic, value, trigger and action families; it lists nine Vanilla class
directories and rejects expansion/test family names such as DeathKnight,
glyph, vehicle, Karazhan, Arena, RTSC and BossAura ([`TortoiseBots.cmake`](../../TortoiseBots.cmake#L55-L180)).
`AiFactory` creates per-class contexts for the nine retained classes
([`ai/playerbot/AiFactory.cpp`](../../ai/playerbot/AiFactory.cpp#L1-L90)).

**Fact:** The pinned mod-playerbots donor describes itself as an AzerothCore
module and lists alt bots, random bots, raids/battlegrounds and configurable
behavior as existing features. Its pinned strategy files contain concrete
follow, combat and Priest healing trigger/action behavior, not just an
interface ([donor README](https://github.com/mod-playerbots/mod-playerbots/blob/5397110cba484a9b7209bc9f632652e9d4bd6a70/README.md#L20-L29),
[follow strategy](https://github.com/mod-playerbots/mod-playerbots/blob/5397110cba484a9b7209bc9f632652e9d4bd6a70/src/Ai/Base/Strategy/FollowMasterStrategy.cpp#L9-L17),
[Priest healing strategy](https://github.com/mod-playerbots/mod-playerbots/blob/5397110cba484a9b7209bc9f632652e9d4bd6a70/src/Ai/Class/Priest/Strategy/HealPriestStrategy.cpp#L23-L120)).
The local provenance records this donor layer as already forward-ported and
adapted ([`PROVENANCE.md`](../PROVENANCE.md#L17-L19)).

**Inference:** “Import more combat/movement/class AI for parity” is not a
current first priority. It would mostly duplicate behavior already in the
compiled graph and risks reintroducing expansion-only or host-specific APIs.
Use a failing acceptance scenario to justify each additional behavior port.

### Shyalya is useful behavior evidence, not an architecture target

**Fact:** At the pinned Shyalya commit, the README says the fork is built
around roughly 1,000 permanently online bots, uses `BUILD_PLAYERBOTS=ON`, and
vendors PlayerBots under `src/modules/PlayerBots`. It also documents fixes in
bot login, LFG, battleground, healing, targeting, queueing and strategy
rebuilds ([pinned README](https://github.com/Shyalya/tortoise-wow/blob/1f9497e0f42bfc1055841bb6ebdc7caa3515de0b/README.md#L176-L221)).

**Fact:** The pinned Shyalya source has a `PlayerbotMgr`/`RandomPlayerbotMgr`
manager layer and host hooks. `PlayerbotMgr.cpp` allocates a free-floating
`WorldSession` per bot and deliberately bypasses `sWorld.AddSession`
([source](https://github.com/Shyalya/tortoise-wow/blob/1f9497e0f42bfc1055841bb6ebdc7caa3515de0b/src/modules/PlayerBots/playerbot/PlayerbotMgr.cpp#L157-L167));
`HostHooks.cpp` adds Player-owned bot manager/AI methods, per-Player updates,
world bot ticks, packet dispatch and random-manager calls
([source](https://github.com/Shyalya/tortoise-wow/blob/1f9497e0f42bfc1055841bb6ebdc7caa3515de0b/src/modules/PlayerBots/playerbot/HostHooks.cpp#L1-L12),
[hooks](https://github.com/Shyalya/tortoise-wow/blob/1f9497e0f42bfc1055841bb6ebdc7caa3515de0b/src/modules/PlayerBots/playerbot/HostHooks.cpp#L25-L158)).
The same source contains detached worker-thread entry points for random-bot
stats, BG checks, LFG checks and player checks
([source](https://github.com/Shyalya/tortoise-wow/blob/1f9497e0f42bfc1055841bb6ebdc7caa3515de0b/src/modules/PlayerBots/playerbot/RandomPlayerbotMgr.cpp#L53-L160)).

**Inference:** Harvesting the observed behavior is coherent; copying this
manager/session/thread architecture is not. It would create a second owner
for sessions and world updates, spread bot-specific state through core seams,
and conflict with the current generic Headless contract.

## Core PR validation

### #411 is a delivery prerequisite, not a new gameplay milestone

**Fact:** Official PR #411 is currently open and proposes generic Headless
`WorldSession` support. Its stated contract includes immutable
Network/Headless transport, GUID-keyed Headless construction and lifecycle
owned by `World`, identity-bearing async login dispatch, human reclaim and
preserved account online semantics ([PR #411](https://github.com/Penqle/tortoise-wow/pull/411#L175-L195)).
The PR page identifies the current tip as `8037fc8` and reports no real-client
login/reconnect/reclaim or adversarial stale-callback matrix
([PR history](https://github.com/Penqle/tortoise-wow/pull/411#L403-L417)).

**Inference:** The roadmap should make “merge #411, rebase, rebuild and run
the manual client gate” a release gate. Treating #411 as if it completed
owned-bot gameplay would overstate the evidence; treating it as optional
would leave the module unable to target plain upstream `main`.

### #416 is a separate, ordered participant dependency

**Fact:** Official PR #416 is also open. It proposes generic
`CharacterCreation::CreateCharacter`, LFT participant lifecycle methods,
copy-only BG demand snapshots, and read-only state accessors while retaining
queue/offer/group ownership in core ([PR #416](https://github.com/Penqle/tortoise-wow/pull/416#L175-L191)).
Its page says it is rebased onto corrected #411 and that #416 adds no Headless
session behavior ([PR history](https://github.com/Penqle/tortoise-wow/pull/416#L183-L191)).

**Fact:** The local services hard-require those APIs where needed: LFT fill
fails compilation without `LFT/LFTMgr.h` from #416, and the local contract
documents the random auto-create and BG demand dependencies
([`runtime/LftBotFillService.cpp`](../../runtime/LftBotFillService.cpp#L1-L23),
[`HOST_API.md` §16, §18 and §19](../HOST_API.md#16-lft-queue-integration-optional-default-off)).

**Inference:** The dependency chain is `#411 → #416 → module rebase/build →
runtime acceptance`. Do not add PlayerBots-specific substitutes for these
generic APIs while waiting for upstream review.

## Roadmap-direction assessment

| Direction in the active plan/status | Assessment | Evidence and correction |
|---|---|---|
| Preserve optional native module and generic Headless/core boundary | **Keep; foundational** | The module has an explicit source graph and host adapters, while the core contract keeps bot meaning in the module ([`HOST_API.md`](../HOST_API.md#1-boundary-rule)). A core-off build remains a first-class requirement ([`README.md`](../../README.md#L20-L39)). |
| Merge #411 and #416 | **Keep; make it the first external gate** | Both are open as of this check; #416 is rebased on #411 and provides participant APIs, not more AI ([#411](https://github.com/Penqle/tortoise-wow/pull/411), [#416](https://github.com/Penqle/tortoise-wow/pull/416)). Record the merged SHAs and rebuild ON/OFF configurations. |
| Owned-bot login/follow/combat/loot/death/relogin/teleport/reclaim | **Highest product priority** | The plan calls this the current gameplay milestone, but the README still marks manual acceptance pending ([`PLAN.md` §6–7](../PLAN.md#6-current-milestone--gameplay-acceptance), [`README.md`](../../README.md#L25-L42)). A source fixture is not a real-client acceptance. |
| Human + four bots in a 5-player dungeon | **Second product priority** | This is the stated first-release definition of done ([`PLAN.md` §7 and §11](../PLAN.md#7-manual-gameplay-phase)). Keep the first role matrix narrow: Warrior tank, Priest healer, and Mage/Rogue/Hunter DPS; expand from failures. |
| Add all more classes/specs/Turtle strategies immediately | **Defer and make failure-driven** | Nine Vanilla contexts and broad generic behavior are already compiled ([`TortoiseBots.cmake`](../../TortoiseBots.cmake#L124-L180), [`PROVENANCE.md`](../PROVENANCE.md#L17-L19)). New source imports before acceptance would duplicate/expose existing behavior without proving it works on Turtle data. |
| Rebuild or expose inherited commands | **Productization later; do not reimplement AI commands** | Native `.bot` already supports add/remove/follow/invite/uninvite/stay/list/stats/pullback/summon/command/help ([`commands/BotCommands.cpp`](../../commands/BotCommands.cpp#L772-L849)); `.bot command` delegates to `PlayerbotAI::HandleCommand` ([same file](../../commands/BotCommands.cpp#L267-L295)). Add wrappers only for measured discoverability/UX gaps. |
| Add addon UI | **Mostly already present; defer polish** | README links the companion TortoiseBots Manager addon ([`README.md`](../../README.md#L1-L8), [`README.md`](../../README.md#L100-L125)). A new addon protocol or duplicate control channel would be unnecessary; validate the existing control path first. |
| Random-bot auto-create, LFT fill, AH market, BG auto-queue | **Already implemented; validate operationally, do not duplicate** | These are separate bounded/default-off services in the source graph and contract ([`PLAN.md`](../PLAN.md#4-implemented-architecture-summary), [`TortoiseBots.cmake`](../../TortoiseBots.cmake#L55-L87)). LFT preserves core offer/group ownership ([`HOST_API.md`](../HOST_API.md#16-lft-queue-integration-optional-default-off)); AH uses native auction handlers ([§17](../HOST_API.md#17-ah-market-population-optional-default-off)); BG observes human demand and uses native queue handlers ([§19](../HOST_API.md#19-battleground-auto-queue-optional-default-off)). The missing work is one-service-at-a-time runtime/operations evidence. |
| Broad autonomous population or “1,000 bots” scale-up | **Defer until small-party reliability and measurement** | The current services are bounded and off by default; the plan explicitly says not to scale before the small party works ([`PLAN.md` §8](../PLAN.md#8-later-roadmap)). Copying Shyalya's permanent-population/thread model would violate the current performance and ownership rules. |
| Import Shyalya donor manager/login/queue threads | **Reject** | The pinned donor source uses free-floating sessions, Player-owned hooks and detached checks, whereas current TortoiseBots uses World-owned Headless sessions and one world listener. This is architecture duplication, not a missing feature. |
| Add DK/glyph/vehicle/Arena/Karazhan/other expansion donor families | **Reject for this product** | The plan defines Vanilla/Turtle scope and the CMake filename guard excludes these families ([`PLAN.md` §5](../PLAN.md#5-vanillaturtle-boundary), [`TortoiseBots.cmake`](../../TortoiseBots.cmake#L169-L180)). Reintroduce only through an explicit product-scope change. |
| Add custom dungeon/portal strategies now | **Defer** | Historical provenance records custom encounter behavior as an external content gap and the current roadmap prioritizes native 5-player acceptance. Do not turn absent core script/data behavior into PlayerBots-specific fake success ([`PLAN.md` §6.1](../PLAN.md#61-f-03f-27-closure), [`PROVENANCE.md`](../PROVENANCE.md#L142-L146)). |
| Add new path/navigation seam or private navmesh access | **Only after a generic-core case** | The audit documents path-area/avoidance limitations and the module now fails closed rather than pretending those calls work ([`PLAYERBOTS_AUDIT.md`](../archive/PLAYERBOTS_AUDIT.md#f-24-core-path-filters-are-not-implemented-p1)). A future seam must pass the three tests in [`HOST_API.md` §20](../HOST_API.md#20-new-core-seam-test) and remain bot-neutral. |
| Optional asynchronous LLM | **Last; never a gameplay dependency** | The architecture explicitly isolates LLM from combat, movement, healing, threat, interrupts and CC ([`AGENTS.md`](../../AGENTS.md#architecture-invariants)). No LLM work should block deterministic bot acceptance. |

## Corrected execution order and gates

### 1. Canonical integration gate

Merge or otherwise obtain the actual reviewed #411 tip, then #416 on top;
record the resulting SHAs. Rebase TortoiseBots and run the smallest complete
build matrix: module enabled/static and module disabled, with legacy
PlayerBots off. This gate is about upstream delivery and compatibility, not
feature breadth. Until it passes, local compatibility evidence is tied to
pinned PR revisions, not upstream `main`.

### 2. Real-client owned-bot gate

Validate one disposable same-account journey end to end: add/login, native
commands, follow/stay, group invite/accept, combat, loot, death/resurrection,
teleport, save/logout/relogin, human reconnect/reclaim and shutdown. Capture
server logs and explicitly separate server-side packet fixtures from real
client incoming-packet evidence. This closes the largest user-visible gap in
the current status checklist.

### 3. Dungeon vertical slice

Use one ordinary 5-player dungeon and `human + 4 bots`. Start with Warrior
tank, Priest healer and Mage/Rogue/Hunter DPS. Prove role assignment, threat,
healing, interrupts/CC, loot/quests, doors/gossip, death and wipe recovery,
regroup and instance transitions. Fix only failures observed in this matrix;
do not widen donor source selection as a substitute for a failing test.

### 4. Turtle and class reliability expansion

Expand class/spec combinations and Goblin/High Elf, Turtle spell/talent and
collection-mount cases from concrete failures. Keep the existing data-source
order: target core/data first, then donor behavior as comparison. Every
substantial port gets provenance and a deterministic acceptance case.

### 5. Opt-in service acceptance

After owned-bot/dungeon reliability, test the existing services separately:

* random pool and bounded auto-create, including restart/idempotence and
  account/character persistence;
* LFT fill with a human demand, native offer/accept/cancel and no human
  ownership transfer;
* AH posting through native auction validation, bounded cadence and no party
  bot selection;
* WSG/AB/AV demand-aware queueing, invite/port/reclaim and queue cleanup.

These tests should remain default-off and bounded. They are validation and
operations work, not a reason to add another manager or queue implementation.

### 6. Productization and scale measurement

Only after the core journeys pass should the project decide which inherited
commands need native aliases, which addon controls need polish, and which GM
status/configuration controls are safe. Then measure bot update time, DB
queries, path requests, AI decisions/sec and memory at increasing population;
set a supported scale from measurements rather than adopting Shyalya's
1,000-bot operating point as a requirement.

## Bottom line

The plan's architecture and first-release outcome are sound. The main
correction is to stop treating broad source presence and the existence of
default-off services as completed product behavior. Merge/rebase on #411/#416,
prove the ordinary human-owned-bot loop with a real client, prove the first
dungeon, and only then spend effort on Turtle breadth, optional population
services, UX or scale. Donor manager/session/queue architecture and expansion
families should remain out of scope unless a new, explicit product decision
overrides the current Vanilla/Turtle and generic-core invariants.

No production code was changed for this report.
