# Yatzy browser (WASM) solo solver — design

Date: 2026-07-19

## Goal

Make the standard-Yatzy solver usable in a plain web browser — no Tauri, no
installed app — by compiling the existing C++ engine to WebAssembly and
serving Solo mode as a page on `ellewsen.no` (SvelteKit site,
`tellewsen.github.io` repo, adapter-static → GitHub Pages).

## Scope

- **In scope**: Solo mode (expected-value category/reroll recommendations),
  running fully client-side via WASM, no server component.
- **Out of scope**: vs-Computer / win-probability mode. Its table is
  `mask × (CapScore+1) × NumThresholds × 4 bytes` ≈ 32768 × 64 × 375 × 4 ≈
  **3 GB** — not shippable as a static asset. The browser build disables
  this mode; it stays desktop (Tauri)-only. Revisiting this is a separate
  future design if ever wanted (e.g. a server component), not part of this
  work.
- Same C++ engine, not a TypeScript reimplementation — the DP solve/scoring
  logic stays in exactly one place, compiled rather than ported, matching
  this repo's existing architecture principle.

## Part 1 — `optimal-yatzy`: WASM build of the engine

### New C++: `wasm_bridge.cpp`

A small new file exposing one `extern "C" EMSCRIPTEN_KEEPALIVE` entry point:

```
char* query_json(int usedMask, int upperTotal, const char* diceCsv, int rerollsLeft);
```

On first call it runs `buildFlatTables()` (cheap combinatorics — not the DP
solve) and `loadDP()` from the baked-in cache file, caching both in function
statics. Every call after that is pure table lookups via the existing
`query()` — no solving happens client-side, so there's no threading
requirement and therefore no `SharedArrayBuffer`/COOP-COEP cross-origin
isolation headers to configure. This is what makes plain static hosting
(GitHub Pages) sufficient.

### Refactor: shared JSON serialization

`yatzy_cpu.cpp` currently inlines the `--json` output format. Extract it into
`queryResultToJson(const QueryResult&) -> std::string` in
`yatzy_engine.cpp`/`.h`, used by both the native CLI and `wasm_bridge.cpp`.
One definition of the JSON shape; `parseResult.ts` keeps working unmodified
on either path. Pure refactor — `test_yatzy_cli.sh` already asserts on the
`--json` output and covers this with no behavior change.

### Baking in the DP table

The DP cache (`yatzy_cpu_dp.bin`, ~8MB, generated the normal way by running
native `yatzy_cpu` once) is fed into the Emscripten build via
`--preload-file`, mapped to the same path `loadDP()` already expects inside
Emscripten's virtual filesystem. The existing `fopen`-based cache-loading
code in `yatzy_engine.cpp` runs completely unmodified — no engine code
changes for loading.

### Build script

New `scripts/build-wasm.sh` (alongside the existing `build-sidecar.sh`):
runs `emcc` against `yatzy_engine.cpp` + `wasm_bridge.cpp` with
`-O3 -std=c++17 -s MODULARIZE=1 -s EXPORT_ES6=1 -s ENVIRONMENT=web
--preload-file <dp.bin path>`, outputting to `gui/src/wasm/`
(`yatzy_engine.js` glue, `.wasm`, `.data`). Requires `emsdk` installed
locally (one-time setup, not currently present on this machine) — the
implementation plan includes install steps.

## Part 2 — `optimal-yatzy/gui`: engine seam for the Tauri app

`sidecar.ts` already has exactly the seam needed: two functions,
`getRecommendation()` and `isEngineWarm()`, gated on
`"__TAURI_INTERNALS__" in window`. `main.ts` calls only those two functions
(one import line, five call sites).

- **New `engine.ts`**: same two-function interface, dispatching to
  `sidecar.ts` when Tauri is present, or the new `wasmEngine.ts` when it
  isn't. `main.ts` changes its one import line (`./sidecar` → `./engine`);
  no other change to `main.ts`/`state.ts`/`match.ts`/`render.ts`.
