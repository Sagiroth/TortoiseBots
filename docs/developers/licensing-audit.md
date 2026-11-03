---
id: developers-licensing-audit
title: Donor License Compatibility Audit
category: developers
summary: Audit record of donor source licenses, compatibility with core AGPL-3.0, and release gates.
tags: [license, legal, audit, compliance]
relates_to:
  - developers-architecture
  - developers-donor-hierarchy
---

# Donor licence compatibility audit

**Status:** Open  
**Scope:** TortoiseBots donor-derived source and its combination with
`tortoise-wow/tortoise-wow`  
**Baseline reviewed:** repository `main` at
`0774a3e10c5529b315cd7f55306ebeadd94c0f58`

This is an engineering provenance record, not legal advice. A repository-level
licence file is evidence, but retained file headers and upstream history can
grant different or additional terms. Compatibility must be established for the
actual files included in a build.

## Initial donor matrix

| Donor | Pinned evidence reviewed | Use recorded in provenance | Current finding |
| --- | --- | --- | --- |
| `Shyalya/tortoise-wow@1f9497e` | Root `LICENSE`: AGPL-3.0 | Foundational Tortoise/Vanilla PlayerBots tree copied as a baseline | Repository-level grant is AGPL-3.0; retained upstream notices still need a file-by-file map |
| `cmangos/playerbots@c33dfac` | No root `LICENSE` or `COPYING` found at the pinned tree root | Underlying PlayerBots behavior and vendored lineage | **Unresolved:** establish grants from file notices and upstream history before treating code as GPL-2.0-only or GPL-2.0-or-later |
| `cmangos/mangos-classic@9b682be` | Root `LICENSE`: GPL-2.0 text | Host/API patterns, mostly reimplemented | No copied body is identified by the current provenance entries; if one is added, record the exact grant variant |
| `mangoszero/server@1817ae1` | Root `LICENSE`: GPL-3.0 text | Lifecycle patterns, recorded as reimplemented | Compatible as a reference; any future copied body must retain its grant and notices |
| `mod-playerbots/mod-playerbots@5397110cba48` | Root `LICENSE`: GPL-2.0 text; sampled ported source headers grant version 2 or later | Modern generic strategies and nine class contexts | Headered GPL-2.0-or-later files can move to GPLv3-compatible terms; audit all copied/ported files rather than extrapolating from the sample |
| `mod-playerbots/mod-playerbots@b6696bdb` (data only) | Root `LICENSE`: GPL-2.0 text; `data/sql/playerbots/base/ai_playerbot_texts.sql` carries no file header | Full `ai_playerbot_texts` seed (1,739 rows) ported as `data/sql/world/20260913090000_world.sql`, INSERT-only | **Unresolved:** headerless data file, exact grant variant (only vs or-later) not established from the file itself; release gate applies before distribution |
| `Shyalya/tortoise-wow@05ba90b2` (data only) | Repository root `LICENSE`: AGPL-3.0; the `modules/mod-playerbots` subtree carries no `LICENSE` of its own and `sql/world/classic/ai_playerbot_weightscales.sql` has no file header | Vanilla spec weight scales (28 spec rows, 180 stat-weight rows) ported as `data/sql/world/20260916090001_world.sql` | **Unresolved:** headerless data file inside an AGPL-3.0 repository whose own upstream grant is not established from the file itself; release gate applies |
| `T-imothy/tortoise-wow@0fb4d7c` branch `mantech-turtle` (data only; file blob `7497bbaf`, last changed in `8415f1b`; Shyalya `mod-playerbots` lineage) | Fork root `LICENSE`: AGPL-3.0; the `modules/mod-playerbots` subtree carries no `LICENSE`, `COPYING` or `NOTICE`; `sql/world/classic/ai_playerbot_named_location.sql` has no file header | `FISH_LOCATION` fishing spots ported as `data/sql/world/20261013150000_world.sql`: 52,766 of the dump's 54,038 rows (maps 0 and 1; 1,264 instance-map rows and 8 rows with no terrain height left out). The 17 `AV_*` named points (map 30) are ported as `data/sql/world/20261013100000_world.sql`: the same dump, a DELETE of the `AV_` names, then the 17 rows | **Unresolved:** headerless data file inside an AGPL-3.0 repository, same open status as the `05ba90b2` weightscales row above; owner approved shipping on 2026-10-13 |
| `jrad7/mod-dungeon-clear@da0bdfbc` | Root `LICENSE`: AGPL-3.0-or-later; `DungeonClearMath.cpp` carries the AzerothCore AGPL v3 header | The pull aggro-count estimate (`EstimateAggroCount`), rewritten as `ai/playerbot/PullAggroEstimatePolicy.h` with the vanilla core's wake-up rules; no block copied verbatim | AGPL into AGPL; the file header names the donor file, function and commit |
| `jrad7/mod-dungeon-clear@da0bdfbc` (walk ring) | Root `LICENSE`: AGPL-3.0-or-later; `DungeonClearMath.cpp`'s header (the base of `PullAggroEstimatePolicy.h`) | The walk/stand/fight-point ring, its call-for-help hops and the drag sweep (`EstimateAggroRing`, `ReadPullJoiners`, `ReadPullAhead`) are new code written from TortoiseBots' own measurements; no donor source was read or copied for it | AGPL into AGPL; the new code adds no donor-derived block, and the file's existing header names the donor file, function and commit |
| `jrad7/mod-dungeon-clear@da0bdfbc` (rez hold) | Root `LICENSE`: AGPL-3.0-or-later; `DcRezDecision.h` carries the AzerothCore AGPL v3 header | The resurrection-hold decision idea (`DcRezDecision`: hold while a living resurrector can act, wait for its mana, fall back to the revive order), rewritten as `runtime/RezHoldPolicy.h` with 1.12 classes and spells; no block copied verbatim | AGPL into AGPL; the file header names the donor file, function and commit |
| `tortoise-wow/tortoise-wow` | Root `LICENSE`: AGPL-3.0 | Target core | The combined build must satisfy AGPL-3.0 and every included module component's compatible terms |

## Release gate

Before publishing a binary or operating a combined network service:

- [ ] Map every copied or ported file to its donor repository, pinned commit and
      source path (donor table above; per-change detail in the pull request
      and Git history).
- [ ] Record the effective expression for each mapped file
      (`GPL-2.0-only`, `GPL-2.0-or-later`, `GPL-3.0`, `AGPL-3.0`, or
      unresolved) and the evidence supporting it.
- [ ] Preserve donor copyright and licence notices in copied/ported files.
- [ ] Resolve every GPL-2.0-only or unclear component before combining it with
      the AGPL-3.0 target core, either through additional permission or a clean
      compatible replacement.
- [ ] Reconcile `NOTICE.md`, README badges/metadata and release artifacts with
      the completed matrix.
- [ ] Have the completed matrix reviewed by a qualified open-source licensing
      professional if the project will be publicly deployed or distributed.

Until those checks are complete, the documentation must not state categorically
that all standalone module source is uniformly GPL-2.0 or that the combined
binary is automatically AGPL-3.0 merely because the target core is.
