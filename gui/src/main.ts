// main.ts — wires game state, sidecar calls, and rendering together.
import {
  initialGameState, setDice, advanceReroll, scoreCategory, applyHold, rollRemaining,
  allDiceValid, GameState,
} from "./state";
import { getRecommendation, isEngineWarm } from "./sidecar";
import { QueryResult } from "./parseResult";
import {
  renderScorecard, renderRerollsIndicator, renderRecommendation, renderError,
  renderFinalTotal, renderDiceInputs, renderComputing, renderWarmUp,
} from "./render";

let state: GameState = initialGameState();
let lastResult: QueryResult | null = null;

// Bumped on every state change so an in-flight query can detect that its
// result is stale (the state it was computed for no longer applies) before
// applying it — see maybeQuery.
let generation = 0;

function setState(next: GameState): void {
  state = next;
  generation++;
}

function renderAll(): void {
  renderScorecard(state);
  renderRerollsIndicator(state);
  renderFinalTotal(state);
  renderRecommendation(lastResult, handleScoreCategory, handleHold);
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
      const myGeneration = generation;
      if (!allDiceValid(state.dice)) {
        lastResult = null;
        renderAll();
        continue;
      }
      renderComputing();
      try {
        const result = await getRecommendation(state, state.dice as number[], (level, total) => {
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
  const dice = [...state.dice];
  dice[index] = value;
  setState(setDice(state, dice));
  void maybeQuery();
}

function handleScoreCategory(category: number, resultingScore: number): void {
  setState(scoreCategory(state, category, resultingScore));
  lastResult = null;
  renderAll();
}

function handleHold(holdValues: number[]): void {
  setState(applyHold(state, holdValues));
  lastResult = null;
  renderAll();
  // Holding all 5 dice leaves the roll unchanged and still valid, so this
  // cascades straight into the next decision (category options once
  // rerollsLeft hits 0) without waiting for new dice input.
  void maybeQuery();
}

function handleReroll(): void {
  setState(advanceReroll(state));
  lastResult = null;
  renderAll();
}

function handleRollRemaining(): void {
  setState(setDice(state, rollRemaining(state.dice)));
  renderAll();
  void maybeQuery();
}

function handleNewGame(): void {
  setState(initialGameState());
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
