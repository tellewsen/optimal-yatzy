# Yatzy GUI Visual Redesign Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Restyle the Yatzy desktop GUI from unstyled default browser controls into a playful "warm felt-table" two-column layout with clickable pip dice, per [2026-07-13-yatzy-gui-visual-redesign-design.md](../specs/2026-07-13-yatzy-gui-visual-redesign-design.md).

**Architecture:** Pure rendering/markup/CSS change. `gui/index.html` and `gui/src/styles.css` carry the new visual structure; `gui/src/render.ts`'s exported functions are rewritten to emit the new markup but keep their existing signatures, so `gui/src/main.ts`'s wiring (`renderAll`, `handleDiceChange`, etc.) needs zero changes. `state.ts`, `sidecar.ts`, `parseResult.ts`, `cliArgs.ts` and their tests are untouched.

**Tech Stack:** Vanilla TypeScript, HTML, CSS (no framework), Vite dev server, Vitest for the existing unit tests.

## Global Constraints

- No network font loading — the app must work fully offline (Tauri desktop app). Use the system font stack `"Segoe UI Rounded", "SF Pro Rounded", system-ui, sans-serif`.
- Do not modify `gui/src/state.ts`, `gui/src/sidecar.ts`, `gui/src/parseResult.ts`, `gui/src/cliArgs.ts`, or their existing test files (`*.test.ts`) — this redesign is rendering/markup/CSS only.
- Preserve every existing element `id` that `main.ts` looks up via `getElementById` (`scorecard`, `dice-inputs`, `roll-remaining-button`, `rerolls-indicator`, `reroll-button`, `recommendation`, `error-banner`, `final-total`, `new-game-button`) so `main.ts` requires no changes.
- Preserve the exact text content of recommendation-option rows and the reroll/category button click behavior — only the surrounding markup/styling changes (per spec: "same click behavior and same text content as today").
- Target window is 800×600 (`gui/src-tauri/tauri.conf.json`) — design for that, with graceful flex-wrap below it.
- After every task: run `npm test` (from `gui/`) to confirm the existing unit tests (`cliArgs.test.ts`, `parseResult.test.ts`, `state.test.ts`) still pass unmodified — they don't touch any file this plan changes, so this is a regression guard, not new coverage.

---

### Task 1: Page shell & theme foundation

**Files:**
- Modify: `gui/index.html` (full rewrite)
- Modify: `gui/src/styles.css` (full rewrite — foundation rules only; later tasks append)

**Interfaces:**
- Produces: the container structure and element `id`s every later task's CSS/render.ts changes target (`#scorecard-card`, `#turn-card`, `.card`, `.btn`, `.btn-primary`, plus all the pre-existing `id`s listed in Global Constraints, unchanged).
- Consumes: nothing (first task).

- [ ] **Step 1: Rewrite `gui/index.html`**

```html
<!doctype html>
<html lang="en">
  <head>
    <meta charset="UTF-8" />
    <link rel="stylesheet" href="/src/styles.css" />
    <meta name="viewport" content="width=device-width, initial-scale=1.0" />
    <title>Yatzy Assistant</title>
  </head>

  <body>
    <div id="app">
      <header class="app-header">
        <h1 class="app-title">🎲 Yatzy Assistant</h1>
        <button id="new-game-button" class="btn btn-primary">New Game</button>
      </header>
      <main class="game-layout">
        <section id="scorecard-card" class="card scorecard-card">
          <h2 class="card-title">Scorecard</h2>
          <section id="scorecard"></section>
        </section>
        <section id="turn-card" class="card turn-card">
          <h2 class="card-title">This Turn</h2>
          <div id="dice-inputs" class="dice-row"></div>
          <div class="turn-actions">
            <button id="roll-remaining-button" class="btn btn-primary">🎲 Roll remaining dice</button>
            <div id="rerolls-indicator" class="rerolls-dots"></div>
          </div>
          <button id="reroll-button" class="btn btn-primary" disabled>Reroll →</button>
          <div id="recommendation" class="recommendation-list"></div>
          <div id="error-banner" class="error-banner" hidden></div>
        </section>
      </main>
      <div id="final-total" class="final-banner" hidden></div>
    </div>
    <script type="module" src="/src/main.ts"></script>
  </body>
</html>
```

- [ ] **Step 2: Rewrite `gui/src/styles.css`**

