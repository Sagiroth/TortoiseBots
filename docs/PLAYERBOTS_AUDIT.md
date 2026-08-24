# TortoiseBots PlayerBots Audit

- **Date:** 2026-08-24
- **Audit target:** TortoiseBots `13c0632f6a42f0f685de763c17f19c96bc392892`
- **Target core:** local `tortoise-wow` `9487c5150a6553c665fafc1f4568669b8b00f011` (`playerbots-integration-gh`)
- **Client/data:** local Turtle WoW 1.18.1 package; the base `WoW.exe` reports client build 5875, while the local Turtle documentation/addon package identifies the patched client as build 7272.
- **Authority:** local Tortoise core, local extracted DBC/maps/vmaps/mmaps, and local client files. The external wiki was not needed for this audit.

## Executive verdict

The current TortoiseBots repository is a substantial and mostly coherent
Vanilla/Turtle behavior harvest. The cleanup commit genuinely removed the
physical Death Knight, Karazhan, glyph, vehicle, Arena, donor-manager/login,
and test families from the native source graph. The native host layer also uses
generic `WorldScript`, `PlayerScript`, `ServerScript`, and `AllCommandScript`
seams rather than reintroducing `GetBot()`, `m_bot`, or `sPlayerBotMgr` into the
module repository.

It is not yet a clean baseline for expansion. Four integration problems should
be treated as blockers before expanding the feature set:

1. The current repository installs module SQL into lowercase `world/` and
   `character/` directories, while this core's Linux AutoUpdater uses the
   default case-sensitive `World/` and `Char/` paths. The local runtime image
   visibly contains the lowercase paths.
2. The local core checkout contains both a stale, untracked
   `modules/TortoiseBots/` copy and a tracked legacy
   `src/modules/PlayerBots/` escape hatch. Direct builds can therefore compile
   a materially different module from the one reviewed here, and the core
   still contains bot-specific LFT/chat code outside the clean native boundary.
3. The shipped schema is not a complete contract for the code that writes the
   optional caches. In particular, the item-info cache migration omits the 32
   `scale_*` columns that `RandomItemMgr` inserts, while zone-level data is
   created and bulk-populated at startup instead of being part of the module
   migration.
4. An owner-authorized mature command still exposes RTSC file save/load with a
   caller-controlled filename. Path traversal can escape `LogsDir` and write
   server-side files. That is a security blocker independent of whether the
   gameplay behavior is otherwise useful.

The native module itself still carries donor-era, later-expansion data and
behavior in active translation units, especially consumable tables, level
gates, spell lists, and the 4,937-line configuration template. Most of this is
unreachable at the core's level-60 cap or harmless when the corresponding item
does not exist, but it is still misleading product surface and should be
removed or made data-driven.

Turtle-specific support is mixed:

- Goblin/High Elf race/class legality, custom starting data, and the bot
  workarounds for their player-only starting zones are handled deliberately.
- Turtle Druid Eclipse and Holy Paladin Daybreak spell IDs match local server
  data.
- Turtle collection mounts, custom dungeon/zone encounter behavior, and broad
  custom spell/talent coverage are not yet native. Existing bots can often use
  data they already possess, but factory/randomized bots do not fully model the
  Turtle product.

**Recommendation:** keep the native architecture, fix the packaging and
source-of-truth blockers, then reduce the module to a clearly documented
Vanilla/Turtle product surface before adding more classes or dungeon behavior.

## Severity

- **P0 — blocker:** can make the reviewed module not be the module built or can
  prevent required module data from loading.
- **P1 — high:** violates the intended boundary or can silently produce
  expansion-era/wrong-Turtle behavior.
- **P2 — medium:** intentional scope gap, donor surface, or behavior debt that
  should be resolved before calling the module production-ready.
- **Accepted:** deliberate current limitation, provided it remains documented
  and tested as a limitation.

## Findings summary

