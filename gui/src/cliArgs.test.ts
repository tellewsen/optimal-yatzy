import { describe, it, expect } from "vitest";
import { buildCliArgs } from "./cliArgs";
import { initialGameState, scoreCategory } from "./state";

describe("buildCliArgs", () => {
  it("builds args for a fresh game with no categories used", () => {
    const args = buildCliArgs(initialGameState(), [2, 2, 3, 5, 6]);
    expect(args).toEqual([
      "--used", "",
      "--upper", "0",
      "--dice", "2,2,3,5,6",
      "--rerolls", "2",
      "--json",
    ]);
  });

  it("lists used category indices comma-separated", () => {
    let s = scoreCategory(initialGameState(), 0, 3); // Ones used
    s = scoreCategory(s, 14, 50); // Yatzy used
    const args = buildCliArgs(s, [1, 1, 1, 1, 1]);
    expect(args).toEqual([
      "--used", "0,14",
      "--upper", "3",
      "--dice", "1,1,1,1,1",
      "--rerolls", "2",
      "--json",
    ]);
  });
});
