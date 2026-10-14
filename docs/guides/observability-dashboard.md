---
id: guide-observability-dashboard
title: Observability Dashboard & Telemetry
category: guides
summary: Guide to the Go observability service, Prometheus metrics, live web dashboard, 2D world map, and stuck-bot issue tracker.
tags: [guide, observability, telemetry, dashboard, metrics]
relates_to:
  - guide-configuration-tuning
  - concept-architecture-invariants
---

# Observability Dashboard & Telemetry

TortoiseBots includes an optional observability subsystem located in [`tools/observability`](../../tools/observability). It is zero-overhead while disabled (default off); once enabled the emitter serialises heartbeat + roster batches to UDP each cycle (non-blocking). It provides live visibility into bot fleet health, active locations, combat states, and navigation stuck episodes.

```text
TortoiseBots Module (C++)
    │
    │  UDP Packets (non-blocking, port 9195)
    ▼
Go Observability Daemon (tools/observability)
    │
    ├──► Prometheus Metrics HTTP Endpoint (/metrics)
    ├──► REST API (/api/v1/status, /api/v1/bots, /api/v1/issues, /api/v1/anomalies, /api/v1/anomalies/totals, /api/v1/commanders)
    └──► WebSocket (/api/v1/stream) -> Embedded Web SPA Dashboard (:8095)
```

---

## 1. Web Dashboard Features