| ID | Severity | Finding | Status |
| --- | --- | --- | --- |
| F-01 | P0 | Module SQL install path does not match the core AutoUpdater's case-sensitive `World/Char` lookup. | Fix before relying on fresh installs or migrations. |
| F-02 | P0 | The local core has a stale untracked module copy, and `BUILD_PLAYERBOTS=OFF` does not itself gate an explicitly enabled native module. | Fix source selection, build gating, and reproducibility checks. |
| F-03 | P1 | Bot-specific legacy code remains in the core: LFT random-bot filling, bot command stubs, bot slots, and legacy module hooks. | Quarantine/remove from the product core or explicitly accept as transitional debt. |
| F-04 | P1 | Active native code retains later-expansion consumable IDs, item IDs, spell IDs, and level gates. | Remove or replace with server-data lookups. |
| F-05 | P1 | The compatibility shim contains silent no-op/default implementations for movement, instance, chat-channel, transport, formation, emote, session-state, and loot semantics. | Turn each into a tested limitation or a real Tortoise adapter. |
| F-06 | P1/P2 | Custom Goblin/High Elf starting areas are deliberately bypassed because local navigation data is incomplete. | Keep as an explicit limitation until custom MMAP/pathing is validated. |
| F-07 | P1/P2 | Turtle collection mounts are not modeled by the factory/randomization path. | Use server mount/item data instead of adding hardcoded spells. |
| F-08 | P2 | Turtle custom dungeon/zone encounter behavior is not represented by explicit strategies. | Add it as a separate behavior backlog, not as assumed Vanilla coverage. |
| F-09 | P1/P2 | Talent validation is server-aware, but broad Turtle custom talent interactions remain data/acceptance-test debt. | Validate each class/spec against local DBC and server spell/aura data. |
| F-10 | P2 | The configuration template is a large donor configuration surface, including random bots, economy, LFG/social behavior, gear progression, and LLM settings. | Split MVP config from deferred systems. |
| F-11 | P2/Accepted | The native command surface is intentionally narrower than the Shyalya behavior baseline. | Document partial compatibility and remove stale command registrations. |
| F-12 | P2/Accepted | No PlayerBots client addon is present; only the normal Turtle addons and TortoiseGMManager are installed. | Fine for server-side `.bot` MVP; document addon/state-query work as future scope. |
| F-13 | P2 | `file(GLOB)` still compiles every action/value/trigger/generic source added to those directories. | Add a positive source manifest or a mechanical expansion guard. |
| F-14 | P2 | The developer quest-ledger script contains hard-coded `/home/ubuntu` paths and is not a runtime module component. | Move to tooling or make paths portable. |
| F-15 | P1 | The optional cache schema is incomplete: `ai_playerbot_item_info_cache` lacks the `scale_1`–`scale_32` columns written by `RandomItemMgr`; help-generation schema also has a latent `template_changed` mismatch. | Reconcile every migration with the active SQL read/write contract before shipping optional datasets. |
| F-16 | P1 | World/character table ownership is inconsistent, one runtime table is created outside migrations, and missing travel tables can trigger full map/path generation plus destructive cache rewrites. | Make database ownership, startup generation, and cache persistence explicit and tested on an empty schema. |
| F-17 | P0 | Owner-accessible RTSC file save/load accepts unrestricted filenames and can escape `LogsDir`. | Remove it from the player surface or confine it to a safe basename/GM-only diagnostic API. |
| F-18 | P1 | Custom strategy editing interpolates player-controlled strings into raw SQL through `DirectPExecute`. | Use prepared statements or the core escape API and validate command length/content. |
| F-19 | P1/P2 | The cleaned physical tree still compiles later-rank factory/boss-aura code and a custom RTSC/test surface; some of it is dead only because no context registers it. | Delete/quarantine unregistered donor code and validate every remaining spell/item against Turtle data. |
| F-20 | P2 | README/provenance claims about schema-only packaging and cache-safe startup are now contradicted by the active code and the lowercase install path. | Update the contract after the migration/startup design is corrected. |
| F-21 | P2 | The native migrations still create removed/inert donor tables (`random_bots`, `rpg_races`, `tele_cache`, and an uncalled rarity cache builder). | Remove them or move them into an explicitly deferred dataset migration. |

## 1. What is clean and correct

### 1.1 Native source cleanup succeeded

Target commit `322120e` removed the physical donor families and large manager
trees. The current tracked native tree has nine class directories only:

```text
druid hunter mage paladin priest rogue shaman warlock warrior
```

The positive class selection is visible in `TortoiseBots.cmake:90-105`.
The native graph does not include `strategy/deathknight`, glyph, vehicle,
Karazhan, Arena, or `strategy/tests` source files. The cleanup provenance is
recorded in `docs/PROVENANCE.md` and the deletion history is in commit
`322120e`.

This is a real success. The remaining 1,017 tracked files under
`ai/playerbot/` are harvested behavior and framework code, not proof that the
removed expansion families are still compiled.

### 1.2 The reviewed module does not recreate the old ownership API

The target repository has no matches for the forbidden core/session ownership
patterns:

```text
WorldSession::GetBot()
WorldSession::SetBot()
m_bot
sPlayerBotMgr
PlayerBotEntry
PB_STATE_
```

`host/BotSessionAdapter.cpp` treats `SessionTransport::Headless` as the
transport capability, and `runtime/BotManager` owns module records and AI
adapters. `host/BotHostAdapter.cpp`, `host/BotPlayerAdapter.cpp`,
`host/BotPacketAdapter.cpp`, and `host/BotChatAdapter.cpp` are a compact,
recognizable boundary.

The mature module does contain internal `PlayerbotAI::GetBot()` calls. Those
return the `Player*` owned by an AI object and are not the forbidden
`WorldSession` bot-identity API; they should remain module-internal.

The local core itself is a different matter; see F-03.

### 1.3 Several Turtle-specific paths are correctly grounded in local data

The core's `SharedDefines.h:39-70` defines the Turtle races Goblin (`9`) and
High Elf (`10`) and their faction masks. The local
`tw_world_playercreateinfo.sql` has the corresponding class combinations:

- Goblin: Warrior, Hunter, Rogue, Mage, Warlock.
- High Elf: Warrior, Paladin, Hunter, Rogue, Priest, Mage.

`PlayerbotAIConfig.cpp:300-329` matches those legal combinations, and
`TravelNode.cpp:2170-2182` names both custom starting nodes.

