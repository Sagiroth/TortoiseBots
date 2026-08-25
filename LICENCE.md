# Licence

## TortoiseBots

TortoiseBots is a native module that **links against** the Tortoise core. Its
own source (host/runtime/commands, Turtle compatibility shims) is released under
the same terms as its donor lineage:

> **GNU General Public License v2.0** (GPL-2.0, or at your option any later
> version where the donor header says so).

Every file ported from a donor retains its original copyright and GPL header —
see `ai/playerbot/` headers and [`docs/PROVENANCE.md`](docs/PROVENANCE.md) for
exact donor commits. Do not strip those headers.

## Upstream — Penqle/tortoise-wow

The canonical target core for this module is:

* **Penqle/tortoise-wow** — <https://github.com/Penqle/tortoise-wow>
* Licence: **GNU Affero General Public License v3.0 (AGPL-3.0)**
* Full text: <https://github.com/Penqle/tortoise-wow/blob/main/LICENSE>

When TortoiseBots is built as a module inside that core, the **combined
binary is AGPL-3.0**. If you run a modified combined server and let players
connect over the network, you must offer them the Corresponding Source of the
combined work (AGPL §13).

## Donor / reference projects

| Project | Licence | Notes |
| --- | --- | --- |
| `Shyalya/tortoise-wow` | GPL-2.0 | Turtle 1.18.1 donor baseline |
| `cmangos/playerbots` | GPL-2.0 | PlayerBots behavior (primarily AzerothCore/mod-playerbots) |
| `cmangos/mangos-classic` | GPL-2.0 | Host API reference |
| `mangoszero/server` | GPL-2.0 | Lifecycle patterns |
| `mod-playerbots` (`AzerothCore`) | GPL-2.0+ / AGPL-3.0 (see its repo) | Newer behavior |
| `TortoiseWoWKnowledgeBase` | per-repo (see its `LICENSE`) | Docs/behavioral spec only |

Upstream licences are preserved — see each referenced repository for its full
licence text.

## What this means for you

* You may use, modify and redistribute TortoiseBots under **GPL-2.0** (respecting
  donor headers).
* If you distribute or **run as a network service** a combined build with
  Penqle's AGPL-3.0 core, the AGPL's network-source requirement applies to the
  combined work.
* Keep copyright notices intact and record substantial ports in
  [`docs/PROVENANCE.md`](docs/PROVENANCE.md).