When the daemon is running, sign in with a game account of GM rank ≥ 2 (or run with `--dev-no-auth` for local dev), then open `http://localhost:8095` in your browser (port is configurable via `--http-port`). The dashboard has eight tabs, each answering one question; the URL hash (`#/bots?bot=123&p=gear`) records the tab and the open bot profile, and the last tab is remembered across reloads:
- **Overview** — is everything running? World tick, bots/players online, snapshot age and active issues; the 10-minute population and tick charts; the live-state census (click a state to list those bots) and a short needs-attention list (longest issues, low-HP count).
- **Bots** — the one list of bots, online and offline together. Filter by name/class/spec or by any column with fields in the search box (`lvl:50-60 ilvl:>40 zone:tanaris gold:<10 deaths:0`; numbers take `N`, `A-B`, `>N`, `>=N`, `<N`, `<=N`, text fields match a substring, all terms must match), online/offline, state chips and class chips (the counts double as the fleet census) or "With issues". Three column sets: **Live** (status, health, power, target, zone), **Progress** (XP bar with XP/hour, average item level, gold) and **Activity** (session quests, loot, notable loot, kills, deaths, money, vendors, repairs, give-ups, gathering, AH). Click a row to open the profile.
- **Map** — where the bots are: continent tabs with zone chips, the 2D world/zone map, breadcrumb trails and class legend. Click a marker to open the profile (a narrow side panel on this tab).
- **Commanders** — who leads which bots, and what did they order. One card per standing `.bot commander` binding ([`player-controls.md`](player-controls.md) §1a), headed `Gbmage → 5 bots · Uldaman · fighting` (the lead's zone or dungeon, and what the party is doing). Under it a **warnings** line shows only what needs a glance: someone dead or low on health, a bot more than 40 yd from the lead or on another map, the newest order refused, the commander offline, or no update for 30 s (then the card is dimmed and kept for 5 minutes instead of vanishing). The **party** has one row per bot: role icon and colour (tank blue, healer green, DPS orange, dead grey), name (the lead starred), class and level, health bar, mana/rage/energy bar, and the distance to the lead. **Last orders** are in plain words, newest first, with the module's answer in colour and a reason when refused (`Move → refused: no walkable path`, `Attack a target → OK (5 bots)`); hover for the raw line. The **map** shows the party as role-coloured named dots (lead ringed) on a plain grid, north up and one unit a yard, with the zone outline when the zone is known and the lead's recent path (and, with `TBDASH_MAP_DIR`, the real game map under it; see **Map layers** below); it fits and follows the party; zooming (wheel, pinch or the buttons) keeps following (around the party, not the cursor) and only a drag pauses it until you click the "Paused - click to follow" chip or **Follow**. With the module's map data the **Layers** panel can also draw the mobs near the lead (hostile red, neutral grey, critters small), a line from each mob to whom it targets, a line from each bot to its target, a ring of about 20 yd around mobs nobody fights yet, and the commander with a line to the lead; a **Dim the map picture** slider (0-80 %) darkens the real map under the party. Layers whose data an older module does not send are greyed out with the reason. A newer module also reports the AI state, so the header says "fighting" itself; an older one sends no combat flag and "fighting", "moving" and the like are inferred from HP changes, lead movement and the last accepted order. It works on a phone (rows stack, the map is touch-draggable), refreshes by itself from the stream, and needs `AiPlayerbot.RemoteCommander = 1`; with no binding it explains how one shows up. It reports what the module knows, the same for any client of the commander protocol. **Optional link to the agent's live map.** Set `TBDASH_COMMANDER_URL` (or `--commander-url`) to the address of a commander dashboard, for example turtle-agent's `turtle-agent dashboard` (`http://127.0.0.1:8790`), and a collapsed "Open the agent's live map" section appears below the cards with a link to it; the frame is loaded only when you open the section, unset nothing of it appears. The address is opened by the viewer's browser, not by the daemon, so `127.0.0.1` is the viewer's machine; the other dashboard must allow being framed from this page (turtle-agent allows loopback pages, other origins via its `--embed-from`), and an `http` frame does not load inside an `https` dashboard (use the link).

  **What each character is doing, and the unit box.** From a newer module, each party row has a second line saying what that character does right now (`fighting Revelosh`, `casting Frostbolt on Revelosh`, `drinking`, `moving`, `following`, `stunned`, `dead`; the full text and the last action name on hover). Rest a mouse on any unit on a map (a bot, the commander, a hostile, neutral or friendly unit, an event pin) and a box shows its name (or creature entry), level, rank (Boss, Elite, Rare), reaction (Hostile, Neutral, Friendly, critter, guard ...), health, what it is casting, whom it attacks (or its target), crowd control (stunned, polymorphed or asleep, feared, rooted) and the distance to the lead; on a phone, tap a unit (tap a bot again for its details, tap elsewhere to close). Bosses are drawn bigger with a crown and their name always shown. Extra layers in the Layers panel: names of hostile units (off), the group's names (on), health bars over units (on), status icons (off: skull dead, spark casting, sheep crowd controlled, cup drinking or eating, arrow fleeing), threat colours (off: a mob's ring is blue when it attacks the tank and red when it attacks anyone else; the tank's current target has a white outline and the pull target a dashed gold ring), event pins and an event feed under the map (off: deaths, wipes, boss kills, objects opened or looted, with the time into the run; derived in the browser from the reports, kept for the page's lifetime), and a run line on the card (off: run timer, bosses killed out of those seen, the next boss). The legend under each map explains whatever is switched on.

  **A run leader's run ("observe").** A party that clears a dungeon plan by itself (`.bot run`, see [dungeon-run-leader.md](dungeon-run-leader.md)) is a card like any other, with an `observe` badge (`observe · paused` when it stopped), its plan, step and section in the card's second line (`uldaman · step 78/205 · ancient stone keeper · pull · rest`; hover for the note and the time of the last change), and a `Run paused: <reason>` warning, so a stuck run shows its reason. It needs a module that sends the run (`run` in the `COMMANDERS` report); an older one shows the party without it.

  **Click a character.** Every character on a card opens its normal bot detail view (the slide-over the Bots tab uses, route `#/commanders?bot=<guid>`): the commander and the lead in the header, each name in the party table, and each bot's dot or name on the map (a tap; a drag still pans). Names are real links, so a middle click or copied link works; hover shows a pointer and a tooltip. The bots' guid rides in the `COMMANDERS` report already; the commander's own is `commander_guid` (an older module omits it, then the commander is not linked). The commander is a player: the detail view shows what is known of an online player.

  **Map layers.** A collapsible **Map layers** panel above the cards chooses what every map draws; the choice is one setting for all cards and maps, kept in this browser (`localStorage`, one key), with a **Reset to defaults** button. Defaults are what the maps drew before layers existed: game map picture (only when the daemon has pictures), grid, zone outline and name, bot dots, bot names, lead mark and the lead's path. Off by default: names of the zones around, distance scale bar, times on the path, the lead's heading arrow (from its movement: the module sends no facing), healer range (a 40 yd ring around each living healer), and the layers that need data the module's report does not carry yet (commander position and the line to the lead, hostile/neutral/critter mobs, threat lines, bot target lines, aggro range around pull targets). A layer with no data on any map is greyed out with its reason beside it; its saved choice is kept for when the data arrives.

  **Real maps (`TBDASH_MAP_DIR`).** Set `TBDASH_MAP_DIR` (or `--map-dir`) to a folder of map pictures and the Commanders maps draw the game's own map under the party; unset, they keep the plain grid. The folder is what `turtle-agent minimap-extract` writes from your own client: per map id `<id>.png` + `<id>.json` (the main picture) and optionally `<id>.<minimap|worldmap|navmesh>[-<n>f].png` + `.json` (other sources, floors; a picker appears on the card when a map has more than one). The JSON carries `width`, `height`, `px_per_yard` and the world coordinates of the picture's edges (`north`/`south` = world x of the top and bottom edge, `west`/`east` = world y of the left and right edge); the page draws the picture at `(-west, -north)`, `west - east` yards wide and `north - south` yards tall in the map's plane (north up, west left), so a game point `(x, y)` lands on pixel column `(west - y) * px_per_yard`, row `(north - x) * px_per_yard`. **These are extracted game assets: never commit them.** The daemon serves from that folder only (`GET /api/v1/maps/<id>` lists a map's pictures, `GET /api/v1/mapfiles/<name>` serves a `.png` or `.json` whose name is exactly one of those patterns, no sub-folders or links, behind the same login). Copying them to the live server (m910q): `rsync -a --include='*.png' --include='*.json' --exclude='*' ~/turtle-agent-minimaps/ m910q:/home/pk/tortoise-wow-server/data/maps/` (about 105 MB), then in the compose service add the volume `./data/maps:/maps:ro` and the environment `TBDASH_MAP_DIR: /maps` and `docker compose up -d --no-deps tortoise-observability`.
