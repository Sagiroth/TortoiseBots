#!/usr/bin/env bash
# Sync TortoiseBots module into the Penqle Docker source tree.
# Single source of truth is this repo (TortoiseBots). The Docker source
# at ../tortoise-docker-penqle/source/modules/TortoiseBots is an
# ephemeral copy (gitignored in that repo) used only for `docker compose build`.
#
# Usage:
#   ./scripts/sync-to-docker.sh
#   ./scripts/sync-to-docker.sh --check   # just show what would be copied
#
# The copy excludes docs/ and other repo-only files that would otherwise
# pollute the core's `rg` audits and are not needed to build the module.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MODULE_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
# Default Docker source location (sibling checkout). Override with env.
DOCKER_ROOT="${DOCKER_ROOT:-$(cd "${MODULE_ROOT}/../tortoise-docker-penqle" && pwd)}"
DEST="${DOCKER_ROOT}/source/modules/TortoiseBots"

if [[ ! -d "${DOCKER_ROOT}/source" ]]; then
  echo "Docker source not found at ${DOCKER_ROOT}/source" >&2
  exit 1
fi

mkdir -p "${DEST}"

RSYNC_ARGS=(
  -a --delete --delete-excluded --prune-empty-dirs
  --exclude='.git/'
  --exclude='build/'
  --exclude='.venv/'
  --exclude='docs/'
  --exclude='AGENTS.md'
  --exclude='README.md'
  --exclude='.gitignore'
  --exclude='scripts/'
)

if [[ "${1:-}" == "--check" ]]; then
  echo "Would sync:"
  echo "  from: ${MODULE_ROOT}/"
  echo "  to:   ${DEST}/"
  rsync -n "${RSYNC_ARGS[@]}" "${MODULE_ROOT}/" "${DEST}/" | head -n 50
  exit 0
fi

rsync "${RSYNC_ARGS[@]}" "${MODULE_ROOT}/" "${DEST}/"
echo "Synced TortoiseBots -> ${DEST}"
ls -la "${DEST}" | head -n 20
