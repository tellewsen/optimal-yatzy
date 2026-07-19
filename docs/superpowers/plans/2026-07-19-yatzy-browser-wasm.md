# Yatzy Browser (WASM) Solo Solver Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make standard-Yatzy Solo mode playable in a plain web browser by compiling the existing C++ engine to WebAssembly, and publish it as a native page on `ellewsen.no` (the `tellewsen.github.io` SvelteKit site).

**Architecture:** The C++ engine (`yatzy_engine.cpp`/`precompute_std.h`) compiles unmodified to WASM via Emscripten; a small new `wasm_bridge.cpp` exposes one query function via Embind. The DP table (~8MB) ships pre-solved, baked into the WASM module at build time via `--preload-file`, so the browser never solves anything — only looks up a fixed table. This means no threads, no `SharedArrayBuffer`, no COOP/COEP headers: plain static hosting is enough. In `optimal-yatzy/gui`, a new `engine.ts` dispatches between the existing Tauri `sidecar.ts` and the new `wasmEngine.ts` based on runtime detection, so the desktop app is unaffected. The pure logic files (`state.ts`, `match.ts`, `cliArgs.ts`, `parseResult.ts`, `wasmEngine.ts`) and the compiled WASM artifacts are then copied into `tellewsen.github.io` and wired into a new native SvelteKit route.

**Tech Stack:** C++17, Emscripten (emcc + Embind), TypeScript, Vite, SvelteKit (adapter-static).

## Global Constraints

- Solo mode only. vs-Computer / win-probability mode is explicitly out of scope: its table is `mask × (CapScore+1) × NumThresholds × 4 bytes` ≈ 32768 × 64 × 375 × 4 ≈ 3GB, not shippable statically. It stays desktop (Tauri)-only.
- Same C++ engine, compiled — not reimplemented in TypeScript. The DP solve/scoring logic must stay in exactly one place.
- No client-side solving, no threads, no `SharedArrayBuffer`/COOP-COEP headers — the DP table ships pre-solved via `--preload-file`.
- `sidecar.ts` stays the only Tauri-touching file in `optimal-yatzy/gui` — preserve this existing invariant.
- Manual copy between repos — no cross-repo build automation or shared package.
- The ~8MB DP data file is committed as-is to `tellewsen.github.io` (no git-lfs, no pre-compression).
- `tellewsen.github.io` has no test suite; don't introduce one for this feature — copied logic keeps its coverage at the source repo, the copy is verified manually.
- Route lives at `tellewsen.github.io/src/routes/utils/yatzy/+page.svelte`, matching the existing `utils/<tool>` pattern.
- Do not run `pnpm deploy` (publishes to the live site) as part of this plan — stop at a verified local build; deploying is a separate, explicitly-confirmed action.

---

### Task 1: Install Emscripten (emsdk)

**Files:** none (local toolchain setup only).

**Interfaces:** N/A — this task only makes `emcc` available on `PATH` for later tasks.

- [ ] **Step 1: Clone emsdk**

```bash
git clone https://github.com/emscripten-core/emsdk.git ~/emsdk
```

- [ ] **Step 2: Install and activate the latest SDK**

```bash
cd ~/emsdk
./emsdk install latest
./emsdk activate latest
```

- [ ] **Step 3: Source the environment and verify**

```bash
source ~/emsdk/emsdk_env.sh
emcc --version
```

Expected: prints an `emcc (Emscripten gcc/clang-like replacement)` version banner, no error. Note the exact `source` command needed — later tasks that invoke `emcc` need this sourced first in their shell.

- [ ] **Step 4: Commit nothing (no repo files changed)**

Nothing to commit — this step only sets up the local toolchain.

---

### Task 2: Extract `queryResultToJson` and add CLI test coverage

**Files:**
- Modify: `yatzy_engine.h`
- Modify: `yatzy_engine.cpp`
- Modify: `yatzy_cpu.cpp`
- Modify: `test_yatzy_cli.sh`

**Interfaces:**
- Produces: `std::string queryResultToJson(const QueryResult& r)` in `yatzy_engine.h`/`.cpp`, reused by `yatzy_cpu.cpp` and (in Task 3) `wasm_bridge.cpp`.

- [ ] **Step 1: Add a `--json` assertion for the plain (non-winprob) query path to `test_yatzy_cli.sh`**

This exercises the current inline JSON-building code in `yatzy_cpu.cpp` as a regression baseline, before it gets refactored in Step 4. Edit `test_yatzy_cli.sh`, inserting a new block right after the existing plain-text assertions and before the second `rm -f test_cli_dp_cache.bin` line:

```bash
output=$(./yatzy_cpu --used 0,1,2,3,4,5,6,7,8,9,10,11,12,13 --upper 0 --dice 6,6,6,6,6 --rerolls 0 --dp-cache test_cli_dp_cache.bin 2>/dev/null)

echo "$output" | grep -q "Yatzy" || { echo "FAIL: expected Yatzy in output"; exit 1; }
echo "$output" | grep -qE "score[[:space:]]+50" || { echo "FAIL: expected score 50"; exit 1; }

json_output=$(./yatzy_cpu --used 0,1,2,3,4,5,6,7,8,9,10,11,12,13 --upper 0 --dice 6,6,6,6,6 --rerolls 0 --dp-cache test_cli_dp_cache.bin --json 2>/dev/null)

echo "$json_output" | grep -q '"isRerollDecision":false' || { echo "FAIL: expected isRerollDecision:false in --json output"; exit 1; }
echo "$json_output" | grep -q '"categoryName":"Yatzy"' || { echo "FAIL: expected Yatzy category in --json output"; exit 1; }
echo "$json_output" | grep -q '"resultingScore":50' || { echo "FAIL: expected resultingScore 50 in --json output"; exit 1; }

rm -f test_cli_dp_cache.bin
```

