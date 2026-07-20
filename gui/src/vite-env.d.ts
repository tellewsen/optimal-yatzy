/// <reference types="vite/client" />

// Resolved by vite.config.ts's `resolve.alias` to either wasmEngine.ts or
// wasmEngineStub.ts depending on build mode — see engine.ts.
declare module "virtual:wasm-engine" {
  export { getRecommendation, isEngineWarm } from "./wasmEngine";
}
