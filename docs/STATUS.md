# TortoiseBots current status

**Target:** Tortoise WoW 1.18.1 / Penqle core  
**Status:** F-03/F-27 core/data closure complete as far as local evidence allows; architecture freeze ready for review

This file is the short resume point for active work. It should stay current and
small. Historical investigation and validation detail belongs in the audit and
provenance records.

## Known-good baseline

The current integration checkpoint is:

```text
TortoiseBots code:           07cf7976c546fac27083c7b46e73299c25b095f3
Pinned Tortoise core:        7353989c94399f80572a2f8ec2eb73c63a6c79f8
TortoiseBots branch:         cleanup/f03-f27-code-freeze
Core branch:                 cleanup/f03-f27-code-freeze
```

PR #13 merged the module-side Turtle 1.18.1 cleanup/audit closure into `main`.
Later README/documentation commits do not replace the behavior baseline above.

The final documentation commit on the TortoiseBots branch will supersede the
code checkpoint above; use its full SHA for the final pair.

## Current architecture

TortoiseBots is an optional native module.

```text
Tortoise core
    |
    | generic Headless / lifecycle / packet / command seams
    v
TortoiseBots
    |
    +-- BotManager
    +-- PlayerbotAIAdapter / PlayerbotAIStorage
    +-- host adapters
    +-- mature PlayerbotAI
```

Session invariant:

```text
one account
    +-- <= 1 Network session
    +-- N Headless character sessions
```

`World` owns Network and Headless `WorldSession` lifetime. TortoiseBots owns bot
records and AI. Headless sessions are character-GUID keyed; normal Network
sessions remain account keyed.

The core must not regain `GetBot()`, `m_bot`, `sPlayerBotMgr`, `PlayerBotEntry`
or scattered bot-specific gameplay branches.

See [HOST_API.md](HOST_API.md) for the implemented host contract.

## Module-side work completed

The current baseline has completed the broad port/cleanup/audit phase:

- Native optional module integration
- Headless login/logout/save/relogin lifecycle
- Same-account owned bots and human reclaim
- Mature `PlayerbotAI` runtime
- Engine / Strategy / Trigger / Action / Value stack
- All nine Vanilla classes
- Follow/stay/group command path
- Packet bridge
- Vanilla/Turtle source cleanup
- Removal of large TBC/WotLK/later donor families
- Turtle Goblin / High Elf data integration
- Turtle-aware race, spell, talent and collection-mount fixes where confirmed
  against target data
- Native migrations and fail-closed cache/startup behavior
- Deep compatibility audit against the pinned core/data
- Fail-closed `tools/verify_turtle_surface.sh`

Do not start another broad donor cleanup unless a concrete defect proves that
one is necessary.

## Validation already recorded

The audited baseline records successful evidence for:

| Check | Status |
| --- | --- |
| Surface verifier | Passed |
| `git diff --check` | Passed |
| Cached native PlayerBots-on build | Passed / linked `mangosd` |
| Cached PlayerBots-off build | Passed |
| Module-absent build | Passed |
| Fresh/repeat module migrations | Passed |
| Preserved-data runtime startup | World ready |
| Headless login / AI / save / logout / relog | Passed |
| Pending add/remove cleanup | Passed |
| Human reclaim | Passed in the recorded integration spike |
| Native command fixture (`list`/`stats`/`follow`) | Passed |
| Natural group invite / mature AI accept | Passed |
| Goblin lifecycle fixture | Passed |
| High Elf lifecycle fixture | Passed |

Do not rerun this whole matrix for unrelated edits. Reuse unchanged evidence and
validate only behavior affected by the current change.

## Current blockers / next work

### F-03 — legacy PlayerBots coupling in the core

Resolved in core commit `7353989c94399f80572a2f8ec2eb73c63a6c79f8`. The cleanup
removed the legacy LFT filler, stale command/stub surface, bot slots, hardwired
RNDBOT filters, stale include paths, and bot-named core diagnostics. The
tracked `src/modules/PlayerBots` tree remains only as an explicit unsupported
historical escape hatch:

- `BUILD_LEGACY_PLAYERBOTS=OFF` is required for supported builds
- native selection is controlled only by `MODULE_TORTOISEBOTS`

Target outcome:

```text
Tortoise core
    -> generic capabilities only
TortoiseBots
    -> the single supported PlayerBots implementation
```

Keep `BUILD_LEGACY_PLAYERBOTS=OFF` for native TortoiseBots work.

### F-27 — core/data script registry mismatch

The local core/data mismatch is resolved where the local source proves the
contract: the existing `npc_teslinah` callback is now registered, and the
invalid literal ScriptName `0` is cleared by core migration
`20260825090000_world.sql`. The preserved restart reached world-ready without
those warnings.

The remaining 17 names (`custom_dungeon_portal`, `spell_druid_wrath`, and the
listed GO/item/NPC/duplicate scripts) have no implementation or legitimate
replacement in the pinned core/history. They remain explicitly unverified
Turtle content gaps; no empty scripts or guessed behavior were added. Full
per-script evidence is in [PLAYERBOTS_AUDIT.md](PLAYERBOTS_AUDIT.md).

The original classification for each mismatch was:

1. the implementation exists but is not registered,
2. the data references the wrong/stale name,
3. the intended implementation is genuinely missing, or
4. the behavior cannot yet be proven.

Do not create empty scripts merely to silence the remaining warnings.

## After F-03 / F-27

The core/data work is stable for the evidence available locally:

1. record the final documentation SHA for the exact pair here;
2. keep the architecture frozen unless manual gameplay exposes a defect;
3. move to manual gameplay acceptance with the remaining content gaps visible.

## Manual gameplay phase

The next product milestone is actual play, not another static audit.

Start with one owned bot:

- add/login
- follow/stay
- combat and targeting
- loot
- death/resurrection
- logout/relogin/human reclaim
- teleport and map transition

Then test a real 5-player dungeon group:

- tank pulls / threat
- healing
- DPS
- interrupts / CC
- loot and quests
- doors/gossip
- wipes / corpse recovery
- regrouping / instance transitions

Representative first class coverage:

```text
Warrior tank
Priest healer
Mage DPS
Rogue DPS
Hunter DPS
```

Expand to all nine classes and Turtle-specific interactions based on real
failures rather than speculative completeness work.

## Not yet claimed

The current baseline does not claim complete acceptance for:

- full manual dungeon gameplay
- broad Turtle custom class/spec/talent interactions
- all custom dungeon/zone scripts
- every custom-area movement/death path
- physical collection-mount item use
- real-client command/addon UX
- large random-bot populations
- random account/character creation
- complete AH/economy simulation

These are future validation/product tasks, not hidden compatibility promises.

## Working rules

- Treat the pinned Tortoise core/data as authoritative for Turtle behavior.
- Use donor repositories as references, not product truth.
- Keep PlayerBots optional.
- Keep host integration generic and centralized.
- Prefer module-only changes.
- Fail closed when a donor capability has no real Tortoise equivalent.
- Use the persistent builder in sibling `tortoise-docker-penqle`.
- Validate the smallest thing affected by the current change.
- Record substantial imported behavior in [PROVENANCE.md](PROVENANCE.md).

For the durable architecture rules and roadmap, continue with
[PLAN.md](PLAN.md).
