# TortoiseBots documentation

Use this folder by purpose rather than reading every document on every task.

| Document | Purpose | Read when |
| --- | --- | --- |
| [`STATUS.md`](STATUS.md) | Current tested baseline, open issues and immediate next work | Start here for any new task |
| [`PLAN.md`](PLAN.md) | Durable architecture rules and roadmap | Planning or implementing PlayerBots work |
| [`HOST_API.md`](HOST_API.md) | Current implemented core/module contract | Touching sessions, lifecycle, packets, commands, build/module integration or core seams |
| [`PROVENANCE.md`](PROVENANCE.md) | Append-oriented source lineage and validation history | Porting/adapting donor behavior or checking attribution |
| [`PLAYERBOTS_AUDIT.md`](PLAYERBOTS_AUDIT.md) | Historical deep-audit findings and evidence | Investigating a specific audit finding or old validation claim |
| [`PLAYERBOTS_HANDOVER.md`](PLAYERBOTS_HANDOVER.md) | Historical PR #13 audit handover | Only when reconstructing that completed audit phase |

The active implementation path is:

```text
STATUS -> PLAN -> relevant HOST_API/PROVENANCE detail
```

The audit and handover are evidence records, not the current execution plan.
Older design proposals that were removed from the active docs remain available
through Git history.

Read [`AGENTS.md`](../AGENTS.md) for repository working, validation and safety
rules.
