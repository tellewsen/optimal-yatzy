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
    <div class="score-row total-row"><strong>Upper total</strong><span>${state.upperTotal} (${bonusText})</span></div>
    <div class="score-row total-row"><strong>Grand total</strong><span>${totalScore(state)}</span></div>
  `;
}

export function renderRerollsIndicator(state: GameState): void {
  document.getElementById("rerolls-indicator")!.textContent = `Rerolls left: ${state.rerollsLeft}`;
}

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

export function renderComputing(): void {
  const el = document.getElementById("recommendation")!;
  const rerollButton = document.getElementById("reroll-button") as HTMLButtonElement;
  el.innerHTML = `<div class="option-row">Computing recommendation… (the first solve for a fresh dice/state combo can take a couple of minutes)</div>`;
  rerollButton.disabled = true;
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
