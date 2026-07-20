// engine.ts — dispatches queries to the real Tauri sidecar when running
// inside the desktop app, or the WASM engine when running as a plain
// browser page. This is the single seam main.ts calls through.
//
// wasmEngine.ts is imported via the "virtual:wasm-engine" specifier rather
// than by its real path — vite.config.ts's resolve.alias points that
// specifier at wasmEngine.ts normally, but at a lightweight stub when
// building in `tauri` mode (see package.json's "build:tauri" script and
// tauri.conf.json's beforeBuildCommand). Resolving the alias to the stub
// means Rollup's module graph for the desktop build never references the
// real wasmEngine.ts at all, so the ~8.4MB pre-solved DP table it pulls in
// from gui/src/wasm/ never enters the build. The desktop app always has
// the native sidecar and never needs that table; without this, it ends up
// bundled into the installer for no reason.
import { GameState } from "./state";
import { QueryResult } from "./parseResult";
import { hasTauriRuntime, getRecommendation as sidecarGetRecommendation, isEngineWarm as sidecarIsEngineWarm } from "./sidecar";

export { hasTauriRuntime };

export async function isEngineWarm(): Promise<boolean> {
  if (hasTauriRuntime()) return sidecarIsEngineWarm();
  const wasmEngine = await import("virtual:wasm-engine");
  return wasmEngine.isEngineWarm();
}

export async function getRecommendation(
  state: GameState,
  dice: number[],
  onProgress?: (level: number, total: number) => void
): Promise<QueryResult> {
  if (hasTauriRuntime()) return sidecarGetRecommendation(state, dice, onProgress);
  const wasmEngine = await import("virtual:wasm-engine");
  return wasmEngine.getRecommendation(state, dice, onProgress);
}