- **Bots** — the one list of bots, online and offline together, plus real online players with a "player" badge (Bots + players / Bots / Players filter). Filter by name/class/spec or by any column with fields in the search box (`lvl:50-60 ilvl:>40 zone:tanaris gold:<10 deaths:0`; numbers take `N`, `A-B`, `>N`, `>=N`, `<N`, `<=N`, text fields match a substring, all terms must match), online/offline, state chips and class chips (the counts double as the fleet census) or "With issues". Three column sets: **Live** (status, health, power, target, zone), **Progress** (XP bar with XP/hour, average item level, gold) and **Activity** (session quests, loot, notable loot, kills, deaths, money, vendors, repairs, give-ups, gathering, AH). Click a row to open the profile.
- **Map** — where the bots are: continent tabs with zone chips, the 2D world/zone map, breadcrumb trails and class legend. Real players show as rings in their class colour (tooltip and profile show group instead of the AI role). Click a marker to open the profile (a narrow side panel on this tab).
- **Progress** — are the bots levelling? Gaining-XP, median/total XP per hour, deaths per minute and died-in-5-min KPIs; level bands (per level while the pool spans ≤10 levels, otherwise five-level buckets) with the band's average equipped item level and its ratio to the band's level; the level-up timeline.
- **Activity** — what happened: pool counters grouped as Quests, Combat, Loot & gathering, Money and Services (the window they cover is labelled `SINCE HH:MM`), then one feed panel switching between quests, notable loot/money/gather drops (filter by quality, class, level, bot) and the live event console.
- **Auction House** — what is listed right now, view only: market summary (listings, per quality, buyout value), then the auction table filtered by item name, minimum quality, required level, seller and bot/player (an Alliance/Horde/neutral switch appears only when the realm has separate houses), sortable by any column and paged 100 rows at a time. Loaded when the tab opens and by its Refresh button; bot sellers open their profile.
- **Issues** — what is broken: KPIs, active issues over time and by zone, then one panel switching between active issues (stuck 60 s while moving, dead 2+ min, unable to reach a combat target 2+ min), recently resolved episodes and the rolling incident feed (last 1000 anomalies ≈ 30 min) with the session-totals line (`GET /api/v1/anomalies/totals`). Only episodes past their gate are shown and no dead-long rows open in the first 5 min after a server restart.
- **Server** — effective settings and versions (RATES, BOTS · POOL / TUNING / GROUPS / WORLD, DIAGNOSTICS), all read from the running server's own getters, never config files. **Copy diagnostic report** turns them plus pool health into a paste-ready plain-text snapshot (no secrets); use it when reporting bugs.
- **Bot profile** — a slide-over from any tab (also the topbar find-a-bot box, shortcut `/`): Overview (live status, issues, recent quest/loot events), Gear & stats (paperdoll, item level), Talents, Spells, Professions, Skills, Bags and Activity (session counters, level history). Offline bots show database values only. Player profiles show the live Overview only (status, group, zone, target, XP) — players are not pool bots, so the database subtabs stay empty.
- **Mobile & Tablet Friendly:** Fully responsive layout for phones (360–430 px) and tablets (768 px) featuring an off-canvas navigation drawer, stacked grids, reflowing settings columns, scrollable tables, and touch-friendly controls while preserving the desktop layout.
- **State census semantics** (Overview): `busy` is standing still but doing real work (loot, cast, sit to eat/drink, or movement within the last 45 s); `stalled` is standing still for 45+ s whose only activity was churn — an active travel target or changing action names, i.e. holding a destination and getting nowhere; `idle` is no movement, action, cast, loot or active target for 45+ s. One colour per state is shared by the census, the roster badges and the map markers, and the 3-min rolling ratios stay in Prometheus (`tortoisebots_state_ratio`) for alerting.