The module also consciously avoids sending randomized bots into the custom
Blackstone Island and Thalassian Highlands starting areas because the local
navigation data does not cover them (`TravelMgr.cpp:1244-1257`), and routes
Goblin/High Elf corpse recovery through homebind instead of the excluded racial
spawn (`ReleaseSpiritAction.h:190-209`). This is a sound defensive policy, but
it is a product compromise rather than full custom-zone support.

The custom class mechanics checked in this audit also match local server data:

- Druid Eclipse: module triggers use 51442/51443 and recognize talent/proc
  51444; local `tw_world_spell_template.sql` defines Nature Eclipse, Arcane
  Eclipse, and Eclipse, with the companion 51445 record.
- Holy Paladin Daybreak: module checks the 51322 buff and distinguishes the
  related 50931/51323 records; local `tw_world_spell_template.sql` defines the
  same server-side records.
- Priest Spirit Tap: the module distinguishes the passive talent ranks from
  proc buff 15271; local spell data includes the custom 42003 rank record.

These are valid Turtle adaptations, not WotLK leftovers.

## 2. Blockers and core integration problems

### F-01 — SQL install path case mismatch (P0)

The reviewed repository currently installs migrations as:

```cmake
TortoiseBots.cmake:27-33
.../modules/TortoiseBots/data/sql/world
.../modules/TortoiseBots/data/sql/character
```

The core AutoUpdater reads the configured folder names with defaults
`World` and `Char` (`tortoise-wow/src/shared/Database/AutoUpdater.cpp:497-512`)
and constructs the module path verbatim at `:269-276`. Linux paths are
case-sensitive.

The local running image was inspected read-only and contains:

```text
/opt/turtle/modules/TortoiseBots/data/sql/world/20260824090000_world.sql
/opt/turtle/modules/TortoiseBots/data/sql/character/20260824090001_char.sql
```

The local `render-config.sh` changes `Database.AutoUpdate.Path` but does not
change `WorldUpdateName` or `CharUpdateName`. Unless those names are overridden
elsewhere, the module migrations are not on the path the core scans. The stale
untracked core copy uses the uppercase destinations (`tortoise-wow/modules/TortoiseBots/TortoiseBots.cmake:29-38`), which explains how earlier runtime evidence could appear correct while the reviewed repository is not.

**Required action:** install to `data/sql/World` and `data/sql/Char`, or make
the core/module folder names an explicit shared contract. Add a packaging test
that installs the module and asserts that both migration files are found at
the exact AutoUpdater path. Do not claim a fresh-schema runtime test until this
is checked.

### F-02 — Two different module trees are present locally (P0)

The target repository is clean at `13c0632`, but the local core checkout at
`9487c515` has an untracked `modules/TortoiseBots/` directory. It differs
substantially from this repository:

- the core copy still has `runtime/BotController.cpp`;
- it still has `ai/World/WorldState.h`, donor login/manager-era files, and old
  compatibility source;
- its CMake file has the old explicit/filtered graph and different SQL install
  destinations;
- the reviewed repository has already removed those files and uses the
  cleaned positive graph.

The dev compose workflow intentionally overlays the reviewed repository onto
the core at `/work/core/modules/TortoiseBots`
(`tortoise-docker-penqle/docker-compose.dev.yml:3-5,44-49`). A direct CMake
build from the core checkout, an image build, or a future sync that omits this
overlay can therefore compile the stale copy instead.

There is a second gating ambiguity. The core's `BUILD_PLAYERBOTS` option is
only consulted by the legacy escape-hatch block
(`tortoise-wow/CMakeLists.txt:697-710`). When the native module is explicitly
selected through `MODULE_TORTOISEBOTS`, the reviewed `TortoiseBots.cmake`
unconditionally adds `BUILD_PLAYERBOTS=1` to its target
(`TortoiseBots.cmake:148-153`). Therefore `BUILD_PLAYERBOTS=OFF` plus an
explicitly enabled native module is not a true OFF configuration. The current
dev OFF script avoids this only because it also sets `MODULES=disabled`.

**Required action:** make one source-of-truth workflow. Either remove the
untracked copy after resolving ownership, or have the build system consume a
checked-out module path explicitly. Make `BUILD_PLAYERBOTS=OFF` disable the
native module even when module linkage is requested, or rename the option and
make the module linkage the single documented switch. Add a build diagnostic
that prints the module root and current module commit/hash. The final product
must never rely on an accidental bind-mount overlay or an implicit flag
combination to select the reviewed code.

### F-03 — PlayerBots knowledge has leaked into the core (P1)

The native module boundary is cleaner than the legacy design, but the local
core is not bot-agnostic yet:

- `src/game/LFT/LFTBotFill.cpp` is compiled by
  `src/game/CMakeLists.txt:135`. It identifies `RNDBOT` accounts, calls
  `Script_IsAIControlled`, seeds/withdraws bots from the LFT queue, and
  accepts bot offers inside a normal core queue.
- `src/game/Chat/Chat.cpp:1001-1007` registers `.bot`, `.rndbot`, `.ahbot`,
  and `.perfmon` unconditionally.
- `src/game/PlayerbotStubs.cpp:34-45` provides “not built” handlers for those
  commands. The reviewed native module implements `.bot` through
  `AllCommandScript`, but it does not implement the old `.rndbot`, `.ahbot`, or
  `.perfmon` `ChatHandler` methods. Those command names therefore remain stale
  core UI and report “not built” even when the native module is enabled.
