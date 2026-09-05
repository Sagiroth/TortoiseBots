# CCP per-bot crowd-control mark research

**Date:** 2026-09-05
**Scope:** Read-only investigation of the MicroBot/CCP references, donor
PlayerBots behavior, and the current TortoiseBots/TortoiseBotsManager control
surface. No reference repository or production code was changed.

**Revisions inspected:** `TortoiseBots b0ba267b`,
`TortoiseBotsManager c09ba8d`, `mod-playerbots 5397110c`,
`cmangos-playerbots 076045e`, `cmangos-mangos-classic 9b682be6`, and the
working Penqle core checkout containing the current `Group` API.

## Conclusion

The requested behavior is feasible, and most of the AI machinery already
exists:

```text
enemy has Circle  -> Warlock's rti cc value is circle -> Warlock CC target
enemy has Moon    -> Mage's rti cc value is moon    -> Mage CC target
```

The current product surface is only a partial slice of that behavior. It
hard-codes the public action to `cc moon`, but the underlying `rti cc` value
accepts all raid-mark names. Targeting an owned bot before the current
`CC Moon` action already selects that bot as the executor. A generalized
`CC <mark>` assignment/action would therefore be a small module/addon control
extension, not a new CC engine.

The phrase “only the marked enemy” needs one qualification: `rti cc` stores a
mark preference, not a permanent target GUID. The mature AI will prefer the
currently valid unit carrying that icon, maintain an existing CC through its
normal current-CC checks, and avoid that unit as a normal attack target. If
the icon is absent, the unit is out of sight/range, the spell is illegal for
the creature type, or the CC spell is unavailable, normal AI fallback rules
still apply. A strict exclusive lock would be a different feature.

## What CCP demonstrates