(This reuses the same `test_cli_dp_cache.bin` the first call just solved, so the DP table is only solved once across both calls in this block.)

- [ ] **Step 2: Run the test to confirm it passes against the current (pre-refactor) code**

```bash
make test_yatzy
```

Expected: all three sub-tests print their trailing `passed` line (or run cleanly with no `FAIL`), including the new json assertions — this is the baseline proving the extraction in the next steps doesn't change behavior. Cold DP solve may take up to a couple of minutes.

- [ ] **Step 3: Add the `queryResultToJson` declaration to `yatzy_engine.h`**

Add this line near the `QueryResult` struct definition (after its closing `};`, before `struct WinRerollOption`):

```cpp
std::string queryResultToJson(const QueryResult& r);
```

- [ ] **Step 4: Implement `queryResultToJson` in `yatzy_engine.cpp`**

Add `#include <sstream>` to the top of `yatzy_engine.cpp` alongside the existing includes, then add this function (matches the existing inline `printf` format in `yatzy_cpu.cpp` byte-for-byte, including `%f`'s six-decimal formatting):

```cpp
std::string queryResultToJson(const QueryResult& r) {
    char buf[64];
    std::ostringstream out;
    out << "{\"isRerollDecision\":" << (r.isRerollDecision ? "true" : "false") << ",";
    if (r.isRerollDecision) {
        out << "\"rerollOptions\":[";
        for (size_t i = 0; i < r.rerollOptions.size(); i++) {
            const auto& opt = r.rerollOptions[i];
            out << (i ? "," : "") << "{\"holdValues\":[";
            for (size_t j = 0; j < opt.heldValues.size(); j++)
                out << (j ? "," : "") << opt.heldValues[j];
            std::snprintf(buf, sizeof(buf), "%f", opt.expectedValue);
            out << "],\"expectedValue\":" << buf << "}";
        }
        out << "]";
    } else {
        out << "\"categoryOptions\":[";
        for (size_t i = 0; i < r.categoryOptions.size(); i++) {
            const auto& opt = r.categoryOptions[i];
            std::snprintf(buf, sizeof(buf), "%f", opt.expectedValue);
            out << (i ? "," : "") << "{\"category\":" << opt.category
                << ",\"categoryName\":\"" << CategoryNames[opt.category]
                << "\",\"resultingScore\":" << opt.resultingScore
                << ",\"expectedValue\":" << buf << "}";
        }
        out << "]";
    }
    out << "}";
    return out.str();
}
```

- [ ] **Step 5: Replace the inline JSON block in `yatzy_cpu.cpp` with a call to `queryResultToJson`**

In `yatzy_cpu.cpp`, find the block (currently lines ~176-197) that starts with:

```cpp
    if (jsonOutput) {
        printf("{\"isRerollDecision\":%s,", r.isRerollDecision ? "true" : "false");
```

and ends with the matching:

```cpp
        printf("}\n");
    } else {
```

(this is the **second** `if (jsonOutput) { ... } else {` block in the file — the one after `QueryResult r = query(...)`, not the earlier one handling `WinQueryResult`, which stays untouched since win-prob JSON output is out of scope for this refactor). Replace that entire `if (jsonOutput) { ... }` body with:

```cpp
    if (jsonOutput) {
        printf("%s\n", queryResultToJson(r).c_str());
    } else {
```

- [ ] **Step 6: Rebuild and rerun the test to confirm no regression**

```bash
make clean
make test_yatzy
```

Expected: same as Step 2 — all assertions pass, including the new `--json` block, now exercised against the refactored code.

- [ ] **Step 7: Commit**

```bash
git add yatzy_engine.h yatzy_engine.cpp yatzy_cpu.cpp test_yatzy_cli.sh
git commit -m "refactor(engine): extract queryResultToJson, shared by CLI and future WASM bridge

Adds CLI smoke-test coverage for the plain --json query path (previously
only the win-probability --json path was asserted on)."
```

---

### Task 3: WASM bridge, build script, and first build

**Files:**
- Create: `wasm_bridge.cpp`
- Create: `gui/scripts/build-wasm.sh`

**Interfaces:**
- Consumes: `buildFlatTables()`, `loadOrSolveDP()`, `query()`, `queryResultToJson()` from `yatzy_engine.h` (Task 2).
- Produces: a JS-callable `queryJson(usedMask: number, upperTotal: number, diceCsv: string, rerollsLeft: number): string` function on the compiled module, plus the compiled artifacts `gui/src/wasm/yatzy_engine.js` / `.wasm` / `.data`, consumed by `wasmEngine.ts` in Task 4.

- [ ] **Step 1: Write `wasm_bridge.cpp`**

```cpp
// wasm_bridge.cpp — Emscripten entry point exposing the standard-Yatzy
// engine's Solo-mode query to JS. The DP table ships pre-solved (baked in
// at build time via --preload-file), so this never solves — only looks up
// the table built once at first call.
#include "yatzy_engine.h"
#include <emscripten/bind.h>
#include <array>
#include <sstream>
#include <string>

namespace {

FlatTables* g_tables = nullptr;
std::vector<float>* g_dp = nullptr;

void ensureLoaded() {
    if (g_tables) return;
    g_tables = new FlatTables(buildFlatTables());
    g_dp = new std::vector<float>(loadOrSolveDP(*g_tables, "yatzy_cpu_dp.bin"));
}

std::array<int, 5> parseDice(const std::string& csv) {
    std::array<int, 5> dice{};
    std::stringstream ss(csv);
    std::string item;
    int i = 0;
    while (std::getline(ss, item, ',') && i < 5) dice[i++] = std::atoi(item.c_str());
    return dice;
}

} // namespace

std::string queryJson(int usedMask, int upperTotal, std::string diceCsv, int rerollsLeft) {
    ensureLoaded();
    std::array<int, 5> dice = parseDice(diceCsv);
    QueryResult r = query(*g_tables, *g_dp, usedMask, upperTotal, dice, rerollsLeft);
    return queryResultToJson(r);
}

EMSCRIPTEN_BINDINGS(yatzy_module) {
    emscripten::function("queryJson", &queryJson);
}
```

- [ ] **Step 2: Solve and cache the canonical DP table at the repo root, if not already present**

```bash
cd /home/ae/projects/privat/claude/optimal-yatzy
ls yatzy_cpu_dp.bin 2>/dev/null || ./yatzy_cpu --used 0 --upper 0 --dice 1,1,1,1,1 --rerolls 2
ls -la yatzy_cpu_dp.bin
```

Expected: `yatzy_cpu_dp.bin` exists at the repo root, ~8MB (cold solve may take up to a couple of minutes if it didn't already exist).

- [ ] **Step 3: Write `gui/scripts/build-wasm.sh`**

```bash
#!/usr/bin/env bash
# gui/scripts/build-wasm.sh — compiles the standard-Yatzy engine's query
# path to WebAssembly for the browser (Solo mode only). Requires a
# pre-solved yatzy_cpu_dp.bin at the repo root (run ./yatzy_cpu once first)
# and emcc (Emscripten) on PATH. Bakes the DP table into the module via
# --preload-file so no client-side solve or threading is ever needed.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
GUI_DIR="$(dirname "$SCRIPT_DIR")"
REPO_ROOT="$(dirname "$GUI_DIR")"
DP_CACHE="$REPO_ROOT/yatzy_cpu_dp.bin"
OUT_DIR="$GUI_DIR/src/wasm"

if [ ! -f "$DP_CACHE" ]; then
  echo "error: $DP_CACHE not found — run ./yatzy_cpu once first to solve+cache it" >&2
  exit 1
fi

mkdir -p "$OUT_DIR"

emcc -O3 -std=c++17 --bind \
  "$REPO_ROOT/yatzy_engine.cpp" "$REPO_ROOT/wasm_bridge.cpp" \
  --preload-file "$DP_CACHE@/yatzy_cpu_dp.bin" \
  -s MODULARIZE=1 \
  -s EXPORT_ES6=1 \
  -s ENVIRONMENT=web \
  -s ALLOW_MEMORY_GROWTH=1 \
  -s EXPORT_NAME=createYatzyModule \
  -o "$OUT_DIR/yatzy_engine.js"

echo "built $OUT_DIR/yatzy_engine.js (+ .wasm, .data)"
```

```bash
chmod +x gui/scripts/build-wasm.sh
```

- [ ] **Step 4: Run the build and sanity-check the output**

```bash
source ~/emsdk/emsdk_env.sh   # if not already sourced in this shell
cd gui
./scripts/build-wasm.sh
ls -la src/wasm/
```

Expected: `yatzy_engine.js`, `yatzy_engine.wasm`, and `yatzy_engine.data` all exist in `gui/src/wasm/`; `.data` is close to the ~8MB size of `yatzy_cpu_dp.bin` (it wraps that file plus a small metadata header); `.wasm` is at least tens of KB (a zero-byte or missing file means the build failed silently — check the `emcc` output above for errors).

- [ ] **Step 5: Add `gui/src/wasm/` to `.gitignore` (build artifact, not source)**

Add this line to `.gitignore` (create a `gui/.gitignore` if one doesn't already exist, otherwise append to the existing one — check first with `cat gui/.gitignore` or the root `.gitignore`):

```
gui/src/wasm/
```

- [ ] **Step 6: Commit**

```bash
git add wasm_bridge.cpp gui/scripts/build-wasm.sh .gitignore
git commit -m "build: add Emscripten bridge and build script for browser WASM engine

Compiles the existing query()/queryResultToJson() path (Solo mode only)
to WASM via Embind, with the DP table baked in via --preload-file so no
client-side solve or threading is ever needed."
```

---

### Task 4: `wasmEngine.ts` and the `engine.ts` dispatcher

**Files:**
- Create: `gui/src/wasmEngine.ts`
- Create: `gui/src/engine.ts`
- Modify: `gui/src/sidecar.ts`

**Interfaces:**
- Consumes: compiled module at `gui/src/wasm/yatzy_engine.js` (Task 3, default export `createYatzyModule(): Promise<{queryJson(usedMask, upperTotal, diceCsv, rerollsLeft): string}>`); `GameState` from `state.ts`; `parseQueryResult`/`QueryResult` from `parseResult.ts`.
- Produces: `getRecommendation(state, dice, onProgress?): Promise<QueryResult>` and `isEngineWarm(): Promise<boolean>` from both `wasmEngine.ts` and (unchanged) `sidecar.ts`; `hasTauriRuntime(): boolean` exported from `sidecar.ts`; the same two-function interface re-exported (dispatched) from `engine.ts`, consumed by `main.ts` in Task 5.

- [ ] **Step 1: Export `hasTauriRuntime` from `sidecar.ts`**

In `gui/src/sidecar.ts`, change:

```ts
function hasTauriRuntime(): boolean {
```

to:

```ts
export function hasTauriRuntime(): boolean {
```

(No other change to `sidecar.ts` — it remains the only Tauri-touching file.)

- [ ] **Step 2: Write `gui/src/wasmEngine.ts`**

```ts
// wasmEngine.ts — the only file that touches the compiled WASM module.
// Solo mode only: the DP table ships pre-solved (baked in at build time via
// --preload-file), so there's never a client-side solve — every query is an
// instant table lookup. isEngineWarm() is therefore always true, and there
// is no solve progress to report (onProgress is accepted for interface
// parity with sidecar.ts but never invoked).
import { GameState } from "./state";
import { parseQueryResult, QueryResult } from "./parseResult";

interface YatzyModule {
  queryJson(usedMask: number, upperTotal: number, diceCsv: string, rerollsLeft: number): string;
}

let modulePromise: Promise<YatzyModule> | null = null;

function loadModule(): Promise<YatzyModule> {
  if (!modulePromise) {
    modulePromise = import("./wasm/yatzy_engine.js").then((mod) => mod.default());
  }
  return modulePromise;
}

export async function isEngineWarm(): Promise<boolean> {
  return true;
}

export async function getRecommendation(
  state: GameState,
  dice: number[],
  _onProgress?: (level: number, total: number) => void
): Promise<QueryResult> {
  const mod = await loadModule();
  const json = mod.queryJson(state.usedMask, state.upperTotal, dice.join(","), state.rerollsLeft);
  return parseQueryResult(json);
}
```

- [ ] **Step 3: Write `gui/src/engine.ts`**

```ts
// engine.ts — dispatches queries to the real Tauri sidecar when running
// inside the desktop app, or the WASM engine when running as a plain
// browser page. This is the single seam main.ts calls through.
import { GameState } from "./state";
import { QueryResult } from "./parseResult";
import { hasTauriRuntime, getRecommendation as sidecarGetRecommendation, isEngineWarm as sidecarIsEngineWarm } from "./sidecar";
import { getRecommendation as wasmGetRecommendation, isEngineWarm as wasmIsEngineWarm } from "./wasmEngine";

export { hasTauriRuntime };

export function isEngineWarm(): Promise<boolean> {
  return hasTauriRuntime() ? sidecarIsEngineWarm() : wasmIsEngineWarm();
}

export function getRecommendation(
  state: GameState,
  dice: number[],
  onProgress?: (level: number, total: number) => void
): Promise<QueryResult> {
  return hasTauriRuntime()
    ? sidecarGetRecommendation(state, dice, onProgress)
    : wasmGetRecommendation(state, dice, onProgress);
}
```

- [ ] **Step 4: Typecheck**

```bash
cd gui
npx tsc --noEmit
```

Expected: no errors.

- [ ] **Step 5: Commit**

```bash
git add gui/src/sidecar.ts gui/src/wasmEngine.ts gui/src/engine.ts
git commit -m "feat(gui): add wasmEngine.ts and engine.ts dispatch seam

engine.ts picks the Tauri sidecar or the WASM engine at runtime based on
hasTauriRuntime() (now exported from sidecar.ts); sidecar.ts itself is
otherwise unchanged and remains the only Tauri-touching file."
```

---

### Task 5: Wire `main.ts` to `engine.ts`, gate vs-Computer mode, verify in a real browser

**Files:**
- Modify: `gui/src/main.ts`

**Interfaces:**
- Consumes: `getRecommendation`, `isEngineWarm`, `hasTauriRuntime` from `engine.ts` (Task 4).

- [ ] **Step 1: Change the import in `main.ts`**

Change:

```ts
import { getRecommendation, isEngineWarm } from "./sidecar";
```

to:

```ts
import { getRecommendation, isEngineWarm, hasTauriRuntime } from "./engine";
```

- [ ] **Step 2: Disable the vs-Computer button in a browser context**

Right after the existing line:

```ts
document.getElementById("mode-vs-computer-button")!.addEventListener("click", () => startNewGame("vsComputer"));
```

add:

```ts
if (!hasTauriRuntime()) {
  const vsComputerButton = document.getElementById("mode-vs-computer-button") as HTMLButtonElement;
  vsComputerButton.disabled = true;
  vsComputerButton.title = "vs Computer mode requires the desktop app (the win-probability table is too large to ship to a browser)";
}
```

- [ ] **Step 3: Typecheck**

```bash
cd gui
npx tsc --noEmit
```

Expected: no errors.

- [ ] **Step 4: Run the unit test suite (unaffected pure-logic tests)**

```bash
npm test
```

Expected: all existing tests in `state.test.ts`, `match.test.ts`, `cliArgs.test.ts`, `parseResult.test.ts` still pass — none of this task's changes touch pure logic.

- [ ] **Step 5: Manually verify in a real browser (not Tauri)**

```bash
npm run dev
```

Open the printed local URL (e.g. `http://localhost:5173`) in a normal browser tab. Verify:
- The "vs Computer" button is visibly disabled with the tooltip text from Step 2 on hover.
- Solo mode: click dice to set values 1-6 (or use whatever dice-input control is on screen), fill all five, and confirm a recommendation list appears within roughly a second — no "couple of minutes" wait, since the DP table is pre-baked (proves the WASM path is being used, not an attempted native solve).
- Open the browser devtools console: no errors, in particular no 404s for `yatzy_engine.wasm` or `yatzy_engine.data` (if you do see 404s, check that `npm run dev`'s Vite dev server is serving `gui/src/wasm/` — Vite serves files relative to the importing module by default, so this should work without extra config, but confirm before moving on).
- Click a recommended hold/category option and confirm the scorecard/dice update as expected.
- Click "New Game" and confirm state resets cleanly.

If anything above fails, stop and diagnose before proceeding — this is the only verification for `main.ts`/`wasmEngine.ts`/`engine.ts` per this repo's testing philosophy (DOM/WASM-dependent code is verified by running the real app, not unit tests).

- [ ] **Step 6: Commit**

```bash
git add gui/src/main.ts
git commit -m "feat(gui): route engine calls through engine.ts, gate vs-Computer in browser

vs-Computer mode is disabled with an explanatory tooltip when running
outside Tauri, since its win-probability table can't ship to a browser."
```

---

### Task 6: Update `optimal-yatzy/CLAUDE.md`

**Files:**
- Modify: `CLAUDE.md`

**Interfaces:** none — documentation only.

- [ ] **Step 1: Add the new build command to the `gui/` build commands section**

In the ` ```gui/` (TypeScript/Tauri) ` code block, after the existing `./scripts/build-sidecar.sh` line, add:

```
./scripts/build-wasm.sh        # compiles the engine's query path to WASM for the browser build
                                # (Solo mode only); requires emcc (Emscripten) on PATH and a
                                # pre-solved yatzy_cpu_dp.bin at the repo root
```

- [ ] **Step 2: Document the new files in the "GUI layering" section**

After the existing `sidecar.ts` bullet, add:

```
- **`engine.ts`** — dispatches `getRecommendation`/`isEngineWarm` to `sidecar.ts` (inside Tauri) or `wasmEngine.ts` (plain browser), based on `hasTauriRuntime()` (exported from `sidecar.ts`). This is the seam `main.ts` calls through instead of `sidecar.ts` directly.
- **`wasmEngine.ts`** — the browser counterpart to `sidecar.ts`: calls the compiled WASM module (`src/wasm/yatzy_engine.js`, built by `scripts/build-wasm.sh`, gitignored). The DP table ships pre-solved (baked in via `--preload-file`), so there is no client-side solve, no threading, and `isEngineWarm()` always resolves `true`. Solo mode only — vs-Computer mode is disabled in `main.ts` when `hasTauriRuntime()` is false, since the win-probability table (~3GB) can't ship to a browser.
```

- [ ] **Step 3: Commit**

```bash
git add CLAUDE.md
git commit -m "docs: document engine.ts/wasmEngine.ts and the browser WASM build"
```

---

### Task 7: Copy pure logic and WASM artifacts into `tellewsen.github.io`

**Files:**
- Create: `tellewsen.github.io/src/lib/yatzy/state.ts`
- Create: `tellewsen.github.io/src/lib/yatzy/match.ts`
- Create: `tellewsen.github.io/src/lib/yatzy/cliArgs.ts`
- Create: `tellewsen.github.io/src/lib/yatzy/parseResult.ts`
- Create: `tellewsen.github.io/src/lib/yatzy/wasmEngine.ts`
- Create: `tellewsen.github.io/src/lib/yatzy/wasm/yatzy_engine.js` (+ `.wasm`, `.data`)

**Interfaces:**
- Produces: the same exports as their `optimal-yatzy/gui/src/` sources (`initialGameState`, `GameState`, etc. from `state.ts`; `MatchState`, `initialMatchState`, `activeGameState`, `withActiveGameState`, `afterScore` from `match.ts`; `QueryResult`, `parseQueryResult` from `parseResult.ts`; `getRecommendation`, `isEngineWarm` from `wasmEngine.ts`) — consumed by the new Svelte route in Task 8. `cliArgs.ts` is copied for parity/future use but not imported by the new page (the WASM bridge takes structured params directly, not CLI args).

- [ ] **Step 1: Copy the pure logic files**

```bash
mkdir -p /home/ae/projects/privat/claude/tellewsen.github.io/src/lib/yatzy
cp /home/ae/projects/privat/claude/optimal-yatzy/gui/src/state.ts \
   /home/ae/projects/privat/claude/optimal-yatzy/gui/src/match.ts \
   /home/ae/projects/privat/claude/optimal-yatzy/gui/src/cliArgs.ts \
   /home/ae/projects/privat/claude/optimal-yatzy/gui/src/parseResult.ts \
   /home/ae/projects/privat/claude/optimal-yatzy/gui/src/wasmEngine.ts \
   /home/ae/projects/privat/claude/tellewsen.github.io/src/lib/yatzy/
```

- [ ] **Step 2: Copy the compiled WASM artifacts**

```bash
mkdir -p /home/ae/projects/privat/claude/tellewsen.github.io/src/lib/yatzy/wasm
cp /home/ae/projects/privat/claude/optimal-yatzy/gui/src/wasm/yatzy_engine.js \
   /home/ae/projects/privat/claude/optimal-yatzy/gui/src/wasm/yatzy_engine.wasm \
   /home/ae/projects/privat/claude/optimal-yatzy/gui/src/wasm/yatzy_engine.data \
   /home/ae/projects/privat/claude/tellewsen.github.io/src/lib/yatzy/wasm/
```

- [ ] **Step 3: Typecheck the website repo**

```bash
cd /home/ae/projects/privat/claude/tellewsen.github.io
pnpm check
```

Expected: no new errors from the copied files (they have no Tauri/DOM dependency and should typecheck standalone).

- [ ] **Step 4: Commit (in the `tellewsen.github.io` repo)**

```bash
cd /home/ae/projects/privat/claude/tellewsen.github.io
git add src/lib/yatzy/
git commit -m "feat: vendor Yatzy solver logic and WASM engine from optimal-yatzy

Copied (not shared via package) from optimal-yatzy/gui/src/: state.ts,
match.ts, cliArgs.ts, parseResult.ts, wasmEngine.ts, and the compiled
WASM module. Manual copy, no cross-repo automation — re-copy needed if
the source engine or logic changes."
```

---

### Task 8: New SvelteKit route `/utils/yatzy`

**Files:**
- Create: `tellewsen.github.io/src/routes/utils/yatzy/+page.svelte`
- Modify: `tellewsen.github.io/src/routes/utils/+page.svelte`

**Interfaces:**
- Consumes: everything from `$lib/yatzy/*` produced in Task 7.

- [ ] **Step 1: Write `src/routes/utils/yatzy/+page.svelte`**

```svelte
<script lang="ts">
	import {
		initialMatchState,
		activeGameState,
		withActiveGameState,
		afterScore,
		type MatchState
	} from '$lib/yatzy/match';
	import {
		setDice,
		advanceReroll,
		applyHold,
		rollRemaining,
		scoreCategory,
		allDiceValid,
		isGameComplete,
		totalScore,
		bonusEarned,
		CATEGORY_NAMES,
		NUM_CATEGORIES
	} from '$lib/yatzy/state';
	import { getRecommendation } from '$lib/yatzy/wasmEngine';
	import type { QueryResult } from '$lib/yatzy/parseResult';

	const PIP_LAYOUTS: Record<number, string[]> = {
		1: ['mc'],
		2: ['tl', 'br'],
		3: ['tl', 'mc', 'br'],
		4: ['tl', 'tr', 'bl', 'br'],
		5: ['tl', 'tr', 'mc', 'bl', 'br'],
		6: ['tl', 'ml', 'bl', 'tr', 'mr', 'br']
	};

	let match: MatchState = initialMatchState('solo');
	let lastResult: QueryResult | null = null;
	let errorMessage: string | null = null;
	let computing = false;
	let generation = 0;

	$: active = activeGameState(match);
	$: complete = isGameComplete(active);

	function setMatch(next: MatchState) {
		match = next;
		generation += 1;
	}

	let queryInFlight = false;
	let queryQueued = false;

	async function maybeQuery(): Promise<void> {
		if (queryInFlight) {
			queryQueued = true;
			return;
		}
		queryInFlight = true;
		try {
			do {
				queryQueued = false;
				const myGeneration = generation;
				const state = activeGameState(match);
				if (!allDiceValid(state.dice)) {
					lastResult = null;
					break;
				}
				computing = true;
				try {
					const result = await getRecommendation(state, state.dice as number[]);
					if (generation !== myGeneration) {
						queryQueued = true;
						continue;
					}
					lastResult = result;
					errorMessage = null;
				} catch (err) {
					if (generation !== myGeneration) {
						queryQueued = true;
						continue;
					}
					lastResult = null;
					errorMessage = err instanceof Error ? err.message : String(err);
				}
			} while (queryQueued);
		} finally {
			queryInFlight = false;
			computing = false;
		}
	}

	function handleDieClick(index: number) {
		const state = activeGameState(match);
		const dice = [...state.dice];
		const current = dice[index];
		dice[index] = current === null ? 1 : (current % 6) + 1;
		setMatch(withActiveGameState(match, setDice(state, dice)));
		void maybeQuery();
	}

	function handleDieClear(index: number) {
		const state = activeGameState(match);
		const dice = [...state.dice];
		dice[index] = null;
		setMatch(withActiveGameState(match, setDice(state, dice)));
		lastResult = null;
	}

	function handleRollRemaining() {
		const state = activeGameState(match);
		setMatch(withActiveGameState(match, setDice(state, rollRemaining(state.dice))));
		void maybeQuery();
	}

	function handleReroll() {
		const state = activeGameState(match);
		setMatch(withActiveGameState(match, advanceReroll(state)));
		lastResult = null;
	}

	function handleHold(holdValues: number[]) {
		const state = activeGameState(match);
		setMatch(withActiveGameState(match, applyHold(state, holdValues)));
		lastResult = null;
		void maybeQuery();
	}

	function handleScoreCategory(category: number, resultingScore: number) {
		const state = activeGameState(match);
		const scored = scoreCategory(state, category, resultingScore);
		setMatch(afterScore(withActiveGameState(match, scored)));
		lastResult = null;
	}

	function newGame() {
		setMatch(initialMatchState('solo'));
		lastResult = null;
		errorMessage = null;
	}
</script>

<svelte:head>
	<title>Yatzy solver — ellewsen.no</title>
</svelte:head>

<section class="yatzy">
	<div class="section-label">// utils / yatzy</div>

	<p class="intro">
		Optimal-play Yatzy assistant. Roll physical dice and enter the values below (click a die to
		cycle 1–6, × to clear), or use "Roll remaining" to simulate a roll. Runs the same solver as my
		<a href="https://github.com/tellewsen/optimal-yatzy">desktop app</a>, compiled to WebAssembly —
		everything happens in your browser, nothing is sent anywhere.
	</p>

	{#if errorMessage}
		<div class="error-banner">{errorMessage}</div>
	{/if}

	<div class="card dice-card">
		<div class="dice-row">
			{#each active.dice as value, i (i)}
				<div class="die-wrapper">
					<button
						type="button"
						class="die"
						class:die-empty={value === null}
						on:click={() => handleDieClick(i)}
					>
						{#if value !== null}
							{#each PIP_LAYOUTS[value] as pos}
								<span class="pip pip-{pos}"></span>
							{/each}
						{/if}
					</button>
					{#if value !== null}
						<button
							type="button"
							class="die-clear"
							aria-label="Clear die"
							on:click={() => handleDieClear(i)}>×</button
						>
					{/if}
				</div>
			{/each}
		</div>

		<div class="dice-actions">
			<div class="reroll-dots" aria-label={`Rerolls left: ${active.rerollsLeft}`}>
				{#each [0, 1] as i}
					<span class="reroll-dot" class:reroll-dot-filled={i < active.rerollsLeft}></span>
				{/each}
			</div>
			<button type="button" class="btn btn-secondary" on:click={handleRollRemaining} disabled={complete}
				>Roll remaining</button
			>
			<button
				type="button"
				class="btn btn-secondary"
				on:click={handleReroll}
				disabled={complete || !lastResult || !lastResult.isRerollDecision}>Reroll</button
			>
			<button type="button" class="btn btn-secondary" on:click={newGame}>New game</button>
		</div>
	</div>

	<div class="card recommendation-card">
		{#if complete}
			<p class="final-total">🎉 Game complete! Final score: {totalScore(active)}</p>
		{:else if computing}
			<div class="computing-row"><span class="computing-die"></span>Computing recommendation…</div>
		{:else if lastResult === null}
			<p class="hint">Enter all five dice to see recommendations.</p>
		{:else if lastResult.isRerollDecision}
			{#each lastResult.rerollOptions as opt, index (opt.holdValues.join(','))}
				<button
					type="button"
					class="option-card"
					class:best-option={index === 0}
					on:click={() => handleHold(opt.holdValues)}
				>
					{#if index === 0}<span class="best-badge">★ Best move</span>{/if}
					<span class="option-text"
						>Hold [{opt.holdValues.join(',')}]{opt.holdValues.length === 5
							? ' (stop rerolling)'
							: ''} — expected value {opt.expectedValue.toFixed(2)}</span
					>
				</button>
			{/each}
		{:else}
			{#each lastResult.categoryOptions as opt, index (opt.category)}
				<button
					type="button"
					class="option-card"
					class:best-option={index === 0}
					on:click={() => handleScoreCategory(opt.category, opt.resultingScore)}
				>
					{#if index === 0}<span class="best-badge">★ Best move</span>{/if}
					<span class="option-text"
						>{opt.categoryName} — score {opt.resultingScore} (expected value {opt.expectedValue.toFixed(
							2
						)})</span
					>
				</button>
			{/each}
		{/if}
	</div>

	<div class="card scorecard-card">
		<div class="section-label">// scorecard</div>
		{#each Array(NUM_CATEGORIES) as _, cat (cat)}
			<div class="score-row">
				<span>{CATEGORY_NAMES[cat]}</span>
				<span>{active.categoryScores[cat] === null ? '—' : active.categoryScores[cat]}</span>
			</div>
		{/each}
		<div class="score-row total-row">
			<strong>Upper total</strong>
			<span
				>{active.upperTotal} ({bonusEarned(active)
					? 'bonus earned (+50)'
					: `${active.upperTotal}/63 for bonus`})</span
			>
		</div>
		<div class="score-row total-row">
			<strong>Grand total</strong>
			<span>{totalScore(active)}</span>
		</div>
	</div>
</section>

<style>
	.yatzy {
		padding: 56px 0;
	}

	.intro {
		color: var(--muted);
		font-size: 13px;
		max-width: 640px;
	}

	.error-banner {
		background: rgba(247, 129, 102, 0.12);
		border: 1px solid var(--accent3);
		color: var(--accent3);
		padding: 10px 14px;
		border-radius: 6px;
		margin-bottom: 16px;
		font-size: 13px;
	}

	.card {
		margin-bottom: 16px;
	}

	.dice-row {
		display: flex;
		gap: 10px;
		margin-bottom: 14px;
	}

	.die-wrapper {
		position: relative;
	}

	.die {
		width: 48px;
		height: 48px;
		border-radius: 8px;
		background: var(--bg2);
		border: 2px solid var(--border);
		display: grid;
		grid-template-columns: repeat(3, 1fr);
		grid-template-rows: repeat(3, 1fr);
		padding: 6px;
		cursor: pointer;
	}

	.die.die-empty {
		border-style: dashed;
		background: transparent;
	}

	.die:hover {
		border-color: var(--accent);
	}

	.pip {
		width: 8px;
		height: 8px;
		border-radius: 50%;
		background: var(--bright);
		align-self: center;
		justify-self: center;
	}

	.pip-tl { grid-row: 1; grid-column: 1; }
	.pip-tr { grid-row: 1; grid-column: 3; }
	.pip-ml { grid-row: 2; grid-column: 1; }
	.pip-mc { grid-row: 2; grid-column: 2; }
	.pip-mr { grid-row: 2; grid-column: 3; }
	.pip-bl { grid-row: 3; grid-column: 1; }
	.pip-br { grid-row: 3; grid-column: 3; }

	.die-clear {
		position: absolute;
		top: -6px;
		right: -6px;
		width: 18px;
		height: 18px;
		border-radius: 50%;
		border: 1px solid var(--border);
		background: var(--bg2);
		color: var(--muted);
		font-size: 11px;
		line-height: 1;
		padding: 0;
		display: flex;
		align-items: center;
		justify-content: center;
	}

	.die-clear:hover {
		border-color: var(--accent3);
		color: var(--accent3);
	}

	.dice-actions {
		display: flex;
		align-items: center;
		gap: 10px;
		flex-wrap: wrap;
	}

	.reroll-dots {
		display: inline-flex;
		gap: 4px;
		margin-right: 4px;
	}

	.reroll-dot {
		width: 9px;
		height: 9px;
		border-radius: 50%;
		background: var(--dim);
	}

	.reroll-dot-filled {
		background: var(--accent);
	}

	.option-card {
		display: flex;
		align-items: center;
		gap: 8px;
		width: 100%;
		text-align: left;
		padding: 8px 12px;
		margin: 4px 0;
		border: 1px solid var(--border);
		border-radius: 6px;
		background: var(--bg2);
		font-size: 13px;
	}

	.option-card:hover {
		border-color: var(--accent2);
	}

	.option-card.best-option {
		border-color: var(--accent);
		background: rgba(57, 211, 83, 0.06);
	}

	.best-badge {
		flex-shrink: 0;
		font-size: 11px;
		font-weight: 700;
		color: var(--accent);
		white-space: nowrap;
	}

	.hint {
		color: var(--muted);
		font-size: 13px;
	}

	.computing-row {
		display: flex;
		align-items: center;
		gap: 8px;
		font-size: 13px;
		color: var(--muted);
	}

	.computing-die {
		width: 14px;
		height: 14px;
		border-radius: 4px;
		background: var(--accent);
		animation: computing-pulse 1s ease-in-out infinite;
	}

	@keyframes computing-pulse {
		0%, 100% { opacity: 0.4; transform: scale(0.9); }
		50% { opacity: 1; transform: scale(1.1); }
	}

	.final-total {
		font-size: 14px;
		color: var(--bright);
	}

	.score-row {
		display: flex;
		justify-content: space-between;
		padding: 3px 0;
		font-size: 13px;
	}

	.score-row.total-row {
		border-top: 1px solid var(--border);
		margin-top: 6px;
		padding-top: 6px;
	}
</style>
```

- [ ] **Step 2: Add the link card to `src/routes/utils/+page.svelte`**

In the `.utils-grid` block, add a new card (matching the existing alphabetically-unsorted order is fine — append at the end):

```svelte
<a href="/utils/yatzy" class="card util-card">
	<div class="util-name">yatzy</div>
	<div class="util-desc">Optimal-play Yatzy solver (WASM)</div>
</a>
```

- [ ] **Step 3: Typecheck**

```bash
cd /home/ae/projects/privat/claude/tellewsen.github.io
pnpm check
```

Expected: no errors.

- [ ] **Step 4: Build and preview locally**

```bash
pnpm build
pnpm preview
```

Open the printed local preview URL, navigate to `/utils` then click through to `/utils/yatzy` (or go directly to `/utils/yatzy`). Verify:
- Page renders with the site's nav, footer, and dark theme intact.
- Filling all five dice (click each die 1-6 times to cycle to a value, or use "Roll remaining") produces a recommendation within about a second.
- No console errors, in particular no 404s for the WASM/data files under `_app`/`internal` (SvelteKit's `adapter-static` renames `_app` to `internal` per this repo's existing config — if you see 404s, check that the built output under `build/` actually contains the `.wasm`/`.data` files next to the JS chunk that imports them).
- Clicking a recommended option updates the scorecard; "New game" resets cleanly.
- The vs-Computer-equivalent isn't offered anywhere on this page (Solo mode only, by design — nothing to verify beyond "it's simply not there").

- [ ] **Step 5: Commit**

```bash
git add src/routes/utils/yatzy/+page.svelte src/routes/utils/+page.svelte
git commit -m "feat: add /utils/yatzy — optimal-play Yatzy solver (WASM, solo mode)"
```

---

### Task 9: Update `tellewsen.github.io/CLAUDE.md`

**Files:**
- Modify: `tellewsen.github.io/CLAUDE.md`

**Interfaces:** none — documentation only.

- [ ] **Step 1: Add the new route to the "Routing & pages" list**

In the `### Routing & pages` section, after the existing `utils/` bullet, add:

```
- `src/routes/utils/yatzy/` — optimal-play Yatzy solver. Vendored (copied, not shared via package) from `optimal-yatzy/gui/src/` — pure game-state/match logic plus a WASM build of the C++ solver engine, baked with a pre-solved DP table. Solo mode only. Re-copy `src/lib/yatzy/` manually if the source engine or logic changes; there's no automated sync.
```

- [ ] **Step 2: Commit**

```bash
git add CLAUDE.md
git commit -m "docs: document the /utils/yatzy route and its vendoring from optimal-yatzy"
```

**Note:** this plan stops here. Publishing live (`pnpm deploy`, which pushes to the `gh-pages` branch) is a separate, explicit action — confirm with the user before running it.

---

## Self-Review Notes

- **Spec coverage:** Part 1 (WASM build) → Tasks 1-3. Part 2 (engine seam + vs-Computer gating) → Tasks 4-5. Part 3 (SvelteKit integration) → Tasks 7-8. Docs → Tasks 6, 9. Testing philosophy (thin glue stays manually verified, pure logic keeps its existing coverage) → reflected in each task's verification step. "Commit dp.bin as-is, no LFS" → Task 7 Step 2 (plain `cp`/`git add`, no special handling).
- **No placeholders:** every step has literal file contents, exact commands, or exact expected output — verified by re-reading each task.
- **Type consistency:** `getRecommendation(state, dice, onProgress?)` and `isEngineWarm()` signatures match across `sidecar.ts` (existing), `wasmEngine.ts` (Task 4), and `engine.ts` (Task 4); `queryJson(usedMask, upperTotal, diceCsv, rerollsLeft)` matches between `wasm_bridge.cpp` (Task 3) and `wasmEngine.ts`'s `YatzyModule` interface (Task 4).
