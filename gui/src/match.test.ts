import { describe, it, expect } from "vitest";
import {
  initialMatchState, activeGameState, withActiveGameState, afterScore,
  isMatchComplete, matchWinner, compareCategoryScores,
} from "./match";
import { initialGameState, scoreCategory, NUM_CATEGORIES } from "./state";

describe("initialMatchState", () => {
  it("solo mode has no computer and starts on the player's turn", () => {
    const m = initialMatchState("solo");
    expect(m.mode).toBe("solo");
    expect(m.turn).toBe("player");
    expect(m.computer).toBeNull();
  });
  it("vsComputer mode seeds a fresh computer GameState and starts on the player's turn", () => {
    const m = initialMatchState("vsComputer");
    expect(m.turn).toBe("player");
    expect(m.computer).toEqual(initialGameState());
  });
});

describe("activeGameState / withActiveGameState", () => {
  it("returns and replaces the player's state when it's the player's turn", () => {
    const m = initialMatchState("vsComputer");
    expect(activeGameState(m)).toBe(m.player);
    const next = scoreCategory(m.player, 0, 3);
    const updated = withActiveGameState(m, next);
    expect(updated.player).toBe(next);
    expect(updated.computer).toBe(m.computer);
  });
  it("returns and replaces the computer's state when it's the computer's turn", () => {
    const m: ReturnType<typeof initialMatchState> = { ...initialMatchState("vsComputer"), turn: "computer" };
    expect(activeGameState(m)).toBe(m.computer);
    const next = scoreCategory(m.computer!, 0, 3);
    const updated = withActiveGameState(m, next);
    expect(updated.computer).toBe(next);
    expect(updated.player).toBe(m.player);
  });
});

describe("afterScore", () => {
  it("never flips turn in solo mode", () => {
    const m = initialMatchState("solo");
    expect(afterScore(m).turn).toBe("player");
  });
  it("flips player to computer and back in vsComputer mode", () => {
    let m = initialMatchState("vsComputer");
    m = afterScore(m);
    expect(m.turn).toBe("computer");
    m = afterScore(m);
    expect(m.turn).toBe("player");
  });
});

function completeGame(scoreValue: number): ReturnType<typeof initialGameState> {
  let s = initialGameState();
  for (let cat = 0; cat < NUM_CATEGORIES; cat++) s = scoreCategory(s, cat, scoreValue);
  return s;
}

describe("isMatchComplete", () => {
  it("solo: complete when the player's scorecard is full", () => {
    const m = initialMatchState("solo");
    expect(isMatchComplete(m)).toBe(false);
    expect(isMatchComplete({ ...m, player: completeGame(0) })).toBe(true);
  });
  it("vsComputer: requires BOTH scorecards full", () => {
    const m = initialMatchState("vsComputer");
    expect(isMatchComplete({ ...m, player: completeGame(0) })).toBe(false);
    expect(isMatchComplete({ ...m, player: completeGame(0), computer: completeGame(0) })).toBe(true);
  });
});

describe("matchWinner", () => {
  it("is null in solo mode even when complete", () => {
    const m = { ...initialMatchState("solo"), player: completeGame(0) };
    expect(matchWinner(m)).toBeNull();
  });
  it("is null before the match is complete", () => {
    expect(matchWinner(initialMatchState("vsComputer"))).toBeNull();
  });
  it("declares the player the winner when their total is higher", () => {
    const m = { ...initialMatchState("vsComputer"), player: completeGame(10), computer: completeGame(5) };
    expect(matchWinner(m)).toBe("player");
  });
  it("declares the computer the winner when their total is higher", () => {
    const m = { ...initialMatchState("vsComputer"), player: completeGame(5), computer: completeGame(10) };
    expect(matchWinner(m)).toBe("computer");
  });
  it("declares a tie when totals are equal", () => {
    const m = { ...initialMatchState("vsComputer"), player: completeGame(7), computer: completeGame(7) };
    expect(matchWinner(m)).toBe("tie");
  });
});

describe("compareCategoryScores", () => {
  it("returns null if either score is null", () => {
    expect(compareCategoryScores(null, 5)).toBeNull();
    expect(compareCategoryScores(5, null)).toBeNull();
    expect(compareCategoryScores(null, null)).toBeNull();
  });
  it("returns 'a' when the first score is higher", () => {
    expect(compareCategoryScores(10, 5)).toBe("a");
  });
  it("returns 'b' when the second score is higher", () => {
    expect(compareCategoryScores(5, 10)).toBe("b");
  });
  it("returns 'tie' when equal", () => {
    expect(compareCategoryScores(5, 5)).toBe("tie");
  });
});