CCP’s mark panel has separate CC and focus modes (`ccmark` and `focusmark`),
and its button handler serializes the selected mark as a plain `.z` command
(`.z ccmark circle`, `.z ccmark moon`, etc.) rather than using a private
opcode ([CCP.lua:519-526](../../playerbots-references/MicroBot%20Data/CCP/CCP.lua#L519-L526)).
The XML includes buttons for all eight raid icons, including Circle and Moon
([CCP_Commands.xml:944-1083](../../playerbots-references/MicroBot%20Data/CCP/CCP_Commands.xml#L944-L1083)).

The reference wiki defines `ccmark` as a **dynamic** command: when one
companion is targeted it affects that companion; otherwise it can apply to the
companion set. It also explicitly describes `ccmark [raidmark]` as a
per-companion CC-mark assignment and supports multiple marks in assignment
order ([microbot-wikidot-synthesis.md:170-177](../../playerbots-references/MicroBot%20Data/microbot-wikidot-synthesis.md#L170-L177),
[microbot-wikidot-synthesis.md:202-210](../../playerbots-references/MicroBot%20Data/microbot-wikidot-synthesis.md#L202-L210)).

CCP’s active-roster payload carries separate `focus` and `ccm` fields for each
companion and parses them into that row ([CCP_Roster.lua:163-190](../../playerbots-references/MicroBot%20Data/CCP/CCP_Roster.lua#L163-L190),
[CCP_Roster.lua:363-381](../../playerbots-references/MicroBot%20Data/CCP/CCP_Roster.lua#L363-L381)).
Its companion-info view also has a separate MARK response containing focus and
CC lists ([CCP_Roster.lua:754-780](../../playerbots-references/MicroBot%20Data/CCP/CCP_Roster.lua#L754-L780)).
That is strong protocol evidence that MicroBot stores CC preferences per
companion, rather than merely remembering one global Moon target.

The roster row click targets the companion ([CCP_Roster.lua:575-586](../../playerbots-references/MicroBot%20Data/CCP/CCP_Roster.lua#L575-L586)),
which is how the dynamic command can be used in two steps: target Warlock,
choose Circle; target Mage, choose Moon.

## Donor AI behavior

The relevant donor abstraction is a per-AI string value, not a second combat
controller:

* `RtiAction` treats `cc <mark>` as a write to the bot’s `rti cc` value
  ([mod-playerbots RtiAction.cpp:12-44](../../playerbots-references/mod-playerbots/src/Ai/Base/Actions/RtiAction.cpp#L12-L44)).
* `RtiCcTargetValue` resolves the group target icon corresponding to that
  value ([mod-playerbots RtiTargetValue.h:15-45](../../playerbots-references/mod-playerbots/src/Ai/Base/Value/RtiTargetValue.h#L15-L45)).
* `RtiCcValue` defaults to Moon and implements Save/Load, so the assignment is
  conceptually a bot preference ([mod-playerbots RtiValue.cpp:10-24](../../playerbots-references/mod-playerbots/src/Ai/Base/Value/RtiValue.cpp#L10-L24)).
* `FindNonCcTargetStrategy::IsCcTarget` walks the group’s bot AIs and treats
  each bot’s selected `rti cc` icon as a protected CC target, while retaining
  Moon as the default fallback ([mod-playerbots TargetValue.cpp:56-88](../../playerbots-references/mod-playerbots/src/Ai/Base/Value/TargetValue.cpp#L56-L88)).

The CMaNGOS PlayerBots code has the same `cc <mark>` value path. Its
`MANGOSBOT_TWO` branch additionally calls a three-argument
`SetTargetIcon(index, botGuid, targetGuid)` when a bot marks a target
([cmangos-playerbots RtiAction.cpp:86-100](../../playerbots-references/cmangos-playerbots/playerbot/strategy/actions/RtiAction.cpp#L86-L100)).
That is a host-specific per-bot raid-icon extension used by the WotLK-side
donor; it is not required for the value-based preference described above.

## Current Tortoise implementation

The current Tortoise AI retained the useful Vanilla-compatible pieces:

* `RtiAction` parses `cc <mark>` and writes `rti cc`; it does not restrict the
  mark name ([RtiAction.cpp:9-35](../ai/playerbot/strategy/actions/RtiAction.cpp#L9-L35)).
* `RtiCcValue` defaults to Moon and supports Save/Load
  ([RtiValue.h:8-23](../ai/playerbot/strategy/values/RtiValue.h#L8-L23)).
* `RtiTargetValue` maps Circle, Moon, and the other six icons, and
  `RtiCcTargetValue` is the `rti cc` specialization
  ([RtiTargetValue.h:19-68](../ai/playerbot/strategy/values/RtiTargetValue.h#L19-L68)).
* CC target selection gives the bot’s configured RTI target first priority
  ([CcTargetValue.cpp:20-39](../ai/playerbot/strategy/values/CcTargetValue.cpp#L20-L39)).
* CC spell actions explicitly use `cc target` and `reach spell`, and the
  trigger fires only when a valid CC target has no current CC aura
  ([GenericSpellActions.h:420-430](../ai/playerbot/strategy/actions/GenericSpellActions.h#L420-L430),
  [GenericTriggers.cpp:641-650](../ai/playerbot/strategy/triggers/GenericTriggers.cpp#L641-L650)).
* Mage and Warlock default class factories include the `cc` strategy, so this
  is not dependent on enabling a hidden LLM or separate controller
  ([AiFactory.cpp:320-336](../ai/playerbot/AiFactory.cpp#L320-L336),
  [AiFactory.cpp:501-517](../ai/playerbot/AiFactory.cpp#L501-L517)).

The public command path is the limiting layer:

* `ParseAction` accepts only `focus skull` and `cc moon`
  ([BotCommands.cpp:1107-1150](../commands/BotCommands.cpp#L1107-L1150)).
* The CC handler looks up the global Moon icon, not an arbitrary requested
  icon ([BotCommands.cpp:1202-1222](../commands/BotCommands.cpp#L1202-L1222)).
* Once an executor is selected, it writes `cc moon` into that executor’s
  `rti cc` value and immediately casts; selecting an owned bot first forces
  that bot rather than falling back to another party bot
  ([BotCommandContext.cpp:376-411](../commands/BotCommandContext.cpp#L376-L411),
  [BotCommands.cpp:1232-1249](../commands/BotCommands.cpp#L1232-L1249)).
* The addon exposes only `CC Moon`, and its documentation already advertises
  the existing targeted-bot workflow ([TortoiseBotsManager/Constants.lua:111-145](../../TortoiseBotsManager/Constants.lua#L111-L145),
  [TortoiseBotsManager/UI.lua:83-97](../../TortoiseBotsManager/UI.lua#L83-L97),
  [TortoiseBotsManager/README.md:65-70](../../TortoiseBotsManager/README.md#L65-L70)).

There is an immediate but non-product diagnostic path today:
`.bot command <WarlockName> rti cc circle` forwards the mature command to one
owned bot ([BotCommands.cpp:501-531](../commands/BotCommands.cpp#L501-L531)).
After Circle is placed on an enemy, the Warlock’s existing CC strategy can use
that circle target. This path is deliberately documented as transitional and
does not provide a structured ACK, mark validation, or immediate cast.

## Important host limitation

The target Penqle core’s `Group` owns one global target GUID per icon: its API
has only `SetTargetIcon(uint8, ObjectGuid)` and `GetTargetIcon(uint8)`
([tortoise-wow Group.h:315-320](../../tortoise-wow/src/game/Group/Group.h#L315-L320)),
with a single eight-entry `m_targetIcons` array ([Group.h:448-454](../../tortoise-wow/src/game/Group/Group.h#L448-L454)).
Do not port the CMaNGOS three-argument overload into core merely to obtain this
feature. The desired behavior can remain module-owned: the group icon identifies
the enemy, while each bot’s `rti cc` value identifies which icon that bot owns.
That preserves the modular boundary and avoids adding bot-aware state to core
group systems.

## Feasibility matrix

| Desired behavior | Current status | Evidence / gap |
| --- | --- | --- |
| Target one owned bot and assign Moon CC | **Partially available now** | Current `CC Moon` selects an explicitly targeted bot and writes its `rti cc=moon`. |
| Warlock owns Circle while Mage owns Moon | **AI-feasible; public UX missing** | `rti cc` and icon lookup accept Circle/Moon independently; public parser only accepts Moon. |
| Each bot keeps recasting its assigned mark | **Supported by mature AI while valid** | `cc target`, current-CC, reach, and class CC strategies are already wired. |
| Strictly never CC another target | **Not guaranteed by mark preference alone** | Missing/out-of-range/illegal preferred target can invoke normal CC selection fallback. |
| Assignment survives bot AI recreation | **Persistence path exists but needs explicit save semantics** | Values implement Save/Load and DB store can serialize them ([PlayerbotDbStore.cpp:16-62](../ai/playerbot/PlayerbotDbStore.cpp#L16-L62)); the `rti` command itself does not call DB save. |
| Separate per-bot target icons in Penqle core | **Not available and unnecessary** | Core has only global icon slots; keep preference state in module AI. |

## Smallest viable product slice

If implemented later, the cleanest slice is:

1. Generalize the native intent from `cc moon` to `cc <mark>` over a fixed
   allow-list of the eight RTI names. Keep `cc moon` as a compatibility alias.
2. Resolve the requested icon instead of hard-coding Moon, require a live
   target, and preserve the existing selected-bot-first executor rule.
3. Write `rti cc <mark>` on the selected executor and use the mature class CC
   action/target path for maintenance. Avoid a new per-bot GUID map.
4. Add an addon assignment workflow with two explicit steps: mark the enemy
   using normal raid marking, target the owned bot in the Party tab, then choose
   an icon from `CC Mark`. The Party tab shows the matching icon from the
   server's separate `TBM:CC_ASSIGN_*` snapshot.
5. Define clear/reassign behavior and persist assignment changes through the
   existing DB store. The draft implements the write path; logout/relogin and
   AI-recreation acceptance remain a runtime test gate.

This keeps the CCP-proven UX, uses the existing Tortoise AI seam, and avoids
the donor’s expansion-specific Group API.
