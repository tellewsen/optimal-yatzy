# Yatzy GUI vs-Computer Mode Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a two-player "vs Computer" match mode to the Yatzy GUI, where the computer always plays the DP-optimal move, plus a global toggle to hide the advisory recommendation list and a computer-turn pacing setting, per [2026-07-15-yatzy-gui-vs-computer-design.md](../specs/2026-07-15-yatzy-gui-vs-computer-design.md).

**Architecture:** A new pure `match.ts` module wraps two existing, unmodified `GameState`s (from `state.ts`) in a `MatchState` (`mode`, `turn`, `player`, `computer`). `main.ts` migrates from a bare `GameState` to `MatchState`, threading every existing transition through `activeGameState`/`withActiveGameState` so solo mode's behavior is byte-for-byte preserved. The computer's turn reuses the exact same `getRecommendation` sidecar call (with its existing `onProgress` streaming) as the human's turn, just driven in a loop instead of by clicks, always taking the top-ranked option.

**Tech Stack:** Vanilla TypeScript, HTML, CSS (no framework), Vite dev server, Vitest for unit tests. No sidecar/Rust/C++ changes.

## Global Constraints

- Do not modify `gui/src/state.ts`, `gui/src/sidecar.ts`, `gui/src/parseResult.ts`, `gui/src/cliArgs.ts`, or their existing test files — every existing pure transition function (`setDice`, `applyHold`, `scoreCategory`, `advanceReroll`, `rollRemaining`, `allDiceValid`, `isGameComplete`, `totalScore`, `bonusEarned`, `initialGameState`) is reused as-is.
- Solo mode's behavior must remain exactly what it is today at every task checkpoint until vs-Computer-specific UI is added — the migration to `MatchState` is a behavior-preserving refactor for solo mode, not a rewrite.
- The computer always takes the top-ranked (index 0) option from `getRecommendation`'s result, at every decision point, with no exceptions.
- You always go first in vs Computer mode; turn alternates every time a category gets scored, via `match.ts`'s `afterScore`.
- The "show optimal moves" toggle is global — it hides `renderRecommendation`'s output regardless of whose turn is showing, defaulting to on. The computer's actual play is never affected by this toggle (it always sees and acts on the real recommendation internally).
- Reuse the existing `onProgress` callback plumbing on `getRecommendation` (already streams `"level P/15"` progress) for the computer's turn exactly as the human's turn already uses it — no sidecar-layer changes.
- Only one sidecar call runs at a time, across human queries, the warm-up call, and every step of the computer's turn — all funnel through the existing `queryInFlight`/`queryQueued` guard.
- Match state is in-memory only (matches the existing app-wide limitation) — no save/resume across restarts.
- Target window is 800×600 — design layout for that, with the existing flex-wrap fallback below it.
- After every task: run `npm test` (from `gui/`) — expect all existing tests plus this plan's new `match.test.ts` tests to keep passing (45/45 once Task 1 lands; no further tests are added after Task 1, matching the project's existing convention that only pure logic gets unit tests, DOM/orchestration code gets manual verification).

---

### Task 1: Match state model (`match.ts` + tests)

**Files:**
- Create: `gui/src/match.ts`
- Create: `gui/src/match.test.ts`

**Interfaces:**
- Consumes: `GameState`, `initialGameState`, `isGameComplete`, `totalScore`, `scoreCategory` (test-only) from `gui/src/state.ts` — all unmodified.
- Produces (used by every later task): `Mode = "solo" | "vsComputer"`, `Turn = "player" | "computer"`, `MatchState { mode, turn, player, computer }`, `initialMatchState(mode)`, `activeGameState(match)`, `withActiveGameState(match, next)`, `afterScore(match)`, `isMatchComplete(match)`, `Winner = "player" | "computer" | "tie"`, `matchWinner(match)`, `Comparison = "a" | "b" | "tie"`, `compareCategoryScores(a, b)`.

- [ ] **Step 1: Write `gui/src/match.ts`**

```ts
// match.ts — pure match-state model wrapping two GameStates (player vs
// computer, or just player in solo mode). No Tauri/DOM dependency.
import { GameState, initialGameState, isGameComplete, totalScore } from "./state";

export type Mode = "solo" | "vsComputer";
export type Turn = "player" | "computer";

export interface MatchState {
  mode: Mode;
  turn: Turn;
  player: GameState;
  computer: GameState | null;
}

export function initialMatchState(mode: Mode): MatchState {
  return {
    mode,
    turn: "player",
    player: initialGameState(),
    computer: mode === "vsComputer" ? initialGameState() : null,
  };
}

export function activeGameState(match: MatchState): GameState {
  return match.turn === "player" ? match.player : match.computer!;
}

export function withActiveGameState(match: MatchState, next: GameState): MatchState {
  return match.turn === "player" ? { ...match, player: next } : { ...match, computer: next };
}

// Call after scoreCategory has been applied to the active side. In
// vsComputer mode this flips whose turn is next; solo mode never has a
// second side to flip to.
export function afterScore(match: MatchState): MatchState {
  if (match.mode === "solo") return match;
  return { ...match, turn: match.turn === "player" ? "computer" : "player" };
}

export function isMatchComplete(match: MatchState): boolean {
  if (match.mode === "solo") return isGameComplete(match.player);
  return isGameComplete(match.player) && isGameComplete(match.computer!);
}

export type Winner = "player" | "computer" | "tie";

export function matchWinner(match: MatchState): Winner | null {
  if (match.mode === "solo" || !isMatchComplete(match)) return null;
  const playerTotal = totalScore(match.player);
  const computerTotal = totalScore(match.computer!);
  if (playerTotal > computerTotal) return "player";
  if (computerTotal > playerTotal) return "computer";
  return "tie";
}

export type Comparison = "a" | "b" | "tie";

export function compareCategoryScores(a: number | null, b: number | null): Comparison | null {
  if (a === null || b === null) return null;
  if (a > b) return "a";
  if (b > a) return "b";
  return "tie";
}
```

- [ ] **Step 2: Write `gui/src/match.test.ts`**