- `src/game/ModuleSlots.h:21-26` reserves bot-specific slots that the reviewed
  `BotManager` does not use.
- `src/modules/PlayerBots/` is still a tracked 1,019-file legacy module. Its
  CMake path is an explicit `BUILD_LEGACY_PLAYERBOTS` escape hatch
  (`tortoise-wow/CMakeLists.txt:697-710`), but it contains the old
  `GetBot()`/`PlayerbotMgr` ownership model and expansion families. Turning it
  on is incompatible with the architecture being audited.

The generic script query names (`Script_IsAIControlled`,
`Script_IsMachineDriven`, role hooks) are a reasonable generalized seam. The
LFT implementation and legacy command/slot surface are not generalized; they
are PlayerBots-specific concepts in core code.

**Required action:** keep `BUILD_LEGACY_PLAYERBOTS=OFF`, remove or quarantine
the old module from the normal source distribution, move LFT bot filling behind
a generic queue-provider seam or into the native module, and remove stale
command symbols once the native command surface is authoritative. Re-run the
core coupling audit over all of `src/`, not only `src/game/`.

## 3. Expansion and donor residue in the active native graph

The physical expansion directories are gone, but some later-era data remains
in compiled source. These are not all reachable at level 60, which is why they
can survive a successful build; they are still wrong as a product baseline.

### F-04 — Later-era consumables and gates (P1)

Representative active paths:

- `PlayerbotAI.h:156-177` and `ImbueAction.h:34-38` prioritize
  `SUPER_HEALING_POTION=37807`, `CRYSTAL_HEALING_POTION=47132`,
  `FEL_REGENERATION_POTION=37864`, and
  `MAJOR_DREAMLESS_SLEEP_POTION=37845`.
- `PlayerbotAI.h:189-220` and `PlayerbotAI.cpp:7304-7314` retain Fel and
  Adamantite sharpening/weightstone and later wizard-oil display IDs.
- `PlayerbotFactory.h:138-200` includes Fel/Adamantite stones, Netherweave
  bandages, and Instant/Deadly Poison VIII/IX item IDs.
- `ImbueAction.cpp:12,87,150,177` uses `GetLevel() > 70` guards. The core
  client/product cap is `PLAYER_MAX_LEVEL 60` (`tortoise-wow/src/game/Database/DBCEnums.h:25-39`).
- `UseItemAction.h:187-219` contains level 61/63/68/71 healthstone branches;
  `UseItemAction.h:345-355` selects Super Sapper Charge at level 68; and
  `UseItemAction.cpp:1178-1187` uses spell 54403 for the later Scourgestone
  fallback.
- `MageActions.h:160-184,305-330` contains level 68/77 branches and later
  spell IDs. These branches are unreachable on a normal level-60 Tortoise
  realm, but they are still expansion code in the native graph.
- `PlayerbotFactory.cpp:3300-3332` has a level `>69` reagent branch.
- `EstimatedLifetimeValue.cpp:120-155` keeps level 61-70/80 DPS and gear-score
  formulas.
- `PlayerbotAI.cpp:3749,3795,3862,4001,4128` carries later-rank spell IDs
  `27023`, `27025`, `34600`, `49055`, `49056`, `49066`, and `49067` in neutral
  and out-of-control spell lists. They were not found in the local
  `spell_template` SQL and should be checked against the actual extracted
  `Spell.dbc` before being retained.

The local core's large `item_display_info` namespace contains many later-era
display records because the client data namespace is broader than the
level-60 item product. That is not proof that the corresponding items are
valid Tortoise items. Product code should validate item IDs through the loaded
`item_template`/server data, not through numeric presence in a display table.

The second-pass local DBC check makes several spell residues more concrete:
the local `Spell.dbc` contains the verified Turtle IDs 42003, 51322-51323, and
51442-51445, but does not contain the later list entries 27023, 27025, 27045,
27101, 27151-27153, 34600, 42985, 49055-49067, 49071, 49383, or 54403.
Those branches are either unreachable under the level-60 cap or silently
inert, but they should not remain presented as supported spell behavior.

**Required action:** remove later-era constants from the active tables; make
consumable selection query `ItemPrototype`/server data and level/riding/skill
requirements; replace impossible level gates with the target core's actual cap
or delete them. Keep a small documented whitelist for genuine Turtle custom
IDs.

### F-05 — Compatibility shim can silently change behavior (P1)

`ai/cmangos-compat-shim.h` is not merely a naming adapter. It contains
behavioral substitutes that compile donor code while deliberately losing
semantics:

| Location | Substitute | Risk |
| --- | --- | --- |
| `:85-95` | `UNIT_FLAG_CLIENT_CONTROL_LOST` becomes zero; `movementFlagsMask` becomes all bits. | Control/movement checks can become no-ops or over-broad. |
| `:110-120` and `WorldPosition.h:238-240` | Zero-valued `InstanceTemplate`; `getInstanceTemplate()` returns null. | Dungeon level/player-limit/reset logic cannot behave like the core. |
| `:237-260` | CMaNGOS trigger-cast bitmasks collapse to `bool` true/false. | Ignore-GCD/aura-scaling distinctions are lost. |
| `:483-497` | `Taxi::Map` is always empty. | In-flight taxi state and path reasoning are unavailable. |
| `:516-525` | ScriptDevAI gossip callback is a false-returning no-op. | Donor gossip behavior can silently disappear. |
| `:688-700` | Chat-channel store always returns null/zero. | `JoinChatChannels` is effectively a no-op despite the local client/core having `ChatChannels.dbc` and channel data. |
| `:721-733` | `TransportAnimation` is structural only. | Transport movement cannot be assumed correct. |
| `:786-802` | Formation slot data is a stub. | Formation/squad semantics are not implemented by this compatibility layer. |
| `:851-870` | Session states are synthetic ints; emote sound lookup returns null. | State/emote-dependent behavior can silently degrade. |
| `:892-910` | Loot status flags and `NOT_GROUP_TYPE_LOOT` are synthetic values. | Loot/roll state needs real acceptance tests. |

`ServerFacade.cpp:152-161` also returns safe defaults for chase target,
chase angle, and chase offset and explicitly labels actual generator inspection
as future work.

These are acceptable temporary module-local seams only if each is documented as
unsupported and covered by a focused test. They must not be mistaken for
Tortoise-compatible behavior merely because the module compiles.

## 4. Turtle-specific gaps and mismatches

### F-06 — Custom starting areas are deliberately bypassed

The core SQL restores Goblin and High Elf `playercreateinfo` rows to custom
zones (`20260620130000_world.sql:1-4`), but `TravelMgr.cpp:1244-1257` excludes
those coordinate ranges because the local navigation data does not support
them. `ReleaseSpiritAction.h:190-209` consequently sends those bots to
homebind instead of their racial start.

This is a good crash/stuck prevention rule, not a complete Turtle experience.
Record it as an accepted limitation until the custom-zone MMAP/pathing data is
available and tested.

### F-07 — Custom collection mounts are not part of factory behavior

The core has a `collection_mount` path and Turtle mount collection scripts,
including spell 46499 (`tortoise-wow/sql/database_updates/world/20260714180156_world.sql`
and `20260721013813_world.sql`). `PlayerbotFactory::InitMounts()` instead
learns a hardcoded list of ordinary race mounts (`PlayerbotFactory.cpp:3135-3238`)
and explicitly falls back to faction mounts for Goblin/High Elf.

An existing character that already owns a Turtle mount may still use it, but
randomized/factory-created characters will not model the collection system.
This should be fixed through server mount/item data, not by adding more
hardcoded spell IDs.

### F-08 — Turtle custom dungeons are not encounter-aware

The core's local migrations document Turtle-split dungeon zones and custom
graveyard coverage (`20260730120100_world.sql:1-48` and the related
`sql/tools/graveyards_turtle_dungeons.sql`). The native module's explicit
dungeon strategy registrations are the mature Vanilla raid set, with
`DungeonStrategy.h` listing Onyxia's Lair and Molten Core as related strategies;
there are no Turtle-custom encounter strategies in the current graph.

Generic travel/target discovery may still find database content, but that is
not the same as encounter behavior. Treat custom dungeons as a separate
behavior backlog item rather than assuming the Vanilla donor logic covers them.

### F-09 — Talent data is server-aware, but generation is still a risk

The config comments state that the hand-built level-60 specs were generated
against Turtle's reworked talent trees (`aiplayerbot.conf.dist.in:510-528`),
and `Talentspec.cpp` validates every link against loaded `Talent.dbc`/server
talent data before accepting it (`:45-105`). This is the correct direction.

The remaining risk is coverage: there is no general data-driven class-profile
layer for every Turtle custom talent interaction. The module currently adds
explicit overrides for a few known mechanics and otherwise relies on mature
Vanilla strategy names. Every class/spec should be validated against the
local DBC and live server spell/aura behavior before being called supported.

## 5. Product surface and client audit

### F-10 — Configuration is much larger than the current product

`ai/playerbot/aiplayerbot.conf.dist.in` is 4,937 lines. It exposes mature donor
families for:

- random population and auto-login;
- gear progression and auction/economy behavior;
- guild/social broadcasts and LFG chat;
- dozens of debug/maintenance paths;
- optional LLM prompts and network settings.

The native runtime intentionally defaults random-bot activation and account /
character creation off (`aiplayerbot.conf.dist.in:14-48`,
`runtime/RandomBotService.cpp:1-80`). The LLM `Generate()` path is currently
disabled and returns the existing no-response fallback
(`PlayerbotLLMInterface.cpp:376-383`), so it is not currently blocking combat.

The large template is still an operator-facing claim surface. A user can
reasonably assume every setting is supported because it is shipped. Split the
configuration into an MVP file and clearly marked deferred files, or generate
the template from only settings that the native module actually owns.

### F-11 — Current server command surface is intentionally narrow

The native `.bot` handler currently supports:

```text
add remove follow invite uninvite stay list stats command
```

(`commands/BotCommands.cpp:454-507`). The Knowledge Base baseline describes a
much broader Shyalya-fork surface (`.rndbot`, chat triggers, `@` filters,
addon/TCP state queries), but those docs are a behavior reference for a
different fork, not proof that this native module should expose them now.

This is acceptable for the current human + owned-bot MVP. Mark it explicitly
as “partial compatibility” rather than allowing the stale core `.rndbot`/AHBot
command registrations to imply otherwise.

