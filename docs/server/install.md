---
id: server-install
title: Building, Installing & Updating TortoiseBots
category: server
summary: How to build, compile, and configure TortoiseBots natively with the Tortoise WoW core, and verify your running build version.
tags: [server, install, build, cmake, compile, update, version]
relates_to:
  - server-configuration
  - server-bot-pool
  - server-dashboard
---

# Building, Installing & Updating TortoiseBots

TortoiseBots compiles natively as an in-tree module for the Tortoise WoW 1.18.1 server core. This page covers cloning the module, compiling with CMake, config file and database setup, and checking which version is running.

For what the settings mean, see [How configuration works](configuration.md).

## Prerequisites

A working build environment meeting the [`tortoise-wow`](https://github.com/tortoise-wow/tortoise-wow) repository requirements on the `main` branch.

## Quick Build & Installation

From the root of your `tortoise-wow` server checkout:

### 1. Clone the Module

Clone TortoiseBots into the `modules/` directory:

```bash
git clone https://github.com/Sagiroth/TortoiseBots.git modules/TortoiseBots
```

### 2. Configure with CMake

```bash
cmake -B build \
  -DMODULES=static \
  -DMODULE_TORTOISEBOTS=static
```

| CMake Option | Purpose |
| :--- | :--- |
| `MODULES` | Static module linkage into the core executable. |
| `MODULE_TORTOISEBOTS` | Selects TortoiseBots as the active native bot engine. |

### 3. Compile the Server

```bash
cmake --build build -j"$(nproc)"
```

### 4. Install Binaries and Assets

```bash
cmake --install build
```

## Configuration Files and Database Migrations

### Configuration Files

On install:

- `aiplayerbot.conf.dist` is installed next to `mangosd.conf`. On the first install only, `aiplayerbot.conf` is created from it. Later installs keep your edited file.
- `modules/tortoise_bots.conf` is created from the `conf/tortoise_bots.conf.dist` template the same way: created once, never overwritten.

### SQL Migrations

Module database updates live in `data/sql/world/` and `data/sql/character/` of the module source tree. The core's automatic database updater reads these directories and applies new migrations at startup. No manual SQL patches needed.

## Verifying Your Build Version

The version comes from the repository root `VERSION` file, written by the release workflow on every release. If the file is unreadable or absent, the build reports `dev`.

Four ways to check the running version:

| Verification Method | Where to Look | Expected Output |
| :--- | :--- | :--- |
| **Server startup log** | Server console or `Server.log` | `TortoiseBots <version> (AI enabled\|disabled)`, plus the author/source attribution line. |
| **In-game chat command** | `.bot version` (aliases `.bot v`, `.bot about`, `.bot credits`) | `TortoiseBots <version>`, `TBM:VERSION|<version>`, and the author attribution line. |
| **Addon roster reply** | Addon roster responses carrying `TBM:VERSION|<version>` | The addon shows the server version from this line (shows `server ?` when the connected module never sends it). |
| **Git release tags** | Git repository tags | Semver release tags (e.g. `v1.4.0`), alongside older per-merge `<UTC date>-v<N>` build tags. |

If the log shows `dev`, the build had no readable root `VERSION` file.

## Updating TortoiseBots

1. Pull the latest module code:
   ```bash
   cd modules/TortoiseBots
   git pull origin main
   ```
2. From the core repository root, recompile and reinstall:
   ```bash
   cd ../..
   cmake --build build -j"$(nproc)"
   cmake --install build
   ```
3. Restart `mangosd`. The core auto-updater applies new SQL migrations automatically, and your `aiplayerbot.conf` settings stay intact.
4. If the update adds new settings, check `aiplayerbot.conf.dist` and merge the keys you want.