---

## 2. Enabling Observability

### No Go toolchain? Use the one-command setup (Linux / Windows)

Prebuilt dashboard binaries ship with every daily release — no Go, no Docker:

```bash
# Linux: from the module checkout
./tools/observability/run-dashboard.sh --mangosd-conf /path/to/etc/mangosd.conf
# Windows (PowerShell): from the module checkout
.\tools\observability\run-dashboard.ps1 -MangosdConf C:\path\to\etc\mangosd.conf
```

The script downloads the latest `tortoise-observability` binary, reads the
database connection from `mangosd.conf` (`LoginDatabase.Info` /
`WorldDatabase.Info` / `CharacterDatabase.Info`), enables
`AiPlayerbot.Observability = 1` in `aiplayerbot.conf` next to it, and starts
the daemon. Open `http://localhost:8095/dashboard` and sign in with a game
account of GM rank ≥ 2, then restart `mangosd` so the telemetry flag takes
effect. Re-running the script while the dashboard is up is a no-op: stop the
daemon first to update the binary (`--tag vYYYY-MM-DD` pins a version).

### Manual setup (developers)

#### Step 1: Enable Telemetry in `tortoise_bots.conf`
```ini
[TortoiseBotsConf]
AiPlayerbot.Observability = 1
AiPlayerbot.ObservabilityHost = 127.0.0.1
AiPlayerbot.ObservabilityPort = 9195
```

#### Step 2: Run the Daemon

You can run the daemon directly or via Docker:

#### Direct Go Run:
```bash
cd tools/observability
go run cmd/server/main.go --http-port 8095 --udp-port 9195
```

#### Via Docker Compose:
The `observability` service is part of the compose stack (opt-in via `--profile observability`); the dashboard is on port 8095, and talents/class-spell grouping needs the DB env plus the `/dbc` mount (`${DATA_PATH}/dbc:/dbc:ro`).