### F-12 — No PlayerBots client addon is present

The local client has `Turtle_General`, `Turtle_GroupUI`, and
`TortoiseGMManager`, but no `TortoiseBots` or `PlayerBots` addon. The native
module has a packet bridge (`BotPacketAdapter.cpp`) for server-side AI event
delivery and inherited `CHAT_MSG_ADDON` code in mature `PlayerbotAI`, but it
does not provide a Turtle client addon, addon prefix contract, or bot state
query UI.

This is not a blocker for server-side `.bot add/follow/stay` work. It is a
clear future boundary: any addon must use the local Turtle addon transport and
client build, not assume modern client APIs or an expansion-era addon API.

The base `WoW.exe` reporting build 5875 is expected for a Vanilla 1.12.1
client lineage; the Turtle launcher/MPQ package is what supplies the local
1.18.1 product layer. The server/module uses core opcode constants rather than
hardcoding a modern client build, which is the correct approach.

## 6. Build graph and maintainability risks

### F-13 — Positive class list, negative behavior graph

The class directories are explicit, but generic/actions/triggers/values are
still globbed (`TortoiseBots.cmake:112-117`). That means a future donor file
named `Arena...`, `Fishing...`, `Glyph...`, or an expansion-specific helper
added to one of those directories will enter the build automatically.

The cleanup currently depends on physical deletion. Add a CI audit that fails
when forbidden families or later-level constants appear, or replace the broad
globs with generated/checked-in positive manifests. Keep a whitelist for
documented Turtle custom IDs so the guard does not reject valid custom content.

### F-14 — Research tooling is mixed into the module tree

`ai/playerbot/scripts/analyze_quest_ledger.py` is not compiled by CMake, but it
has hard-coded developer paths at `:22-23`:

```text
/home/ubuntu/cmake-build-debug/src/logs/bot_events.csv
/home/ubuntu/cmake-build-debug/src/logs/deaths.csv
```

Move it to a tooling/research directory or make the defaults relative to the
runtime log directory. It should not be treated as part of the deployable
TortoiseBots product.

## 7. Second-pass findings

The second pass concentrated on contracts that a clean source-tree sweep can
miss: the exact database schema, startup side effects, owner-command security,
and code that is compiled but not reachable through the normal context
registries.

### F-15 — Optional cache schema does not match active writers (P1)

The character migration creates `ai_playerbot_item_info_cache` with only the
metadata columns (`data/sql/char/20260824090001_char.sql:39-50`). Once a
non-empty weight-scale dataset is supplied, `RandomItemMgr::BuildItemInfoCache`
(`ai/playerbot/RandomItemMgr.cpp:743-797`) reaches its writer and inserts
`scale_1` through `scale_32` (`:1298-1326`). None of those 32 columns exists in
the shipped table, so an operator who enables the optional gear-weight data
gets failing cache writes during AI startup.

The default empty weight-scale migration currently masks this defect because
the initializer returns when `m_weightScales[1]` has no stats. That is not a
valid schema contract; the first real Turtle gear dataset will expose it.

There is a second, latent mismatch in the optional `GenerateBotHelp` build:
`PlayerbotHelpMgr::SaveTemplates` writes `template_changed`
(`ai/playerbot/PlayerbotHelpMgr.cpp:785-801`), but the world migration's help
table has no such column (`data/sql/world/20260824090000_world.sql:52-67`).
The normal CMake path does not define `GenerateBotHelp`, so this is not the
default startup failure, but it is still a shipped schema that cannot support
the feature it names.

**Required action:** derive the migrations from the active query/write
contract, add the scale columns or remove the writer, decide whether help
generation is supported, and add a schema test that exercises both empty and
populated optional datasets.

### F-16 — Database ownership and startup cache generation are not cleanly bounded (P1)

The module's table ownership is inconsistent:

| Contract | Evidence | Problem |
| --- | --- | --- |
| Travel graph | `TravelNode.cpp:3248-3430` always uses `WorldDatabase`; the world migration creates all three tables at `:90-124`. | The character migration redundantly creates `ai_playerbot_travelnode_link` and `ai_playerbot_travelnode_path` (`data/sql/char/20260824090001_char.sql:87-113`). Those copies are never used and make an empty character database look more complete than it is. |
| Zone levels | `TravelMgr.cpp:1204-1250` uses `WorldDatabase`, executes `CREATE TABLE IF NOT EXISTS`, then inserts one row for each loaded area. | `ai_playerbot_zone_level` is absent from the module world migration, so schema ownership is hidden in runtime code and startup mutates the world database. |
| Item/equipment caches | `RandomItemMgr::Init` calls the cache builders at `RandomItemMgr.cpp:72-82`; missing tables fall through to generation paths such as `:128-215` and `:2825-2984`. | A missed migration can turn startup into a large per-item scan and one-INSERT-per-item workload on the world thread. |

When AI is enabled, `BotHostAdapter::OnStartup` calls
`PlayerbotAIConfig::Initialize` (`host/BotHostAdapter.cpp:47-49`), which loads
area levels, factories, caches, texts, talents, quest travel, battleground
metadata, and auction data (`ai/playerbot/PlayerbotAIConfig.cpp:756-790`).
This is acceptable only when the database contract is complete and the writes
are bounded.

