// cliArgs.ts — pure function building yatzy_cpu CLI args from game state.
import { GameState, NUM_CATEGORIES } from "./state";

export function buildCliArgs(state: GameState, dice: number[]): string[] {
  const usedIndices: number[] = [];
  for (let cat = 0; cat < NUM_CATEGORIES; cat++) {
    if (state.usedMask & (1 << cat)) usedIndices.push(cat);
  }
  return [
    "--used", usedIndices.join(","),
    "--upper", String(state.upperTotal),
    "--dice", dice.join(","),
    "--rerolls", String(state.rerollsLeft),
    "--json",
  ];
}
