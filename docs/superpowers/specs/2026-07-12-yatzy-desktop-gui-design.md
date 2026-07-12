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
- **Tauri** (Rust backend + vanilla TypeScript/HTML/CSS frontend — no JS
  framework; this is a small single-page tool and a framework would be more
  machinery than the UI needs).
- **The C++ engine is kept as-is and reused, not rewritten.** `yatzy_cpu` gets
  cross-compiled to a native Windows binary (MinGW-w64, from WSL — avoids
  needing Visual Studio installed) and bundled as a **Tauri sidecar binary**
  (Tauri's standard mechanism for shipping and invoking a companion
  executable). This is exactly the seam the CLI's `--json` mode was built
  for. The existing Linux/WSL build of `yatzy_cpu` is untouched and still
  works standalone for CLI use.

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
the app automatically invokes the backend's `get_recommendation` command (no
extra button click) and renders one of two result views:

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
   calls the Rust `get_recommendation(state)` Tauri command.
3. The Rust command translates that state into `yatzy_cpu` CLI args
   (`--used`, `--upper`, `--dice`, `--rerolls`, `--json`), spawns the bundled
   sidecar binary, parses its JSON stdout, and returns the parsed
   `QueryResult` (reroll options or category options) to the frontend.
4. The frontend renders the result. Choosing a category option updates the
   scorecard state and resets the turn state for the next roll. Choosing a
   reroll option is purely advisory (informs what you do with the physical
   dice) and does not itself change any state — you drive state forward only
   by entering new dice values or by scoring a category.

## Error handling

If the sidecar fails to spawn, exits non-zero, or returns output that
doesn't parse as the expected JSON shape, show an inline error banner in the
turn panel. This is a personal tool used by one person on one machine — a
clear error message is sufficient; no retry queue or recovery workflow is
needed.

## Testing

- **Rust backend:** an integration-style test that invokes the real bundled
  sidecar binary with known arguments and asserts on the parsed result —
  consistent with this project's existing preference for exercising real
  behavior over mocking the subprocess boundary. The DP cache makes repeat
  invocations fast after the first solve.
- **Frontend:** unit tests for the pure game-state-transition logic (e.g.
  "given the current scorecard state and a chosen category, produce the next
  state") — this logic has no dependency on a real browser or Tauri runtime
  and can be tested in isolation.
- **Manual verification:** launch the actual dev build and play one full
  15-turn game by hand before considering this done. A GUI's real proof is
  driving it end-to-end, not just passing unit tests.

## Build/packaging

- Cross-compile `yatzy_cpu` for Windows via MinGW-w64 from WSL, producing
  `yatzy_cpu.exe`, placed where Tauri's sidecar configuration expects it.
- `npm run tauri dev` for iteration, `npm run tauri build` to produce the
  distributable Windows app.
- No changes to the existing Linux/WSL build of `yatzy_cpu`, its Makefile
  targets, or any of the prior phase's code.

## Out of scope for this spec

- Save/resume of an in-progress game across app restarts.
- Any platform beyond Windows (no macOS/Linux GUI packaging).
- The Maxi Yatzy / GPU solver — still untouched, still deferred.
- Automated end-to-end GUI testing (manual click-through is the verification
  method for this phase).
