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