```ts
import { describe, it, expect } from "vitest";
import {
  initialMatchState, activeGameState, withActiveGameState, afterScore,
  isMatchComplete, matchWinner, compareCategoryScores,
} from "./match";
import { initialGameState, scoreCategory, NUM_CATEGORIES } from "./state";

describe("initialMatchState", () => {
  it("solo mode has no computer and starts on the player's turn", () => {
    const m = initialMatchState("solo");
    expect(m.mode).toBe("solo");
    expect(m.turn).toBe("player");
    expect(m.computer).toBeNull();
  });
  it("vsComputer mode seeds a fresh computer GameState and starts on the player's turn", () => {
    const m = initialMatchState("vsComputer");
    expect(m.turn).toBe("player");
    expect(m.computer).toEqual(initialGameState());
  });
});

describe("activeGameState / withActiveGameState", () => {
  it("returns and replaces the player's state when it's the player's turn", () => {
    const m = initialMatchState("vsComputer");
    expect(activeGameState(m)).toBe(m.player);
    const next = scoreCategory(m.player, 0, 3);
    const updated = withActiveGameState(m, next);
    expect(updated.player).toBe(next);
    expect(updated.computer).toBe(m.computer);
  });
  it("returns and replaces the computer's state when it's the computer's turn", () => {
    const m: ReturnType<typeof initialMatchState> = { ...initialMatchState("vsComputer"), turn: "computer" };
    expect(activeGameState(m)).toBe(m.computer);
    const next = scoreCategory(m.computer!, 0, 3);
    const updated = withActiveGameState(m, next);
    expect(updated.computer).toBe(next);
    expect(updated.player).toBe(m.player);
  });
});

describe("afterScore", () => {
  it("never flips turn in solo mode", () => {
    const m = initialMatchState("solo");
    expect(afterScore(m).turn).toBe("player");
  });
  it("flips player to computer and back in vsComputer mode", () => {
    let m = initialMatchState("vsComputer");
    m = afterScore(m);
    expect(m.turn).toBe("computer");
    m = afterScore(m);
    expect(m.turn).toBe("player");
  });
});

function completeGame(scoreValue: number): ReturnType<typeof initialGameState> {
  let s = initialGameState();
  for (let cat = 0; cat < NUM_CATEGORIES; cat++) s = scoreCategory(s, cat, scoreValue);
  return s;
}

describe("isMatchComplete", () => {
  it("solo: complete when the player's scorecard is full", () => {
    const m = initialMatchState("solo");
    expect(isMatchComplete(m)).toBe(false);
    expect(isMatchComplete({ ...m, player: completeGame(0) })).toBe(true);
  });
  it("vsComputer: requires BOTH scorecards full", () => {
    const m = initialMatchState("vsComputer");
    expect(isMatchComplete({ ...m, player: completeGame(0) })).toBe(false);
    expect(isMatchComplete({ ...m, player: completeGame(0), computer: completeGame(0) })).toBe(true);
  });
});

describe("matchWinner", () => {
  it("is null in solo mode even when complete", () => {
    const m = { ...initialMatchState("solo"), player: completeGame(0) };
    expect(matchWinner(m)).toBeNull();
  });
  it("is null before the match is complete", () => {
    expect(matchWinner(initialMatchState("vsComputer"))).toBeNull();
  });
  it("declares the player the winner when their total is higher", () => {
    const m = { ...initialMatchState("vsComputer"), player: completeGame(10), computer: completeGame(5) };
    expect(matchWinner(m)).toBe("player");
  });
  it("declares the computer the winner when their total is higher", () => {
    const m = { ...initialMatchState("vsComputer"), player: completeGame(5), computer: completeGame(10) };
    expect(matchWinner(m)).toBe("computer");
  });
  it("declares a tie when totals are equal", () => {
    const m = { ...initialMatchState("vsComputer"), player: completeGame(7), computer: completeGame(7) };
    expect(matchWinner(m)).toBe("tie");
  });
});

describe("compareCategoryScores", () => {
  it("returns null if either score is null", () => {
    expect(compareCategoryScores(null, 5)).toBeNull();
    expect(compareCategoryScores(5, null)).toBeNull();
    expect(compareCategoryScores(null, null)).toBeNull();
  });
  it("returns 'a' when the first score is higher", () => {
    expect(compareCategoryScores(10, 5)).toBe("a");
  });
  it("returns 'b' when the second score is higher", () => {
    expect(compareCategoryScores(5, 10)).toBe("b");
  });
  it("returns 'tie' when equal", () => {
    expect(compareCategoryScores(5, 5)).toBe("tie");
  });
});
```

- [ ] **Step 3: Run the tests**

Run: `cd gui && npm test`
Expected: 45/45 passing (28 existing + 17 new in `match.test.ts`), 4 test files.

- [ ] **Step 4: Commit**

```bash
git add gui/src/match.ts gui/src/match.test.ts
git commit -m "feat(gui): add pure MatchState model for solo/vs-computer matches"
```

---

### Task 2: Migrate `main.ts`/`render.ts` to `MatchState` (solo-only, behavior-preserving)

**Files:**
- Modify: `gui/src/render.ts` (imports, `renderScorecard`, `renderFinalTotal`)
- Modify: `gui/src/main.ts` (full internal migration from `GameState` to `MatchState`)

**Interfaces:**
- Consumes: `MatchState`, `Mode`, `initialMatchState`, `activeGameState`, `withActiveGameState`, `afterScore`, `isMatchComplete`, `matchWinner`, `compareCategoryScores` from Task 1's `match.ts`.
- Produces: `renderScorecard(containerId, state, opponent)` and `renderFinalTotal(match)` — new signatures every later task builds on.

- [ ] **Step 1: Update `render.ts`'s imports**

Replace:

```ts
import {
  GameState, CATEGORY_NAMES, NUM_CATEGORIES, bonusEarned, totalScore, isGameComplete,
} from "./state";
import { QueryResult } from "./parseResult";
```

with:

```ts
import {
  GameState, CATEGORY_NAMES, NUM_CATEGORIES, bonusEarned, totalScore,
} from "./state";
import { MatchState, isMatchComplete, matchWinner, compareCategoryScores } from "./match";
import { QueryResult } from "./parseResult";
```

- [ ] **Step 2: Replace `renderScorecard`**

Replace:

