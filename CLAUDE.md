# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this repo is

Three related but independent Yatzy solvers/apps sharing one repo:

1. **Standard (5-dice, Scandinavian) Yatzy CPU solver** — `yatzy_engine.h`/`.cpp`, `precompute_std.h`, `yatzy_cpu.cpp`. A backward-induction DP solver exposed as a stateless CLI (`yatzy_cpu`). This is the actively-developed core.
2. **Maxi (6-dice) Yatzy GPU/CUDA solver** — `kernel.cu`, `main.cu`, `precompute.h`. Same algorithm family, redesigned for GPU. Per the repo's original README, the CUDA kernels have never been compiled or run on real hardware (the dev sandbox had no GPU) — treat as unverified, expect first-compile issues.
3. **Yatzy Desktop GUI** (`gui/`) — a Tauri desktop app (vanilla TypeScript/HTML/CSS, no framework) that wraps `yatzy_cpu` as a sidecar binary and adds a full turn-by-turn play UI (Solo and vs-Computer modes). This is where most current work happens.

## Build & test commands

### Root (C++ engines)

```bash
make yatzy_cpu       # build the standard-Yatzy CLI
make test_yatzy      # build + run all C++ tests (precompute, engine, CLI smoke test)
make                  # build the Maxi GPU solver (requires nvcc + matching -arch in the Makefile)
make clean
```

Run a single C++ test binary directly after building it (e.g. `make test_yatzy_engine && ./test_yatzy_engine`, or `make test_precompute_std && ./test_precompute_std`, or `./test_yatzy_cli.sh` for the CLI smoke test).

Query the CLI directly:

```bash
./yatzy_cpu --used 0,3,7 --upper 12 --dice 2,2,3,5,6 --rerolls 2 [--json] [--dp-cache <path>]
```

### `gui/` (TypeScript/Tauri)

```bash
npm test                      # vitest — pure-logic unit tests only (see Testing below)
npx tsc --noEmit               # typecheck
npm run dev                    # vite dev server — browser preview, NO Tauri/sidecar available
./scripts/build-sidecar.sh     # cross-compile yatzy_cpu for Windows; run after ANY change to
                                # yatzy_engine.cpp / yatzy_cpu.cpp / precompute_std.h at repo root
./scripts/sync-to-windows.sh   # rsync gui/ to a native Windows path (Windows can't execute off
                                # the WSL UNC path) — run before any Windows dev/build
```

Run a single test file: `npx vitest run src/match.test.ts` (or any other `*.test.ts`).

The actual Tauri app only runs on Windows. After syncing, from PowerShell (or `powershell.exe` from WSL):

```powershell
cd <synced-folder>
npm install
npm run tauri dev      # iterate
npm run tauri build    # produce gui.exe + MSI/NSIS installers
```

## Architecture

### The CLI is deliberately stateless

`yatzy_cpu` never remembers anything between invocations — every call passes the full game state (`--used`, `--upper`, `--dice`, `--rerolls`) and gets back one ranked decision (reroll options or category options). This is a deliberate seam: the DP solve/scoring logic lives in exactly one place (`yatzy_engine.cpp`), and the GUI owns all session state itself rather than duplicating engine logic.

### DP solve & caching

The DP table over all reachable states is small enough (~2.1M `(mask, upperTotal)` states) to solve fully up front rather than per-query. `solveDP()` in `yatzy_engine.cpp` does this once (a couple of minutes cold), and `loadOrSolveDP()` caches the result to a binary file (`yatzy_cpu_dp.bin` by default) so every later query, across every future run, is near-instant. The solver emits one `"popcount P/15"` progress line per DP level to stderr during a cold solve; the GUI streams these live via `Command.spawn()` (not `.execute()`) to show solve progress instead of only the final result.

### GUI layering (`gui/src/`)

Each file has one job, and they compose in this order:

- **`state.ts`** — pure `GameState` (one scorecard: category scores, dice, rerolls left) and its transitions (`applyHold`, `scoreCategory`, `rollRemaining`, etc.). No DOM, no Tauri.
- **`match.ts`** — pure `MatchState` wrapping one or two `GameState`s (`{ mode: "solo"|"vsComputer", turn, player, computer }`) for the two play modes. `activeGameState`/`withActiveGameState` are the seam every handler routes state changes through, so solo mode is just the degenerate case (`computer: null`, `turn` never flips).
- **`cliArgs.ts`** / **`parseResult.ts`** — pure functions building the CLI args from a `GameState` and parsing its `--json` stdout into a typed result.
- **`sidecar.ts`** — the only file that touches Tauri: spawns `yatzy_cpu` as a sidecar, streams progress, resolves with a parsed result. Also exposes `isEngineWarm()` (checks whether the DP cache file already exists) for the "Warm Up Engine" button.
- **`render.ts`** — pure DOM writes with no state of its own; every function takes data in and writes to fixed element IDs in `index.html`.
- **`main.ts`** — the only stateful, orchestrating file. Owns `match`/`lastResult`/settings as module-level `let`s, wires DOM events to `state.ts`/`match.ts` transitions, and calls `sidecar.ts` + `render.ts`. Uses a `generation` counter, bumped on every `setMatch()`, to detect and discard stale async results (e.g. a sidecar call that resolves after "New Game" was clicked).

  **This generation-counter pattern is subtle and has caused real, deterministic bugs** (see `docs/superpowers/plans/2026-07-15-yatzy-gui-vs-computer.md` for the incident). The specific failure mode: capturing a `generation` snapshot *before* an operation that itself calls `setMatch` (which unconditionally bumps `generation`), then comparing against that now-stale snapshot afterward — the guard becomes permanently, not just occasionally, wrong. When adding a new async flow that checks `generation`, snapshot it *after* any `setMatch` call that's part of the same logical operation, not before.

### Testing philosophy

Only pure logic gets unit tests (`state.test.ts`, `match.test.ts`, `cliArgs.test.ts`, `parseResult.test.ts` — there's no jsdom in this project). `render.ts`, `main.ts`, and `sidecar.ts` are DOM/Tauri-dependent and are verified manually by running the real app — `Command.spawn()` only works through Tauri's real IPC bridge, so a plain browser preview (`npm run dev`) can exercise everything except the actual sidecar call.

### Design/plan docs

Non-trivial features in this repo go through a documented spec → plan → implementation cycle, recorded in `docs/superpowers/specs/` and `docs/superpowers/plans/` as paired, dated `YYYY-MM-DD-<topic>(-design).md` files. Check there for the reasoning behind an existing feature before re-deriving it from the diff.
