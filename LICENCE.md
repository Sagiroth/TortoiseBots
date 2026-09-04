# Licence

## TortoiseBots

TortoiseBots is a native module that **links against** the Tortoise core. It
contains original source plus copied, ported and reimplemented work from several
donor lineages. The licence that applies to a file follows its retained notice,
the donor grant and the project's own grant for original work; these must not be
collapsed into a blanket "GPL-2.0" label without checking the exact variant.

Every file ported from a donor must retain its original copyright and licence
notice. See `ai/playerbot/` headers,
[`docs/PROVENANCE.md`](docs/PROVENANCE.md), and
[`docs/LICENSE_AUDIT.md`](docs/LICENSE_AUDIT.md) for source commits and the
open compatibility audit. Do not strip those notices.

## Upstream — Penqle/tortoise-wow

The canonical target core for this module is:

* **Penqle/tortoise-wow** — <https://github.com/Penqle/tortoise-wow>
* Licence: **GNU Affero General Public License v3.0 (AGPL-3.0)**
* Full text: <https://github.com/Penqle/tortoise-wow/blob/main/LICENSE>

A combined build may be distributed or operated only when every included
TortoiseBots component is available under terms compatible with AGPL-3.0. Once
that condition is established, the combined work is governed by AGPL-3.0,
including its Corresponding Source requirement for network use. The donor
compatibility audit is still open; do not rely on the target core's licence
alone to resolve an incompatible or unclear donor grant.

## Donor / reference projects

| Project | Licence | Notes |
| --- | --- | --- |
| `Shyalya/tortoise-wow` | AGPL-3.0 at pinned repository root | Turtle 1.18.1 donor baseline; verify retained upstream notices per copied file |
| `cmangos/playerbots` | No root licence file found at pinned commit | PlayerBots behavior; resolve through file notices and upstream history |
| `cmangos/mangos-classic` | GPL-2.0 at pinned repository root | Host API reference; determine only/or-later if code is copied |
| `mangoszero/server` | GPL-3.0 at pinned repository root | Lifecycle reference/reimplementation |
| `mod-playerbots` (`AzerothCore`) | GPL-2.0 at pinned repository root; sampled ported headers grant GPL-2.0-or-later | Newer behavior; verify every copied/ported file |

Upstream licences are preserved — see each referenced repository for its full
licence text.

## What this means for you

* Follow the exact licence and copyright notices applicable to each file; do
  not assume GPL-2.0-only and GPL-2.0-or-later are interchangeable.
* Do not distribute or operate a combined build until the donor compatibility
  matrix is complete and every included grant is compatible with AGPL-3.0.
* Keep notices intact and record substantial ports in
  [`docs/PROVENANCE.md`](docs/PROVENANCE.md).
* Track evidence and unresolved items in
  [`docs/LICENSE_AUDIT.md`](docs/LICENSE_AUDIT.md).