```ts
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
    <div class="score-row total-row"><strong>Upper total</strong><span>${state.upperTotal} (${bonusText})</span></div>
    <div class="score-row total-row"><strong>Grand total</strong><span>${totalScore(state)}</span></div>
  `;
}
```

with:

```ts
export function renderScorecard(containerId: string, state: GameState, opponent: GameState | null): void {
  const el = document.getElementById(containerId)!;
  const rows: string[] = [];
  for (let cat = 0; cat < NUM_CATEGORIES; cat++) {
    const score = state.categoryScores[cat];
    const cmp = opponent ? compareCategoryScores(score, opponent.categoryScores[cat]) : null;
    const highlightClass = cmp === "a" ? " score-leading" : cmp === "b" ? " score-trailing" : "";
    rows.push(
      `<div class="score-row${highlightClass}"><span>${CATEGORY_NAMES[cat]}</span><span>${score === null ? "—" : score}</span></div>`
    );
  }
  const bonusText = bonusEarned(state) ? "bonus earned (+50)" : `${state.upperTotal}/63 for bonus`;
  el.innerHTML = `
    ${rows.join("")}
    <div class="score-row total-row"><strong>Upper total</strong><span>${state.upperTotal} (${bonusText})</span></div>
    <div class="score-row total-row"><strong>Grand total</strong><span>${totalScore(state)}</span></div>
  `;
}
```

(`opponent` is always `null` until Task 4 adds a second scorecard call site, so `cmp` is always `null` and `highlightClass` is always `""` for now — no visible change yet.)

- [ ] **Step 3: Replace `renderFinalTotal`**

Replace:

```ts
export function renderFinalTotal(state: GameState): void {
  const el = document.getElementById("final-total")!;
  if (isGameComplete(state)) {
    el.hidden = false;
    el.textContent = `🎉 Game complete! Final score: ${totalScore(state)}`;
  } else {
    el.hidden = true;
  }
}
```

with:

```ts
export function renderFinalTotal(match: MatchState): void {
  const el = document.getElementById("final-total")!;
  if (!isMatchComplete(match)) {
    el.hidden = true;
    return;
  }
  el.hidden = false;
  if (match.mode === "solo") {
    el.textContent = `🎉 Game complete! Final score: ${totalScore(match.player)}`;
    return;
  }
  const playerTotal = totalScore(match.player);
  const computerTotal = totalScore(match.computer!);
  const winner = matchWinner(match);
  const verdict = winner === "tie" ? "It's a tie!" : winner === "player" ? "You win! 🎉" : "Computer wins.";
  el.textContent = `You: ${playerTotal} — Computer: ${computerTotal} — ${verdict}`;
}
```

(The `mode === "vsComputer"` branch is unreachable until Task 3 lets you actually start a vsComputer match — harmless to write now.)

- [ ] **Step 4: Rewrite `gui/src/main.ts`**

Replace the entire file with:

```ts
// main.ts — wires match state, sidecar calls, and rendering together.
import {
  initialGameState, setDice, advanceReroll, scoreCategory, applyHold, rollRemaining,
  allDiceValid,
} from "./state";
import {
  MatchState, initialMatchState, activeGameState, withActiveGameState, afterScore,
} from "./match";
import { getRecommendation, isEngineWarm } from "./sidecar";
import { QueryResult } from "./parseResult";
import {
  renderScorecard, renderRerollsIndicator, renderRecommendation, renderError,
  renderFinalTotal, renderDiceInputs, renderComputing, renderWarmUp,
} from "./render";

let match: MatchState = initialMatchState("solo");
let lastResult: QueryResult | null = null;

// Bumped on every state change so an in-flight query can detect that its
// result is stale (the state it was computed for no longer applies) before
// applying it — see maybeQuery.
let generation = 0;

function setMatch(next: MatchState): void {
  match = next;
  generation++;
}

function renderAll(): void {
  const active = activeGameState(match);
  renderScorecard("scorecard", match.player, match.mode === "vsComputer" ? match.computer : null);
  renderRerollsIndicator(active);
  renderFinalTotal(match);
  renderRecommendation(lastResult, handleScoreCategory, handleHold);
  renderDiceInputs(active.dice, handleDiceChange);
}

// Only one sidecar call runs at a time. Each solve is a real DP computation
// that can take minutes on a cold cache, so firing one per keystroke would
// pile up concurrent subprocesses. Edits that arrive while a call is in
// flight are coalesced into a single trailing re-query against the latest
// state once the current call finishes.
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
      const active = activeGameState(match);
      if (!allDiceValid(active.dice)) {
        lastResult = null;
        renderAll();
        continue;
      }
      renderComputing();
      try {
        const result = await getRecommendation(active, active.dice as number[], (level, total) => {
          if (generation === myGeneration) {
            renderComputing(`level ${level}/${total}`);
          }
        });
        // State moved on while this call was in flight (e.g. New Game) —
        // this result no longer applies. Force another pass so the next
        // iteration re-evaluates against the current state instead.
        if (generation !== myGeneration) {
          queryQueued = true;
          continue;
        }
        lastResult = result;
        renderError(null);
      } catch (err) {
        if (generation !== myGeneration) {
          queryQueued = true;
          continue;
        }
        lastResult = null;
        renderError(err instanceof Error ? err.message : String(err));
      }
      renderAll();
    } while (queryQueued);
  } finally {
    queryInFlight = false;
  }
}

function handleDiceChange(index: number, value: number | null): void {
  const active = activeGameState(match);
  const dice = [...active.dice];
  dice[index] = value;
  setMatch(withActiveGameState(match, setDice(active, dice)));
  void maybeQuery();
}

function handleScoreCategory(category: number, resultingScore: number): void {
  const active = activeGameState(match);
  const scored = scoreCategory(active, category, resultingScore);
  setMatch(afterScore(withActiveGameState(match, scored)));
  lastResult = null;
  renderAll();
}

function handleHold(holdValues: number[]): void {
  const active = activeGameState(match);
  setMatch(withActiveGameState(match, applyHold(active, holdValues)));
  lastResult = null;
  renderAll();
  // Holding all 5 dice leaves the roll unchanged and still valid, so this
  // cascades straight into the next decision (category options once
  // rerollsLeft hits 0) without waiting for new dice input.
  void maybeQuery();
}

function handleReroll(): void {
  const active = activeGameState(match);
  setMatch(withActiveGameState(match, advanceReroll(active)));
  lastResult = null;
  renderAll();
}

function handleRollRemaining(): void {
  const active = activeGameState(match);
  setMatch(withActiveGameState(match, setDice(active, rollRemaining(active.dice))));
  renderAll();
  void maybeQuery();
}

function handleNewGame(): void {
  setMatch(initialMatchState("solo"));
  lastResult = null;
  renderError(null);
  renderAll();
}

// Warming up runs through the same queryInFlight guard as a real dice query
// so the two never spawn overlapping (CPU-heavy, multi-threaded) sidecar
// solves against each other.
async function handleWarmUp(): Promise<void> {
  if (queryInFlight) return;
  queryInFlight = true;
  renderWarmUp("warming");
  try {
    await getRecommendation(initialGameState(), [1, 1, 1, 1, 1], (level, total) => {
      renderWarmUp("warming", `level ${level}/${total}`);
    });
    renderWarmUp("warm");
  } catch (err) {
    renderWarmUp("cold");
    renderError(err instanceof Error ? err.message : String(err));
  } finally {
    queryInFlight = false;
    if (queryQueued) void maybeQuery();
  }
}

document.getElementById("reroll-button")!.addEventListener("click", handleReroll);
document.getElementById("roll-remaining-button")!.addEventListener("click", handleRollRemaining);
document.getElementById("new-game-button")!.addEventListener("click", handleNewGame);
document.getElementById("warmup-button")!.addEventListener("click", () => void handleWarmUp());
renderAll();