```css
:root {
  --felt: #1b5e3a;
  --felt-dark: #164a2e;
  --card-bg: #fdf8ec;
  --card-border: #e6dcc3;
  --text: #2b2016;
  --text-on-felt: #fdf8ec;
  --primary: #b3273e;
  --primary-text: #fdf8ec;
  --gold: #d4a017;
  --gold-bg: #fbf0d4;
  --error-bg: #b00020;
  --dot-filled: #2b2016;
  --dot-empty: #cfc4a3;
  --font-rounded: "Segoe UI Rounded", "SF Pro Rounded", system-ui, sans-serif;
}

* {
  box-sizing: border-box;
}

body {
  font-family: var(--font-rounded);
  color: var(--text-on-felt);
  margin: 0;
  min-height: 100vh;
  background: radial-gradient(circle at 50% 0%, var(--felt) 0%, var(--felt-dark) 100%);
}

#app {
  display: flex;
  flex-direction: column;
  min-height: 100vh;
  padding: 1rem 1.5rem;
  gap: 1rem;
}

.app-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
}

.app-title {
  margin: 0;
  font-size: 1.4rem;
}

.btn {
  font-family: inherit;
  font-size: 0.95rem;
  border: none;
  border-radius: 8px;
  padding: 8px 16px;
  cursor: pointer;
}

.btn:disabled {
  opacity: 0.5;
  cursor: not-allowed;
}

.btn-primary {
  background: var(--primary);
  color: var(--primary-text);
}

.btn-primary:hover:not(:disabled) {
  filter: brightness(1.08);
}

.game-layout {
  display: flex;
  flex-wrap: wrap;
  gap: 1rem;
  flex: 1;
}

.card {
  background: var(--card-bg);
  color: var(--text);
  border: 1px solid var(--card-border);
  border-radius: 12px;
  box-shadow: 0 4px 12px rgba(43, 32, 22, 0.25);
  padding: 1rem 1.25rem;
}

.card-title {
  margin: 0 0 0.75rem;
  font-size: 1.1rem;
}

.scorecard-card {
  flex: 1 1 320px;
}

.turn-card {
  flex: 1 1 380px;
}

.turn-actions {
  display: flex;
  align-items: center;
  gap: 10px;
  margin: 8px 0;
}
```

- [ ] **Step 3: Run the existing unit tests (regression guard)**

Run: `cd gui && npm test`
Expected: all existing tests in `cliArgs.test.ts`, `parseResult.test.ts`, `state.test.ts` still PASS (this task touched no file they cover).

- [ ] **Step 4: Manually verify the shell in a browser**

