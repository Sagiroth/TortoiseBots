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

## Upstream — tortoise-wow/tortoise-wow

The canonical target core for this module is:

* **tortoise-wow/tortoise-wow** — <https://github.com/tortoise-wow/tortoise-wow>
* Licence: **GNU Affero General Public License v3.0 (AGPL-3.0)**
* Full text: <https://github.com/tortoise-wow/tortoise-wow/blob/main/LICENSE>

A combined build may be distributed or operated only when every included
TortoiseBots component is available under terms compatible with AGPL-3.0. Once
that condition is established, the combined work is governed by AGPL-3.0,
including its Corresponding Source requirement for network use. The donor
compatibility audit is still open; do not rely on the target core's licence
alone to resolve an incompatible or unclear donor grant.

## Attribution (author notice)

TortoiseBots is created and maintained by **Sagiroth**
(<https://github.com/Sagiroth/TortoiseBots>).

The module shows this attribution as its Appropriate Legal Notice (AGPL-3.0
section 5(d)): in the worldserver startup log, in a login message to every
player, and from `.bot version` / `.bot about`. Under AGPL-3.0 section 7(b), for
original TortoiseBots work, the following additional term applies:

> Modified versions, forks, repacks and bundles must preserve these author
> attributions and the source link, and must not remove or hide them from
> the places listed above. You may add your own credit alongside them.

Distributing or publicly showing a build (for example a Docker bundle, repack,
video or public server) should also credit TortoiseBots by Sagiroth with a link
to the repository. See the README for ready-to-paste credit text.

## Donor / reference projects

| Project | Licence | Notes |
| --- | --- | --- |
| `Shyalya/tortoise-wow` | AGPL-3.0 at pinned repository root | Tortoise 1.18.1 donor baseline; verify retained upstream notices per copied file |
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
* Keep the TortoiseBots author attribution and source link intact (see
  *Attribution* above).
* Keep notices intact and record substantial ports in
  [`docs/PROVENANCE.md`](docs/PROVENANCE.md).
* Track evidence and unresolved items in
  [`docs/LICENSE_AUDIT.md`](docs/LICENSE_AUDIT.md).