renderWarmUp("checking");
void isEngineWarm().then((warm) => renderWarmUp(warm ? "warm" : "cold"));
```

- [ ] **Step 5: Typecheck and run the tests**

Run: `cd gui && npx tsc --noEmit`
Expected: no output (clean).

Run: `cd gui && npm test`
Expected: 45/45 passing (unchanged from Task 1 — this task touches no test file).

- [ ] **Step 6: Manually verify solo mode is unchanged**

Run: `cd gui && npm run dev`, open the local URL. Play through: click dice to set values, confirm the recommendation list appears once all 5 are set, click a hold option, confirm dice partially clear and rerolls-left decrements, continue to a category decision, click one, confirm the scorecard updates and a new turn starts. This must look and behave identically to before this task — it's a pure internal refactor.

- [ ] **Step 7: Commit**

```bash
git add gui/src/render.ts gui/src/main.ts
git commit -m "refactor(gui): migrate main.ts and render.ts to MatchState (solo-only)"
```

---

### Task 3: New Game mode-picker modal

**Files:**
- Modify: `gui/index.html` (add modal markup)
- Modify: `gui/src/styles.css` (append modal styles)
- Modify: `gui/src/main.ts` (replace `handleNewGame` with modal open/close/start-game flow)

**Interfaces:**
- Consumes: `Mode`, `initialMatchState` from `match.ts` (Task 1).
- Produces: nothing new for later tasks (New Game always reopens this modal going forward).

- [ ] **Step 1: Add the modal markup to `gui/index.html`**

Replace:

```html
    <script type="module" src="/src/main.ts"></script>
  </body>
</html>
```

with:

```html
    <div id="new-game-modal" class="modal-overlay" hidden>
      <div class="modal card">
        <h2 class="card-title">New Game</h2>
        <button id="mode-solo-button" class="btn btn-primary">Solo</button>
        <button id="mode-vs-computer-button" class="btn btn-primary">vs Computer</button>
      </div>
    </div>
    <script type="module" src="/src/main.ts"></script>
  </body>
</html>
```

- [ ] **Step 2: Append modal styles to `gui/src/styles.css`**

```css
.modal-overlay {
  position: fixed;
  inset: 0;
  background: rgba(22, 74, 46, 0.6);
  display: flex;
  align-items: center;
  justify-content: center;
  z-index: 10;
}

.modal {
  display: flex;
  flex-direction: column;
  align-items: stretch;
  gap: 0.5rem;
  min-width: 240px;
}
```

- [ ] **Step 3: Replace `handleNewGame` in `gui/src/main.ts`**

Replace:

```ts
function handleNewGame(): void {
  setMatch(initialMatchState("solo"));
  lastResult = null;
  renderError(null);
  renderAll();
}
```

with:

```ts
function openNewGameModal(): void {
  document.getElementById("new-game-modal")!.hidden = false;
}

function closeNewGameModal(): void {
  document.getElementById("new-game-modal")!.hidden = true;
}

function startNewGame(mode: Mode): void {
  setMatch(initialMatchState(mode));
  lastResult = null;
  renderError(null);
  renderAll();
  closeNewGameModal();
}
```

- [ ] **Step 4: Update the import and event-listener wiring in `gui/src/main.ts`**

Replace:

```ts
import {
  MatchState, initialMatchState, activeGameState, withActiveGameState, afterScore,
} from "./match";
```

with:

```ts
import {
  MatchState, Mode, initialMatchState, activeGameState, withActiveGameState, afterScore,
} from "./match";
```

Replace:

```ts
document.getElementById("reroll-button")!.addEventListener("click", handleReroll);
document.getElementById("roll-remaining-button")!.addEventListener("click", handleRollRemaining);
document.getElementById("new-game-button")!.addEventListener("click", handleNewGame);
document.getElementById("warmup-button")!.addEventListener("click", () => void handleWarmUp());
```

with:

```ts
document.getElementById("reroll-button")!.addEventListener("click", handleReroll);
document.getElementById("roll-remaining-button")!.addEventListener("click", handleRollRemaining);
document.getElementById("new-game-button")!.addEventListener("click", openNewGameModal);
document.getElementById("mode-solo-button")!.addEventListener("click", () => startNewGame("solo"));
document.getElementById("mode-vs-computer-button")!.addEventListener("click", () => startNewGame("vsComputer"));
document.getElementById("warmup-button")!.addEventListener("click", () => void handleWarmUp());
```

- [ ] **Step 5: Typecheck and run the tests**

Run: `cd gui && npx tsc --noEmit` — expect clean.
Run: `cd gui && npm test` — expect 45/45 passing.

- [ ] **Step 6: Manually verify**

Run: `cd gui && npm run dev`, open the local URL. Click "New Game" — expect a centered modal overlay with "Solo" and "vs Computer" buttons, background dimmed. Click "Solo" — modal closes, a fresh solo game starts (scorecard reset, dice cleared). Click "New Game" again, click "vs Computer" — modal closes; the app doesn't yet show a computer scorecard or turn indicator (that's Task 4/6), but confirm no errors in the console and the player's own scorecard/dice still work normally.

- [ ] **Step 7: Commit**

```bash
git add gui/index.html gui/src/styles.css gui/src/main.ts
git commit -m "feat(gui): add New Game mode-picker modal (Solo / vs Computer)"
```

---

### Task 4: Dual scorecard rendering + category-leader highlighting

**Files:**
- Modify: `gui/index.html` (wrap scorecard in a row, add computer's scorecard card, add ids)
- Modify: `gui/src/styles.css` (modify `.scorecard-card`, append row/highlight styles)
- Modify: `gui/src/render.ts` (add `renderLayoutMode`, add `Mode` to the match.ts import)
- Modify: `gui/src/main.ts` (`renderAll` renders both scorecards and the layout mode in vsComputer mode)

**Interfaces:**
- Consumes: `Mode`, `compareCategoryScores` (already imported in Task 2) from `match.ts`.
- Produces: `renderLayoutMode(mode)`, consumed by `renderAll` from here on.

- [ ] **Step 1: Restructure the scorecard area in `gui/index.html`**

Replace:

```html
      <main class="game-layout">
        <section id="scorecard-card" class="card scorecard-card">
          <h2 class="card-title">Scorecard</h2>
          <section id="scorecard"></section>
        </section>
        <section id="turn-card" class="card turn-card">
```

with:

```html
      <main id="game-layout" class="game-layout">
        <div id="scorecards-row" class="scorecards-row">
          <section id="scorecard-card" class="card scorecard-card">
            <h2 id="scorecard-title" class="card-title">Scorecard</h2>
            <section id="scorecard"></section>
          </section>
          <section id="computer-scorecard-card" class="card scorecard-card" hidden>
            <h2 class="card-title">Computer</h2>
            <section id="computer-scorecard"></section>
          </section>
        </div>
        <section id="turn-card" class="card turn-card">
```

- [ ] **Step 2: Adjust `.scorecard-card`'s flex-basis and append new styles to `gui/src/styles.css`**

Replace:

```css
.scorecard-card {
  flex: 1 1 320px;
}
```

with:

```css
.scorecard-card {
  flex: 1 1 220px;
}
```

Append:

```css
.scorecards-row {
  display: flex;
  gap: 1rem;
  flex: 1 1 320px;
}

.game-layout.vs-computer .scorecards-row {
  flex: 1 1 100%;
}

.score-row.score-leading {
  background: var(--gold-bg);
  border-radius: 4px;
}

.score-row.score-trailing {
  opacity: 0.7;
}
```

- [ ] **Step 3: Add `renderLayoutMode` to `gui/src/render.ts`**

Replace:

```ts
import { MatchState, isMatchComplete, matchWinner, compareCategoryScores } from "./match";
```

with:

```ts
import { MatchState, Mode, isMatchComplete, matchWinner, compareCategoryScores } from "./match";
```

Add, after `renderFinalTotal`:

```ts

