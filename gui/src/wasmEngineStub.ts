// wasmEngineStub.ts — swapped in for wasmEngine.ts by vite.config.ts's
// "virtual:wasm-engine" alias when building in `tauri` mode (see
// engine.ts). The desktop app always has hasTauriRuntime() === true, so
// this code never actually runs there — it exists purely so the Tauri
// build's module graph never references the real wasmEngine.ts, keeping
// its ~8.4MB pre-solved DP table out of the installer.
import { GameState } from "./state";
import { QueryResult } from "./parseResult";

export async function isEngineWarm(): Promise<boolean> {
  throw new Error("wasm engine excluded from the Tauri build");
}

export async function getRecommendation(
  _state: GameState,
  _dice: number[],
  _onProgress?: (level: number, total: number) => void
): Promise<QueryResult> {
  throw new Error("wasm engine excluded from the Tauri build");
}
