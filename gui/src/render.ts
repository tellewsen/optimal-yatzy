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
