# Feature-surface comparison: TortoiseBots and the documented Shyalya baseline

**Scope.** Read-only source/documentation review at TortoiseBots
`88afd3b3` (2026-08-31).  The requested comparison excludes guided/custom
dungeon encounter work.  This repository does not contain a Shyalya checkout,
so statements about Shyalya are limited to the pinned donor description in this
repository; they are not a fresh source-to-source audit of the remote fork.
The documented donor is `Shyalya/tortoise-wow` branch
`playerbots-integration-gh` at `1f9497e0f42bfc1055841bb6ebdc7caa3515de0b`
([audit evidence](../archive/PLAYERBOTS_AUDIT.md#L1203-L1212)).

## Bottom line

For the **core player-controlled bot experience**, this is not a thin rewrite:
the full strategy runtime, generic behavior, and nine Vanilla class families
are in the compiled graph.  The donor provenance says the base engine,
strategy/action/trigger/value stack was brought from the Shyalya-compatible
baseline, then the generic and class behavior was forward-ported from
`mod-playerbots` ([PROVENANCE.md:17-19](../PROVENANCE.md#L17-L19)).

The more accurate position is therefore:

* **Underlying AI coverage:** broadly comparable to the documented Shyalya
  baseline for Vanilla classes and ordinary follow, combat, healing, tanking,
  CC, quests, loot, travel, group, PvP, economy and interaction behaviors.
  Presence in the source graph is not equivalent to a gameplay acceptance
  claim.
* **Population/queue services:** TortoiseBots has module-owned, default-off
  implementations for random population, auto-creation, LFT filling, AH
  supply, and BG demand filling.  These are explicit current features, pending
  their stated generic core APIs ([PLAN.md:35-39](../PLAN.md#L35-L39)).
* **What still prevents a “complete Shyalya-like product” claim (excluding
  guided dungeons):** operator and player control UX is narrower, several
  donor helpers are deliberately excluded, and broad Turtle/class/custom-race
  behavior remains much less runtime-proven than its source presence suggests.

## Evidence for implemented gameplay surface

| Area | Current source evidence | Interpretation |
| --- | --- | --- |
| Nine Vanilla classes and specs | The build explicitly permits only Druid, Hunter, Mage, Paladin, Priest, Rogue, Shaman, Warlock and Warrior, and includes every class `.cpp` tree ([TortoiseBots.cmake:124-165](../../TortoiseBots.cmake#L124-L165)). `AiFactory` creates a dedicated context for each ([AiFactory.cpp:22-82](../../ai/playerbot/AiFactory.cpp#L22-L82)) and selects per-class/spec combat strategies such as healing, tank assist, DPS assist, CC, AoE, buffs, pets, poisons, totems and curses ([AiFactory.cpp:293-517](../../ai/playerbot/AiFactory.cpp#L293-L517)). | All nine playable Vanilla classes have real class contexts, not a generic fallback. This is the main parity signal. |
| Core combat/group/death loop | `PlayerbotAI` creates combat, non-combat, dead and reaction engines ([PlayerbotAI.cpp:175-191](../../ai/playerbot/PlayerbotAI.cpp#L175-L191)); the active CMake graph compiles generic strategies plus all actions, triggers and values ([TortoiseBots.cmake:124-165](../../TortoiseBots.cmake#L124-L165)). | Follow/combat/heal/tank/CC/loot/death behavior is compiled from the mature engine rather than reimplemented as a narrow controller. |
| Quest, loot, travel, social/economy commands | The mature command handler registers quest, loot, corpse run, taxi, talents, strategy, formation, mail, crafting, AH, guild, BG, movement, skill and other actions ([ChatCommandHandlerStrategy.cpp:29-113](../../ai/playerbot/strategy/generic/ChatCommandHandlerStrategy.cpp#L29-L113)). The AI maps relevant master packets for quest, loot roll, gossip, taxi, group/uninvite, quest share and resurrection ([PlayerbotAI.cpp:193-209](../../ai/playerbot/PlayerbotAI.cpp#L193-L209)). | The capabilities are present behind the inherited command/action layer, although their UI exposure differs from Shyalya. |
| Random bots | `RandomBotService` loads a bounded database-backed candidate pool once at startup, avoiding per-tick scans ([RandomBotService.cpp:157-217](../../runtime/RandomBotService.cpp#L157-L217)); auto-create uses the generic `CharacterCreation` API and has bounded retry/failure handling ([RandomBotService.cpp:286-411](../../runtime/RandomBotService.cpp#L286-L411)). | This is a current capability, not the earlier historical “existing characters only” gap. |
| LFT/AH/BG population | The module has separately compiled services for LFT fill, AH market and BG queueing ([TortoiseBots.cmake:70-87](../../TortoiseBots.cmake#L70-L87)). Their contracts are bounded/default-off and preserve native ownership of LFT, auctions and BG queues ([HOST_API.md:284-383](../HOST_API.md#L284-L383)). | These are explicit services beyond the core owned-bot loop; actual use requires the pending #411/#416 host APIs. |
| Turtle scope | Current advertised scope includes Goblin/High Elf plus validated Turtle spells/talents/mounts ([README.md:110-125](../../README.md#L110-L125)). The product intentionally retains Vanilla/Turtle, WSG/AB/AV and applicable raids while excluding DK/glyph/vehicle/Arena ([PLAN.md:45-45](../PLAN.md#L45-L45)). | Do not count post-Vanilla systems as missing parity; they are an intentional product boundary. |

## Control surface: present, but not yet Shyalya-complete UX

There are two layers, which is easy to miss:

1. The **native `.bot` shell** currently implements `add`, `remove`, `follow`,
   `invite`, `uninvite`, `stay`, `list`, `stats`, `pullback`, `summon`,
   `command`, and help ([BotCommands.cpp:772-828](../../commands/BotCommands.cpp#L772-L828)).
   `pullback` and `summon` are current source but their introducing commit calls
   them a POC; they should not be marketed as accepted gameplay without a run.
2. `.bot command <bot> <command>` forwards to `PlayerbotAI::HandleCommand`
   ([BotCommands.cpp:267-291](../../commands/BotCommands.cpp#L267-L291)), so
   many inherited commands are reachable per selected bot.  This substantially
   reduces the apparent command gap, but it is less discoverable and less
   convenient than exposing the common commands directly or via an addon.

The strongest documented delta versus Shyalya is therefore **not lack of the
underlying action code**, but its operator-facing surface.  The local historical
audit describes Shyalya's broader interface as `.rndbot`, chat triggers, `@`
filters and addon/TCP state queries, while recording the native shell as
partial compatibility ([PLAYERBOTS_AUDIT.md:797-812](../archive/PLAYERBOTS_AUDIT.md#L797-L812)).
Current source confirms the module registers only the `bot` core command
([BotChatAdapter.cpp:15-31](../../host/BotChatAdapter.cpp#L15-L31)); no native
`.rndbot`, `.ahbot`, or `.perfmon` command family is registered here.

The previously-audited “no addon” finding is obsolete: current documentation
links the external **TortoiseBots Manager** addon, with roster and
Spawn/Summon/Follow/Stay/Invite/Pull controls
([README.md:231-236](../../README.md#L231-L236)).  Its source is not in this
checkout, so this review cannot compare it with Shyalya's addon/TCP state query
surface or call it feature-complete.

## Remaining differences and gaps worth addressing

### Product-facing gaps (highest value)

* **Expose and document common mature commands.** The action layer has a large
  command vocabulary, but it is hidden behind `.bot command` and a bot-name
  argument.  A deliberate native/addon command contract for strategy, loot,
  role, formation, pull, travel, quest, gear and status operations would close
  more of the user-visible gap than importing more combat files.
* **Random/AH/BG operational controls.** Services exist, but there is no
  native management command family matching documented Shyalya `.rndbot`,
  `.ahbot` and performance controls.  Configuration is the current interface;
  decide which safe status/start/stop/diagnostic controls should be exposed to
  GMs/operators.
* **Real-client and gameplay acceptance.** Packet bridging exists
  ([BotPacketAdapter.cpp:22-83](../../host/BotPacketAdapter.cpp#L22-L83)), but
  the host contract says real-client incoming delivery is still a manual
  acceptance boundary ([HOST_API.md:189-192](../HOST_API.md#L189-L192)).
  The source inventory supports “implemented”; it does not prove that all
  commands or all classes behave correctly with a real client.
* **Turtle coverage validation.** The documented unresolved work is broad
  reworked-talent/class testing, High Elf terrain movement/death acceptance and
  physical collection-mount use ([PLAYERBOTS_AUDIT.md:295-302](../archive/PLAYERBOTS_AUDIT.md#L295-L302)).
  These are acceptance gaps rather than absent class trees.
* **Mob avoidance/advanced navigation.** The core path-area filters and random
  nav points are known stubs, so the module deliberately removes/fails closed
  inherited avoidance and cache-generation paths
  ([PLAYERBOTS_AUDIT.md:613-628](../archive/PLAYERBOTS_AUDIT.md#L613-L628)).
  Ordinary path calculation remains usable, but this is a material behavioral
  difference from a host with fully implemented navigation filters.

### Deliberately excluded donor behavior

The current positive source graph blocks Death Knight, glyph, vehicle,
Karazhan, Arena, RTSC and BossAura families
([TortoiseBots.cmake:173-179](../../TortoiseBots.cmake#L173-L179)).  The
historical port record specifically lists advanced fishing, guardian-oriented
`PetsAction`, extended trade reporting, some maintenance/inventory reporting,
and expansion registrations as excluded ([PROVENANCE.md:47](../PROVENANCE.md#L47-L47)).
The later cleanup confirms removal of donor manager/login/command-server,
automatic donor-LFG and advanced-fishing families while retaining the relevant
Vanilla/Turtle classes, raids, BG tactics, taxi and meeting-stone concepts
([PROVENANCE.md:230-240](../PROVENANCE.md#L230-L240)).

These should be triaged before being called “missing”:

* DK/glyph/vehicle/Arena are outside the stated Vanilla/Turtle product.
* Karazhan and custom-dungeon strategy work are excluded from this comparison,
  per the requested scope.
* Guardian control, advanced fishing, extended reporting and some convenience
  helpers are genuine optional parity/ergonomics candidates if players expect
  the whole donor command catalogue.

## Architecture and maintainability assessment

The architecture is demonstrably cleaner at its module/core seam than the
documented Shyalya integration—not because it has more AI features, but because
it isolates ownership:

* The core exports a generic network/headless transport distinction and owns
  session lifetime; the module owns bot records and AI
  ([HOST_API.md:59-94](../HOST_API.md#L59-L94)).
* Human reclaim and the durable master relationship are explicit module/core
  responsibilities ([HOST_API.md:118-125](../HOST_API.md#L118-L125)).
* The module uses five focused host adapters rather than hard-wired calls
  ([HOST_API.md:127-143](../HOST_API.md#L127-L143)); provenance records the
  Shyalya host surface as roughly 80 files and the native replacement as a
  generic transport seam ([PROVENANCE.md:12](../PROVENANCE.md#L12-L12)).
* Donor manager, random-manager and login-manager sources are intentionally
  absent from the build graph ([TortoiseBots.cmake:89-91](../../TortoiseBots.cmake#L89-L91)).

That is strong evidence of lower coupling and a clearer ownership model.  It
is **not** evidence yet that maintenance will cost less in practice: that needs
rebase history and real operations.  The primary maintenance risk today is
different: broad globs compile all generic action/trigger/value files, so a
future donor drop needs source-graph review despite the explicit class list
([TortoiseBots.cmake:124-179](../../TortoiseBots.cmake#L124-L179)).

## Practical conclusion

Once #411 and #416 land and the module is rebased/validated against their
merged form, the remaining work to feel close to Shyalya—without guided
dungeons—is mostly a **productization and acceptance backlog**:

1. validate owned-bot play with a real client across all nine classes and key
   roles;
2. make the high-value inherited commands discoverable and safe in the native
   shell/addon;
3. add GM/operator controls and status for random/LFT/AH/BG services;
4. close Turtle custom-race/talent/navigation acceptance gaps; and
5. decide consciously whether the deliberately excluded convenience helpers
   (advanced fishing, guardian control, richer reporting) belong in the
   Vanilla/Turtle release.

No production code was changed for this research note.
