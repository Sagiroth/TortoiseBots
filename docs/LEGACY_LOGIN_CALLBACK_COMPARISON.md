# Legacy Headless Login Callback Comparison

**Date:** 2026-08-21
**Question:** How do mature PlayerBots implementations complete an async character login when bot sessions are intentionally outside `World::m_sessions`?

## Local source pins

- `cmangos-playerbots@076045efa835da9aab7caa943bca752aebe1baad`
- `shyalya-tortoise-wow@1f9497e0f42bfc1055841bb6ebdc7caa3515de0b`
- `mangoszero-server@1817ae11974a3285f8c963d1d19463c1411a422d`

## Findings

| Reference | Callback mechanism | Session identity / ownership implication |
| --- | --- | --- |
| CMaNGOS PlayerBots | `PlayerbotLoginQueryHolder` stores bot identity plus a `PlayerbotHolder*`; async completion marks it ready, and a later bot-manager tick handles it. | Does not route bot completion through the account-keyed normal callback. It retains bot-manager ownership, which TortoiseBots must not copy. |
| Shyalya | Stores `holder* → {guid, masterAccount}`. Its bot callback creates a null-socket session and directly calls `HandlePlayerLogin`; it does not call `sWorld.AddSession`. | Bypasses the normal account callback and relies on PlayerBots ownership of the free-floating session. Not compatible with World-owned TortoiseBots sessions. |
| MangosZero | Uses a derived holder carrying `PlayerbotHolder*` and master account, then a dedicated bot callback creates a free-floating `WorldSession` and directly calls `HandlePlayerLogin`. Normal callbacks remain account-routed. | Separates bot and Network callback paths, but retains PlayerbotHolder ownership. Not suitable for the TortoiseBots lifetime model. |

Evidence: `cmangos-playerbots/playerbot/PlayerbotLoginMgr.cpp:23-36,170-193,299-307`; `shyalya-tortoise-wow/src/modules/PlayerBots/playerbot/PlayerbotMgr.cpp:137-154,168-226`; `mangoszero-server/src/game/WorldHandlers/CharacterHandler.cpp:105-123,188-197,216-234,340-347`.

## TortoiseBots conclusion

The generic, World-owned equivalent is to carry **transport provenance** in the existing `LoginQueryHolder`, not a `WorldSession*` and not a PlayerBots holder:

```text
Network holder  → FindSession(accountId)
Headless holder → FindHeadlessSession(characterGuid)
```

The callback must not infer Headless from GUID presence: a real Network reclaim of a Headless character uses the same GUID. The holder transport is fixed when `WorldSession::LoginPlayer` schedules the query, so this routing preserves the Phase 3 reclaim flow. `HandlePlayerLogin` still transfers the existing player to the new Network session; `BotManager` then observes that the GUID no longer belongs to its Headless registry and releases the bot record.

No external implementation code was copied.