Run: `cd gui && npm run dev`
Open the printed local URL (e.g. `http://localhost:5173`) in a browser.
Expected: deep-green felt background; header row with "🎲 Yatzy Assistant" title (left) and a red "New Game" button (right); two cream, rounded, shadowed cards side by side below — "Scorecard" (showing the 15 category rows with dashes, from the initial game state) on the left, "This Turn" (currently showing unstyled default number inputs/buttons — expected, since Task 3 hasn't restyled them yet) on the right. Browser console shows no errors.

- [ ] **Step 5: Commit**

```bash
git add gui/index.html gui/src/styles.css
git commit -m "feat(gui): rebuild page shell as a two-column felt-table layout"
```

---

### Task 2: Scorecard row styling

**Files:**
- Modify: `gui/src/render.ts:8-23` (`renderScorecard`)
- Modify: `gui/src/styles.css` (append)

**Interfaces:**
- Consumes: `.card`, `.card-title`, `--card-border`, `--text` from Task 1.
- Produces: `.score-row.total-row` class on the upper-total and grand-total rows, for later reference (none — this is a leaf task).

- [ ] **Step 1: Add a `total-row` class to the two summary rows in `renderScorecard`**

In `gui/src/render.ts`, replace:

```ts
  el.innerHTML = `
    ${rows.join("")}
    <div class="score-row"><strong>Upper total</strong><span>${state.upperTotal} (${bonusText})</span></div>
    <div class="score-row"><strong>Grand total</strong><span>${totalScore(state)}</span></div>
  `;
```

with:

```ts
  el.innerHTML = `
    ${rows.join("")}
    <div class="score-row total-row"><strong>Upper total</strong><span>${state.upperTotal} (${bonusText})</span></div>
    <div class="score-row total-row"><strong>Grand total</strong><span>${totalScore(state)}</span></div>
  `;
```

- [ ] **Step 2: Append scorecard styles to `gui/src/styles.css`**

```css
.score-row {
  display: flex;
  justify-content: space-between;
  padding: 3px 0;
  font-size: 0.95rem;
}

.score-row.total-row {
  border-top: 1px solid var(--card-border);
  margin-top: 6px;
  padding-top: 6px;
}
```

- [ ] **Step 3: Run the existing unit tests**

Run: `cd gui && npm test`
Expected: PASS (unchanged).

- [ ] **Step 4: Manually verify**

Run: `cd gui && npm run dev`, open the local URL.
Expected: the Scorecard card shows the 15 category rows tightly spaced, then a thin divider line above "Upper total" and another above "Grand total", visually separating the summary rows from the category list.

- [ ] **Step 5: Commit**

```bash
git add gui/src/render.ts gui/src/styles.css
git commit -m "style(gui): add divider styling to scorecard summary rows"
```

---

### Task 3: Clickable pip dice

**Files:**
- Modify: `gui/src/render.ts:95-120` (`renderDiceInputs`, full rewrite)
- Modify: `gui/src/styles.css` (append)
- Modify: `gui/README.md:10-11` (usage instructions — dice entry is now click-to-cycle, not typing)

**Interfaces:**
- Consumes: `.card`, `--card-bg`, `--card-border`, `--text`, `--gold` from Task 1.
- Produces: no new exports — `renderDiceInputs(dice, onChange)` keeps its existing signature, so `main.ts` (which calls it as `renderDiceInputs(state.dice, handleDiceChange)`) needs no change.

- [ ] **Step 1: Replace `renderDiceInputs` in `gui/src/render.ts`**

Replace the entire existing function:

```ts
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
        onChange(i, value !== null && Number.isInteger(value) && value >= 1 && value <= 6 ? value : null);
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

with:

```ts
const PIP_LAYOUTS: Record<number, string[]> = {
  1: ["mc"],
  2: ["tl", "br"],
  3: ["tl", "mc", "br"],
  4: ["tl", "tr", "bl", "br"],
  5: ["tl", "tr", "mc", "bl", "br"],
  6: ["tl", "ml", "bl", "tr", "mr", "br"],
};

export function renderDiceInputs(
  dice: (number | null)[],
  onChange: (index: number, value: number | null) => void
): void {
  const el = document.getElementById("dice-inputs")!;
  if (el.childElementCount !== 5) {
    el.innerHTML = "";
    for (let i = 0; i < 5; i++) {
      const button = document.createElement("button");
      button.type = "button";
      button.className = "die";
      // Read the current value from the button's own dataset at click time,
      // not from the `dice` array captured in this closure — this listener
      // is created once, but `dice` is a fresh array on every render call,
      // so closing over it here would cycle from a stale value forever.
      button.addEventListener("click", () => {
        const raw = button.dataset.value;
        const current = raw ? Number(raw) : null;
        onChange(i, current === null ? 1 : (current % 6) + 1);
      });
      el.appendChild(button);
    }
  }
  const buttons = el.querySelectorAll<HTMLButtonElement>(".die");
  buttons.forEach((button, i) => {
    const value = dice[i];
    button.dataset.value = value === null ? "" : String(value);
    button.classList.toggle("die-empty", value === null);
    const pips = value === null ? [] : PIP_LAYOUTS[value];
    button.innerHTML = pips.map((pos) => `<span class="pip pip-${pos}"></span>`).join("");
  });
}
```

- [ ] **Step 2: Append pip-dice styles to `gui/src/styles.css`**

```css
.dice-row {
  display: flex;
  gap: 10px;
  margin-bottom: 0.5rem;
}

.die {
  width: 64px;
  height: 64px;
  border-radius: 12px;
  background: var(--card-bg);
  border: 2px solid var(--card-border);
  box-shadow: inset 0 2px 4px rgba(43, 32, 22, 0.15);
  display: grid;
  grid-template-columns: repeat(3, 1fr);
  grid-template-rows: repeat(3, 1fr);
  padding: 8px;
  cursor: pointer;
}

.die.die-empty {
  border-style: dashed;
  background: transparent;
  box-shadow: none;
}

.die:focus-visible {
  outline: 2px solid var(--gold);
  outline-offset: 2px;
}

.pip {
  width: 10px;
  height: 10px;
  border-radius: 50%;
  background: var(--text);
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
```

- [ ] **Step 3: Update `gui/README.md`'s usage instructions**

Replace:

```markdown
1. Enter your 5 dice values (1-6) in the boxes, or click "🎲 Roll remaining
   dice" to randomize whichever slots aren't currently held.
```

with:

```markdown
1. Click each die to cycle it through 1→6 to match what you physically
   rolled (it wraps back to 1 after 6), or click "🎲 Roll remaining dice" to
   randomize whichever dice aren't currently held.
```

- [ ] **Step 4: Run the existing unit tests**

Run: `cd gui && npm test`
Expected: PASS (unchanged — no test covers `render.ts`).

- [ ] **Step 5: Manually verify**

Run: `cd gui && npm run dev`, open the local URL.
Expected: 5 dashed empty squares in the turn card. Clicking one shows a single centered pip (value 1); repeated clicks cycle 2, 3 (diagonal), 4 (corners), 5 (corners+center), 6 (two columns of three), then back to 1. Tab-focusing a die and pressing Enter or Space also cycles it (native `<button>` behavior). Once all 5 dice show a value, note that a request goes out (check the browser Network/console — it will fail with a Tauri-related error since this is a plain browser preview, not a running Tauri app; that's expected here and unrelated to this task).

- [ ] **Step 6: Commit**

```bash
git add gui/src/render.ts gui/src/styles.css gui/README.md
git commit -m "feat(gui): replace numeric dice inputs with clickable pip dice"
```

---

### Task 4: Rerolls-left dot indicator

**Files:**
- Modify: `gui/src/render.ts:25-27` (`renderRerollsIndicator`)
- Modify: `gui/src/styles.css` (append)

**Interfaces:**
- Consumes: `--dot-filled`, `--dot-empty` from Task 1.
- Produces: nothing new (leaf task).

- [ ] **Step 1: Replace `renderRerollsIndicator` in `gui/src/render.ts`**

Replace:

```ts
export function renderRerollsIndicator(state: GameState): void {
  document.getElementById("rerolls-indicator")!.textContent = `Rerolls left: ${state.rerollsLeft}`;
}
```

with:

```ts
export function renderRerollsIndicator(state: GameState): void {
  const el = document.getElementById("rerolls-indicator")!;
  const dots = [0, 1].map((i) => {
    const filled = i < state.rerollsLeft;
    return `<span class="reroll-dot${filled ? " reroll-dot-filled" : ""}"></span>`;
  });
  el.innerHTML = dots.join("");
  el.setAttribute("aria-label", `Rerolls left: ${state.rerollsLeft}`);
}
```

- [ ] **Step 2: Append reroll-dot styles to `gui/src/styles.css`**

```css
.rerolls-dots {
  display: inline-flex;
  gap: 4px;
}

.reroll-dot {
  width: 10px;
  height: 10px;
  border-radius: 50%;
  background: var(--dot-empty);
}

.reroll-dot-filled {
  background: var(--dot-filled);
}
```

- [ ] **Step 3: Run the existing unit tests**

Run: `cd gui && npm test`
Expected: PASS (unchanged).

- [ ] **Step 4: Manually verify**

Run: `cd gui && npm run dev`, open the local URL.
Expected: next to "🎲 Roll remaining dice", two small dots, both dark-filled (2 rerolls left, the initial state). Set all 5 dice via clicking, then click "Reroll →" (it's enabled once all 5 dice have values) — expected one dot to go from filled to hollow/muted after each reroll, until both are hollow.

- [ ] **Step 5: Commit**

```bash
git add gui/src/render.ts gui/src/styles.css
git commit -m "feat(gui): show rerolls-left as dot indicators instead of text"
```

---

### Task 5: Recommendation cards, best-move highlight, computing pulse

**Files:**
- Modify: `gui/src/render.ts:29-72` (`renderRecommendation`, `renderComputing`)
- Modify: `gui/src/styles.css` (append)

**Interfaces:**
- Consumes: `--card-bg`, `--card-border`, `--gold`, `--gold-bg`, `--primary` from Task 1.
- Produces: nothing new (leaf task).

- [ ] **Step 1: Replace `renderRecommendation` in `gui/src/render.ts`**

Replace:

```ts
export function renderRecommendation(
  result: QueryResult | null,
  onScoreCategory: (category: number, resultingScore: number) => void,
  onHold: (holdValues: number[]) => void
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
    el.innerHTML = "";
    for (const opt of result.rerollOptions) {
      const button = document.createElement("button");
      button.className = "hold-option";
      const stopNote = opt.holdValues.length === 5 ? " (stop rerolling)" : "";
      button.textContent = `Hold [${opt.holdValues.join(",")}]${stopNote} — expected value ${opt.expectedValue.toFixed(2)}`;
      button.addEventListener("click", () => onHold(opt.holdValues));
      el.appendChild(button);
    }
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
```

with:

```ts
const BEST_BADGE = `<span class="best-badge">★ Best move</span>`;

export function renderRecommendation(
  result: QueryResult | null,
  onScoreCategory: (category: number, resultingScore: number) => void,
  onHold: (holdValues: number[]) => void
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
    el.innerHTML = "";
    result.rerollOptions.forEach((opt, index) => {
      const button = document.createElement("button");
      button.className = `option-card${index === 0 ? " best-option" : ""}`;
      const stopNote = opt.holdValues.length === 5 ? " (stop rerolling)" : "";
      const badge = index === 0 ? BEST_BADGE : "";
      button.innerHTML = `${badge}<span class="option-text">Hold [${opt.holdValues.join(",")}]${stopNote} — expected value ${opt.expectedValue.toFixed(2)}</span>`;
      button.addEventListener("click", () => onHold(opt.holdValues));
      el.appendChild(button);
    });
  } else {
    rerollButton.disabled = true;
    el.innerHTML = "";
    result.categoryOptions.forEach((opt, index) => {
      const button = document.createElement("button");
      button.className = `option-card${index === 0 ? " best-option" : ""}`;
      const badge = index === 0 ? BEST_BADGE : "";
      button.innerHTML = `${badge}<span class="option-text">${opt.categoryName} — score ${opt.resultingScore} (expected value ${opt.expectedValue.toFixed(2)})</span>`;
      button.addEventListener("click", () => onScoreCategory(opt.category, opt.resultingScore));
      el.appendChild(button);
    });
  }
}
```

- [ ] **Step 2: Replace `renderComputing` in `gui/src/render.ts`**

Replace:

```ts
export function renderComputing(): void {
  const el = document.getElementById("recommendation")!;
  const rerollButton = document.getElementById("reroll-button") as HTMLButtonElement;
  el.innerHTML = `<div class="option-row">Computing recommendation… (the first solve for a fresh dice/state combo can take a couple of minutes)</div>`;
  rerollButton.disabled = true;
}
```

with:

```ts
export function renderComputing(): void {
  const el = document.getElementById("recommendation")!;
  const rerollButton = document.getElementById("reroll-button") as HTMLButtonElement;
  el.innerHTML = `<div class="computing-row"><span class="computing-die"></span>Computing recommendation… (the first solve for a fresh dice/state combo can take a couple of minutes)</div>`;
  rerollButton.disabled = true;
}
```

- [ ] **Step 3: Append recommendation/computing styles to `gui/src/styles.css`**

```css
.option-card {
  display: flex;
  align-items: center;
  gap: 8px;
  width: 100%;
  text-align: left;
  padding: 8px 10px;
  margin: 4px 0;
  border: 1px solid var(--card-border);
  border-radius: 8px;
  background: var(--card-bg);
  color: var(--text);
  font-family: inherit;
  font-size: 0.9rem;
  cursor: pointer;
}

.option-card:hover {
  background: #f6ecd4;
}

.option-card.best-option {
  border-color: var(--gold);
  background: var(--gold-bg);
}

.best-badge {
  flex-shrink: 0;
  font-size: 0.75rem;
  font-weight: 700;
  color: var(--gold);
  white-space: nowrap;
}

.computing-row {
  display: flex;
  align-items: center;
  gap: 8px;
  padding: 8px 0;
  font-size: 0.9rem;
}

.computing-die {
  width: 16px;
  height: 16px;
  border-radius: 4px;
  background: var(--primary);
  animation: computing-pulse 1s ease-in-out infinite;
}

@keyframes computing-pulse {
  0%, 100% { opacity: 0.4; transform: scale(0.9); }
  50% { opacity: 1; transform: scale(1.1); }
}
```

- [ ] **Step 4: Run the existing unit tests**

Run: `cd gui && npm test`
Expected: PASS (unchanged).

- [ ] **Step 5: Manually verify via a browser-console smoke check**

`render.ts` has no Tauri dependency, so its exports can be exercised directly against the running Vite dev server without a real sidecar call.

Run: `cd gui && npm run dev`, open the local URL, open the browser devtools console, and run:

```js
const m = await import("/src/render.ts");
m.renderComputing();
```

Expected: the recommendation area in the "This Turn" card shows a small pulsing dark-red square next to "Computing recommendation…".

Then run:

```js
m.renderRecommendation(
  { isRerollDecision: true, rerollOptions: [
    { holdValues: [3, 3, 5], expectedValue: 24.5 },
    { holdValues: [3, 3], expectedValue: 21.1 },
  ] },
  () => {}, () => {}
);
```

Expected: two option rows — the first ("Hold [3,3,5] — expected value 24.50") has a gold border/background and a "★ Best move" badge; the second ("Hold [3,3] — expected value 21.10") is a plain cream row.

- [ ] **Step 6: Commit**

```bash
git add gui/src/render.ts gui/src/styles.css
git commit -m "feat(gui): style recommendation list as cards, highlight best move"
```

---

### Task 6: Final-score and error banners

**Files:**
- Modify: `gui/src/render.ts:74-93` (`renderError`, `renderFinalTotal`)
- Modify: `gui/src/styles.css` (append)

**Interfaces:**
- Consumes: `--error-bg`, `--text-on-felt`, `--gold` from Task 1.
- Produces: nothing new (leaf task).

- [ ] **Step 1: Update `renderFinalTotal`'s message in `gui/src/render.ts`**

Replace:

```ts
    el.textContent = `Game complete! Final score: ${totalScore(state)}`;
```

with:

```ts
    el.textContent = `🎉 Game complete! Final score: ${totalScore(state)}`;
```

(`renderError` and the rest of `renderFinalTotal` are unchanged — their show/hide logic and target `id`s already match the new `.error-banner`/`.final-banner` classes set on those elements in `gui/index.html` from Task 1.)

- [ ] **Step 2: Append banner styles to `gui/src/styles.css`**

```css
.error-banner {
  background: var(--error-bg);
  color: var(--text-on-felt);
  padding: 10px 14px;
  border-radius: 8px;
  margin-top: 10px;
}

.final-banner {
  background: linear-gradient(90deg, var(--gold), #f0c869);
  color: var(--text);
  font-weight: 700;
  text-align: center;
  padding: 12px;
  border-radius: 10px;
}
```

- [ ] **Step 3: Run the existing unit tests**

Run: `cd gui && npm test`
Expected: PASS (unchanged).

- [ ] **Step 4: Manually verify via the browser-console smoke check**

Run: `cd gui && npm run dev`, open the local URL, open devtools console, and run:

```js
const m = await import("/src/render.ts");
m.renderError("yatzy_cpu exited with code 1: example failure");
```

Expected: a rounded dark-red banner appears below the recommendation area reading "yatzy_cpu exited with code 1: example failure".

Then run:

```js
m.renderError(null);
document.getElementById("final-total").hidden = false;
document.getElementById("final-total").textContent = "🎉 Game complete! Final score: 250";
```

Expected: the error banner disappears; a gold ribbon-style banner spanning the width below both cards reads "🎉 Game complete! Final score: 250".

- [ ] **Step 5: Commit**

```bash
git add gui/src/render.ts gui/src/styles.css
git commit -m "style(gui): restyle error and final-score banners"
```

---

### Task 7: Full manual playthrough on Windows

**Files:** none (verification only).

**Interfaces:** none.

- [ ] **Step 1: Sync to the Windows build-staging folder**

Run: `./scripts/sync-to-windows.sh` (from the repo root)

- [ ] **Step 2: Launch the dev build on Windows**

From Windows PowerShell (or `powershell.exe` from WSL):

```powershell
cd C:\Users\<you>\yatzy-gui-build
npm install
npm run tauri dev
```

- [ ] **Step 3: Play one full 15-turn game, checking every redesigned element**

Checklist:
- Felt-green background, cream two-column cards, red-accented header/buttons render correctly in the real Tauri window (not just the browser preview).
- Clicking dice cycles 1→6 with correct pip layouts; empty dice show as dashed squares.
- Rerolls-left dots decrement correctly across a turn with 2, 1, 0 rerolls remaining.
- The real sidecar call succeeds (first call may take a couple of minutes to solve; later calls are near-instant) and the recommendation list renders with the top (first) option gold-highlighted with the "★ Best move" badge in both the reroll-options view and the category-options view.
- "Computing recommendation…" shows the pulsing die while a query is in flight.
- Complete all 15 categories and confirm the gold "🎉 Game complete!" banner appears with the correct final score (bonus included if earned).
- Click "New Game" mid-game and at game-end; confirm the scorecard, dice, and rerolls dots all reset correctly.

- [ ] **Step 4: Report outcome**

If everything checks out, this plan is complete — no commit needed for this task (it's verification-only). If any visual or functional issue is found, note it and fix it in the relevant earlier task's files before considering the redesign done.