export function renderLayoutMode(mode: Mode): void {
  document.getElementById("computer-scorecard-card")!.hidden = mode !== "vsComputer";
  document.getElementById("game-layout")!.classList.toggle("vs-computer", mode === "vsComputer");
  document.getElementById("scorecard-title")!.textContent = mode === "vsComputer" ? "You" : "Scorecard";
}
```

- [ ] **Step 4: Update `renderAll` in `gui/src/main.ts`**

Replace:

```ts
function renderAll(): void {
  const active = activeGameState(match);
  renderScorecard("scorecard", match.player, match.mode === "vsComputer" ? match.computer : null);
  renderRerollsIndicator(active);
  renderFinalTotal(match);
  renderRecommendation(lastResult, handleScoreCategory, handleHold);
  renderDiceInputs(active.dice, handleDiceChange);
}
```

with:

```ts
function renderAll(): void {
  const active = activeGameState(match);
  renderLayoutMode(match.mode);
  renderScorecard("scorecard", match.player, match.mode === "vsComputer" ? match.computer : null);
  if (match.mode === "vsComputer") {
    renderScorecard("computer-scorecard", match.computer!, match.player);
  }
  renderRerollsIndicator(active);
  renderFinalTotal(match);
  renderRecommendation(lastResult, handleScoreCategory, handleHold);
  renderDiceInputs(active.dice, handleDiceChange);
}
```

And update the `render` import to add `renderLayoutMode`:

Replace:

```ts
import {
  renderScorecard, renderRerollsIndicator, renderRecommendation, renderError,
  renderFinalTotal, renderDiceInputs, renderComputing, renderWarmUp,
} from "./render";
```

with:

```ts
import {
  renderScorecard, renderRerollsIndicator, renderRecommendation, renderError,
  renderFinalTotal, renderDiceInputs, renderComputing, renderWarmUp, renderLayoutMode,
} from "./render";
```

- [ ] **Step 5: Typecheck and run the tests**

Run: `cd gui && npx tsc --noEmit` — expect clean.
Run: `cd gui && npm test` — expect 45/45 passing.

- [ ] **Step 6: Manually verify**

Run: `cd gui && npm run dev`, open the local URL. Confirm solo mode still shows a single scorecard beside the turn panel, unchanged. Click New Game → vs Computer: confirm two scorecard cards appear side by side above the turn panel (which now spans the full width below them), the left card's title reads "You", the right reads "Computer". Full highlight-color verification (both cards actually filling in) isn't possible yet — that requires Task 6's computer turn — but confirm no layout breakage or console errors.

- [ ] **Step 7: Commit**

```bash
git add gui/index.html gui/src/styles.css gui/src/render.ts gui/src/main.ts
git commit -m "feat(gui): render both scorecards side by side in vs-Computer mode"
```

---

### Task 5: Settings panel (show-hints toggle + computer-pacing select)

**Files:**
- Modify: `gui/index.html` (header actions wrapper, settings gear button, settings panel)
- Modify: `gui/src/styles.css` (append header/settings styles)
- Modify: `gui/src/main.ts` (`showHints`/`computerPacing` state, gating `renderRecommendation`, wiring)

**Interfaces:**
- Produces: `showHints: boolean` and `computerPacing: "instant" | "step"` module state in `main.ts`, read by Task 6/7's computer-turn logic.

- [ ] **Step 1: Add the settings UI to `gui/index.html`**

Replace:

```html
      <header class="app-header">
        <h1 class="app-title">🎲 Yatzy Assistant</h1>
        <button id="new-game-button" class="btn btn-primary">New Game</button>
      </header>
      <main id="game-layout" class="game-layout">
```

with:

```html
      <header class="app-header">
        <h1 class="app-title">🎲 Yatzy Assistant</h1>
        <div class="header-actions">
          <button id="settings-button" class="btn btn-icon" aria-label="Settings">⚙️</button>
          <button id="new-game-button" class="btn btn-primary">New Game</button>
        </div>
      </header>
      <div id="settings-panel" class="settings-panel card" hidden>
        <label class="settings-row">
          <input type="checkbox" id="show-hints-toggle" checked />
          Show optimal move suggestions
        </label>
        <label class="settings-row">
          <span>Computer plays:</span>
          <select id="computer-pacing-select">
            <option value="instant">Instantly</option>
            <option value="step">Step-by-step</option>
          </select>
        </label>
      </div>
      <main id="game-layout" class="game-layout">
```

- [ ] **Step 2: Append header/settings styles to `gui/src/styles.css`**

```css
.header-actions {
  display: flex;
  gap: 8px;
}

.btn-icon {
  background: var(--card-bg);
  color: var(--text);
  padding: 8px 10px;
}

.btn-icon:hover {
  filter: brightness(0.97);
}

.settings-panel {
  display: flex;
  flex-direction: column;
  gap: 8px;
  margin-bottom: 1rem;
}

