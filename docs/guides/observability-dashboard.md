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

When the daemon is running, sign in with a game account of GM rank ≥ 2 (or run with `--dev-no-auth` for local dev), then open `http://localhost:8095` in your browser (port is configurable via `--http-port`):
- **2D World Map:** Live rendering of Kalimdor and Eastern Kingdoms with continent tabs, zone chips, and real-time bot position markers.
- **Roster & Health Overview:** Real-time list of all active bots, class names (icons exist only for items/spells/talents), current levels, per-bot XP progress bars (XP/hour + last-gain age), health/mana percentages, target units with target level, and active travel purpose/destination.
- **Grinding panel:** Pool-wide "are they grinding" rollup — bots gaining XP, median/total XP per hour, kills/min and % killed in the last 5 min, % in combat, % on grind travel, and level distribution.
- **Server panel (effective settings):** Module/core versions, live server rates (XP kill/quest/explore, drops, honor/rep, talent, bot XP multiplier), key TortoiseBots flags, and diagnostics toggles — all read from running getters, never config files. The **Copy diagnostic report** button produces a compact plain-text snapshot (no secrets) to paste into GitHub issues or Discord; use it when reporting bugs.
- **Macro-State Breakdown:** Fleet-wide 3-min rolling visualization: `combat` (in combat), `moving` (a movement generator owns the bot), `busy` (standing still but doing something: looting, casting, sitting to eat/drink, or working an active travel target — plus anything with observable activity in the last 45 s, e.g. the pause between waypoints), `resting` (rest flag), `idle` (no movement, action, cast, loot, or active target for 45+ s — really doing nothing), `dead`.
- **Persistent Issue Tracker:** Bots stuck (60 s frozen while moving), dead long (2+ min), or unable to reach a combat target (2+ min, auto-closes when the snapshot contradicts it) are tracked as episodes. The tab defaults to the ≥10 min persistent filter — the subset worth reading; only episodes past their gate are shown, short ones are discarded, and no dead-long rows open in the first 5 min after a server restart. Incidents is a rolling last-1000-event window, not history.
- **Mobile & Tablet Friendly:** Fully responsive layout for phones (360–430 px) and tablets (768 px) featuring an off-canvas navigation drawer, stacked grids, scrollable tables, and touch-friendly controls while preserving the desktop layout.

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
- `tortoisebots_state_ratio`: Rolling-window ratio (0.0–1.0) of time spent per macro state (`combat`/`moving`/`busy`/`resting`/`idle`/`dead`).
- `tortoisebots_grinding_xp_per_hour_total` / `tortoisebots_grinding_bots_gaining` / `tortoisebots_grinding_kills_per_min` / `tortoisebots_grinding_pct_in_combat`: Pool XP and kill activity behind the grinding panel.
- `tortoisebots_issues_active`: Number of active stuck-episode issues by type.
- `tortoisebots_snapshots_total` / `tortoisebots_anomalies_total`: Telemetry ingest counters (roster snapshots published / anomalies detected).

---

## 4. Bot Armory (DB inspector)

The **Armory** tab inspects any bot straight from the database — no live session and no telemetry needed — so it also covers offline bots: equipment and bag slots with resolved items, live/saved stats, spells, skills, and talents.

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