---

## 3. Prometheus Metrics Endpoint

The daemon exports Prometheus metrics at `http://localhost:8095/metrics`, allowing you to visualize bot performance in Grafana:
- `tortoisebots_active_count{class,role}`: Number of active bots by class and role.
- `tortoisebots_state_ratio`: Rolling-window ratio (0.0–1.0) of time spent per macro state (`combat`/`moving`/`busy`/`stalled`/`resting`/`idle`/`dead`).
- `tortoisebots_grinding_xp_per_hour_total` / `tortoisebots_grinding_bots_gaining` / `tortoisebots_grinding_deaths_per_min` / `tortoisebots_grinding_pct_in_combat`: Pool XP and bot-death activity behind the grinding panel.
- `tortoisebots_issues_active`: Number of active stuck-episode issues by type.
- `tortoisebots_snapshots_total` / `tortoisebots_anomalies_total`: Telemetry ingest counters (roster snapshots published / anomalies detected).

### Commanders API

`GET /api/v1/commanders` — the standing remote-commander bindings from the module's `COMMANDERS` datagrams: `commanders[]` (`commander`, `commander_guid`, `online`, `view`, `group`, `lead`, `bots[]` with class, role, level, hp/max_hp, power/max_power/power_type, alive, lead, map, zone, x/y/z (and, from a newer module, `id` and `target` as decimal-string guids, `st`, `act`, `res`, `cast`, `cc`, `rest`, `mv`, `tname`), `bound_at` (unix seconds, newer module), `run` (`plan`, `state`, `step`, `steps`, `leg`, `kind`, `phase`, `note`, `changed`; only while the run leader has a run), `commander_pos` (`map`, `x`, `y`), `mobs_map` and `mobs[]` (`id`, `entry`, `name`, `hp` percent, `x`, `y`, `victim`, `reaction` h/n/f, `kind` k/v/g/u, and from a newer module `lvl`, `rank` b/e/r, `cc` s/c/f/r, `cast`; `null` when the module does not report them, `[]` when none are near), and `orders[]` oldest first with `at`, `verb`, `detail`, `ok`, `result`), `updated_at`, `age_sec`, `fresh` (false after 12 s without a datagram, with `commanders` empty) `real_maps` (true when `TBDASH_MAP_DIR` is set) and, only when `TBDASH_COMMANDER_URL` is set, `external_url`. With a map folder, `GET /api/v1/maps/<map id>` lists the pictures of a map (`name`, `source`, `url`, and the rectangle `x`/`y`/`w`/`h` they cover in the map's plane; 404 when there are none) and `GET /api/v1/mapfiles/<name>.png|.json` serves them. In memory only; the same list is pushed as the `commanders` stream event.

### Activity API

The per-bot activity data feeding the Activity tab and the Bots list's Activity columns is also served as JSON:

- `GET /api/v1/activity` — pool summary counters, the per-bot activity table, and the pool-wide level timeline (`at`, bot, class, level). The counters are also embedded per bot in every roster snapshot (WebSocket `snapshot` / `/api/v1/bots`) as `activity`. `summary.since`/`summary.since_str` state the window the counters cover (the current game-server session); the dashboard labels every counter panel with it.
- `GET /api/v1/activity/loot?min_quality=&class=&min_level=&max_level=&bot=&limit=` — the loot feed, newest first (`source` is `loot`, `gather`, `skin` or `money`; item rows carry `quality`, `value` = sell price, and `money` = copper picked up).
- `GET /api/v1/activity/quests?bot=&limit=` — the quest feed (`QuestRewarded`, `AcceptQuestAction`, `QuestCompleted`, `QuestUpdateCompleteAction`, `QuestDropped`, `TalkToQuestGiverAction`).

All three read in-memory bounded state; no database queries are made. Money earned/spent are net copper deltas observed between forwarded activity events (vendor cheat-gold borrows are restored before the next event, so they do not distort the totals). Gathering skill-ups are counted per gathered/skinned drop; there is no separate crafting skill-up event. Feed rows carry `time_str` — the daemon's own rendering of the timestamp — so the feeds, the level timeline and the incident table all show the server's clock rather than the viewer's.