.settings-row {
  display: flex;
  align-items: center;
  gap: 8px;
  font-size: 0.9rem;
}
```

- [ ] **Step 3: Add settings state and gate `renderRecommendation` in `gui/src/main.ts`**

Replace:

```ts
let match: MatchState = initialMatchState("solo");
let lastResult: QueryResult | null = null;
```

with:

```ts
let match: MatchState = initialMatchState("solo");
let lastResult: QueryResult | null = null;
let showHints = true;
let computerPacing: "instant" | "step" = "instant";
```

Replace:

```ts
function renderAll(): void {
  const active = activeGameState(match);
  renderLayoutMode(match.mode);
  renderScorecard("scorecard", match.player, match.mode === "vsComputer" ? match.computer : null);
  if (match.mode === "vsComputer") {
    renderScorecard("computer-scorecard", match.computer!, match.player);
  }
  renderRerollsIndicator(active);
  renderFinalTotal(match);
  renderRecommendation(lastResult, handleScoreCategory, handleHold);
  renderDiceInputs(active.dice, handleDiceChange);
}
```

with:

```ts
function renderAll(): void {
  const active = activeGameState(match);
  renderLayoutMode(match.mode);
  renderScorecard("scorecard", match.player, match.mode === "vsComputer" ? match.computer : null);
  if (match.mode === "vsComputer") {
    renderScorecard("computer-scorecard", match.computer!, match.player);
  }
  renderRerollsIndicator(active);
  renderFinalTotal(match);
  if (showHints) {
    renderRecommendation(lastResult, handleScoreCategory, handleHold);
  } else {
    renderRecommendation(null, () => {}, () => {});
  }
  renderDiceInputs(active.dice, handleDiceChange);
}
```

- [ ] **Step 4: Wire the settings controls**

Replace:

```ts
document.getElementById("warmup-button")!.addEventListener("click", () => void handleWarmUp());
```

with:

```ts
document.getElementById("warmup-button")!.addEventListener("click", () => void handleWarmUp());
document.getElementById("settings-button")!.addEventListener("click", () => {
  const panel = document.getElementById("settings-panel")!;
  panel.hidden = !panel.hidden;
});
document.getElementById("show-hints-toggle")!.addEventListener("change", (event) => {
  showHints = (event.target as HTMLInputElement).checked;
  renderAll();
});
document.getElementById("computer-pacing-select")!.addEventListener("change", (event) => {
  computerPacing = (event.target as HTMLSelectElement).value as "instant" | "step";
});
```

- [ ] **Step 5: Typecheck and run the tests**

Run: `cd gui && npx tsc --noEmit` — expect clean.
Run: `cd gui && npm test` — expect 45/45 passing.

- [ ] **Step 6: Manually verify**

Run: `cd gui && npm run dev`, open the local URL. Click the ⚙️ icon — a panel appears with the two controls. In solo mode, set some dice so a recommendation list appears; uncheck "Show optimal move suggestions" — the list disappears immediately; re-check it — it reappears with the same result (no re-query). Changing the "Computer plays" select doesn't do anything visible yet (Task 6/7 consume it) — just confirm it doesn't error.

- [ ] **Step 7: Commit**

```bash
git add gui/index.html gui/src/styles.css gui/src/main.ts
git commit -m "feat(gui): add settings panel with hint-visibility and computer-pacing toggles"
```

---

### Task 6: Computer's turn — instant pacing, turn indicator, winner banner

**Files:**
- Modify: `gui/index.html` (turn indicator, computer-turn button/status, `id` on turn-actions)
- Modify: `gui/src/styles.css` (append turn-indicator/computer-status styles)
- Modify: `gui/src/render.ts` (`renderTurnIndicator`, `renderTurnControls`, `renderComputerTurnStatus`, `renderComputerTurnButtonLabel`; add `Turn` to the match.ts import)
- Modify: `gui/src/main.ts` (the computer-turn instant-play loop, wiring)

**Interfaces:**
- Consumes: `Turn`, `isMatchComplete` from `match.ts`; `getRecommendation`, `rollRemaining`, `applyHold`, `scoreCategory`, `setDice`, `afterScore`, `withActiveGameState` (all already available).
- Produces: `renderTurnControls(turn)` accepting `Turn | null` (`null` = match over, hide all turn controls) — consumed by Task 7 unchanged.

- [ ] **Step 1: Add turn-indicator and computer-turn elements to `gui/index.html`**

Replace:

```html
      </div>
      <main id="game-layout" class="game-layout">
```

with:

```html
      </div>
      <div id="turn-indicator" class="turn-indicator" hidden></div>
      <main id="game-layout" class="game-layout">
```

(This targets the `</div>` that closes `#settings-panel` from Task 5, immediately before `<main id="game-layout" ...>`.)

Replace:

```html
          <div class="turn-actions">
            <button id="roll-remaining-button" class="btn btn-primary">🎲 Roll remaining dice</button>
            <div id="rerolls-indicator" class="rerolls-dots"></div>
          </div>
          <button id="reroll-button" class="btn btn-primary" disabled>Reroll →</button>
          <div id="recommendation" class="recommendation-list"></div>
```

with:

```html
          <div id="turn-actions" class="turn-actions">
            <button id="roll-remaining-button" class="btn btn-primary">🎲 Roll remaining dice</button>
            <div id="rerolls-indicator" class="rerolls-dots"></div>
          </div>
          <button id="reroll-button" class="btn btn-primary" disabled>Reroll →</button>
          <button id="computer-turn-button" class="btn btn-primary" hidden>Play Computer's Turn</button>
          <div id="computer-turn-status" class="computer-turn-status" hidden></div>
          <div id="recommendation" class="recommendation-list"></div>
```

- [ ] **Step 2: Append styles to `gui/src/styles.css`**

```css
.turn-indicator {
  font-weight: 700;
  text-align: center;
}

.computer-turn-status {
  font-size: 0.9rem;
  margin: 8px 0;
}
```

- [ ] **Step 3: Add the new render functions to `gui/src/render.ts`**

Replace:

```ts
import { MatchState, Mode, isMatchComplete, matchWinner, compareCategoryScores } from "./match";
```

with:

```ts
import { MatchState, Mode, Turn, isMatchComplete, matchWinner, compareCategoryScores } from "./match";
```

Add, after `renderLayoutMode`:

```ts

export function renderTurnIndicator(match: MatchState): void {
  const el = document.getElementById("turn-indicator")!;
  if (match.mode === "solo" || isMatchComplete(match)) {
    el.hidden = true;
    return;
  }
  el.hidden = false;
  el.textContent = match.turn === "player" ? "Your turn" : "Computer's turn";
}

export function renderTurnControls(turn: Turn | null): void {
  const isPlayerTurn = turn === "player";
  const isComputerTurn = turn === "computer";
  document.getElementById("dice-inputs")!.hidden = !isPlayerTurn;
  document.getElementById("turn-actions")!.hidden = !isPlayerTurn;
  (document.getElementById("reroll-button") as HTMLButtonElement).hidden = !isPlayerTurn;
  (document.getElementById("computer-turn-button") as HTMLButtonElement).hidden = !isComputerTurn;
  document.getElementById("computer-turn-status")!.hidden = !isComputerTurn;
  if (!isPlayerTurn) {
    (document.getElementById("warmup-button") as HTMLButtonElement).hidden = true;
    document.getElementById("warmup-status")!.hidden = true;
  }
}

export function renderComputerTurnStatus(text: string): void {
  document.getElementById("computer-turn-status")!.textContent = text;
}

export function renderComputerTurnButtonLabel(pacing: "instant" | "step"): void {
  const button = document.getElementById("computer-turn-button") as HTMLButtonElement;
  button.textContent = pacing === "instant" ? "Play Computer's Turn" : "Next →";
}
```

- [ ] **Step 4: Wire the computer's instant turn in `gui/src/main.ts`**

Replace:

```ts
import {
  MatchState, Mode, initialMatchState, activeGameState, withActiveGameState, afterScore,
} from "./match";
```

with:

```ts
import {
  MatchState, Mode, initialMatchState, activeGameState, withActiveGameState, afterScore, isMatchComplete,
} from "./match";
```

Replace:

```ts
import {
  renderScorecard, renderRerollsIndicator, renderRecommendation, renderError,
  renderFinalTotal, renderDiceInputs, renderComputing, renderWarmUp, renderLayoutMode,
} from "./render";
```

with:

```ts
import {
  renderScorecard, renderRerollsIndicator, renderRecommendation, renderError,
  renderFinalTotal, renderDiceInputs, renderComputing, renderWarmUp, renderLayoutMode,
  renderTurnIndicator, renderTurnControls, renderComputerTurnStatus, renderComputerTurnButtonLabel,
} from "./render";
```

Replace:

