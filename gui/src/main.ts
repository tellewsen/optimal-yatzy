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
