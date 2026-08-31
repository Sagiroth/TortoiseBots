# Core PR #411/#416: TortoiseBots feature delta

**Checked:** 2026-08-31.  The official PR pages still show both pull requests as
open; this is a merge/rebuild dependency analysis, not a claim that either is in
`Penqle/tortoise-wow` `main`.

## What each PR unlocks

| Core change | Module capability that is blocked without it | What becomes possible after merge (and a module rebuild) |
| --- | --- | --- |
| [PR #411 — generic Headless `WorldSession`](https://github.com/Penqle/tortoise-wow/pull/411) | The module cannot create or retain a logged-in no-socket character session on plain upstream `main`. This is a hard host/lifecycle dependency, not an AI limitation. | Owned-bot login/autologin, follow/combat/loot through a live Headless player, logout/relogin, same-account `Network + N Headless`, human reclaim, and safe shutdown. The PR supplies `SessionTransport`, GUID-keyed `World`-owned Headless registries, deferred normal character login, and reclaim ordering. See [`HOST_API.md`](../HOST_API.md#3-session-transport), [`HOST_API.md`](../HOST_API.md#4-session-registry-and-lifetime), and [`host/BotSessionAdapter.cpp`](../../host/BotSessionAdapter.cpp). |
| [PR #416 — generic participant primitives](https://github.com/Penqle/tortoise-wow/pull/416) (rebased on #411) | Character auto-create fails to compile without the generic `CharacterCreation::CreateCharacter` seam. LFT fill and BG demand-aware queueing fail closed at compile time without `LFT/LFTMgr.h` and its participant/snapshot APIs. #416 explicitly requires #411 first. | Optional random account/character auto-creation; bounded LFT fill using native queue/offer/accept ownership; and bounded WSG/AB/AV auto-queue driven by a copy-only human-demand snapshot. It also exposes read-only group-target, dynamic-object, and homebind accessors used by module behavior, without adding PlayerBots concepts to core. See [`runtime/RandomBotService.cpp`](../../runtime/RandomBotService.cpp), [`runtime/LftBotFillService.cpp`](../../runtime/LftBotFillService.cpp), [`runtime/BattlegroundQueueService.cpp`](../../runtime/BattlegroundQueueService.cpp), and [`HOST_API.md`](../HOST_API.md#16-lft-queue-integration-optional-default-off). |

### Dependency shape

`#411` is the prerequisite transport/lifecycle seam. `#416` is logically
separate but its current branch is rebased on #411; therefore the complete
feature stack is `#411 -> #416 -> module rebuild`. #416 does not create another
session model, own queues, or make the AI itself more capable. The module's
explicit compile guards document the hard failures: [`RandomBotService.cpp`](../../runtime/RandomBotService.cpp#L20), [`LftBotFillService.cpp`](../../runtime/LftBotFillService.cpp#L18), and [`AhMarketService.cpp`](../../runtime/AhMarketService.cpp#L24).

The AH market service is not a feature *added* by #416. It already uses native
auction APIs, but its safety guard intentionally hard-requires #416's LFT
queries; consequently AH can only build/run in the #416 stack. See
[`HOST_API.md`](../HOST_API.md#17-ah-market-population-optional-default-off).

## User-visible gaps that remain after both merge

Merging the PRs removes host API blockers; it does not constitute gameplay
acceptance or turn optional services on.

* **Real-client acceptance is still outstanding.** The module's packet fixture
  is server-side; incoming packet delivery from a real Network client and the
  real reconnect/reclaim journey remain manual gates. No real-client login,
  reconnect, or reclaim is claimed by the PR validation. See
  [`HOST_API.md`](../HOST_API.md#10-packet-bridge) and [`README.md`](../../README.md#validation).
* **Core gameplay acceptance is still outstanding.** Owned-bot add/follow,
  combat, loot, death/recovery, teleport, relogin, and especially the complete
  human + four-bot 5-player dungeon journey (roles, interrupts/CC, loot/quests,
  doors/gossip, wipe recovery/regroup) still need manual validation. See
  [`PLAN.md`](../PLAN.md#7-manual-gameplay-phase).
* **The new population/queue services remain opt-in and bounded.**
  `RandomBotAutoCreate`, LFT fill, AH market, and BG auto-queue default off;
  their existence/configuration is not a claim that a live populated server,
  LFT offer/group flow, AH market, or BG match has been accepted. The local
  checkpoint explicitly claims no live LFT/BG gameplay and still needs broader
  random-population testing. See [`HOST_API.md`](../HOST_API.md#18-random-bot-auto-create-optional-default-off), [`HOST_API.md`](../HOST_API.md#19-battleground-auto-queue-optional-default-off), and [`README.md`](../../README.md#validation).
* **Coverage remains intentionally Vanilla/Turtle-scoped.** Nine Vanilla
  classes are present, but broader Turtle class/spec/content testing remains;
  expansion-only donor systems (DK, glyphs, vehicles, Arena, Karazhan) are
  excluded by the positive source graph. The post-Vanilla fishing wrapper and
  donor guardian-oriented `PetsAction` remain excluded. See
  [`TortoiseBots.cmake`](../../TortoiseBots.cmake) and the scope notes in
  [`PROVENANCE.md`](../PROVENANCE.md#native-runtime-and-vanillaturtle-behavior-checkpoint--2026-08-24).
* **Upstream delivery itself remains a gate.** Both official PR pages are
  currently open; until the actual merged commits are selected and rebuilt,
  the local synchronized integration checkpoint is evidence against its
  pinned revisions only, not against upstream `main`. See
  [`HOST_API.md`](../HOST_API.md#2-compatible-baseline).

## Bottom line

After #411/#416 merge and rebuild, the module has the generic host surface for
Headless owned bots plus optional character creation, LFT fill, and demand-aware
BG participation. The remaining work is user-visible reliability/acceptance
(real client and dungeon journeys), opt-in service validation at population,
and deliberate Vanilla/Turtle scope expansion—not another PlayerBots-specific
core seam.
