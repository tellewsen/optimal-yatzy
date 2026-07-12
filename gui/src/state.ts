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
  return dice.length === 5 && dice.every((d) => d !== null && Number.isInteger(d) && d >= 1 && d <= 6);
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

export function applyHold(state: GameState, holdValues: number[]): GameState {
  const kept = new Array(5).fill(false);
  for (const value of holdValues) {
    const idx = state.dice.findIndex((d, i) => d === value && !kept[i]);
    if (idx !== -1) kept[idx] = true;
  }
  const dice = state.dice.map((d, i) => (kept[i] ? d : null));
  return {
    ...state,
    dice,
    rerollsLeft: Math.max(0, state.rerollsLeft - 1),
  };
}

export function rollRemaining(
  dice: (number | null)[],
  randomFn: () => number = Math.random
): (number | null)[] {
  return dice.map((d) => (d === null ? Math.floor(randomFn() * 6) + 1 : d));
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
