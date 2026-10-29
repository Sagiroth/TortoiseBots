---
id: server-dashboard
title: Observability Dashboard & Telemetry
category: server
summary: Setup, architecture, Prometheus metrics, and feature guide for the real-time bot observability service.
tags: [server, observability, telemetry, dashboard, metrics, prometheus]
relates_to:
  - server-settings-logging
  - server-bot-pool
  - server-admin-commands
---

# Observability Dashboard & Telemetry

TortoiseBots includes an optional real-time observability service located in `tools/observability`. When disabled, telemetry has zero overhead; once enabled, the game server emits non-blocking UDP status datagrams to a standalone daemon that hosts a web management dashboard and a Prometheus metrics endpoint.

For configuration keys controlling telemetry emission, see [Logging & diagnostic settings](settings/logging.md).

## Architecture

The observability stack consists of a lightweight C++ emitter in the game server and an external Go telemetry daemon:

```text
Game Server (TortoiseBots C++)
    │
    │  UDP Datagrams (non-blocking, default port 9195)
    ▼
Go Observability Daemon (tools/observability)
    │
    ├──► Prometheus Metrics HTTP Endpoint (:8095/metrics)
    ├──► REST API (/api/v1/status, /api/v1/bots, /api/v1/issues, /api/v1/commanders)
    └──► Embedded Web SPA Dashboard (:8095/dashboard)
```

## Quick Setup

Prebuilt dashboard binaries ship with every daily release. No Go toolchain is required for operation.

### Running with Setup Scripts

From the root of your TortoiseBots checkout:

- **Linux:**
  ```bash
  ./tools/observability/run-dashboard.sh --mangosd-conf /path/to/mangosd.conf
  ```
- **Windows (PowerShell):**
  ```powershell
  .\tools\observability\run-dashboard.ps1 -MangosdConf C:\path\to\mangosd.conf
  ```

The runner script reads your database connection settings from `mangosd.conf`, enables [`AiPlayerbot.Observability`](settings/logging.md) in `aiplayerbot.conf`, and starts the service (if already running, it continues without interrupting). Note that changing `AiPlayerbot.Observability` in `aiplayerbot.conf` requires restarting `mangosd`. Open `http://localhost:8095` in your browser and sign in using any game account with GM rank 2 or higher.

### Manual Configuration

