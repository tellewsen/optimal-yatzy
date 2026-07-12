import { describe, it, expect } from "vitest";
import {
  initialGameState, isGameComplete, allDiceValid, setDice, advanceReroll,
  scoreCategory, bonusEarned, totalScore, NUM_CATEGORIES, applyHold, rollRemaining,
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
  it("is false if a value is not an integer", () => {
    expect(allDiceValid([1, 2, 3, 4, 5.5])).toBe(false);
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

describe("applyHold", () => {
  it("keeps dice matching the held values by position and nulls the rest", () => {
    const s = setDice(initialGameState(), [2, 2, 3, 5, 6]);
    const next = applyHold(s, [2, 2]);
    expect(next.dice).toEqual([2, 2, null, null, null]);
  });
  it("decrements rerollsLeft", () => {
    const s = setDice(initialGameState(), [2, 2, 3, 5, 6]);
    const next = applyHold(s, [2, 2]);
    expect(next.rerollsLeft).toBe(1);
  });
  it("holding all 5 values keeps every die", () => {
    const s = setDice(initialGameState(), [6, 6, 6, 6, 6]);
    const next = applyHold(s, [6, 6, 6, 6, 6]);
    expect(next.dice).toEqual([6, 6, 6, 6, 6]);
  });
  it("holding no values clears every die", () => {
    const s = setDice(initialGameState(), [1, 2, 3, 4, 5]);
    const next = applyHold(s, []);
    expect(next.dice).toEqual([null, null, null, null, null]);
  });
  it("matches each held value to a distinct die, not the same one repeatedly", () => {
    const s = setDice(initialGameState(), [3, 3, 3, 5, 6]);
    const next = applyHold(s, [3, 3]);
    expect(next.dice.filter((d) => d === 3)).toEqual([3, 3]);
    expect(next.dice.filter((d) => d === null)).toHaveLength(3);
  });
  it("never goes below 0 rerolls", () => {
    let s = setDice(initialGameState(), [1, 2, 3, 4, 5]);
    s = applyHold(s, []);
    s = setDice(s, [1, 2, 3, 4, 5]);
    s = applyHold(s, []);
    s = setDice(s, [1, 2, 3, 4, 5]);
    s = applyHold(s, []);
    expect(s.rerollsLeft).toBe(0);
  });
});

describe("rollRemaining", () => {
  it("leaves held (non-null) dice untouched", () => {
    const result = rollRemaining([2, null, 4, null, null], () => 0);
    expect(result[0]).toBe(2);
    expect(result[2]).toBe(4);
  });
  it("fills null slots using the provided random source, mapped to 1-6", () => {
    const result = rollRemaining([null, null, null, null, null], () => 0);
    expect(result).toEqual([1, 1, 1, 1, 1]);
  });
  it("maps a random source near 1 to 6, not 7", () => {
    const result = rollRemaining([null], () => 0.9999);
    expect(result).toEqual([6]);
  });
  it("defaults to Math.random when no random source is given", () => {
    const result = rollRemaining([null, null, null, null, null]);
    expect(result.every((d) => d !== null && d >= 1 && d <= 6)).toBe(true);
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
