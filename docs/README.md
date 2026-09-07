# TortoiseBots documentation

Use this folder by purpose rather than reading every document on every task.

| Document | Purpose | Read when |
| --- | --- | --- |
| [`PLAN.md`](PLAN.md) | Durable architecture rules and roadmap | Planning or implementing PlayerBots work |
| [`HOST_API.md`](HOST_API.md) | Current implemented core/module contract | Touching sessions, lifecycle, packets, commands, build/module integration or core seams |
| [`PLAYER_CONTROL.md`](PLAYER_CONTROL.md) | Public owned-bot command and addon contract | Exposing or changing player-facing bot controls |
| [`PROVENANCE.md`](PROVENANCE.md) | Append-oriented source lineage and validation history | Porting/adapting donor behavior or checking attribution |
| [`LICENSE_AUDIT.md`](LICENSE_AUDIT.md) | Module licensing and third-party code records | Licensing or copyright review |

The active implementation path is:

```text
PLAN -> relevant HOST_API/PROVENANCE detail
```

Read [`AGENTS.md`](../AGENTS.md) for repository working, validation and safety
rules.
