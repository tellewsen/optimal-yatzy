# Yatzy Desktop GUI Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a Windows-native Tauri desktop app that lets you enter dice rolls during a real game of standard Yatzy and get back the optimal move, by calling the existing `yatzy_cpu` CLI engine as a bundled sidecar binary.

**Architecture:** A Tauri app (vanilla TypeScript/HTML/CSS frontend, Vite dev server, no JS framework) living in `gui/` in this repo. The frontend owns all game state and calls `Command.sidecar(...)` (from `@tauri-apps/plugin-shell`) directly — no custom Rust command. `yatzy_cpu` is cross-compiled to a Windows binary via MinGW-w64 from WSL and bundled as the sidecar. Because Windows won't execute a binary sitting on the WSL UNC path, an `rsync` script copies `gui/` to a native Windows build-staging folder where `npm run tauri dev`/`build` actually run.

**Tech Stack:** TypeScript (vanilla, no framework), Vite, Vitest, Tauri v2, Rust (scaffold-only, no custom commands), `@tauri-apps/plugin-shell` + `tauri-plugin-shell = "2"`, MinGW-w64 (`g++-mingw-w64-x86-64-posix`) for cross-compiling the C++ engine.

## Global Constraints

- Do not modify `precompute.h`, `kernel.cu`, `main.cu`, `precompute_std.h`, `yatzy_engine.h`/`.cpp`, `yatzy_cpu.cpp`, or the existing `Makefile` — this phase only adds a new `gui/` directory and cross-compiles the existing, unmodified `yatzy_cpu.cpp`/`yatzy_engine.cpp`/`precompute_std.h` sources for Windows.
- Windows Rust host target triple for the sidecar filename: `x86_64-pc-windows-msvc` (confirmed via `rustc --print host-tuple` on the target machine; re-verify if run on a different machine).
- MinGW cross-compiler must be the **POSIX** threading variant (`g++-mingw-w64-x86-64-posix` / `x86_64-w64-mingw32-g++-posix`) — the `win32` variant lacks `std::thread` support, which `yatzy_engine.cpp` requires.
- The `Command.sidecar` API only works inside a running Tauri app — no automated test can invoke the real sidecar. Automated tests cover only pure logic (state transitions, CLI-arg building, JSON parsing); the sidecar call itself is verified by manual playthrough.
- No custom Rust command/logic — Rust side is Tauri's scaffold plus sidecar config only.
- In-memory game state only, no persistence across app restarts.
- Windows build/run commands are executed via WSL-Windows interop (`powershell.exe -NoProfile -Command "..."` from a WSL shell), since Windows has Rust/Node/npm/cargo natively installed (confirmed: `cargo 1.87.0`, `node v20.20.2`, `npm 10.8.2`).

---

### Task 1: Scaffold the Tauri app

**Files:**
- Create: `gui/` (entire directory tree, via `create-tauri-app`)
- Modify: `.gitignore`

**Interfaces:**
- Produces: the `gui/` project skeleton that every later task adds files into — `gui/src/` (frontend TS), `gui/src-tauri/` (Rust/Tauri config), `gui/package.json`, `gui/index.html`.

- [ ] **Step 1: Run the scaffold tool**

```bash
cd /home/ae/projects/privat/claude/optimal-yatzy
yes "" | npm create tauri-app@latest gui -- --template vanilla-ts
```

`--template vanilla-ts` fully determines frontend language/template/flavor, skipping those prompts. The `yes ""` prefix auto-accepts (with Enter, taking the default) any prompt the `--template` flag doesn't cover (e.g. the bundle identifier) — safe here since the default identifier doesn't affect functionality and can be changed later in `tauri.conf.json` if desired.

- [ ] **Step 2: Verify the expected structure exists**

Run: `ls gui/ gui/src gui/src-tauri gui/src-tauri/src gui/src-tauri/capabilities`
Expected: `gui/package.json`, `gui/index.html`, `gui/src/main.ts`, `gui/src-tauri/Cargo.toml`, `gui/src-tauri/tauri.conf.json`, `gui/src-tauri/src/lib.rs` (or `main.rs`), `gui/src-tauri/capabilities/default.json` all present. If the exact filenames differ slightly from this list (template versions do shift), that's fine — note what actually exists so later tasks can be adjusted.

- [ ] **Step 3: Install frontend dependencies and add Vitest**

```bash
cd /home/ae/projects/privat/claude/optimal-yatzy/gui
npm install
npm install -D vitest
```

- [ ] **Step 4: Add a Vitest config**

Create `gui/vitest.config.ts`:

```typescript
import { defineConfig } from "vitest/config";

export default defineConfig({
  test: {
    environment: "node",
  },
});
```

- [ ] **Step 5: Add a test script**

Edit `gui/package.json`'s `"scripts"` object to add a `"test"` entry (keep every existing script the scaffold generated, e.g. `"dev"`, `"build"`, `"tauri"` — just add this one alongside them):

```json
"test": "vitest run"
```

- [ ] **Step 6: Verify the test runner works (no tests yet, but the command must run cleanly)**

Run: `cd /home/ae/projects/privat/claude/optimal-yatzy/gui && npm test`
Expected: Vitest reports "No test files found" (or similar) but exits without a crash/error — proves the runner itself is wired correctly before any real tests exist.

- [ ] **Step 7: Ignore generated/build artifacts**

Add to `.gitignore` (in the repo root, alongside the existing entries):

