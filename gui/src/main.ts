// main.ts — wires game state, sidecar calls, and rendering together.
import {
  initialGameState, setDice, advanceReroll, scoreCategory, allDiceValid, GameState,
} from "./state";
import { getRecommendation } from "./sidecar";
import { QueryResult } from "./parseResult";
import {
  renderScorecard, renderRerollsIndicator, renderRecommendation, renderError,
  renderFinalTotal, renderDiceInputs, renderComputing,
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
      if (!allDiceValid(state.dice)) {
        lastResult = null;
        renderAll();
        continue;
      }
      renderComputing();
      try {
        lastResult = await getRecommendation(state, state.dice as number[]);
        renderError(null);
      } catch (err) {
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
