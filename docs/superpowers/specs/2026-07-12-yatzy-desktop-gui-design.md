# Yatzy Desktop GUI — Design

## Context

The previous phase ([2026-07-12-standard-yatzy-cpu-solver-design.md](2026-07-12-standard-yatzy-cpu-solver-design.md))
built a stateless CLI (`yatzy_cpu`) around a backward-induction DP engine for
standard Scandinavian Yatzy, deliberately designed with a `--json` output mode
as the seam a future GUI would call through. This spec covers that GUI: a
Windows desktop app used while physically playing a real dice game, where you
enter each roll and get back the optimal move.

## Platform and toolkit

- **Windows native app.** You'll be using this while physically playing, not
  from a WSL terminal, so it needs to launch like any normal Windows program.
- **Tauri** (vanilla TypeScript/HTML/CSS frontend, no JS framework — this is
  a small single-page tool and a framework would be more machinery than the
  UI needs). **No custom Rust backend command is needed**: Tauri's
  `@tauri-apps/plugin-shell` exposes `Command.sidecar(...).execute()`
  directly to TypeScript, so the frontend builds the CLI args, spawns the
  sidecar, and parses its JSON output itself. The Rust side (`src-tauri/`)
  is just Tauri's standard scaffold plus the sidecar's permission/config
  wiring — no custom command logic to write or test.
- **The C++ engine is kept as-is and reused, not rewritten.** `yatzy_cpu` gets
  cross-compiled to a native Windows binary (MinGW-w64, from WSL — avoids
  needing Visual Studio installed) and bundled as a **Tauri sidecar binary**
  (Tauri's standard mechanism for shipping and invoking a companion
  executable, named `yatzy_cpu-<host-target-triple>.exe` per Tauri's
  convention). This is exactly the seam the CLI's `--json` mode was built
  for. The existing Linux/WSL build of `yatzy_cpu` is untouched and still
  works standalone for CLI use.
- **Synced build-staging copy, not a second checkout.** Windows refuses to
  directly execute a binary sitting on the WSL-mounted UNC path
  (`\\wsl.localhost\...`) — it only runs from a native Windows path. This
  repo (WSL-hosted, under `gui/`) stays the sole source of truth; a small
  `rsync` script copies `gui/` to a disposable native-Windows build-staging
  folder (excluding `node_modules`/`target`/`dist`, which are regenerated
  there fresh), and that's where `npm run tauri dev`/`build` actually run.
  No second git checkout, no git-sync complexity — just a one-way file copy,
  re-run whenever there's something new to build or test on Windows.

## Scope

- **Full turn-by-turn game assistant**, not a one-shot query tool. The GUI
  owns all game state (scorecard, current turn) — this is exactly why the CLI
  was kept stateless in the prior phase.
- **In-memory only.** Closing the app loses an in-progress game. A full game
  is ~15 turns / a few minutes, so this is an acceptable v1 limitation, not
  an oversight — save/resume across restarts is explicitly deferred.

## Components and turn flow

**Scorecard panel** — all 15 categories (name + score, or a dash if still
open), the upper-section subtotal with a "63 for bonus" progress indicator,
the lower-section subtotal, and the grand total.

**Turn panel** — five dice-value inputs (1–6 each) and a rerolls-remaining
indicator that steps 2 → 1 → 0. Whenever all 5 dice inputs hold valid values,
the app automatically calls the sidecar and renders one of two result views
(no extra button click to trigger the query itself). A single **"Reroll →"**
button (visible whenever rerolls remain) advances rerolls-remaining down by
one and clears the dice inputs for the next roll — there's no separate "stop
rerolling" action; if you're holding everything, you just retype the same 5
values on the next roll, and the ranked list already reflects that holding
everything is correctly modeled as continuing to the next decision point,
not skipping straight to scoring:

- **Rerolls remaining > 0:** a ranked list of "hold these values, reroll the
  rest" options, each with its expected value, including the "hold all 5 /
  stop rerolling" option. You pick one as guidance, physically reroll the
  dice, then just retype all 5 current values for the next roll — no
  pre-filling of held values, simplest and least error-prone.