```
gui/node_modules/
gui/dist/
gui/src-tauri/target/
gui/src-tauri/binaries/
```

- [ ] **Step 8: Commit**

```bash
cd /home/ae/projects/privat/claude/optimal-yatzy
git add gui .gitignore
git commit -m "feat: scaffold the Tauri desktop GUI project"
```

---

### Task 2: Cross-compile the engine for Windows

**Files:**
- Create: `gui/scripts/build-sidecar.sh`
- Create (generated by the script, gitignored): `gui/src-tauri/binaries/yatzy_cpu-x86_64-pc-windows-msvc.exe`

**Interfaces:**
- Consumes: the existing, unmodified `precompute_std.h`, `yatzy_engine.cpp`, `yatzy_cpu.cpp` from the repo root (built for Task 1/2 of the prior CPU-solver phase).
- Produces: a Windows-executable sidecar binary at the exact path/name Tauri's `externalBin` convention requires, consumed by Task 5's `tauri.conf.json` wiring.

- [ ] **Step 1: Install the POSIX-threading MinGW-w64 cross-compiler**

```bash
sudo apt-get update && sudo apt-get install -y g++-mingw-w64-x86-64-posix
```

- [ ] **Step 2: Write the build script**

Create `gui/scripts/build-sidecar.sh`:

```bash
#!/usr/bin/env bash
# gui/scripts/build-sidecar.sh — cross-compiles the existing yatzy_cpu engine
# (unmodified C++ sources from the repo root) into a Windows binary, named
# per Tauri's sidecar convention: <name>-<host-target-triple>.exe. The
# target-triple suffix must match whatever `rustc --print host-tuple`
# reports on the machine that runs `tauri dev`/`build` — currently
# x86_64-pc-windows-msvc — even though this binary itself is built with an
# unrelated toolchain (MinGW-w64); Tauri only cares about the filename.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
GUI_DIR="$(dirname "$SCRIPT_DIR")"
REPO_ROOT="$(dirname "$GUI_DIR")"
OUT_DIR="$GUI_DIR/src-tauri/binaries"
TARGET_TRIPLE="x86_64-pc-windows-msvc"

mkdir -p "$OUT_DIR"

x86_64-w64-mingw32-g++-posix -O3 -std=c++17 -static \
  "$REPO_ROOT/yatzy_engine.cpp" "$REPO_ROOT/yatzy_cpu.cpp" \
  -o "$OUT_DIR/yatzy_cpu-$TARGET_TRIPLE.exe"

echo "built $OUT_DIR/yatzy_cpu-$TARGET_TRIPLE.exe"
```

`-static` links the MinGW runtime/pthread shim statically, so the resulting `.exe` doesn't depend on any MinGW DLLs being present on the target Windows machine.

- [ ] **Step 3: Run the build script**

```bash
chmod +x /home/ae/projects/privat/claude/optimal-yatzy/gui/scripts/build-sidecar.sh
/home/ae/projects/privat/claude/optimal-yatzy/gui/scripts/build-sidecar.sh
```

Expected: `built .../gui/src-tauri/binaries/yatzy_cpu-x86_64-pc-windows-msvc.exe`, no compile errors.

- [ ] **Step 4: Verify it's a real Windows executable**

Run: `file gui/src-tauri/binaries/yatzy_cpu-x86_64-pc-windows-msvc.exe`
Expected: `PE32+ executable (console) x86-64, for MS Windows`

- [ ] **Step 5: Verify it actually runs and produces a correct recommendation, via Windows interop**

Windows can't execute a binary directly from the WSL UNC path, so copy it to a native Windows temp folder first:

```bash
mkdir -p /mnt/c/Users/AndreasEllewsen/AppData/Local/Temp/yatzy-sidecar-check
cp gui/src-tauri/binaries/yatzy_cpu-x86_64-pc-windows-msvc.exe /mnt/c/Users/AndreasEllewsen/AppData/Local/Temp/yatzy-sidecar-check/
powershell.exe -NoProfile -Command "& 'C:\Users\AndreasEllewsen\AppData\Local\Temp\yatzy-sidecar-check\yatzy_cpu-x86_64-pc-windows-msvc.exe' --used 0,1,2,3,4,5,6,7,8,9,10,11,12,13 --upper 0 --dice 6,6,6,6,6 --rerolls 0 --json"
```

Expected output includes `"categoryName":"Yatzy"` and `"resultingScore":50` (the DP solve runs fresh here since this Windows binary has never cached before — this may take up to a couple of minutes; the resulting `yatzy_cpu_dp.bin` cache file is written next to wherever the `.exe` runs from, harmless to leave in the temp folder).

- [ ] **Step 6: Clean up the temp verification folder**

```bash
rm -rf /mnt/c/Users/AndreasEllewsen/AppData/Local/Temp/yatzy-sidecar-check
```

- [ ] **Step 7: Commit**

```bash
cd /home/ae/projects/privat/claude/optimal-yatzy
git add gui/scripts/build-sidecar.sh
git commit -m "feat: add Windows cross-compile script for the sidecar engine binary"
```

