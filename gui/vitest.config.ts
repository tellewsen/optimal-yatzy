import { defineConfig } from "vitest/config";

export default defineConfig({
  test: {
    environment: "node",
    // No tests exist yet at scaffold time; without this, Vitest 4 exits
    // non-zero on an empty suite, which would break `npm test` until
    // later tasks add real tests.
    passWithNoTests: true,
  },
});
