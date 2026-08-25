# TortoiseBots documentation

Use this folder by purpose rather than reading every document on every task.

| Document | Purpose | Read when |
| --- | --- | --- |
| [`PLAN.md`](PLAN.md) | Durable architecture rules and roadmap | Planning or implementing PlayerBots work |
| [`HOST_API.md`](HOST_API.md) | Current implemented core/module contract | Touching sessions, lifecycle, packets, commands, build/module integration or core seams |
| [`PROVENANCE.md`](PROVENANCE.md) | Append-oriented source lineage and validation history | Porting/adapting donor behavior or checking attribution |

The active implementation path is:

```text
PLAN -> relevant HOST_API/PROVENANCE detail
```

Historical audit and handover records are not part of the active docs.
If retained, they live under `docs/archive/` — older design proposals remain
available through Git history.

Read [`AGENTS.md`](../AGENTS.md) for repository working, validation and safety
rules.