```ts
function renderAll(): void {
  const active = activeGameState(match);
  renderLayoutMode(match.mode);
  renderScorecard("scorecard", match.player, match.mode === "vsComputer" ? match.computer : null);
  if (match.mode === "vsComputer") {
    renderScorecard("computer-scorecard", match.computer!, match.player);
  }
  renderRerollsIndicator(active);
  renderFinalTotal(match);
  if (showHints) {
    renderRecommendation(lastResult, handleScoreCategory, handleHold);
  } else {
    renderRecommendation(null, () => {}, () => {});
  }
  renderDiceInputs(active.dice, handleDiceChange);
}
```

with:

```ts
function renderAll(): void {
  const active = activeGameState(match);
  renderLayoutMode(match.mode);
  renderScorecard("scorecard", match.player, match.mode === "vsComputer" ? match.computer : null);
  if (match.mode === "vsComputer") {
    renderScorecard("computer-scorecard", match.computer!, match.player);
  }
  renderRerollsIndicator(active);
  renderFinalTotal(match);
  renderTurnIndicator(match);
  const controlsTurn = match.mode === "vsComputer" && isMatchComplete(match) ? null : match.turn;
  renderTurnControls(controlsTurn);
  renderComputerTurnButtonLabel(computerPacing);
  if (showHints) {
    renderRecommendation(lastResult, handleScoreCategory, handleHold);
  } else {
    renderRecommendation(null, () => {}, () => {});
  }
  renderDiceInputs(active.dice, handleDiceChange);
}
```

Add, after `handleWarmUp`'s closing brace:

```ts

// Instant pacing: runs the computer's entire turn to completion behind one
// click, only surfacing solve progress (via the same onProgress plumbing
// the human's turn already uses) — no per-roll narration until the final
// outcome. Returns null if the match moved on (e.g. New Game) mid-turn, in
// which case the caller must not touch match/UI state.
async function playComputerTurnInstant(myGeneration: number): Promise<string | null> {
  let active = activeGameState(match);
  for (;;) {
    const dice = rollRemaining(active.dice) as number[];
    renderComputerTurnStatus("Computer is thinking…");
    const result = await getRecommendation(active, dice, (level, total) => {
      if (generation === myGeneration) {
        renderComputerTurnStatus(`Computer is thinking… (level ${level}/${total})`);
      }
    });
    if (generation !== myGeneration) return null;
    if (result.isRerollDecision) {
      const best = result.rerollOptions[0];
      active = applyHold(setDice(active, dice), best.holdValues);
      continue;
    }
    const best = result.categoryOptions[0];
    active = scoreCategory(active, best.category, best.resultingScore);
    setMatch(afterScore(withActiveGameState(match, active)));
    return `Computer rolled [${dice.join(",")}] and scored ${best.categoryName} for ${best.resultingScore} points.`;
  }
}

async function handleComputerTurnInstant(): Promise<void> {
  if (queryInFlight) return;
  queryInFlight = true;
  const myGeneration = generation;
  try {
    const summary = await playComputerTurnInstant(myGeneration);
    if (summary !== null) {
      renderComputerTurnStatus(summary);
      renderAll();
    }
  } catch (err) {
    renderError(err instanceof Error ? err.message : String(err));
  } finally {
    queryInFlight = false;
    if (queryQueued) void maybeQuery();
  }
}
```

Replace:

```ts
document.getElementById("computer-pacing-select")!.addEventListener("change", (event) => {
  computerPacing = (event.target as HTMLSelectElement).value as "instant" | "step";
});
```

with:

```ts
document.getElementById("computer-pacing-select")!.addEventListener("change", (event) => {
  computerPacing = (event.target as HTMLSelectElement).value as "instant" | "step";
  renderAll();
});
document.getElementById("computer-turn-button")!.addEventListener("click", () => void handleComputerTurnInstant());
```

- [ ] **Step 5: Typecheck and run the tests**

Run: `cd gui && npx tsc --noEmit`
Expected: clean.

Run: `cd gui && npm test` — expect 45/45 passing.

- [ ] **Step 6: Manually verify a full vs-Computer match (instant pacing)**

