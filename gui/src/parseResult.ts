// parseResult.ts — pure function parsing yatzy_cpu's --json stdout into a
// typed result.
export interface RerollOption {
  holdValues: number[];
  expectedValue: number;
}

export interface CategoryOption {
  category: number;
  categoryName: string;
  resultingScore: number;
  expectedValue: number;
}

export type QueryResult =
  | { isRerollDecision: true; rerollOptions: RerollOption[] }
  | { isRerollDecision: false; categoryOptions: CategoryOption[] };

export function parseQueryResult(rawStdout: string): QueryResult {
  const parsed = JSON.parse(rawStdout);
  if (typeof parsed.isRerollDecision !== "boolean") {
    throw new Error("malformed sidecar output: missing isRerollDecision");
  }
  if (parsed.isRerollDecision) {
    return { isRerollDecision: true, rerollOptions: parsed.rerollOptions as RerollOption[] };
  }
  return { isRerollDecision: false, categoryOptions: parsed.categoryOptions as CategoryOption[] };
}