**Counter window and persistence.** Counters are cumulative: they carry on across dashboard and game-server restarts (a new game-server session only re-anchors money tracking). The daemon snapshots the rollup to `activity-state.json` once a minute (default `<ICON_CACHE_DIR>/activity-state.json`, override with `--activity-state-file` / `ACTIVITY_STATE_FILE`, empty disables) and restores it at startup. Every 5 minutes the daemon checks which characters still exist and forgets the counters of the deleted ones (for example after a pool reset), so they drop out of memory and out of the file.

## 4. Pool KPI Report (`tools/pool_kpi_report.py`)

Offline counterpart to the live dashboard: one Markdown KPI table for a whole
pool window since a reset timestamp, computed from a snapshot dir plus the
character DB (bag fill only). Stdlib only; no container name is hardcoded —
pass `--db-cmd "docker exec -i <container> mariadb ... -N -B"` (or omit it and
the bag row prints `n/a`). The 2026-10-02 baseline lives in
`tools/pool_kpi_baseline_2026-10-02.json`; pass `--baseline` for a side-by-side
column, `--save-baseline` to freeze a new run (old baselines still load: new
rows show `—` there). Pass `--anomaly-totals-json`
with a `GET /api/v1/anomalies/totals` dump for the `ACTION_LOOP` /
`UNREACHABLE_TARGET` rows: the incident feed (`api/anomalies.json`) is only
the last 1000 rows, while the totals file carries whole-run session counts.

### Zone-migration KPIs (do bots move on to zones that fit their level?)

Pass `--levelup-log logs/levelup.log` (plus `--bots-json` for the snapshot) and
the report adds `zone:` rows. Zone levels come from the world's own
`ai_playerbot_zone_level` cache when `--db-cmd` is given (same table the travel
gates read), otherwise from a small fallback table baked into the script for
the zones a pool snapshot actually contains (the note says which source won).
Fit mirrors the module's RPG/quest-errand ceiling (`GrindSpotPolicy.h` /
`TravelMgr.cpp`): fit is `bot L-2 <= zone_level <= bot L+5`; unknown zone
levels (capitals, instances) count as n/a, never unfit. Start zone is each
bot's earliest zone in `levelup.log` (pool bots are already L2+ when the
window opens, so full history — not just the window — is read; 471/498 agree
with the race-expected start zone on the 2026-10-02 night run).

Rows: fit share per level band (`L1-5` … `L21+`) from the `bots.json` snapshot;
changed-zone count over the window from the levelup trajectory plus the
`LeaveOutgrownZone` request fires in `bot_events.csv` as cross-check (note
that `bot_events.csv` zone rows carry names only, so the `from→to` top pairs
use zone ids from the trajectory); highest zone (by level) reached per bot,
with the count beyond the six start zones; bots still in their start zone
above level 8 (the leave rule fires at L10+, so L9 is still valley age); and
left-start-zone broken down by start zone id (race-expected when the
`bot_events.csv` race disagrees with the trajectory).


## 5. Bot profile (DB inspector)

The bot profile (slide-over) inspects any bot straight from the database — no live session and no telemetry needed — so it also covers offline bots: equipment and bag slots with resolved items, live/saved stats, spells, skills, and talents. Open it from any bot name, row or map marker, the topbar find-a-bot box or a `#/…?bot=<guid>` link; `GET /api/v1/armory/bots` also feeds the offline half of the Bots list.

### Equipped gear summary

Above the paperdoll the profile shows the average item level of the equipped pieces and their quality split (grey/white/green/blue/epic); the same value is repeated as a compact `iLvl` stat next to the bot's gold in the profile header (quality split in its tooltip), so gear level is readable without scrolling. Both use the game's own gear slots — equipment slots 0-18 except the shirt (3) and tabard (18), weapons/offhand/ranged included — and average only the slots that actually hold an item, so a half-dressed bot is not scored as if it wore item level 0 gear. The armory computes this from the profile's own equipment rows; the roster's **Gear** column and the pool panel's per-level-band `ilvl` averages come from the daemon's gear sweep (`armory.GearRollup`: one character-DB query every 5 minutes, never per tick) using the identical slot rule. The copy diagnostic report carries a `- gear:` line plus an `ilvl` value per level band.

