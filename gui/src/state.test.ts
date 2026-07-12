import { describe, it, expect } from "vitest";
import {
  initialGameState, isGameComplete, allDiceValid, setDice, advanceReroll,
  scoreCategory, bonusEarned, totalScore, NUM_CATEGORIES,
} from "./state";

describe("initialGameState", () => {
  it("starts with no categories used and 2 rerolls left", () => {
    const s = initialGameState();
    expect(s.usedMask).toBe(0);
    expect(s.rerollsLeft).toBe(2);
    expect(s.categoryScores).toEqual(new Array(NUM_CATEGORIES).fill(null));
  });
});

describe("allDiceValid", () => {
  it("is true for five values 1-6", () => {
    expect(allDiceValid([1, 2, 3, 4, 5])).toBe(true);
  });
  it("is false if any value is null", () => {
    expect(allDiceValid([1, 2, null, 4, 5])).toBe(false);
  });
  it("is false if a value is out of range", () => {
    expect(allDiceValid([1, 2, 3, 4, 7])).toBe(false);
  });
});

describe("advanceReroll", () => {
  it("decrements rerollsLeft and clears dice", () => {
    const s = setDice(initialGameState(), [6, 6, 6, 6, 6]);
    const next = advanceReroll(s);
    expect(next.rerollsLeft).toBe(1);
    expect(next.dice).toEqual([null, null, null, null, null]);
  });
  it("never goes below 0", () => {
    let s = initialGameState();
    s = advanceReroll(advanceReroll(advanceReroll(s)));
    expect(s.rerollsLeft).toBe(0);
  });
});

describe("scoreCategory", () => {
  it("records the score, marks the category used, and resets the turn", () => {
    const s = scoreCategory(initialGameState(), 14, 50); // Yatzy = index 14
    expect(s.categoryScores[14]).toBe(50);
    expect(s.usedMask & (1 << 14)).not.toBe(0);
    expect(s.rerollsLeft).toBe(2);
    expect(s.dice).toEqual([null, null, null, null, null]);
  });
  it("adds to upperTotal only for upper-section categories (index < 6)", () => {
    let s = scoreCategory(initialGameState(), 0, 3); // Ones
    expect(s.upperTotal).toBe(3);
    s = scoreCategory(s, 14, 50); // Yatzy, a lower category
    expect(s.upperTotal).toBe(3);
  });
});

describe("bonusEarned / totalScore", () => {
  it("awards the 50-point bonus at upperTotal >= 63", () => {
    const s = { ...initialGameState(), upperTotal: 63 };
    expect(bonusEarned(s)).toBe(true);
  });
  it("does not award the bonus below 63", () => {
    const s = { ...initialGameState(), upperTotal: 62 };
    expect(bonusEarned(s)).toBe(false);
  });
  it("sums all recorded scores plus the bonus if earned", () => {
    const s = {
      ...initialGameState(),
      upperTotal: 63,
      categoryScores: [3, 6, 9, 12, 15, 18, 0, 0, 0, 0, 0, 0, 0, 0, 50],
    };
    // upper section 3+6+9+12+15+18 = 63, plus Yatzy 50, plus bonus 50
    expect(totalScore(s)).toBe(63 + 50 + 50);
  });
});

describe("isGameComplete", () => {
  it("is false when categories remain open, true when all 15 are used", () => {
    let s = initialGameState();
    expect(isGameComplete(s)).toBe(false);
    for (let cat = 0; cat < NUM_CATEGORIES; cat++) s = scoreCategory(s, cat, 0);
    expect(isGameComplete(s)).toBe(true);
  });
});
