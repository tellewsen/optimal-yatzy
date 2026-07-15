// render.ts — pure DOM rendering for the scorecard and turn panel. Reads
// application state and a query result, writes to fixed DOM containers.
import {
  GameState, CATEGORY_NAMES, NUM_CATEGORIES, bonusEarned, totalScore,
} from "./state";
import { MatchState, Mode, isMatchComplete, matchWinner, compareCategoryScores } from "./match";
import { QueryResult } from "./parseResult";

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

export function renderRerollsIndicator(state: GameState): void {
  const el = document.getElementById("rerolls-indicator")!;
  const dots = [0, 1].map((i) => {
    const filled = i < state.rerollsLeft;
    return `<span class="reroll-dot${filled ? " reroll-dot-filled" : ""}"></span>`;
  });
  el.innerHTML = dots.join("");
  el.setAttribute("aria-label", `Rerolls left: ${state.rerollsLeft}`);
}

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

export function renderComputing(progressText?: string): void {
  const el = document.getElementById("recommendation")!;
  const rerollButton = document.getElementById("reroll-button") as HTMLButtonElement;
  const detail = progressText ? ` — ${progressText}` : "";
  el.innerHTML = `<div class="computing-row"><span class="computing-die"></span>Computing recommendation… (the first solve for a fresh dice/state combo can take a couple of minutes)${detail}</div>`;
  rerollButton.disabled = true;
}

export function renderWarmUp(status: "checking" | "cold" | "warming" | "warm", progressText?: string): void {
  const button = document.getElementById("warmup-button") as HTMLButtonElement;
  const label = document.getElementById("warmup-status")!;
  if (status === "checking") {
    button.hidden = true;
    label.hidden = true;
    return;
  }
  if (status === "warm") {
    button.hidden = true;
    label.hidden = false;
    label.textContent = "✓ Engine ready";
    return;
  }
  label.hidden = true;
  button.hidden = false;
  button.disabled = status === "warming";
  button.textContent =
    status === "warming"
      ? `Warming up…${progressText ? ` (${progressText})` : ""}`
      : "🔥 Warm Up Engine";
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

export function renderLayoutMode(mode: Mode): void {
  document.getElementById("computer-scorecard-card")!.hidden = mode !== "vsComputer";
  document.getElementById("game-layout")!.classList.toggle("vs-computer", mode === "vsComputer");
  document.getElementById("scorecard-title")!.textContent = mode === "vsComputer" ? "You" : "Scorecard";
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
      const wrapper = document.createElement("div");
      wrapper.className = "die-wrapper";

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

      const clearButton = document.createElement("button");
      clearButton.type = "button";
      clearButton.className = "die-clear";
      clearButton.setAttribute("aria-label", "Clear die");
      clearButton.textContent = "×";
      clearButton.addEventListener("click", () => onChange(i, null));

      wrapper.appendChild(button);
      wrapper.appendChild(clearButton);
      el.appendChild(wrapper);
    }
  }
  const wrappers = el.querySelectorAll<HTMLDivElement>(".die-wrapper");
  wrappers.forEach((wrapper, i) => {
    const value = dice[i];
    const button = wrapper.querySelector<HTMLButtonElement>(".die")!;
    const clearButton = wrapper.querySelector<HTMLButtonElement>(".die-clear")!;
    button.dataset.value = value === null ? "" : String(value);
    button.classList.toggle("die-empty", value === null);
    const pips = value === null ? [] : PIP_LAYOUTS[value];
    button.innerHTML = pips.map((pos) => `<span class="pip pip-${pos}"></span>`).join("");
    clearButton.hidden = value === null;
  });
}