- **New `wasmEngine.ts`**: dynamically imports the compiled WASM glue module
  (so the Tauri build path never fetches it), calls `query_json(...)`, and
  feeds the JSON through the existing `parseResult.ts` unchanged. Since the
  DP table ships pre-solved, there's no cold-solve possible in this path —
  `isEngineWarm()` always resolves `true`, and the `onProgress` callback
  simply never fires.
- **vs-Computer mode gating**: when `hasTauriRuntime()` is false, the
  "vs Computer" button is disabled with a short note that it's desktop-only
  (the win-prob table can't ship to a browser — see Scope above).
- `sidecar.ts` itself is untouched — it remains the only Tauri-touching
  file, preserving the existing architectural invariant documented in this
  repo's `CLAUDE.md`.

## Part 3 — `tellewsen.github.io`: native SvelteKit route

The site already has a `src/routes/utils/<tool>/+page.svelte` pattern for
small browser tools (b64, jwt, qr, cidr, bmi, fnr, uuid) sharing `Nav` and
the site's design system. The Yatzy solver follows the same pattern rather
than being embedded as a disconnected static subtree.

- **New route**: `src/routes/utils/yatzy/+page.svelte`.
- **Reused pure logic, copied across** (manual copy, not a package
  dependency — these repos aren't set up to share code automatically):
  `state.ts`, `match.ts`, `cliArgs.ts`, `parseResult.ts`, `wasmEngine.ts`
  from `optimal-yatzy/gui/src/` into a new `tellewsen.github.io/src/lib/yatzy/`.
  These are pure TS with no Tauri or DOM dependency, so they move
  unmodified. `engine.ts`'s Tauri-detection dispatch is not needed here —
  the website is only ever a plain browser, so the Svelte page calls
  `wasmEngine.ts` directly.
- **`match.ts` is reused as-is** even though the website is solo-only: it
  already models solo mode as the degenerate case (`computer: null`,
  `turn` never flips), including the generation-counter guard against stale
  async results if the user mashes buttons mid-query. No need to re-derive
  a simpler variant.
- **New Svelte UI**: `+page.svelte` (plus perhaps 1-2 small child
  components for the dice/category displays) replaces `render.ts` +
  `main.ts`'s DOM orchestration with Svelte's reactive state, styled to
  match the site rather than reusing the Tauri app's CSS.
- **WASM artifacts** (`yatzy_engine.wasm`, glue `.js`, `.data`) are copied
  into `src/lib/yatzy/wasm/` in the website repo.
- **Deploy**: no new pipeline. Once these files land in the website repo's
  source tree, its existing `pnpm build` / `pnpm deploy`
  (adapter-static → gh-pages) ships it like any other page.
- The ~8MB `.data` file is committed as-is to the website repo (trivial for
  git, no LFS needed; the DP table is fixed and never changes, so it's a
  one-time addition to history).

## Testing

- **optimal-yatzy**: the JSON-serialization extraction is covered by the
  existing `test_yatzy_cli.sh` smoke test. `wasm_bridge.cpp`, `wasmEngine.ts`,
  and `engine.ts` are thin glue with no pure logic of their own — like
  `sidecar.ts` today, they stay untested by design and get verified by
  running the actual browser build, matching this repo's existing testing
  philosophy (only pure logic gets unit tests).
- **tellewsen.github.io**: this repo has no test suite today; not
  introducing one just for this feature. The copied pure-logic files keep
  their vitest coverage at the source (`optimal-yatzy`); the website copy is
  verified manually, consistent with how that repo already operates.

## Docs

- Update `optimal-yatzy/CLAUDE.md`: GUI layering section gets `engine.ts`/
  `wasmEngine.ts`, `scripts/build-wasm.sh` build command, vs-Computer gating
  note.
- Update `tellewsen.github.io/CLAUDE.md`: add the new `/utils/yatzy` route
  to the existing "Routing & pages" list.

## Open risks / notes

- Emscripten (`emsdk`) is not installed on this machine yet — one-time setup
  required before the build script can run.
- If the C++ engine's query logic ever changes, the copied TS files in
  `tellewsen.github.io` (and its WASM artifacts) need a manual re-copy —
  there's no automated sync between the two repos. Acceptable for now per
  the "manual copy, decide on automation later" choice made during this
  design.
