import { describe, it, expect } from "vitest";
import { parseQueryResult } from "./parseResult";

describe("parseQueryResult", () => {
  it("parses a reroll-decision result", () => {
    const raw = JSON.stringify({
      isRerollDecision: true,
      rerollOptions: [{ holdValues: [6, 6, 6, 6, 6], expectedValue: 50 }],
    });
    const result = parseQueryResult(raw);
    expect(result.isRerollDecision).toBe(true);
    if (result.isRerollDecision) {
      expect(result.rerollOptions[0].holdValues).toEqual([6, 6, 6, 6, 6]);
      expect(result.rerollOptions[0].expectedValue).toBe(50);
    }
  });

  it("parses a category-decision result", () => {
    const raw = JSON.stringify({
      isRerollDecision: false,
      categoryOptions: [
        { category: 14, categoryName: "Yatzy", resultingScore: 50, expectedValue: 50 },
      ],
    });
    const result = parseQueryResult(raw);
    expect(result.isRerollDecision).toBe(false);
    if (!result.isRerollDecision) {
      expect(result.categoryOptions[0].categoryName).toBe("Yatzy");
    }
  });

  it("throws on malformed input", () => {
    expect(() => parseQueryResult("{}")).toThrow();
  });
});
