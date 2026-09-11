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

TortoiseBots includes an optional, zero-overhead observability subsystem located in [`tools/observability`](../../tools/observability). It provides live visibility into bot fleet health, active locations, combat states, and navigation stuck episodes.

```text
TortoiseBots Module (C++)
    │
    │  UDP Packets (non-blocking, port 9195)
    ▼
Go Observability Daemon (tools/observability)
    │
    ├──► Prometheus Metrics HTTP Endpoint (/metrics)
    ├──► REST API (/api/state, /api/issues)
    └──► WebSocket (/ws) -> Embedded Web SPA Dashboard (:8080)
```

---

## 1. Web Dashboard Features

When the daemon is running, opening `http://localhost:8080` in your browser provides:
- **2D World Map:** Live rendering of Kalimdor and Eastern Kingdoms with continent tabs, zone chips, and real-time bot position markers.
- **Roster & Health Overview:** Real-time list of all active bots, class icons, current levels, health/mana percentages, and target units.
- **Macro-State Breakdown:** Fleet-wide visualization showing how many bots are currently fighting, resting, traveling, looting, or dead.
- **Persistent Issue Tracker:** Any bot that gets stuck, loops an action, or fails to reach a target for 5+ minutes is automatically logged as a tracked episode. You can inspect the root cause and clear resolved episodes.

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
go run cmd/server/main.go --http-port 8080 --udp-port 9195
```

#### Via Docker Compose:
If using containerized deployment (e.g. Docker Compose runtime), ensure the `observability` service container is running.

---

## 3. Prometheus Metrics Endpoint

The daemon exports Prometheus metrics at `http://localhost:8080/metrics`, allowing you to visualize bot performance in Grafana:
- `tortoise_bots_active_total`: Number of active bots by faction and class.
- `tortoise_bots_state_count`: Total bots in combat, travel, dead, or resting states.
- `tortoise_bots_issues_active`: Number of unresolved stuck episodes.
- `tortoise_bots_telemetry_packets_received_total`: Telemetry ingest rate.
