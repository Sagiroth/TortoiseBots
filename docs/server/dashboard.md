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

The observability service is an optional standalone program that shows what the bot population is doing: a web dashboard with a live map, plus a Prometheus metrics endpoint. The game server sends it status updates as non-blocking UDP datagrams. Telemetry has zero overhead while turned off.

For the settings that control it, see [Logging & diagnostic settings](settings/logging.md).

## Architecture

A small C++ emitter in the game server sends UDP datagrams (default port 9195) to an external Go daemon in `tools/observability`. The daemon serves the dashboard and metrics:

```text
Game Server (TortoiseBots C++)
    │
    │  UDP Datagrams (non-blocking, default port 9195)
    ▼
Go Observability Daemon (tools/observability)
    │
    ├──► Web Dashboard (:8095/dashboard)
    ├──► Prometheus Metrics (:8095/metrics)
    ├──► REST API (/api/v1/status, /api/v1/bots, /api/v1/issues, ...)
    └──► Live stream (/api/v1/live: Server-Sent Events with positions and fight events)
```

## Quick Setup

The dashboard is a separate small program that runs next to your server. Every daily release has a ready file for Windows (`tortoise-observability-windows-amd64.exe`) and Linux (`tortoise-observability-linux-amd64`).

What you need before you start:

- Your server running (database and `mangosd`).
- A game account with GM rank 2 or higher. You sign in to the dashboard with the same name and password you use in the game.
- Your `mangosd.conf` file. The dashboard reads the game's database, so it needs the same database login as your server.

### The easy way: the start script (recommended)

The script finds the newest release, downloads the program, reads the database login from your `mangosd.conf`, turns on telemetry in `aiplayerbot.conf`, and starts the dashboard. The scripts are in the `tools/observability` folder of the TortoiseBots module.

**Windows** — open PowerShell in the TortoiseBots folder and run:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\observability\run-dashboard.ps1 -MangosdConf C:\path\to\mangosd.conf
```

`-ExecutionPolicy Bypass` applies to this one script run. Without it, Windows may refuse with "running scripts is disabled on this system".

**Linux** — in the TortoiseBots folder:

```bash
./tools/observability/run-dashboard.sh --mangosd-conf /path/to/mangosd.conf
```

Then:

1. **Restart `mangosd` once**, so it picks up the telemetry setting the script turned on. Without that you can sign in, but the dashboard stays empty.
2. Open `http://localhost:8095` in your browser and sign in with your GM account.

The program keeps running in the background. Its log is `dashboard.log` next to the script (on Windows also `dashboard.err.log`). Running the script again while the dashboard is up does nothing: stop the dashboard first if you want to update it.

### The manual way: run the program yourself

Only use this if the script does not work for you.