(The `.exe` itself is gitignored per Task 1 — it's a regenerable build artifact, not source.)

---

### Task 3: Game state model

**Files:**
- Create: `gui/src/state.ts`
- Test: `gui/src/state.test.ts`

**Interfaces:**
- Produces: `NUM_CATEGORIES`, `UPPER_CATEGORY_COUNT`, `BONUS_THRESHOLD`, `BONUS_POINTS`, `CATEGORY_NAMES: string[]`, `interface GameState { usedMask: number; categoryScores: (number|null)[]; upperTotal: number; dice: (number|null)[]; rerollsLeft: number; }`, `initialGameState(): GameState`, `isGameComplete(state): boolean`, `allDiceValid(dice): dice is number[]`, `setDice(state, dice): GameState`, `advanceReroll(state): GameState`, `scoreCategory(state, category, resultingScore): GameState`, `bonusEarned(state): boolean`, `totalScore(state): number` — all consumed by Tasks 4 and 6.

- [ ] **Step 1: Write the failing tests**

Create `gui/src/state.test.ts`:

```typescript
import { describe, it, expect } from "vitest";
import {
  initialGameState, isGameComplete, allDiceValid, setDice, advanceReroll,
  scoreCategory, bonusEarned, totalScore, NUM_CATEGORIES,
} from "./state";

describe("initialGameState", () => {
  it("starts with no categories used and 2 rerolls left", () => {
    const s = initialGameState();
    expect(s.usedMask).toBe(0);
    expect(s.rerollsLeft).toBe(2);
    expect(s.categoryScores).toEqual(new Array(NUM_CATEGORIES).fill(null));
  });
});

describe("allDiceValid", () => {
  it("is true for five values 1-6", () => {
    expect(allDiceValid([1, 2, 3, 4, 5])).toBe(true);
  });
  it("is false if any value is null", () => {
    expect(allDiceValid([1, 2, null, 4, 5])).toBe(false);
  });
  it("is false if a value is out of range", () => {
    expect(allDiceValid([1, 2, 3, 4, 7])).toBe(false);
  });
});

describe("advanceReroll", () => {
  it("decrements rerollsLeft and clears dice", () => {
    const s = setDice(initialGameState(), [6, 6, 6, 6, 6]);
    const next = advanceReroll(s);
    expect(next.rerollsLeft).toBe(1);
    expect(next.dice).toEqual([null, null, null, null, null]);
  });
  it("never goes below 0", () => {
    let s = initialGameState();
    s = advanceReroll(advanceReroll(advanceReroll(s)));
    expect(s.rerollsLeft).toBe(0);
  });
});

describe("scoreCategory", () => {
  it("records the score, marks the category used, and resets the turn", () => {
    const s = scoreCategory(initialGameState(), 14, 50); // Yatzy = index 14
    expect(s.categoryScores[14]).toBe(50);
    expect(s.usedMask & (1 << 14)).not.toBe(0);
    expect(s.rerollsLeft).toBe(2);
    expect(s.dice).toEqual([null, null, null, null, null]);
  });
  it("adds to upperTotal only for upper-section categories (index < 6)", () => {
    let s = scoreCategory(initialGameState(), 0, 3); // Ones
    expect(s.upperTotal).toBe(3);
    s = scoreCategory(s, 14, 50); // Yatzy, a lower category
    expect(s.upperTotal).toBe(3);
  });
});

describe("bonusEarned / totalScore", () => {
  it("awards the 50-point bonus at upperTotal >= 63", () => {
    const s = { ...initialGameState(), upperTotal: 63 };
    expect(bonusEarned(s)).toBe(true);
  });
  it("does not award the bonus below 63", () => {
    const s = { ...initialGameState(), upperTotal: 62 };
    expect(bonusEarned(s)).toBe(false);
  });
  it("sums all recorded scores plus the bonus if earned", () => {
    const s = {
      ...initialGameState(),
      upperTotal: 63,
      categoryScores: [3, 6, 9, 12, 15, 18, 0, 0, 0, 0, 0, 0, 0, 0, 50],
    };
    // upper section 3+6+9+12+15+18 = 63, plus Yatzy 50, plus bonus 50
    expect(totalScore(s)).toBe(63 + 50 + 50);
  });
});

describe("isGameComplete", () => {
  it("is false when categories remain open, true when all 15 are used", () => {
    let s = initialGameState();
    expect(isGameComplete(s)).toBe(false);
    for (let cat = 0; cat < NUM_CATEGORIES; cat++) s = scoreCategory(s, cat, 0);
    expect(isGameComplete(s)).toBe(true);
  });
});
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cd /home/ae/projects/privat/claude/optimal-yatzy/gui && npm test`
Expected: FAIL — `Cannot find module './state'`

- [ ] **Step 3: Write the implementation**

Create `gui/src/state.ts`:

```typescript
// state.ts — pure game-state model and transitions. No Tauri/DOM dependency.
export const NUM_CATEGORIES = 15;
export const UPPER_CATEGORY_COUNT = 6;
export const BONUS_THRESHOLD = 63;
export const BONUS_POINTS = 50;

export const CATEGORY_NAMES: string[] = [
  "Ones", "Twos", "Threes", "Fours", "Fives", "Sixes",
  "One Pair", "Two Pairs", "Three of a Kind", "Four of a Kind",
  "Small Straight", "Large Straight", "Full House",
  "Chance", "Yatzy",
];

export interface GameState {
  usedMask: number;
  categoryScores: (number | null)[];
  upperTotal: number;
  dice: (number | null)[];
  rerollsLeft: number;
}

export function initialGameState(): GameState {
  return {
    usedMask: 0,
    categoryScores: new Array(NUM_CATEGORIES).fill(null),
    upperTotal: 0,
    dice: [null, null, null, null, null],
    rerollsLeft: 2,
  };
}

export function isGameComplete(state: GameState): boolean {
  return state.usedMask === (1 << NUM_CATEGORIES) - 1;
}

export function allDiceValid(dice: (number | null)[]): dice is number[] {
  return dice.length === 5 && dice.every((d) => d !== null && d >= 1 && d <= 6);
}

export function setDice(state: GameState, dice: (number | null)[]): GameState {
  return { ...state, dice };
}

export function advanceReroll(state: GameState): GameState {
  return {
    ...state,
    rerollsLeft: Math.max(0, state.rerollsLeft - 1),
    dice: [null, null, null, null, null],
  };
}

export function scoreCategory(state: GameState, category: number, resultingScore: number): GameState {
  const categoryScores = [...state.categoryScores];
  categoryScores[category] = resultingScore;
  const usedMask = state.usedMask | (1 << category);
  const upperTotal = category < UPPER_CATEGORY_COUNT
    ? state.upperTotal + resultingScore
    : state.upperTotal;
  return {
    usedMask,
    categoryScores,
    upperTotal,
    dice: [null, null, null, null, null],
    rerollsLeft: 2,
  };
}

export function bonusEarned(state: GameState): boolean {
  return state.upperTotal >= BONUS_THRESHOLD;
}

export function totalScore(state: GameState): number {
  const sum = state.categoryScores.reduce((acc: number, s) => acc + (s ?? 0), 0);
  return sum + (bonusEarned(state) ? BONUS_POINTS : 0);
}
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `cd /home/ae/projects/privat/claude/optimal-yatzy/gui && npm test`
Expected: all `state.test.ts` tests pass.

- [ ] **Step 5: Commit**

```bash
cd /home/ae/projects/privat/claude/optimal-yatzy
git add gui/src/state.ts gui/src/state.test.ts
git commit -m "feat: add pure game-state model for the GUI"
```

---

### Task 4: CLI-args building and JSON-result parsing

**Files:**
- Create: `gui/src/cliArgs.ts`
- Create: `gui/src/parseResult.ts`
- Test: `gui/src/cliArgs.test.ts`
- Test: `gui/src/parseResult.test.ts`

**Interfaces:**
- Consumes: `GameState` from `gui/src/state.ts` (Task 3).
- Produces: `buildCliArgs(state: GameState, dice: number[]): string[]`; `interface RerollOption { holdValues: number[]; expectedValue: number; }`; `interface CategoryOption { category: number; categoryName: string; resultingScore: number; expectedValue: number; }`; `type QueryResult = { isRerollDecision: true; rerollOptions: RerollOption[] } | { isRerollDecision: false; categoryOptions: CategoryOption[] }`; `parseQueryResult(rawStdout: string): QueryResult` — all consumed by Task 5's `sidecar.ts`.

- [ ] **Step 1: Write the failing tests**

Create `gui/src/cliArgs.test.ts`:

```typescript
import { describe, it, expect } from "vitest";
import { buildCliArgs } from "./cliArgs";
import { initialGameState, scoreCategory } from "./state";

describe("buildCliArgs", () => {
  it("builds args for a fresh game with no categories used", () => {
    const args = buildCliArgs(initialGameState(), [2, 2, 3, 5, 6]);
    expect(args).toEqual([
      "--used", "",
      "--upper", "0",
      "--dice", "2,2,3,5,6",
      "--rerolls", "2",
      "--json",
    ]);
  });

  it("lists used category indices comma-separated", () => {
    let s = scoreCategory(initialGameState(), 0, 3); // Ones used
    s = scoreCategory(s, 14, 50); // Yatzy used
    const args = buildCliArgs(s, [1, 1, 1, 1, 1]);
    expect(args).toEqual([
      "--used", "0,14",
      "--upper", "3",
      "--dice", "1,1,1,1,1",
      "--rerolls", "2",
      "--json",
    ]);
  });
});
```

Create `gui/src/parseResult.test.ts`:

```typescript
import { describe, it, expect } from "vitest";
import { parseQueryResult } from "./parseResult";

describe("parseQueryResult", () => {
  it("parses a reroll-decision result", () => {
    const raw = JSON.stringify({
      isRerollDecision: true,
      rerollOptions: [{ holdValues: [6, 6, 6, 6, 6], expectedValue: 50 }],
    });
    const result = parseQueryResult(raw);
    expect(result.isRerollDecision).toBe(true);
    if (result.isRerollDecision) {
      expect(result.rerollOptions[0].holdValues).toEqual([6, 6, 6, 6, 6]);
      expect(result.rerollOptions[0].expectedValue).toBe(50);
    }
  });

  it("parses a category-decision result", () => {
    const raw = JSON.stringify({
      isRerollDecision: false,
      categoryOptions: [
        { category: 14, categoryName: "Yatzy", resultingScore: 50, expectedValue: 50 },
      ],
    });
    const result = parseQueryResult(raw);
    expect(result.isRerollDecision).toBe(false);
    if (!result.isRerollDecision) {
      expect(result.categoryOptions[0].categoryName).toBe("Yatzy");
    }
  });

  it("throws on malformed input", () => {
    expect(() => parseQueryResult("{}")).toThrow();
  });
});
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cd /home/ae/projects/privat/claude/optimal-yatzy/gui && npm test`
Expected: FAIL — `Cannot find module './cliArgs'` and `Cannot find module './parseResult'`

- [ ] **Step 3: Write the implementations**

Create `gui/src/cliArgs.ts`:

```typescript
// cliArgs.ts — pure function building yatzy_cpu CLI args from game state.
import { GameState, NUM_CATEGORIES } from "./state";

export function buildCliArgs(state: GameState, dice: number[]): string[] {
  const usedIndices: number[] = [];
  for (let cat = 0; cat < NUM_CATEGORIES; cat++) {
    if (state.usedMask & (1 << cat)) usedIndices.push(cat);
  }
  return [
    "--used", usedIndices.join(","),
    "--upper", String(state.upperTotal),
    "--dice", dice.join(","),
    "--rerolls", String(state.rerollsLeft),
    "--json",
  ];
}
```

Create `gui/src/parseResult.ts`:

```typescript
// parseResult.ts — pure function parsing yatzy_cpu's --json stdout into a
// typed result.
export interface RerollOption {
  holdValues: number[];
  expectedValue: number;
}

export interface CategoryOption {
  category: number;
  categoryName: string;
  resultingScore: number;
  expectedValue: number;
}

export type QueryResult =
  | { isRerollDecision: true; rerollOptions: RerollOption[] }
  | { isRerollDecision: false; categoryOptions: CategoryOption[] };

export function parseQueryResult(rawStdout: string): QueryResult {
  const parsed = JSON.parse(rawStdout);
  if (typeof parsed.isRerollDecision !== "boolean") {
    throw new Error("malformed sidecar output: missing isRerollDecision");
  }
  if (parsed.isRerollDecision) {
    return { isRerollDecision: true, rerollOptions: parsed.rerollOptions as RerollOption[] };
  }
  return { isRerollDecision: false, categoryOptions: parsed.categoryOptions as CategoryOption[] };
}
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `cd /home/ae/projects/privat/claude/optimal-yatzy/gui && npm test`
Expected: all `cliArgs.test.ts` and `parseResult.test.ts` tests pass, alongside the still-passing `state.test.ts`.

- [ ] **Step 5: Commit**

```bash
cd /home/ae/projects/privat/claude/optimal-yatzy
git add gui/src/cliArgs.ts gui/src/cliArgs.test.ts gui/src/parseResult.ts gui/src/parseResult.test.ts
git commit -m "feat: add CLI-args building and JSON-result parsing"
```

---

### Task 5: Wire up the sidecar

**Files:**
- Modify: `gui/src-tauri/tauri.conf.json`
- Modify: `gui/src-tauri/capabilities/default.json`
- Modify: `gui/src-tauri/Cargo.toml`
- Modify: `gui/src-tauri/src/lib.rs` (or `main.rs` — whichever file contains `tauri::Builder::default()`, per Task 1's Step 2 findings)
- Create: `gui/src/sidecar.ts`
- Create: `gui/scripts/sync-to-windows.sh`

**Interfaces:**
- Consumes: `GameState` from `state.ts` (Task 3); `buildCliArgs` from `cliArgs.ts`, `parseQueryResult`/`QueryResult` from `parseResult.ts` (Task 4); the sidecar binary from Task 2.
- Produces: `getRecommendation(state: GameState, dice: number[]): Promise<QueryResult>`, consumed by Task 6's `main.ts`.

- [ ] **Step 1: Install the shell plugin**

```bash
cd /home/ae/projects/privat/claude/optimal-yatzy/gui
npm install @tauri-apps/plugin-shell
```

- [ ] **Step 2: Add the Rust-side plugin dependency**

Add to `gui/src-tauri/Cargo.toml`'s `[dependencies]` section (keep every existing dependency the scaffold generated — this is one more line, not a replacement):

```toml
tauri-plugin-shell = "2"
```

- [ ] **Step 3: Register the plugin in Rust**

In whichever file contains `tauri::Builder::default()` (from Task 1 Step 2 — typically `gui/src-tauri/src/lib.rs`), add `.plugin(tauri_plugin_shell::init())` as one more `.plugin(...)` call in the builder chain, alongside whatever plugins the scaffold already registered (e.g. `tauri_plugin_opener::init()`). For example, if the scaffold produced:

```rust
tauri::Builder::default()
    .plugin(tauri_plugin_opener::init())
    .run(tauri::generate_context!())
    .expect("error while running tauri application");
```

change it to:

```rust
tauri::Builder::default()
    .plugin(tauri_plugin_opener::init())
    .plugin(tauri_plugin_shell::init())
    .run(tauri::generate_context!())
    .expect("error while running tauri application");
```

- [ ] **Step 4: Configure the external binary**

In `gui/src-tauri/tauri.conf.json`, add `"externalBin": ["binaries/yatzy_cpu"]` inside the existing `"bundle"` object (keep every other field the scaffold generated — `productName`, `identifier`, `icon`, etc. — this just adds one more key):

```json
"bundle": {
  "externalBin": ["binaries/yatzy_cpu"]
}
```

- [ ] **Step 5: Grant sidecar execute permission**

In `gui/src-tauri/capabilities/default.json`, add a `shell:allow-execute` permission entry to the existing `"permissions"` array (keep `"core:default"` and anything else already listed):

```json
{
  "identifier": "shell:allow-execute",
  "allow": [
    {
      "name": "binaries/yatzy_cpu",
      "sidecar": true,
      "args": true
    }
  ]
}
```

`"args": true` is required (not a fixed arg list) because our args vary per query (`--used`, `--upper`, `--dice`, `--rerolls` all change every call).

- [ ] **Step 6: Write the sidecar wrapper**

Create `gui/src/sidecar.ts`:

```typescript
// sidecar.ts — thin wrapper invoking the bundled yatzy_cpu sidecar. Not unit
// tested directly (Command.sidecar requires a running Tauri app); covered by
// manual verification instead, per the design spec.
import { Command } from "@tauri-apps/plugin-shell";
import { GameState } from "./state";
import { buildCliArgs } from "./cliArgs";
import { parseQueryResult, QueryResult } from "./parseResult";

export async function getRecommendation(state: GameState, dice: number[]): Promise<QueryResult> {
  const args = buildCliArgs(state, dice);
  const command = Command.sidecar("binaries/yatzy_cpu", args);
  const output = await command.execute();
  if (output.code !== 0) {
    throw new Error(`yatzy_cpu exited with code ${output.code}: ${output.stderr}`);
  }
  return parseQueryResult(output.stdout);
}
```

- [ ] **Step 7: Write the Windows sync script**

Create `gui/scripts/sync-to-windows.sh`:

```bash
#!/usr/bin/env bash
# gui/scripts/sync-to-windows.sh — copies gui/ to a native Windows
# build-staging folder. Windows can't execute a binary sitting on the WSL
# UNC path, so `tauri dev`/`build` must run against a native copy. This is
# a one-way disposable sync, not a git checkout: node_modules/target/dist
# are excluded and regenerated fresh on the Windows side.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
GUI_DIR="$(dirname "$SCRIPT_DIR")"
WIN_DEST="/mnt/c/Users/AndreasEllewsen/yatzy-gui-build"

mkdir -p "$WIN_DEST"
rsync -a --delete \
  --exclude node_modules \
  --exclude dist \
  --exclude target \
  "$GUI_DIR/" "$WIN_DEST/"

echo "synced gui/ -> $WIN_DEST (Windows path: C:\\Users\\AndreasEllewsen\\yatzy-gui-build)"
```

- [ ] **Step 8: Sync and verify the Rust side compiles on Windows**

```bash
chmod +x /home/ae/projects/privat/claude/optimal-yatzy/gui/scripts/sync-to-windows.sh
/home/ae/projects/privat/claude/optimal-yatzy/gui/scripts/sync-to-windows.sh
powershell.exe -NoProfile -Command "Set-Location 'C:\Users\AndreasEllewsen\yatzy-gui-build\src-tauri'; cargo check"
```

Expected: `cargo check` succeeds (may take a while on first run while it fetches and compiles dependencies including `tauri`, `tauri-plugin-shell`, `tauri-plugin-opener`). Fix any compile errors surfaced here — most likely causes are a typo in Step 3's Rust edit or a Cargo.toml syntax error from Step 2.

- [ ] **Step 9: Commit**

```bash
cd /home/ae/projects/privat/claude/optimal-yatzy
git add gui/src-tauri/tauri.conf.json gui/src-tauri/capabilities/default.json gui/src-tauri/Cargo.toml gui/src-tauri/src gui/src/sidecar.ts gui/scripts/sync-to-windows.sh gui/package.json gui/package-lock.json
git commit -m "feat: wire up the sidecar binary and shell plugin"
```

---

### Task 6: Build the UI

**Files:**
- Modify: `gui/index.html`
- Create: `gui/src/render.ts`
- Modify: `gui/src/main.ts`
- Modify: `gui/src/styles.css` (or the scaffold's equivalent stylesheet — check Task 1 Step 2's findings for the exact filename)

**Interfaces:**
- Consumes: everything from Tasks 3-5 (`GameState` + transitions, `getRecommendation`, `QueryResult`).
- Produces: the rendered scorecard, turn panel, and recommendation UI — the final user-facing surface, verified manually in Task 7 (no automated test for this task, per the design spec: DOM rendering has no meaningful pure-logic seam left to unit test once `state.ts`/`cliArgs.ts`/`parseResult.ts` already cover the real logic).

- [ ] **Step 1: Replace the scaffolded index.html body**

Replace the contents of `gui/index.html`'s `<body>` (keep the `<head>` as the scaffold generated it — title, charset, stylesheet link — just replace what's inside `<body>`):

```html
<body>
  <div id="app">
    <section id="scorecard"></section>
    <section id="turn">
      <div id="dice-inputs"></div>
      <div id="rerolls-indicator"></div>
      <button id="reroll-button" disabled>Reroll →</button>
      <div id="recommendation"></div>
      <div id="error-banner" hidden></div>
    </section>
    <div id="final-total" hidden></div>
  </div>
  <script type="module" src="/src/main.ts"></script>
</body>
```

- [ ] **Step 2: Write the rendering module**

Create `gui/src/render.ts`:

```typescript
// render.ts — pure DOM rendering for the scorecard and turn panel. Reads
// application state and a query result, writes to fixed DOM containers.
import {
  GameState, CATEGORY_NAMES, NUM_CATEGORIES, bonusEarned, totalScore, isGameComplete,
} from "./state";
import { QueryResult } from "./parseResult";

export function renderScorecard(state: GameState): void {
  const el = document.getElementById("scorecard")!;
  const rows: string[] = [];
  for (let cat = 0; cat < NUM_CATEGORIES; cat++) {
    const score = state.categoryScores[cat];
    rows.push(
      `<div class="score-row"><span>${CATEGORY_NAMES[cat]}</span><span>${score === null ? "—" : score}</span></div>`
    );
  }
  const bonusText = bonusEarned(state) ? "bonus earned (+50)" : `${state.upperTotal}/63 for bonus`;
  el.innerHTML = `
    ${rows.join("")}
    <div class="score-row"><strong>Upper total</strong><span>${state.upperTotal} (${bonusText})</span></div>
    <div class="score-row"><strong>Grand total</strong><span>${totalScore(state)}</span></div>
  `;
}

export function renderRerollsIndicator(state: GameState): void {
  document.getElementById("rerolls-indicator")!.textContent = `Rerolls left: ${state.rerollsLeft}`;
}

export function renderRecommendation(
  result: QueryResult | null,
  onScoreCategory: (category: number, resultingScore: number) => void
): void {
  const el = document.getElementById("recommendation")!;
  const rerollButton = document.getElementById("reroll-button") as HTMLButtonElement;

  if (!result) {
    el.innerHTML = "";
    rerollButton.disabled = true;
    return;
  }

  if (result.isRerollDecision) {
    rerollButton.disabled = false;
    el.innerHTML = result.rerollOptions
      .map(
        (opt) =>
          `<div class="option-row">hold [${opt.holdValues.join(",")}] — expected value ${opt.expectedValue.toFixed(2)}</div>`
      )
      .join("");
  } else {
    rerollButton.disabled = true;
    el.innerHTML = "";
    for (const opt of result.categoryOptions) {
      const button = document.createElement("button");
      button.className = "category-option";
      button.textContent = `${opt.categoryName} — score ${opt.resultingScore} (expected value ${opt.expectedValue.toFixed(2)})`;
      button.addEventListener("click", () => onScoreCategory(opt.category, opt.resultingScore));
      el.appendChild(button);
    }
  }
}

export function renderError(message: string | null): void {
  const el = document.getElementById("error-banner")!;
  if (message) {
    el.hidden = false;
    el.textContent = message;
  } else {
    el.hidden = true;
    el.textContent = "";
  }
}

export function renderFinalTotal(state: GameState): void {
  const el = document.getElementById("final-total")!;
  if (isGameComplete(state)) {
    el.hidden = false;
    el.textContent = `Game complete! Final score: ${totalScore(state)}`;
  } else {
    el.hidden = true;
  }
}

export function renderDiceInputs(
  dice: (number | null)[],
  onChange: (index: number, value: number | null) => void
): void {
  const el = document.getElementById("dice-inputs")!;
  if (el.childElementCount !== 5) {
    el.innerHTML = "";
    for (let i = 0; i < 5; i++) {
      const input = document.createElement("input");
      input.type = "number";
      input.min = "1";
      input.max = "6";
      input.className = "die-input";
      input.addEventListener("input", () => {
        const raw = input.value.trim();
        const value = raw === "" ? null : Number(raw);
        onChange(i, value !== null && value >= 1 && value <= 6 ? value : null);
      });
      el.appendChild(input);
    }
  }
  const inputs = el.querySelectorAll<HTMLInputElement>(".die-input");
  inputs.forEach((input, i) => {
    input.value = dice[i] === null ? "" : String(dice[i]);
  });
}
```

- [ ] **Step 3: Replace main.ts**

Replace the entire contents of `gui/src/main.ts` (the scaffold's default counter-button demo code is being fully replaced):

```typescript
// main.ts — wires game state, sidecar calls, and rendering together.
import {
  initialGameState, setDice, advanceReroll, scoreCategory, allDiceValid, GameState,
} from "./state";
import { getRecommendation } from "./sidecar";
import { QueryResult } from "./parseResult";
import {
  renderScorecard, renderRerollsIndicator, renderRecommendation, renderError,
  renderFinalTotal, renderDiceInputs,
} from "./render";

let state: GameState = initialGameState();
let lastResult: QueryResult | null = null;

function renderAll(): void {
  renderScorecard(state);
  renderRerollsIndicator(state);
  renderFinalTotal(state);
  renderRecommendation(lastResult, handleScoreCategory);
  renderDiceInputs(state.dice, handleDiceChange);
}

async function maybeQuery(): Promise<void> {
  if (!allDiceValid(state.dice)) {
    lastResult = null;
    renderAll();
    return;
  }
  try {
    lastResult = await getRecommendation(state, state.dice as number[]);
    renderError(null);
  } catch (err) {
    lastResult = null;
    renderError(err instanceof Error ? err.message : String(err));
  }
  renderAll();
}

function handleDiceChange(index: number, value: number | null): void {
  const dice = [...state.dice];
  dice[index] = value;
  state = setDice(state, dice);
  void maybeQuery();
}

function handleScoreCategory(category: number, resultingScore: number): void {
  state = scoreCategory(state, category, resultingScore);
  lastResult = null;
  renderAll();
}

function handleReroll(): void {
  state = advanceReroll(state);
  lastResult = null;
  renderAll();
}

document.getElementById("reroll-button")!.addEventListener("click", handleReroll);
renderAll();
```

- [ ] **Step 4: Replace the stylesheet**

Replace the contents of the scaffold's stylesheet (check Task 1 Step 2's findings for its exact path — commonly `gui/src/style.css`, referenced from `gui/index.html`'s `<head>`):

```css
body { font-family: sans-serif; max-width: 480px; margin: 2rem auto; }
.score-row { display: flex; justify-content: space-between; padding: 2px 0; }
.die-input { width: 3rem; margin-right: 4px; }
.option-row { padding: 4px 0; }
.category-option { display: block; width: 100%; text-align: left; padding: 6px; margin: 4px 0; }
#error-banner { color: white; background: #b00020; padding: 8px; margin-top: 8px; }
#final-total { font-weight: bold; margin-top: 12px; }
```

- [ ] **Step 5: Run the existing test suite to make sure nothing broke**

Run: `cd /home/ae/projects/privat/claude/optimal-yatzy/gui && npm test`
Expected: all `state.test.ts`, `cliArgs.test.ts`, `parseResult.test.ts` tests still pass (this task doesn't add new automated tests — `render.ts`/`main.ts` are verified manually in Task 7).

- [ ] **Step 6: Commit**

```bash
cd /home/ae/projects/privat/claude/optimal-yatzy
git add gui/index.html gui/src/render.ts gui/src/main.ts gui/src/style.css
git commit -m "feat: build the scorecard, turn panel, and recommendation UI"
```

(Adjust the stylesheet filename in the `git add` if Task 1 Step 2 found a different name.)

---

### Task 7: Final integration, manual verification, and packaging

**Files:**
- Create: `gui/README.md`

**Interfaces:**
- Consumes: everything from Tasks 1-6.

- [ ] **Step 1: Sync the latest code to the Windows build-staging folder**

```bash
/home/ae/projects/privat/claude/optimal-yatzy/gui/scripts/sync-to-windows.sh
```

- [ ] **Step 2: Install frontend dependencies on the Windows side**

```bash
powershell.exe -NoProfile -Command "Set-Location 'C:\Users\AndreasEllewsen\yatzy-gui-build'; npm install"
```

- [ ] **Step 3: Launch the dev build and confirm it starts without crashing**

```bash
powershell.exe -NoProfile -Command "Set-Location 'C:\Users\AndreasEllewsen\yatzy-gui-build'; npm run tauri dev"
```

Run this in the background (it's a long-lived dev server/window) and check its output after a few seconds for a clean startup (no Rust panic, no "failed to compile" error) rather than waiting for it to exit — it won't exit on its own while the window is open.

- [ ] **Step 4: Hand off for manual playthrough**

This step needs a human at the keyboard — driving the actual rendered Windows GUI window isn't something that can be done from this WSL session. With the dev build running from Step 3:

1. Enter a 5-dice roll in the dice inputs (2 rerolls left) — confirm the reroll recommendation list appears with sensible "hold [...]" options and expected values.
2. Click "Reroll →", enter a new roll (1 reroll left) — confirm the recommendation updates.
3. Click "Reroll →" again, enter a final roll (0 rerolls left) — confirm the view switches to ranked category options.
4. Click a category — confirm the scorecard updates (score recorded, upper total updated if applicable, next turn starts at 2 rerolls).
5. Repeat until all 15 categories are filled — confirm the final total (including the +50 bonus if the upper total reached 63) displays correctly.
6. Deliberately trigger the error path: temporarily rename `C:\Users\AndreasEllewsen\yatzy-gui-build\src-tauri\binaries\yatzy_cpu-x86_64-pc-windows-msvc.exe`, enter a dice roll, confirm the error banner appears with a clear message, then rename the file back.

Report back whether all six checks passed, or what broke.

- [ ] **Step 5: Stop the dev server**

Close the app window / stop the `npm run tauri dev` process once the manual playthrough is done.

- [ ] **Step 6: Produce a distributable build**

```bash
powershell.exe -NoProfile -Command "Set-Location 'C:\Users\AndreasEllewsen\yatzy-gui-build'; npm run tauri build"
```

Expected: a Windows installer/bundle produced under `C:\Users\AndreasEllewsen\yatzy-gui-build\src-tauri\target\release\bundle\`. Note the exact output path in the report.

- [ ] **Step 7: Write gui/README.md**

Create `gui/README.md`:

```markdown
# Yatzy Desktop GUI

A Windows desktop app for standard (5-dice) Yatzy: enter your rolls during a
real game and get the optimal move back, powered by the `yatzy_cpu` DP engine
from the repo root (built for this app via `scripts/build-sidecar.sh`, no
engine logic duplicated here).

## Rebuilding the sidecar

After any change to `yatzy_engine.cpp`, `yatzy_cpu.cpp`, or `precompute_std.h`
in the repo root:

```bash
./scripts/build-sidecar.sh
```

## Developing/running on Windows

Windows can't execute a binary sitting on this repo's WSL UNC path, so sync
to a native Windows folder first:

```bash
./scripts/sync-to-windows.sh
```

Then, from Windows (PowerShell, or via `powershell.exe` from WSL):

```powershell
cd C:\Users\<you>\yatzy-gui-build
npm install
npm run tauri dev    # iterate
npm run tauri build   # produce a distributable
```

## Testing

Pure logic (game-state transitions, CLI-arg building, JSON-result parsing)
has unit tests, runnable directly in WSL (no Windows/Tauri needed):

```bash
npm test
```

The sidecar call itself and the rendered UI require a running Tauri app on
Windows (`Command.sidecar` only works through Tauri's IPC bridge) — verify
those by playing a full game in `npm run tauri dev`.
```

- [ ] **Step 8: Commit**

```bash
cd /home/ae/projects/privat/claude/optimal-yatzy
git add gui/README.md
git commit -m "docs: add gui/README.md covering build, sync, and test workflow"
```