Run: `cd gui && npm run dev`, open the local URL. New Game → vs Computer. Confirm "Your turn" shows above the board. Play your own turn to a category score. Confirm it switches to "Computer's turn", your dice/roll/reroll controls hide, and a "Play Computer's Turn" button appears. Click it — confirm "Computer is thinking…" shows (with live level/total if it's a cold solve), then a summary line ("Computer rolled [...] and scored ... for ... points"), the computer's scorecard updates, and it switches back to "Your turn". Repeat until both scorecards are full (15 rounds each) and confirm the final banner shows both totals and a winner (or tie) instead of the solo-style message. Note: full category-leader color highlighting is best confirmed here too, now that both scorecards can actually fill in.

- [ ] **Step 7: Commit**

```bash
git add gui/index.html gui/src/styles.css gui/src/render.ts gui/src/main.ts
git commit -m "feat(gui): computer's turn (instant pacing), turn indicator, winner banner"
```

---

### Task 7: Computer's turn — step-by-step pacing

**Files:**
- Modify: `gui/src/main.ts` (`describeHold`, `computerStepActive` state, `handleComputerTurnStep`, pacing-based dispatch, reset on New Game)

**Interfaces:**
- Consumes: everything from Task 6 (`renderComputerTurnStatus`, `renderRecommendation`, `activeGameState`, etc.) plus `showHints`/`computerPacing` from Task 5.
- Produces: nothing new for later tasks (this is the last behavioral task).

- [ ] **Step 1: Add `describeHold` and `computerStepActive` state to `gui/src/main.ts`**

Replace:

```ts
import {
  initialGameState, setDice, advanceReroll, scoreCategory, applyHold, rollRemaining,
  allDiceValid,
} from "./state";
```

with:

```ts
import {
  GameState, initialGameState, setDice, advanceReroll, scoreCategory, applyHold, rollRemaining,
  allDiceValid,
} from "./state";
```

Replace:

```ts
let match: MatchState = initialMatchState("solo");
let lastResult: QueryResult | null = null;
let showHints = true;
let computerPacing: "instant" | "step" = "instant";
```

with:

```ts
let match: MatchState = initialMatchState("solo");
let lastResult: QueryResult | null = null;
let showHints = true;
let computerPacing: "instant" | "step" = "instant";
// Holds the computer's in-progress GameState between clicks of the "Next →"
// button in step-by-step pacing; null when no step-mode turn is underway.
let computerStepActive: GameState | null = null;
```

Add, immediately after `playComputerTurnInstant`'s closing brace (before `handleComputerTurnInstant`):

```ts

function describeHold(holdValues: number[], expectedValue: number): string {
  const stopNote = holdValues.length === 5 ? " (stop rerolling)" : "";
  return `Hold [${holdValues.join(",")}]${stopNote} — expected value ${expectedValue.toFixed(2)}`;
}
```

- [ ] **Step 2: Add `handleComputerTurnStep` and the pacing dispatcher**

Add, after `handleComputerTurnInstant`'s closing brace:

```ts

// Step-by-step pacing: one roll/hold/reroll decision per click. In-progress
// state between clicks lives in computerStepActive; cleared once the turn
// ends (category scored) or a new game starts.
async function handleComputerTurnStep(): Promise<void> {
  if (queryInFlight) return;
  queryInFlight = true;
  const myGeneration = generation;
  try {
    const active = computerStepActive ?? activeGameState(match);
    const dice = rollRemaining(active.dice) as number[];
    renderComputerTurnStatus("Computer is thinking…");
    const result = await getRecommendation(active, dice, (level, total) => {
      if (generation === myGeneration) {
        renderComputerTurnStatus(`Computer is thinking… (level ${level}/${total})`);
      }
    });
    if (generation !== myGeneration) return;
    if (showHints) {
      renderRecommendation(result, () => {}, () => {});
    }
    if (result.isRerollDecision) {
      const best = result.rerollOptions[0];
      computerStepActive = applyHold(setDice(active, dice), best.holdValues);
      renderComputerTurnStatus(
        `Rolled [${dice.join(",")}] — ${describeHold(best.holdValues, best.expectedValue)}. Click Next to continue.`
      );
    } else {
      const best = result.categoryOptions[0];
      const scored = scoreCategory(active, best.category, best.resultingScore);
      computerStepActive = null;
      setMatch(afterScore(withActiveGameState(match, scored)));
      renderComputerTurnStatus(`Rolled [${dice.join(",")}] — scored ${best.categoryName} for ${best.resultingScore} points.`);
      renderAll();
    }
  } catch (err) {
    renderError(err instanceof Error ? err.message : String(err));
  } finally {
    queryInFlight = false;
    if (queryQueued) void maybeQuery();
  }
}

function handleComputerTurnButton(): void {
  if (computerPacing === "instant") {
    void handleComputerTurnInstant();
  } else {
    void handleComputerTurnStep();
  }
}
```

- [ ] **Step 3: Dispatch on pacing instead of always calling instant, and reset step state on New Game**

Replace:

```ts
document.getElementById("computer-turn-button")!.addEventListener("click", () => void handleComputerTurnInstant());
```

with:

```ts
document.getElementById("computer-turn-button")!.addEventListener("click", handleComputerTurnButton);
```

Replace:

```ts
function startNewGame(mode: Mode): void {
  setMatch(initialMatchState(mode));
  lastResult = null;
  renderError(null);
  renderAll();
  closeNewGameModal();
}
```

with:

```ts
function startNewGame(mode: Mode): void {
  setMatch(initialMatchState(mode));
  lastResult = null;
  computerStepActive = null;
  renderError(null);
  renderAll();
  closeNewGameModal();
}
```

- [ ] **Step 4: Typecheck and run the tests**

Run: `cd gui && npx tsc --noEmit` — expect clean.
Run: `cd gui && npm test` — expect 45/45 passing.

- [ ] **Step 5: Manually verify step-by-step pacing**

Run: `cd gui && npm run dev`, open the local URL. New Game → vs Computer, open settings (⚙️), set "Computer plays" to "Step-by-step". Play your turn to a category. On the computer's turn, confirm the button now reads "Next →"; click it once — confirm it shows "Computer is thinking…", then a single roll/hold line ("Rolled [...] — Hold [...] — expected value ...") and (if hints are on) the recommendation list for that step, non-interactive in effect (clicking a card does nothing). Click "Next →" again — confirm it advances to the next roll/hold or to a final "scored ... for ... points" line, after which control returns to "Your turn". Toggle hints off mid-computer-turn and confirm the recommendation list stops appearing for subsequent steps. Also re-confirm instant pacing still works (switch the select back to "Instantly" and play another computer turn).

- [ ] **Step 6: Commit**

```bash
git add gui/src/main.ts
git commit -m "feat(gui): add step-by-step pacing for the computer's turn"
```

---

### Task 8: Documentation update

**Files:**
- Modify: `gui/README.md`

**Interfaces:** none.

- [ ] **Step 1: Update `gui/README.md`**

Replace:

```markdown
4. "New Game" resets the scorecard at any point.

The very first query in a session solves the DP fresh (a couple of minutes);
after that it's loaded from a cached table (`yatzy_cpu_dp.bin`, ~8MB) in
Tauri's app-data directory, so it's near-instant on every later query and
every future app launch.

## Rebuilding the sidecar
```

with:

```markdown
4. "New Game" opens a Solo / vs Computer picker before starting a fresh
   scorecard.

The very first query in a session solves the DP fresh (a couple of minutes);
after that it's loaded from a cached table (`yatzy_cpu_dp.bin`, ~8MB) in
Tauri's app-data directory, so it's near-instant on every later query and
every future app launch.

## Playing vs Computer

In vs Computer mode you always go first. After you score a category, the
computer takes its turn automatically — click "Play Computer's Turn" (or
"Next →" in step-by-step mode) to let it play. It always plays the
top-ranked (optimal) move; there's no adjustable difficulty. Once both
scorecards have a score in the same category, the higher one is highlighted
gold, and the match ends with a banner showing both totals and the winner
(or a tie).

The ⚙️ settings icon next to "New Game" opens a panel with two toggles:
hiding the recommendation list if you'd rather play without hints, and
switching the computer between playing its turn instantly or one
roll/hold/reroll decision at a time ("Step-by-step").

## Rebuilding the sidecar
```

- [ ] **Step 2: Commit**

```bash
git add gui/README.md
git commit -m "docs: document vs-Computer mode and settings in gui/README.md"
```

---

### Task 9: Full manual playthrough on Windows

**Files:** none (verification only).

**Interfaces:** none.

- [ ] **Step 1: Sync to the Windows build-staging folder**

Run (from the repo root): `bash gui/scripts/sync-to-windows.sh`

- [ ] **Step 2: Launch the dev build on Windows**

From Windows PowerShell (or `powershell.exe` from WSL):

```powershell
cd C:\Users\<you>\yatzy-gui-build
npm install
npm run tauri dev
```

- [ ] **Step 3: Play a full vs-Computer match end to end, checking every new element**

Checklist:
- New Game modal appears, styled consistently with the rest of the app; both Solo and vs Computer start correctly.
- Solo mode is completely unchanged from before this plan.
- In vs Computer mode: turn indicator correctly alternates "Your turn" / "Computer's turn"; your controls hide during the computer's turn and vice versa.
- Instant pacing: one click resolves the computer's whole turn, showing a sensible final summary.
- Step-by-step pacing: each click advances exactly one roll/hold/reroll or the final score; recommendation list (if hints on) shows the computer's current option ranked list without being clickable-meaningful.
- Hint toggle hides/shows the recommendation list immediately, for both your turn and the computer's step display.
- Category cells highlight gold once both sides have a score there; the higher score is the one highlighted.
- Winner banner at match end shows both totals and the correct winner/tie.
- New Game mid-match correctly abandons the in-progress state (including mid-computer-turn) without leaving stale UI.

- [ ] **Step 4: Report outcome**

If everything checks out, this plan is complete. If any issue is found, fix it in the relevant earlier task's files before considering the feature done.
