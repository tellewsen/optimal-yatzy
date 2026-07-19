// engine.ts — dispatches queries to the real Tauri sidecar when running
// inside the desktop app, or the WASM engine when running as a plain
// browser page. This is the single seam main.ts calls through.
import { GameState } from "./state";
import { QueryResult } from "./parseResult";
import { hasTauriRuntime, getRecommendation as sidecarGetRecommendation, isEngineWarm as sidecarIsEngineWarm } from "./sidecar";
import { getRecommendation as wasmGetRecommendation, isEngineWarm as wasmIsEngineWarm } from "./wasmEngine";

export { hasTauriRuntime };

export function isEngineWarm(): Promise<boolean> {
  return hasTauriRuntime() ? sidecarIsEngineWarm() : wasmIsEngineWarm();
}

export function getRecommendation(
  state: GameState,
  dice: number[],
  onProgress?: (level: number, total: number) => void
): Promise<QueryResult> {
  return hasTauriRuntime()
    ? sidecarGetRecommendation(state, dice, onProgress)
    : wasmGetRecommendation(state, dice, onProgress);
}
