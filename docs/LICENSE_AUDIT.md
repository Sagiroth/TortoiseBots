# Donor licence compatibility audit

**Status:** Open  
**Scope:** TortoiseBots donor-derived source and its combination with
`Penqle/tortoise-wow`  
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
| `Penqle/tortoise-wow` | Root `LICENSE`: AGPL-3.0 | Target core | The combined build must satisfy AGPL-3.0 and every included module component's compatible terms |

## Release gate

Before publishing a binary or operating a combined network service:

- [ ] Map every copied or ported file to its donor repository, pinned commit and
      source path in `docs/PROVENANCE.md`.
- [ ] Record the effective expression for each mapped file
      (`GPL-2.0-only`, `GPL-2.0-or-later`, `GPL-3.0`, `AGPL-3.0`, or
      unresolved) and the evidence supporting it.
- [ ] Preserve donor copyright and licence notices in copied/ported files.
- [ ] Resolve every GPL-2.0-only or unclear component before combining it with
      the AGPL-3.0 target core, either through additional permission or a clean
      compatible replacement.
- [ ] Reconcile `LICENCE.md`, README badges/metadata and release artifacts with
      the completed matrix.
- [ ] Have the completed matrix reviewed by a qualified open-source licensing
      professional if the project will be publicly deployed or distributed.

Until those checks are complete, the documentation must not state categorically
that all standalone module source is uniformly GPL-2.0 or that the combined
binary is automatically AGPL-3.0 merely because the target core is.
