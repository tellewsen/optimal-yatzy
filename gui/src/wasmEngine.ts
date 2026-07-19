// wasmEngine.ts — the only file that touches the compiled WASM module.
// Solo mode only: the DP table ships pre-solved (baked in at build time via
// --preload-file), so there's never a client-side solve — every query is an
// instant table lookup. isEngineWarm() is therefore always true, and there
// is no solve progress to report (onProgress is accepted for interface
// parity with sidecar.ts but never invoked).
import { GameState } from "./state";
import { parseQueryResult, QueryResult } from "./parseResult";

interface YatzyModule {
  queryJson(usedMask: number, upperTotal: number, diceCsv: string, rerollsLeft: number): string;
}

let modulePromise: Promise<YatzyModule> | null = null;

function loadModule(): Promise<YatzyModule> {
  if (!modulePromise) {
    // The Emscripten-generated loader resolves the .wasm binary via
    // `new URL("yatzy_engine.wasm", import.meta.url)` (correctly anchored to
    // this module's own location), but fetches the --preload-file .data
    // package via a bare relative `fetch("yatzy_engine.data")` (anchored to
    // the *page's* URL instead). Passing `locateFile` overrides asset
    // resolution for both, anchoring every requested filename to this
    // file's own directory (one level up from wasm/) so it works regardless
    // of which page loads it.
    // This module is never actually imported at runtime in the desktop Tauri
    // app (see engine.ts's hasTauriRuntime() dispatch), and the WASM
    // artifacts it targets are gitignored build output produced by
    // build-wasm.sh that may not exist on a fresh checkout. Left as a plain
    // `import("./wasm/yatzy_engine.js")`, Rollup's dynamic-import resolver
    // still statically resolves string-literal specifiers at `vite build`
    // time (a /* @vite-ignore */ comment alone only suppresses Vite's
    // *dev-server* analysis warning — it does not stop Rollup's build-time
    // resolution of a literal), so the desktop production build would fail
    // whenever the WASM artifacts hadn't been built yet. Routing the same
    // specifier through a local variable makes it a non-literal expression
    // that Rollup cannot statically resolve; combined with @vite-ignore (to
    // suppress the resulting "cannot be analyzed" warning) this makes the
    // import genuinely runtime-resolved, exactly as intended.
    const wasmModuleSpecifier = "./wasm/yatzy_engine.js";
    modulePromise = import(/* @vite-ignore */ wasmModuleSpecifier).then((mod) =>
      mod.default({
        locateFile: (path: string) => new URL(`./wasm/${path}`, import.meta.url).href,
      })
    );
  }
  return modulePromise;
}

export async function isEngineWarm(): Promise<boolean> {
  return true;
}

export async function getRecommendation(
  state: GameState,
  dice: number[],
  _onProgress?: (level: number, total: number) => void
): Promise<QueryResult> {
  const mod = await loadModule();
  const json = mod.queryJson(state.usedMask, state.upperTotal, dice.join(","), state.rerollsLeft);
  return parseQueryResult(json);
}