- **Rerolls remaining = 0:** a ranked list of open categories with the
  resulting score and expected value for each. Clicking *any* open category
  — the top-ranked one or a deliberate deviation — records that score into
  the scorecard and starts the next turn fresh at rerolls=2.

**End of game:** once all 15 categories are filled, display the final total,
including the bonus if earned.

## Data flow

1. Frontend holds the authoritative game state: `usedMask` (which categories
   are filled), each filled category's recorded score, the running upper
   total, and the current turn's dice values + rerolls-remaining.
2. On every dice-input change where all 5 values are valid, the frontend
   builds the `yatzy_cpu` CLI args (`--used`, `--upper`, `--dice`,
   `--rerolls`, `--json`) from that state, via a pure function.
3. The frontend calls `Command.sidecar('binaries/yatzy_cpu', args).execute()`
   (from `@tauri-apps/plugin-shell`), then parses the JSON from its stdout,
   via another pure function, into the reroll-options or category-options
   shape.
4. The frontend renders the result. Choosing a category option updates the
   scorecard state and resets the turn state for the next roll. Choosing a
   reroll option is purely advisory (informs what you do with the physical
   dice) and does not itself change any state — you drive state forward only
   by entering new dice values, clicking "Reroll →", or scoring a category.

## Error handling

If the sidecar fails to spawn, exits non-zero, or returns output that
doesn't parse as the expected JSON shape, show an inline error banner in the
turn panel. This is a personal tool used by one person on one machine — a
clear error message is sufficient; no retry queue or recovery workflow is
needed.

## Testing

Calling the real sidecar requires a running Tauri app (the `Command.sidecar`
API only works through Tauri's IPC bridge), so it isn't something a plain
Node-based unit test can exercise directly. Testing instead targets the pure
logic around that call, plus a manual end-to-end pass for the call itself:

- **Frontend unit tests** (no Tauri runtime needed):
  - Game-state-transition logic (e.g. "given the current scorecard state and
    a chosen category, produce the next state").
  - CLI-args-building logic (e.g. "given this game state, produce exactly
    these `--used`/`--upper`/`--dice`/`--rerolls`/`--json` args").
  - JSON-result-parsing logic (e.g. "given this raw sidecar stdout string,
    produce this typed reroll-options or category-options result").
- **Manual verification:** launch the actual dev build and play one full
  15-turn game by hand before considering this done. This is what actually
  proves the sidecar spawn + real engine output works end-to-end, and it's
  what proves the rendered UI itself (which no automated test here touches)
  is usable — a GUI's real proof is driving it, not just passing unit tests.

## Build/packaging

- Cross-compile `yatzy_cpu` for Windows via the `g++-mingw-w64-x86-64-posix`
  toolchain (the POSIX threading variant — required for `std::thread`
  support; the `win32` variant lacks it) from WSL, producing a binary named
  `yatzy_cpu-<host-target-triple>.exe` (the target triple comes from running
  `rustc --print host-tuple` on the Windows side — confirmed to be
  `x86_64-pc-windows-msvc` for this machine's Rust install, even though the
  binary itself is built with a different, unrelated toolchain — Tauri only
  cares about the filename matching its own build's target triple, not how
  the sidecar was compiled), placed in `gui/src-tauri/binaries/` in this
  repo, synced along with the rest of `gui/` to the Windows build-staging
  folder.
- `npm run tauri dev` for iteration, `npm run tauri build` to produce the
  distributable Windows app — both run from the synced Windows build-staging
  folder.
- No changes to the existing Linux/WSL build of `yatzy_cpu`, its Makefile
  targets, or any of the prior phase's code.

## Out of scope for this spec

- Save/resume of an in-progress game across app restarts.
- Any platform beyond Windows (no macOS/Linux GUI packaging).
- The Maxi Yatzy / GPU solver — still untouched, still deferred.
- Automated end-to-end GUI testing (manual click-through is the verification
  method for this phase).