### Spells tab

The spell list is assembled from two sources and must be read together:

- **Persisted spells** (`character_spell`): everything the bot learned at runtime — quest rewards, trainer purchases, talents, professions. State shows Active / Inactive / Disabled.
- **Starting spells** (`playercreateinfo_spell` for the bot's race and class): the default spellbook the core grants in `Player::LearnDefaultSpells`. The core adds these as *dependent* spells, and `Player::_SaveSpells` never writes dependent spells, so they have no `character_spell` row on purpose — they are re-learned at every login. They are listed with a **Starting** badge.

Rows are grouped into `Class spells`, `Abilities`, `Auras & Forms`, and `Pet & Minions`. Profession spells live in their own **Professions** tab together with the live profession skill levels — they no longer mix into the spellbook groups. `Class spells` membership comes from the operator's own DBCs: a spell belongs to a class when `SkillLineAbility.dbc` lists it under a `SkillLine.dbc` line whose category is *Class Skills* and whose class mask covers the bot's class — the same rule the client uses to build the spellbook. Without a DBC directory (`--dbc-dir`, `/dbc` in the compose stack) the grouping falls back to name/rank heuristics.

Each group is then split into **active** and **passive** rows (`SPELL_ATTR_PASSIVE`, i.e. talent effects and other never-cast spells such as Malice or Convection). The group header carries both counts, so a "6 class spells" line reads as "4 active · 2 passive" instead of looking like six usable abilities. A handful of legacy talent dummies carry empty attributes and therefore still count as active — the same way the client flags them.

**A short `Class spells` list does not mean the dashboard is hiding spells.** Bot spellbooks are thin by design: with `AiPlayerbot.AutoLearnQuestSpells = 1` and a low `AutoLearnTrainerSpells`, random-pool bots pick up class-quest reward spells (stances, forms, totems, pet skills) via free auto-learn (quest/trainer/dropped spells); owned/manual bots keep the gold-gated paid trainer path and buy trainer spells only when they can afford a trainer visit.

### Professions tab

Profession skills with their live `character_skills` levels plus the profession's own spells. Weapon/armor/language rows stay in Skills.

### Skills tab

One row per skill (deduped server- and client-side; professions moved to their own tab).

### Talents tab

The three class trees in DBC page order (1.12 frame left-to-right), each as a 7-tier × 4-column grid from Talent/TalentTab row/column, with points spent per tree. Data comes from the operator's DBCs (world mirrors as fallback); an empty list means "no data", not "no talents".

---

## 6. Zone Map Artwork Credits

Zone map artwork for custom Turtle WoW locations and upgraded classic zones is by fantasy cartographer **Maps of Mystery (Cameron Holt)** — [Maps of Mystery on ArtStation](https://www.artstation.com/mapsofmystery).

Covered zones (`tools/observability/web/maps/`, WebP at 1002x668):
- Placeholder replacements: Gillijim's Isle (`gillijim.webp`), Gilneas (`gilneas.webp`), Mount Hyjal (`hyjal.webp`), Icepoint Rock (`icepoint.webp`), Lapidis Isle (`lapidis.webp`), Tel'Abim (`telabim.webp`)
- New maps: Balor Island (`balor.webp`), Grim Reaches (`grimreaches.webp`), Northwind (`northwind.webp`)
- High-res upgrades: Alah'Thalas (`alahthalas.webp`), Blackstone Island (`blackstoneisland.webp`), GM Island (`gmisland.webp`), Thalassian Highlands (`thalassianhighlands.webp`), Stonetalon Mountains (`stonetalonmountains.webp`), Upper Karazhan 2F (`upperkarazhan2f.webp`)