1. Download the file for your system from the [latest release](https://github.com/Sagiroth/TortoiseBots/releases/latest).
2. In `aiplayerbot.conf` set `AiPlayerbot.Observability = 1` and restart `mangosd` (more options in [Logging settings](settings/logging.md#aiplayerbotobservability)).
3. Find the `LoginDatabase.Info` line in `mangosd.conf`. It looks like `"127.0.0.1;3306;mangos;mangos;tw_logon"`: host, port, user, password, database name.
4. Start the program with those values. If you start it without them (for example by double-clicking it), it assumes user `mangos` with password `mangos` and database `tw_logon`. When your server uses something else, sign-in fails.

**Windows** (Command Prompt or PowerShell, in the folder with the file):

```powershell
.\tortoise-observability-windows-amd64.exe -db-host 127.0.0.1 -db-port 3306 -db-user YOUR_DB_USER -db-pass YOUR_DB_PASSWORD -db-name tw_logon -db-char tw_char -db-world tw_world
```

**Linux:**

```bash
chmod +x tortoise-observability-linux-amd64
./tortoise-observability-linux-amd64 -db-host 127.0.0.1 -db-port 3306 -db-user YOUR_DB_USER -db-pass YOUR_DB_PASSWORD -db-name tw_logon -db-char tw_char -db-world tw_world
```

`-db-char` and `-db-world` are the database names from the `CharacterDatabase.Info` and `WorldDatabase.Info` lines. Keep the window open while you use the dashboard, then open `http://localhost:8095`.

### Sign-in problems

**"invalid username or password"** usually means the dashboard cannot read your database, not that your password is wrong. The dashboard shows the same message for every sign-in problem. Check, in this order:

1. **Database login.** If you started the program yourself, pass the values from `LoginDatabase.Info` as shown above, or use the start script.
2. **Too many tries.** After 5 failed sign-ins from your address, the dashboard blocks it for 5 minutes. Restart the program (or wait) before you try again.
3. **GM rank.** The account needs rank 2 or higher. In the server console: `account set gmlevel <name> 3`, then sign in again.
4. **Same realm.** The account must exist on the server whose `mangosd.conf` you used.

**The page opens but stays empty.** Telemetry is off, or `mangosd` was not restarted after turning it on. Check that `AiPlayerbot.Observability = 1` in `aiplayerbot.conf` and restart `mangosd`.

**The page does not open.** The program is not running. Look at `dashboard.log` (start script) or the program's window (manual start) for the error.

### Running from source

With Go installed, run it straight from the checkout:

```bash
cd tools/observability
go run ./cmd/server --http-port 8095 --udp-port 9195
```

## Web Dashboard Features

| Tab | Key Questions Answered | Primary Features |
| :--- | :--- | :--- |
| **Overview** | Is the server healthy? | Active population, world tick rate, population graphs, and state census. |
| **Bots** | Who is online and what are they doing? | Roster list, filterable by level, class, spec, gold, average item level, or guild. |
| **Map** | Where is the fleet distributed? | 2D world and zone map with player and bot markers and breadcrumb paths. |
| **Commanders** | Who is leading bot groups? | Party health bars, tactical warnings, commander orders, and live fight view. |
| **Dungeon runs** | How are the dungeon runs going? | Success rate, clear and boss kill times, wipes and deaths per dungeon, read from the run trace summaries. |
| **Dungeons** | What dungeon runs are active? | Live view of active dungeon copies with bots: member health and player badges. |
| **Raids** | What raid runs are active? | Live view of active raid copies by group, no binding needed. |
| **Raid coverage** | Which boss abilities can the bots answer? | Which raid boss abilities have a coded answer, and which gaps are still open. |
| **Progress** | Are bots levelling up smoothly? | XP per hour, level-up timelines, and average gear item level by level band. |
| **Activity** | What events have occurred? | Feed of completed quests, rare loot drops, gathered nodes, and kills. |
| **Guild orders** | What are guild bots doing? | Who works on what, where it goes, and what it gathered. |
| **Auction House** | What is currently on the market? | Searchable catalog of all active auction listings posted by bots and players. |
| **Agent maps** | What did a turtle-agent test run do? | Test run maps. Hidden unless the daemon is given a maps folder. |
| **Issues** | Which bots are stuck or in trouble? | Tracked navigation stalls, long deaths, and unreachable combat targets. |
| **Server** | What settings are active? | Live server rates, active configuration flags, and diagnostic reports. |

### State Census

On the Overview tab, bots are grouped into three states:

- **Busy:** standing still but doing real work (loot, cast, eat, or movement in the last 45 s).
- **Stalled:** standing still for 45+ s with only a travel target or action-name changes, no progress.
- **Idle:** no activity for 45+ s, doing nothing.

### Bot Profile Inspector

Clicking any bot name or map marker opens a slide-over profile panel that reads character data from the database:

- **Overview:** vital stats, current location, active strategy flags, and group status.
- **Gear & Stats:** paperdoll equipment view, average item level, and item quality distribution.
- **Talents:** talent trees with DBC layout.
- **Spells:** learned spells versus default starting spells granted by race/class.
- **Professions:** profession skill ranks with their trade spells.
- **Skills:** weapon, armor, language, and other skill ranks.
- **Bags & Containers:** inventory slots and bag contents.
- **Activity History:** recent kills, quest completions, and loot transactions.

### Dungeons Tab

The **Dungeons** tab shows every active dungeon run that has bots in it, grouped by instance copy so concurrent runs of the same dungeon stay separate. Real players in bot parties get a distinct badge.

Selecting a run shows a live view: an interactive map with bot positions and health bars. The first living tank is the run's lead: the map anchors on it and draws its mark and path.

The Dungeons tab lists dungeon copies; raid copies live on the Raids tab. Raid groups without a commander binding also appear on the Raids tab.

### Raids Tab

The **Raids** tab lists every raid map copy that holds a bot, with or without a commander binding: the instance, the groups inside it, each group's rows (with a name and an offline marker for members not online), the map, and a link to a bound group's Commanders card.

### Commanders Maps

Every Commanders card and Dungeons run draws a live map of its party, with a **Map layers** panel that chooses what every map draws (map picture, party, enemies, lines and run). The choice is kept in this browser.

With a folder of map pictures from your own client (see [Map Artwork Generation](#map-artwork-generation)), the maps draw the game's map under the party. Without pictures, the map shows a note instead while bot position markers keep working.

### Dungeon Runs Tab

The **Dungeon runs** tab (`#/dungeon-runs`) shows how the traced dungeon runs go, per dungeon: runs, success rate (full clears of the runs that finished), best and median clear time, the kill time of each boss per run, wipes and deaths per run, and the same numbers by server build and by driver. Build and driver filters narrow every number on the tab. The data comes from the run trace summaries (`.bot trace`, [run-trace.md](run-trace.md)) in the folder the daemon was given.

It is not the live **Dungeons** tab above: that one shows the instance copies bots are in right now, this one the finished (and running) traced runs.

### Agent Maps Tab

This tab is a tool for [turtle-agent](https://github.com/pjerra/turtle-agent), the headless test client used to test bots in dungeons. If you do not run turtle-agent, you will never see it and can skip this section.

The **Agent maps** tab shows the run maps that `turtle-agent map` writes: one SVG per run, with an optional self-contained HTML replay next to it. Click a file to view it inline.

The tab is off by default. Start the daemon with an agent maps folder to turn it on. Without it the tab stays hidden.

## Issue Tracking & Stalls

The service tracks persistent problems and groups them into episodes on the **Issues** tab:

- **Stuck Navigation (`STUCK`):** a bot remains in a moving state but its position stays frozen for 60+ seconds.
- **Prolonged Death (`DEAD_LONG`):** a dead bot remains a ghost without releasing or reviving for more than 2 minutes.
- **Unreachable Target (`UNREACHABLE_TARGET`):** a bot tries to engage a hostile target that is far away (over 45 yards) or out of line of sight for 10 seconds.
- **Anti-flapping filter:** episodes must persist past an age gate (default 5 minutes; `UNREACHABLE_TARGET` surfaces after 2 minutes) before alerting, so transient pathing pauses do not clutter alerts.
- **Restart blackout:** `DEAD_LONG` alerts are suppressed for the first 5 minutes following a game server restart.

## Telemetry APIs & Metrics

### Prometheus Metrics Endpoint

The daemon exports Prometheus metrics at `http://localhost:8095/metrics` for scraping by Prometheus and Grafana:

- `tortoisebots_active_count{class,role}`: number of active bots grouped by class and role.
- `tortoisebots_state_ratio{state}`: ratio of time spent in each operational state over rolling windows.
- `tortoisebots_grinding_xp_per_hour_total`: collective XP gain rate across the bot pool.
- `tortoisebots_issues_active{type}`: current count of active issue episodes by type.
- `tortoisebots_anomalies_total{type}`: cumulative anomalies and loop triggers encountered.
- `tortoisebots_snapshots_total`: number of telemetry roster snapshots ingested.

### Selected JSON REST Endpoints

The daemon provides admin-relevant HTTP REST endpoints under `/api/v1` (login required, same GM rank as the dashboard):

- `GET /api/v1/status`: daemon health, connected game server status, and pool population summary.
- `GET /api/v1/bots`: complete bot fleet state, coordinates, active strategies, and gear summaries.
- `GET /api/v1/armory/bots`: searchable roster summary for the bot profile inspector.
- `GET /api/v1/armory/bot/{guid}`: detailed character profile, paperdoll gear, talents, and inventory.
- `GET /api/v1/issues`: active and historical navigation and combat stall episodes.
- `GET /api/v1/commanders`: active commander bindings, party member positions, and recent orders.
- `GET /api/v1/dungeons`: live view of active dungeon copies with member states and player indicators.
- `GET /api/v1/raids`: live view of active raid copies by group.
- `GET /api/v1/dungeon-runs`, `GET /api/v1/dungeon-runs/<map>/runs`: run trace summaries per dungeon and the runs of one map.
- `GET /api/v1/activity`, `/activity/loot`, `/activity/quests`: feed of bot actions, items, and quests.
- `GET /api/v1/auctions`: listings currently posted on the auction house by bots and players.
- `WS /api/v1/stream`: real-time event feed streaming live bot updates to connected clients.

Activity counters are periodically saved to `activity-state.json` so statistics persist across daemon restarts.

For the fight debugger, fight history, agent maps, and per-endpoint field lists, see [`tools/observability/README.md`](../../tools/observability/README.md).

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

The `--out` path feeds the Map tab. Commander map views use the folder configured via `--map-dir` or the `TBDASH_MAP_DIR` environment variable. If map images are absent, the Map tab displays a note instead of the picture while bot position markers continue to function normally.
