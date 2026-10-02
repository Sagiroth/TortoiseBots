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
    ├──► REST API (/api/v1/status, /api/v1/bots, /api/v1/issues, /api/v1/anomalies)
    └──► WebSocket (/api/v1/stream) -> Embedded Web SPA Dashboard (:8095)
```

---

## 1. Web Dashboard Features

When the daemon is running, sign in with a game account of GM rank ≥ 2 (or run with `--dev-no-auth` for local dev), then open `http://localhost:8095` in your browser (port is configurable via `--http-port`). The dashboard is organised as purpose tabs, each with a one-line purpose under its title; the tab you were last on is remembered across reloads:
- **Overview** — fleet health at a glance: world tick, bots/players online, snapshot age, uptime and active issues; the 10-minute population and tick charts; the low-HP count; the authoritative live-states census; and the pool XP/deaths rollup (bots gaining XP, median XP/hour, deaths per minute, share died in the last 5 minutes).
- **Bots** — who is in the fleet and what each bot carries. The fleet composition by class and AI combat role, the live roster (class/role/level, **average equipped item level** in the `Gear` column — hover for the piece/quality split — XP progress bars with XP/hour and last-gain age, health/power, state, target, zone), and the armory as a sub-view ("All bots (incl. offline)") covering every bot character in the database; the profile header shows gold and a compact `iLvl` next to it (quality split in the tooltip). The `XP`, `Health (HP)` and `Power` bar columns sit right after `Level` so the bars stay visible without scrolling sideways on a laptop screen.
- **Armory** — the same bot inspector as a top-level tab next to Bots: the "All bots (incl. offline)" list and the per-bot sheet (paperdoll, stats, bags, talents, spells, professions, skills, progress). Clicking `All bots (incl. offline)` in the Bots roster opens this sub-view inside Bots and the sidebar highlights Armory; `← Roster` returns to the roster and highlights Bots.
- **Progress** — levelling: one row per level band (per level while the pool spans ≤10 levels, otherwise five-level buckets) with the band's bot count and its average equipped item level plus that item level's ratio to the band's level, so "is gear keeping up?" is one glance; below it the level-up timeline (the gap between a bot's rows is the time it spent on the previous level).
- **Activity** — what bots are doing: pool counters (quests rewarded, handed in and finished-but-not-handed-in, kills, deaths and ghost time, looted items, gathering/skinning and skill-ups, trainer visits and spells learned, give-ups), the per-bot activity table, the quest feed, and the live event console. The counter header names the window they cover (`SINCE HH:MM`, the current game-server session) and the row tooltip spells it out.
- **Economy** — money, vendors, repairs, auction house and notable loot: pool counters (money earned/spent, items sold/bought and their value, notable loot, vendor visits, repairs and their cost, AH listings/bids) plus the **filterable notable-loot feed** (quality green+, class, level range, bot) covering loot, money pickups and gather/skin drops. Same window label as Activity: these are session counters, not lifetime totals.
- **Live Map** — where the fleet is: the top-zone census (click to open that zone's map), then the 2D world map (Kalimdor/Eastern Kingdoms continent tabs, zone chips, breadcrumb trails) and the per-bot side drawer.
- **Issues** — what is broken: persistent issue episodes (bots stuck 60 s while moving, dead 2+ min, unable to reach a combat target 2+ min) with the active-issues chart, issues by zone, the active table and the recently-resolved list, *plus* the rolling incident feed (last 1000 anomalies ≈ 30 min). Only episodes past their gate are shown, short ones are discarded, and no dead-long rows open in the first 5 min after a server restart.
- **Server** — effective settings and versions: module/core revision and max level, then one key/value row per setting grouped into RATES · CORE WORLD, BOTS · POOL, BOTS · TUNING (XP rate, loot rates, autolearn, level-up mounts, AH market), BOTS · GROUPS, BOTS · WORLD and DIAGNOSTICS · LOGS — all read from the running server's own getters, never config files. The **Copy diagnostic report** button turns them plus pool health into a paste-ready plain-text snapshot (no secrets); use it when reporting bugs.
- **Mobile & Tablet Friendly:** Fully responsive layout for phones (360–430 px) and tablets (768 px) featuring an off-canvas navigation drawer, stacked grids, reflowing settings columns, scrollable tables, and touch-friendly controls while preserving the desktop layout.
- **State census semantics** (Overview): `busy` is standing still but doing real work (loot, cast, sit to eat/drink, or movement within the last 45 s); `stalled` is standing still for 45+ s whose only activity was churn — an active travel target or changing action names, i.e. holding a destination and getting nowhere; `idle` is no movement, action, cast, loot or active target for 45+ s. One colour per state is shared by the census, the roster badges and the map markers, and the 3-min rolling ratios stay in Prometheus (`tortoisebots_state_ratio`) for alerting.

---

## 2. Enabling Observability

### Step 1: Enable Telemetry in `tortoise_bots.conf`
```ini
[TortoiseBotsConf]
AiPlayerbot.Observability = 1
AiPlayerbot.ObservabilityHost = 127.0.0.1
AiPlayerbot.ObservabilityPort = 9195
```

### Step 2: Run the Daemon

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

### Activity API

The per-bot activity data feeding the Activity tab and the roster/Armory counters is also served as JSON:

- `GET /api/v1/activity` — pool summary counters, the per-bot activity table, and the pool-wide level timeline (`at`, bot, class, level). The counters are also embedded per bot in every roster snapshot (WebSocket `snapshot` / `/api/v1/bots`) as `activity`. `summary.since`/`summary.since_str` state the window the counters cover (the current game-server session); the dashboard labels every counter panel with it.
- `GET /api/v1/activity/loot?min_quality=&class=&min_level=&max_level=&bot=&limit=` — the loot feed, newest first (`source` is `loot`, `gather`, `skin` or `money`; item rows carry `quality`, `value` = sell price, and `money` = copper picked up).
- `GET /api/v1/activity/quests?bot=&limit=` — the quest feed (`QuestRewarded`, `AcceptQuestAction`, `QuestCompleted`, `QuestUpdateCompleteAction`, `QuestDropped`, `TalkToQuestGiverAction`).

All three read in-memory bounded state; no database queries are made. Money earned/spent are net copper deltas observed between forwarded activity events (vendor cheat-gold borrows are restored before the next event, so they do not distort the totals). Gathering skill-ups are counted per gathered/skinned drop; there is no separate crafting skill-up event. Feed rows carry `time_str` — the daemon's own rendering of the timestamp — so the feeds, the level timeline and the incident table all show the server's clock rather than the viewer's.

**Counter window and persistence.** Counters are session-scoped: they reset when the game server restarts (new session id) or when a roster wipe invalidates it. To keep them across a *dashboard* restart the daemon snapshots the rollup to `activity-state.json` once a minute (default `<ICON_CACHE_DIR>/activity-state.json`, override with `--activity-state-file` / `ACTIVITY_STATE_FILE`, empty disables) and restores it at startup; a snapshot from a different game-server session is dropped by the first datagram instead of leaking the previous pool's numbers into the new run.

---

## 4. Bot Armory (DB inspector)

The **Armory** tab inspects any bot straight from the database — no live session and no telemetry needed — so it also covers offline bots: equipment and bag slots with resolved items, live/saved stats, spells, skills, and talents. It is reachable two ways: the top-level **Armory** sidebar entry, or `All bots (incl. offline)` in the Bots roster (the sub-view; `← Roster` goes back).

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

## 5. Zone Map Artwork Credits

Zone map artwork for custom Turtle WoW locations and upgraded classic zones is by fantasy cartographer **Maps of Mystery (Cameron Holt)** — [Maps of Mystery on ArtStation](https://www.artstation.com/mapsofmystery).

Covered zones (`tools/observability/web/maps/`, WebP at 1002x668):
- Placeholder replacements: Gillijim's Isle (`gillijim.webp`), Gilneas (`gilneas.webp`), Mount Hyjal (`hyjal.webp`), Icepoint Rock (`icepoint.webp`), Lapidis Isle (`lapidis.webp`), Tel'Abim (`telabim.webp`)
- New maps: Balor Island (`balor.webp`), Grim Reaches (`grimreaches.webp`), Northwind (`northwind.webp`)
- High-res upgrades: Alah'Thalas (`alahthalas.webp`), Blackstone Island (`blackstoneisland.webp`), GM Island (`gmisland.webp`), Thalassian Highlands (`thalassianhighlands.webp`), Stonetalon Mountains (`stonetalonmountains.webp`), Upper Karazhan 2F (`upperkarazhan2f.webp`)
