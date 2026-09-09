# Contributing to TortoiseBots

Thanks for helping make PlayerBots better for Tortoise WoW. Bug reports,
feature ideas, documentation improvements, testing, and code contributions are
all welcome.

## Quick start

1. Search the existing issues before starting.
2. For a larger change, open or comment on an issue first.
3. Fork the repository and create a focused branch.
4. Link the relevant issue in your pull request, for example `Fixes #123`.
5. Test the change as far as possible and explain what you tested.

Small fixes and documentation changes can go straight to a pull request. For
larger gameplay or architecture changes, an issue first helps avoid duplicated
work and keeps the design focused.

## Current core target

TortoiseBots currently targets the `bot-helpers` branch of
[`Penqle/tortoise-wow`](https://github.com/Penqle/tortoise-wow). Until that work
is merged into `main` or `1181dev`, build and test integration changes against
`bot-helpers`.

Pull requests for this repository should normally target TortoiseBots'
`main` branch unless an issue says otherwise.

## Project guidelines

- Keep TortoiseBots optional: the core must still build without the module.
- Prefer module-only changes. If a core change is necessary, explain the
  generic host capability it provides and why an existing seam is insufficient.
- Do not reintroduce direct bot coupling such as `WorldSession::GetBot()` or
  `m_bot` into normal core gameplay code.
- Keep SQL migrations in the appropriate `data/sql/world` or `data/sql/char`
  directory, use a timestamped filename, and comment non-obvious statements.
- If behavior is adapted from another project, record the source and commit in
  [`docs/PROVENANCE.md`](docs/PROVENANCE.md).

## Pull requests

Keep pull requests small enough to review. Include:

- what changed and why;
- the issue or proposal it addresses;
- build, test, or runtime checks performed;
- screenshots or logs when they help explain gameplay behavior.

If a full build or runtime test is not available, say so clearly rather than
leaving the reviewer to guess.