The failure mode is especially bad when F-01 prevents the module tables from
being applied. `TravelNodeMap::loadNodeStore` treats a failed table query as a
full-generation condition (`ai/playerbot/TravelNode.cpp:3344-3388`), and
`generateAll` then loads maps and generates paths (`:3184-3205`). A later save
deletes all travel nodes, links, and path points and repopulates them in
separate transactions (`:3316-3439`). A crash between those commits can leave
the cache empty or partial.

**Required action:** put every table in exactly the database its code uses,
remove duplicate wrong-database tables, migrate `ai_playerbot_zone_level`
explicitly or make it a deliberate non-persistent cache, and never silently
fall back from a missing migration to full map generation on a live world
thread. Use a versioned/staged cache rebuild if travel persistence is retained.

### F-17 — Owner-authorized RTSC command can write outside the log directory (P0)

RTSC is not just a harmless movement strategy. It is registered as a normal
chat action (`ai/playerbot/strategy/actions/ChatActionContext.h:188-200`) and
the mature security layer grants an account owner `ALLOW_ALL`
(`ai/playerbot/PlayerbotSecurity.cpp:20-29`). The action accepts a filename
from the command and calls:

- `sPlayerbotAIConfig.openLog(args[2], "w", true)` for `rtsc file save`
  (`ai/playerbot/strategy/actions/RtscAction.cpp:120-154`);
- `std::ifstream(m_logsDir + fileName)` for `rtsc file load`
  (`RtscAction.cpp:221-234`).

`openLog` simply concatenates `LogsDir` and the caller's string
(`ai/playerbot/PlayerbotAIConfig.cpp:993-1021`). A filename containing `../`
can therefore escape the configured log directory and overwrite a file that
the mangosd OS account can write. The command is reachable through the native
`.bot command` forwarding path, so this is not limited to a server operator.

**Required action:** remove RTSC from the owned-player command surface for the
MVP, or make it GM-only and restrict filenames to a validated basename below a
dedicated diagnostics directory. Do not pass user input to `fopen`/`ifstream`
as a path.

### F-18 — Custom strategy editing interpolates raw player input into SQL (P1)

The `cs` action is registered in `ChatActionContext` and takes a strategy name
and action line from chat. `CustomStrategyEditAction::Edit` inserts, updates,
or deletes those strings with `CharacterDatabase.DirectPExecute`
(`ai/playerbot/strategy/actions/CustomStrategyEditAction.cpp:45-84`). The core
implementation formats the query with `vsnprintf` and executes it directly
(`tortoise-wow/src/shared/Database/Database.cpp:466-484`); it does not escape
the `%s` arguments.

An apostrophe in a legitimate custom action can break the query, and crafted
input can alter the SQL literal or affect more rows belonging to the same
owner. This is a module-level data-integrity/security problem, not a Turtle
content difference.

**Required action:** use a prepared statement or the core's proper escaping
helper, cap name/action lengths, and add a regression case containing quotes,
backslashes, and command separators.

### F-19 — More compiled donor/test residue remains after the physical cleanup (P1/P2)

The second pass found additional expansion-era or diagnostic material beyond
the first pass's consumable list:

- `PlayerbotFactory::InitAvailableSpells` learns `31016` Eviscerate Rank 9,
  `31018` Ferocious Bite Rank 6, `28612` Conjure Food Rank 7, and `28610`
  Shadow Ward Rank 4 for a level-60 factory bot
  (`ai/playerbot/PlayerbotFactory.cpp:2897-2978`). The local core spell SQL
  confirms those names/ranks at `tw_world_spell_template.sql:21850,
  21852,22780,22782`; they are later-rank donor data, not a neutral Vanilla
  baseline.
- `BossAuraTriggers.h:15-35` retains rank IDs `27045`, `27151-27153`,
  `48943/48945/48947`, and `49071`. `BossAuraTriggers.cpp` and
  `BossAuraActions.cpp` are compiled by the broad trigger/action globs (the
  exact ON build compiled both), but their headers and creators are absent
  from the normal `ActionContext`/`TriggerContext` registries. They are dead
  donor code in the binary until some future registration makes them live.
- `RtscAction`/`RTSCStrategy` and spell `30758` (“Aedm”) remain active
  diagnostic/test behavior. The RTSC file surface is separately a P0 issue in
  F-17.
- `AutoLearnSpellAction.cpp:195-208` duplicates the later-rank factory
  learning list, including `31016` and `31018`, so deleting only the factory
  branch would not remove the expansion assumption.
- The existing F-04 list should also include the factory's later pet/consumable
  ranks, Rogue poison VIII/IX (`RogueActions.h:439,457`), and the TBC item
  exclusion list in `PlayerbotFactory.cpp:1974-1989`.

The presence of a numeric row in the core's broad DBC/SQL namespace does not
make it part of the Turtle level-60 product. Each retained ID needs a reason:
Vanilla, a verified Turtle custom mechanic, or a deliberate inert compatibility
case. Everything else should be deleted or moved behind a clearly named,
non-default compatibility/data layer.

### F-20 — Documentation and provenance overstate the current contract (P2)