To configure telemetry manually, adjust `AiPlayerbot.Observability`, `AiPlayerbot.ObservabilityHost`, and `AiPlayerbot.ObservabilityPort` in `aiplayerbot.conf` (see [Logging settings](settings/logging.md#aiplayerbotobservability)).

To run the daemon directly from source:
```bash
cd tools/observability
go run cmd/server/main.go --http-port 8095 --udp-port 9195
```

## Web Dashboard Features

The web interface provides real-time operational visibility into your realm's bot population:

| Tab | Key Questions Answered | Primary Features |
| :--- | :--- | :--- |
| **Overview** | Is the server healthy? | Active population, world tick rate, population graphs, and state census. |
| **Bots** | Who is online and what are they doing? | Complete roster list, filterable by level, class, spec, gold, average item level (`iLvl`), or guild (`guild:name` in the search box, or the sortable **Guild** column; also shown in the bot detail view, dash when guildless). |
| **Map** | Where is the fleet distributed? | 2D world and zone map with player and bot markers and breadcrumb paths. |
| **Commanders** | Who is leading bot groups? | Party health bars, tactical warnings, commander orders, and fight status. |
| **Progress** | Are bots levelling up smoothly? | XP per hour KPIs, level-up timelines, and average gear item level by level band. |
| **Activity** | What events have occurred? | Real-time feed of completed quests, rare loot drops, gathered nodes, and kills. |
| **Dungeons** | What dungeon and raid runs are active? | Live view of active instance copies with bots, member health, and player badges. |
| **Dungeon runs** | How do the dungeon runs go? | Success rate, clear and boss kill times, wipes and deaths per dungeon, read from the run trace summaries; optional Overview strip. |
| **Agent maps** | What did a turtle-agent test run do? | Developer tool for turtle-agent test runs. Hidden unless the daemon is given a maps folder. |
| **Auction House** | What is currently on the market? | Searchable catalog of all active auction listings posted by bots and players. |
| **Issues** | Which bots are stuck or in trouble? | Tracked navigation stalls, long deaths, and unreachable combat targets. |
| **Server** | What settings are active? | Live server rates, active configuration flags, and one-click diagnostic reports. |

### State Census Semantics

On the Overview tab, bots are categorized into three operational states:
- **Busy:** Standing still while performing actions (casting, looting, resting to eat/drink, fight or XP gain in last 60 seconds, or moving within the last 45 seconds).
- **Stalled:** Stationary for 45+ seconds with an active destination, cycling actions without progress, or no fight/XP in 60 seconds while pursuing travel.
- **Idle:** No movement, cast, action, loot, or combat target for 45+ seconds.

### Bot Profile Inspector

Clicking any bot name or map marker opens a slide-over profile panel that inspects character data directly from the database:
- **Overview:** Vital stats, current location, active strategy flags, and group status.
- **Gear & Stats:** Paperdoll equipment view, average item level (`iLvl`), and item quality distribution.
- **Talents:** Visual 7-tier talent trees with DBC layout.
- **Spells:** Distinguishes runtime learned spells from default starting spells granted by race/class.
- **Professions:** Profession skill ranks with their trade spells.
- **Skills:** Weapon, armor, language, and other skill ranks.
- **Bags & Containers:** Inventory slots and bag contents.
- **Activity History:** Recent kills, quest completions, and loot transactions.

### Dungeons Tab

The **Dungeons** tab provides a dedicated live overview of all active dungeon and raid runs containing bots:

- **Instance Isolation:** Groups bots by active instance copy (`(map, instance)` pair), distinctly separating multiple concurrent runs of the same dungeon.
- **Player Highlighting:** Real players in bot parties are visually distinguished with a distinct badge and gold ring indicator.
- **Raid Subgroups:** Raid encounters display members formatted into compact 5-player subgroup tiles.
- **Live Run View:** Selecting an active run presents an interactive instance map rendering bot positions and vital health/power bars. The first living tank is the run's lead: the map anchors on it and draws its mark and path.
- **Mobs and Targets (`DUNGEON_MOBS`):** A module that sends the `DUNGEON_MOBS` datagram (issue #1101; additive, no protocol version bump) adds the mobs the party's bots have seen, each mob's target and every bot's own target; the run's map draws them through the **Enemies**, **Critters**, **Threat colours and lines** and **Target, heal and crowd-control lines** layers (see [Commanders maps](#commanders-maps)). The module caps a run at 24 mobs in a party and 48 in a raid (fewer when many runs are live), and the card says `N of M mobs shown` when the list was cut. Mobs of a copy that leaves the instance vanish within about 8 seconds. `AiPlayerbot.ObservabilityDungeonMobs = 1` (default) turns the feed on, but only while `AiPlayerbot.Observability = 1`; with either off the maps keep working and those layers stay greyed with "it needs the newer module". A mob entry joins a run by map + instance id; when the roster reports no instance at all (a module from before #895), it joins by map plus a shared member guid, never by map alone, and only when exactly one run on that map matches.
- **Power Bars:** Each member row draws its power bar from the roster's `power`/`max_power`/`power_type`, exactly like the Bots tab, so a module that reports no power and a dead member show none.

### Commanders Maps

Every Commanders card and Dungeons run draws a live map of its party.

- **Map layers:** A collapsible **Map layers** panel above the cards chooses what every map draws. The choice is one setting for all cards and both tabs, kept in this browser (`localStorage` key `tbdash.mapLayers.v3`; a saved v2 or v1 choice is migrated once, a merged layer is on when any of its old switches was), with a **Reset to defaults** button. Eighteen rows in five groups: **Map** (map picture, grid and scale bar, zone names), **Party** (bots and lead mark, health bars and status icons, lead path, other members' paths, healer range ring and commander line), **Enemies** (enemies = hostile + neutral, critters, enemy names and boss marker, threat colours and lines, aggro range), **Lines and run** (target/heal/crowd-control lines, run events, stalls and refused orders, run timer and boss checklist) and **Objects** (doors, chests, levers, altars and gongs). Enemies, their names and the threat colours and lines are on by default; other members' paths, the healer ring, critters, aggro range, the action lines, stalls and objects are off. A layer is greyed out only when none of the data it merges is there, with the reason beside it; its saved choice is kept for when the data arrives. Each row's tooltip explains it.
- **Enemies on a Commanders card:** When a card's party is one of the Dungeons runs (same map and a shared bot), the card draws the union by guid of the commander feed's mobs and the run's `DUNGEON_MOBS` list, so it shows every enemy the run knows rather than the feed's 24; the commander feed wins on the fields it carries (threat and targets), and the card says `N of M enemies shown` when the run's list was cut. A card with no matching run draws only the commander feed.
- **Map pictures (`--map-dir` / `TBDASH_MAP_DIR`):** With a folder of pictures written by `turtle-agent minimap-extract` from your own client, the maps draw the game's map under the party. Every map offers the same three pictures in this order: **World map** (one entry per floor, the default), **Minimap** and **Navmesh**. A picture the folder lacks is still listed, disabled as e.g. `Minimap (none)` with its reason as the tooltip; a stored choice of a missing picture falls back to the World map and the line under the picker says why. An optional `<map id>.status.json` in the folder (at most 64 KiB, never served) gives the reason per picture, e.g. `{"minimap":{"reason":"no WMO minimap yet"}}`; without it built-in reasons are used (indoor dungeon: no minimap; extractor run without `--mmaps`: no navmesh; no `WorldMapArea` row: no world map). With no picture at all the picker is hidden and the **Map picture** layer is greyed. These are extracted game assets: never commit them.

### Dungeon runs Tab

The **Dungeon runs** tab (`#/dungeon-runs`) shows how the dungeon runs go, per dungeon: runs, success rate (full clears of the runs that finished), best and median clear time, the kill time of each boss per run (the best of each boss highlighted), wipes and deaths per run, the same numbers by server build and by driver (model and effort, self-reported), and a wipe and death map (a cross for a wipe, a dot for a death, coloured by run) on the same map pictures as the Commanders tab. Build and driver filters narrow every number on the tab. The data comes from the run trace summaries (`.bot trace`, [`run-trace.md`](run-trace.md)) in the folder `TBDASH_TRACE_DIR` (or `--trace-dir`) names, by default the usual `logs/traces` next to the dashboard; with no folder or no runs the tab says so and how to turn tracing on. The Overview has an optional strip, off until its "Dungeon runs" box is ticked, with the last run of each dungeon: result, time and the difference to the best clear. Outcome, boss times and wipe positions need the fields of issue #986 in the summary; a summary without them still counts as a run with outcome "unknown".

It is not the live **Dungeons** tab above: that one shows the instance copies bots are in right now, this one the finished (and running) traced runs.

<details>
<summary>Dungeon runs API</summary>

`GET /api/v1/dungeon-runs` — per-dungeon summaries read from the run trace folder (`meta`, `hint`, `facets`, `dungeons[]`); optional `build`, `driver` and `mode` filters. `GET /api/v1/dungeon-runs/<map>/runs` — the runs of one map, newest first, with the boss columns, wipes and deaths; the same filters and `limit` (default 200, at most 1000). GET only, same login as the other endpoints, no database access: the folder is indexed once at start-up and re-checked every `TBDASH_TRACE_SCAN_SEC` seconds (default 10). Field list: [`tools/observability/README.md`](../../tools/observability/README.md).

</details>

### Run Leader Runs on the Commanders Tab

**A run leader's run ("observe").** A party that clears a dungeon plan by itself (`.bot run`, see [Dungeon Run Leader](../play/dungeon-run-leader.md)) is a card like any other, with an `observe` badge (`observe · paused` when it stopped), its plan, step and section in the card's second line (`uldaman · step 78/205 · ancient stone keeper · pull · rest`; hover for the note and the time of the last change), and a `Run paused: <reason>` warning, so a stuck run shows its reason. It needs a module that sends the run (`run` in the `COMMANDERS` report); an older one shows the party without it.

**Run steps** (a party the run leader clears, "observe"): the plan's steps for the current section - a dashed route (grey = done, blue = ahead), numbered circles for pulls and bosses (green = cleared, yellow = the step it is on, struck out = skipped), a crown and name for bosses, icons for loot, doors, rituals and stops - need a module that sends `run.plan_steps`; an older one leaves the layer greyed.

**Bosses** (owner request 2026-10-21): every known boss of the dungeon a card shows was drawn at its spawn point all the time, with its name - a gold crown while it lived, a grey crown with a tick and the name struck through once killed, an outlined crown when not seen - and the run line counted `bosses 3 / 8 killed` of the run's bosses (the ones the plan names; the others are listed in its tooltip), with `bosses –` on a dungeon map where nothing was known. The module sends the list with each boss's state in that party's own instance (`bosses` in the `COMMANDERS` report); without it the page falls back to the plan's boss steps, the bosses in sight and the kills it saw.

### Agent Maps Tab

This tab is a developer tool for [turtle-agent](https://github.com/pjerra/turtle-agent), the headless test client used to test bots in dungeons. If you do not run turtle-agent, you will never see it and can skip this section.

The **Agent maps** tab shows the run maps that `turtle-agent map` writes: one SVG per run (`run.svg`, or `run-f1.svg`, `run-f2.svg` when the run spans floors) and, with `--html`, a self-contained `run.html` replay next to it. Click a file to view it inline.

The tab is off by default. Start the daemon with `--agent-maps-dir DIR` (or `TBDASH_AGENT_MAPS_DIR=DIR`) to turn it on; without it the tab stays hidden and its API answers 404.

<details>
<summary>What it serves and the security limits</summary>

- Lists the newest 200 `.svg`/`.html` files directly in the folder, newest first, with type, size and modified time. Hidden files, sub-folders and every other extension are not served. A served map is capped at 32 MiB.
- Read-only: nothing is uploaded, written or deleted. The list and the files sit behind the normal GM login, and file names are always shown as text.
- A replay runs in a sandboxed frame: it may run its own inline script, but it has no same-origin access and a `default-src 'none'` content security policy blocks every network request.
- A symlink is followed only to a regular file directly inside the folder. The daemon re-checks the folder before every list and open and refuses if it no longer resolves to the directory it started with.
- **Known limits:** keep the folder and every directory above it writable only by the operator. Files are opened by pathname, so someone who can rename the folder or an ancestor and put a symlink in its place can race a request between the check and the open (closing this needs Go 1.24 `os.Root` or Linux `openat2`; the dashboard is pinned to Go 1.22). A hard link inside the folder to a file outside it is served, so leave `fs.protected_hardlinks` at its default (`1`).

</details>

## Issue Tracking & Stalls

The service monitors persistent problems and groups them into episodes on the **Issues** tab:

- **Stuck Navigation (`STUCK`):** A bot remains in a moving state but its physical coordinates stay frozen for 60+ seconds.
- **Prolonged Death (`DEAD_LONG`):** A dead bot remains a ghost without releasing or reviving for more than 2 minutes.
- **Unreachable Target (`UNREACHABLE_TARGET`):** A bot attempts to engage a hostile target that is >45 yards away or out of line of sight for 10 seconds.
- **Anti-Flapping Filter:** Episodes must persist past an age gate (default 5 minutes; `UNREACHABLE_TARGET` surfaces after 2 minutes) before alerting, preventing transient pathing pauses from cluttering alerts.
- **Restart Blackout:** `DEAD_LONG` alerts are suppressed for the first 5 minutes following a game server restart.

## Telemetry APIs & Metrics

### Prometheus Metrics Endpoint

The daemon exports Prometheus metrics at `http://localhost:8095/metrics` for scraping by Prometheus and Grafana:

- `tortoisebots_active_count{class,role}`: Number of active bots grouped by class and role.
- `tortoisebots_state_ratio{state}`: Ratio of time spent in each operational state over rolling windows.
- `tortoisebots_grinding_xp_per_hour_total`: Collective XP gain rate across the bot pool.
- `tortoisebots_issues_active{type}`: Current count of active issue episodes by type.
- `tortoisebots_anomalies_total{type}`: Cumulative anomalies and loop triggers encountered.
- `tortoisebots_snapshots_total`: Number of telemetry roster snapshots ingested.

### Selected JSON REST Endpoints

The daemon provides selected admin-relevant HTTP REST endpoints under `/api/v1`:

- `GET /api/v1/status`: Daemon health, connected game server status, and pool population summary.
- `GET /api/v1/bots`: Complete bot fleet state, coordinates, active strategies, and gear summaries.
- `GET /api/v1/armory/bots`: Searchable roster summary for the bot armory inspector.
- `GET /api/v1/armory/bot/{guid}`: Detailed character profile, paperdoll gear, talents, and inventory.
- `GET /api/v1/issues`: Active and historical navigation and combat stall episodes.
- `GET /api/v1/commanders`: Active remote commander bindings, party member positions, and recent orders.
- `GET /api/v1/commanders/trails`: Historic movement trails and paths for active commanders.
- `GET /api/v1/dungeons`: Live view of active instance copies with member states and player indicators; from a module that sends `DUNGEON_MOBS`, each run also carries `mobs`, `mobs_map` and `mobs_total`, and each member `id` and `target`.
- `GET /api/v1/dungeon-runs`, `GET /api/v1/dungeon-runs/<map>/runs`: Run trace summaries per dungeon and the runs of one map (the Dungeon runs tab).
- `GET /api/v1/maps/<map id>`: The three picture slots of a map in display order (`kind` worldmap/minimap/navmesh, `floor`, `label`, `available`; a missing slot carries `reason`, an available one `name`, `source`, `url` and the rectangle `x`/`y`/`w`/`h` it covers). Answers 200 also for a map without pictures; 404 only when no map folder is configured.
- `GET /api/v1/mapfiles/<name>`: One available map picture (`.png`) or its placement (`.json`) from the map folder.
- `GET /api/v1/activity`, `/activity/loot`, `/activity/quests`: Real-time feed of bot actions, items, and quests.
- `GET /api/v1/grinding`: Detailed grinding analytics, XP rates, and kill distributions.
- `GET /api/v1/history`: Timeline snapshots of historical bot counts and states.
- `GET /api/v1/server-info`: Server version metadata, active rates, and configuration status.
- `GET /api/v1/anomalies/totals`: Cumulative session totals for anomalies and action loops.
- `GET /api/v1/auctions`: Searchable listings currently posted on the auction house by bots and players.
- `GET /api/v1/agent-maps`: Turtle-agent run maps in the configured folder, newest first (only with `--agent-maps-dir`).
- `GET /api/v1/agent-maps/files/<name>`: One run map, served inline (only with `--agent-maps-dir`).
- `WS /api/v1/stream`: Real-time WebSocket event feed streaming live bot updates to connected clients.

Activity counters are periodically saved to `activity-state.json` so statistics persist across daemon restarts.

<details>
<summary>Agent maps API details</summary>

`GET /api/v1/agent-maps` returns `{"files":[{"name","size","mtime","kind"}...],"max_files":200,"max_file_size":33554432}`. `kind` is `svg` or `html`; `mtime` is unix seconds. Only regular `.svg`/`.html` files directly in the folder are listed (hidden files, sub-folders and symlinks that resolve outside are left out). The routes exist only when the daemon runs with `--agent-maps-dir DIR` (env `TBDASH_AGENT_MAPS_DIR`); unset, they are not registered and answer 404, which the page uses to keep the tab hidden. Both routes require the normal GM login.

`GET /api/v1/agent-maps/files/<name>` serves `.svg` as `image/svg+xml` and `.html` as `text/html; charset=utf-8`. Both carry `X-Content-Type-Options: nosniff`, `Cache-Control: no-cache` and a restrictive `Content-Security-Policy` (`default-src 'none'`, no `connect-src`/`frame-src`/external fetch). The HTML policy also allows the replay's own inline script and style and adds `sandbox allow-scripts` (no `allow-same-origin`), so the replay runs in an opaque origin that cannot read the dashboard's cookies or DOM. A name with a separator, backslash, NUL, control character, leading dot, encoded traversal (`%2e%2e`, `%2f`, `%5c`, `%00`) or any extension other than `.svg`/`.html` is 404; a file over 32 MiB is 413. GET and HEAD only.

</details>

## Pool KPI Report (`tools/pool_kpi_report.py`)

For offline analysis of bot fleet health over extended runs, use the Python KPI reporting tool:

```bash
python3 tools/pool_kpi_report.py \
  --bots-json path/to/bots.json \
  --logs /path/to/logs \
  --reset "2026-10-02 12:54" \
  --baseline tools/pool_kpi_baseline_2026-10-02.json
```

Additional flags include `--db-cmd` (database query command), `--save-baseline` (saving output baseline files), `--out` (writing the Markdown report), and `--anomaly-totals-json` for recording anomaly counters.

The script generates a Markdown summary evaluating:
- Bot level progression and median XP gain rates.
- Deaths per minute and prolonged-ghost counts.
- Zone migration fit: whether bots advance to level-appropriate zones as they outgrow starter areas.

## Map Artwork Generation

Zone and continent map images are client assets that are not bundled with the server repository. You can extract and composite them from your own client:

```bash
python3 -m pip install mpyq Pillow
python3 tools/observability/scripts/gen_zone_maps.py \
  --data "/path/to/WoW/Data" \
  --out tools/observability/maps
```

The `--out` path feeds the Map tab; Commander map views use the folder configured via `--map-dir` or the `TBDASH_MAP_DIR` environment variable. If map images are absent, the Map tab displays a note instead of the picture while bot position markers continue to function normally.
