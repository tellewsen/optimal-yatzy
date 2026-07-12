// sidecar.ts — thin wrapper invoking the bundled yatzy_cpu sidecar. Not unit
// tested directly (Command.sidecar requires a running Tauri app); covered by
// manual verification instead, per the design spec.
import { Command } from "@tauri-apps/plugin-shell";
import { appDataDir, join } from "@tauri-apps/api/path";
import { mkdir } from "@tauri-apps/plugin-fs";
import { GameState } from "./state";
import { buildCliArgs } from "./cliArgs";
import { parseQueryResult, QueryResult } from "./parseResult";

const DP_CACHE_FILENAME = "yatzy_cpu_dp.bin";

// The DP cache (~8MB) is expensive to rebuild (a couple of minutes on a cold
// solve) but loads in milliseconds once written. Pin it to the app-data dir
// so it survives restarts and rebuilds, rather than whatever directory the
// sidecar happens to run from. Resolved once per app session.
let dpCachePathPromise: Promise<string> | null = null;

function resolveDpCachePath(): Promise<string> {
  if (!dpCachePathPromise) {
    dpCachePathPromise = (async () => {
      const dir = await appDataDir();
      await mkdir(dir, { recursive: true });
      return join(dir, DP_CACHE_FILENAME);
    })();
  }
  return dpCachePathPromise;
}

export async function getRecommendation(state: GameState, dice: number[]): Promise<QueryResult> {
  const dpCachePath = await resolveDpCachePath();
  const args = [...buildCliArgs(state, dice), "--dp-cache", dpCachePath];
  const command = Command.sidecar("binaries/yatzy_cpu", args);
  const output = await command.execute();
  if (output.code !== 0) {
    throw new Error(`yatzy_cpu exited with code ${output.code}: ${output.stderr}`);
  }
  return parseQueryResult(output.stdout);
}
