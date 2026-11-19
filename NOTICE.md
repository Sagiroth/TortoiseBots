# Licence

## TortoiseBots

TortoiseBots is a native module that **links against** the Tortoise core. It
contains original source plus copied, ported and reimplemented work from several
donor lineages. The licence that applies to a file follows its retained notice,
the donor grant and the project's own grant for original work; these must not be
collapsed into a blanket "GPL-2.0" label without checking the exact variant.

### Licence for original TortoiseBots work

Original TortoiseBots work — code, SQL, configuration, documentation, tools and
addons written for this repository — is copyright © Sagiroth and the
TortoiseBots contributors, and is dual-licensed: you may use it under
**either**

* the **GNU Affero General Public License, version 3 or any later version
  (AGPL-3.0-or-later)** — full text in [`LICENSE`](LICENSE), **or**
* the **GNU General Public License, version 2 or any later version
  (GPL-2.0-or-later)** — full text in [`LICENSE-GPL-2.0`](LICENSE-GPL-2.0).

SPDX: `AGPL-3.0-or-later OR GPL-2.0-or-later`. The GPL option exists so the
code can be shared with GPL-2.0-or-later projects such as TrinityCore-based
cores.

### Files that stay AGPL-3.0-or-later only

The dual licence covers only work whose authors agreed to it (see
[`CONTRIBUTORS.md`](CONTRIBUTORS.md)). These stay **AGPL-3.0-or-later only**
until they are rewritten or their authors agree:

* code ported from AGPL-3.0 donors (the `Shyalya/tortoise-wow` baseline,
  `jrad7/mod-dungeon-clear`, `T-imothy/tortoise-wow` data, the Tortoise core);
  these files name their donor in the header or in
  [`docs/developers/licensing-audit.md`](docs/developers/licensing-audit.md);
* contributions by authors still marked `pending` in `CONTRIBUTORS.md`.

Before moving a file into a GPL-2.0-or-later project, check its header and
`git log` for the above.

### Contributions

Every contribution submitted to this repository (pull request, patch, commit,
or file attached to an issue) is licensed by its author under
AGPL-3.0-or-later OR GPL-2.0-or-later, the same terms as the project ("inbound
= outbound"). By submitting a contribution you confirm that you wrote it or
otherwise have the right to license it under these terms. Code taken from
another project must keep its original notice and be compatible with AGPL-3.0;
code from an AGPL-only donor stays AGPL-only — see
[`CONTRIBUTING.md`](CONTRIBUTING.md#licence-of-contributions).
Contributors confirm their consent by adding themselves to
[`CONTRIBUTORS.md`](CONTRIBUTORS.md).

### Donor code

Every file ported from a donor must retain its original copyright and licence
notice. See `ai/playerbot/` headers and
[`docs/developers/licensing-audit.md`](docs/developers/licensing-audit.md) for the donors, their source
commits and the open compatibility audit. Do not strip those notices.

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
original TortoiseBots work used under the AGPL option, the following additional
term applies (under the GPL-2.0 option it is a request, not a condition):

> Modified versions, forks, repacks and bundles must preserve these author
> attributions and the source link, and must not remove or hide them from
> the places listed above. You may add your own credit alongside them.

Distributing or publicly showing a build (for example a Docker bundle, repack,
video or public server) should also credit TortoiseBots by Sagiroth with a link
to the repository. See the README for ready-to-paste credit text.

## Disclaimer

TortoiseBots is a non-commercial hobby project. Voluntary donations (Ko-fi) only support development; they buy no access, features or service, and are not tied to any server. The author is not affiliated with, does not operate, endorse or receive money from any server that uses this code, and is not responsible for how third parties use it. TortoiseBots is not affiliated with Turtle WoW or its creators. World of Warcraft is a trademark of Blizzard Entertainment; this project is not affiliated with Blizzard.

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
* Keep notices intact and name the source repository and commit of a port in
  its pull request.
* Track evidence and unresolved items in
  [`docs/developers/licensing-audit.md`](docs/developers/licensing-audit.md).