`docs/PROVENANCE.md` says the native migrations are schema-only and cover the
tables queried by the active initializer, and that empty caches avoid a large
synchronous cache write. F-15 and F-16 show both statements are incomplete.
`README.md:39-42` also presents the installed migrations as ready-to-use,
while F-01 shows that the reviewed CMake destination does not match the core's
default AutoUpdater folder names.

The documentation should be corrected after the migration and startup
contract is fixed. Otherwise a new developer will reproduce the same stale
runtime image/schema assumptions that this audit found.

### F-21 — Migration contains tables with no active owner (P2)

The cleanup removed the donor random-population and older travel/cache
managers, but the two native migrations still create several tables that have
no active use in the reviewed source:

- `ai_playerbot_random_bots` is present in
  `data/sql/char/20260824090001_char.sql:72-85`, but no target source queries
  it after the random manager cleanup;
- `ai_playerbot_rpg_races` is present in the world migration at `:80-88`, but
  no active source references it;
- `ai_playerbot_tele_cache` is present at `data/sql/char/20260824090001_char.sql:29-37`,
  but the reviewed module has no query for it;
- `ai_playerbot_rarity_cache` has a builder in `RandomItemMgr.cpp:3301-3436`,
  but `RandomItemMgr::Init` explicitly leaves `BuildRarityCache()` commented
  out (`:72-82`).

These tables are not harmful merely because they are empty, but they make the
module look more complete than its active graph and keep donor-era database
contracts alive. They also make schema audits harder because a table can exist
without any runtime owner.

**Required action:** keep the native migration limited to tables with an active
reader/writer, or put deferred/donor datasets under a separately named and
opt-in migration with provenance.

## 8. Recommended remediation order

1. **Fix the SQL destination case** and add an installed-tree/AutoUpdater
   assertion.
2. **Eliminate source ambiguity:** stop direct builds from seeing the stale
   untracked module copy; require the reviewed module root/hash.
3. **Reconcile the schema before adding datasets:** add the missing cache
   columns, remove wrong-database/dead tables, and make zone/travel cache
   generation explicit and bounded.
4. **Remove or confine RTSC and harden all owner-controlled persistence**
   (prepared SQL, safe log paths, length/command validation).
5. **Keep the legacy module disabled and remove core bot-specific behavior**
   from normal LFT/chat paths. If LFT bot fill is retained, make it a generic
   queue provider or a native-module adapter.
6. **Remove active later-expansion constants and impossible gates.** Validate
   every remaining item/spell/talent ID against local server data, not just raw
   DBC/display namespaces.
7. **Turn compatibility stubs into explicit capability checks.** Prioritize
   instance metadata, movement/chase, transport/taxi, chat channels, loot/rolls,
   and spell-cast flags.
8. **Create a Turtle data layer** for custom races, spell/talent overrides,
   collection mounts, custom zones, and custom dungeon encounter metadata.
9. **Split deferred systems** (random population, economy, LLM, broad social
   automation) from the owned-bot dungeon MVP configuration.
10. **Add the client addon only after the server command/state contract is
   stable.** Validate it with the local Turtle addon transport and a real
   network-client journey.

## 9. Validation and limitations of this audit

Performed read-only:

- repository status/history and source-graph inventory;
- local core and module CMake/source comparison;
- local core API, SQL, DBC/data, and custom-content inspection;
- local Turtle client/addon directory and executable metadata inspection;
- local reference checkout SHA inspection;
- local Docker stack status and installed module path inspection;
- forbidden-symbol and expansion-family searches.

The exact reviewed checkout was also built through the persistent bind-mounted
builder:

```text
cd ../tortoise-docker-penqle
bash dev/build-playerbots
```

The cached `BUILD_PLAYERBOTS=ON`, `BUILD_LEGACY_PLAYERBOTS=OFF`, static
`MODULE_TORTOISEBOTS` build compiled and linked `mangosd` successfully. The
best-effort install wrapper reported the expected missing `realmd` artifact,
then copied the built `mangosd` to the install volume; this does not invalidate
the successful ON build. The complementary cached `BUILD_PLAYERBOTS=OFF`,
`MODULES=disabled` build also completed successfully. No server restart,
gameplay journey, fresh-schema run, database reset, or reference-checkout
modification was performed.

No source code, SQL, Dockerfile, or compose file was modified. Existing
historical runtime claims in `docs/PROVENANCE.md` were treated as provenance,
not repeated as fresh tests. The installed runtime path observation remains
read-only evidence and is not a fresh AutoUpdater acceptance test.

## 10. Provenance

The native behavior lineage is recorded in `docs/PROVENANCE.md`. The audited
donors available locally were:

```text
Shyalya/tortoise-wow       1f9497e0f42bfc1055841bb6ebdc7caa3515de0b
cmangos/playerbots         076045efa835da9aab7c943bca752aebe1baad
mod-playerbots             5397110cba484a9b7209bc9f632652e9d4bd6a70
mangoszero/server          1817ae11974a3285f8c963d1d19463c1411a422d
cmangos/mangos-classic     9b682be617ac61c127c23aa60d7b4ffbc0ce37e6
```

The correct policy remains: harvest behavior, not architecture. The current
audit supports retaining mature Vanilla behavior while removing the remaining
later-expansion data, stale host paths, and silent compatibility assumptions.
