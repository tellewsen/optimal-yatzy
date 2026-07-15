// sidecar.ts — thin wrapper invoking the bundled yatzy_cpu sidecar. Not unit
// tested directly (Command.sidecar requires a running Tauri app); covered by
// manual verification instead, per the design spec.
import { Command } from "@tauri-apps/plugin-shell";
import { appDataDir, join } from "@tauri-apps/api/path";
import { exists, mkdir } from "@tauri-apps/plugin-fs";
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

export async function isEngineWarm(): Promise<boolean> {
  const dpCachePath = await resolveDpCachePath();
  return exists(dpCachePath);
}

// Matches the engine's `fprintf(stderr, "popcount %2d/%d done (%zu masks)\n", ...)`
// progress line, emitted once per DP level during a cold solve (see
// yatzy_engine.cpp). Only fires on a cache miss — a warm cache skips
// straight to the result with no popcount lines at all.
const PROGRESS_LINE = /popcount\s+(\d+)\/(\d+)/;

export async function getRecommendation(
  state: GameState,
  dice: number[],
  onProgress?: (level: number, total: number) => void
): Promise<QueryResult> {
  const dpCachePath = await resolveDpCachePath();
  const args = [...buildCliArgs(state, dice), "--dp-cache", dpCachePath];
  const command = Command.sidecar("binaries/yatzy_cpu", args);

  return new Promise<QueryResult>((resolve, reject) => {
    let stdout = "";
    let stderr = "";
    command.stdout.on("data", (line) => {
      stdout += line + "\n";
    });
    command.stderr.on("data", (line) => {
      stderr += line + "\n";
      const match = PROGRESS_LINE.exec(line);
      if (match) {
        onProgress?.(Number(match[1]), Number(match[2]));
      }
    });
    command.on("error", (error) => reject(new Error(String(error))));
    command.on("close", (payload) => {
      if (payload.code !== 0) {
        reject(new Error(`yatzy_cpu exited with code ${payload.code}: ${stderr}`));
        return;
      }
      try {
        resolve(parseQueryResult(stdout));
      } catch (err) {
        reject(err instanceof Error ? err : new Error(String(err)));
      }
    });
    command.spawn().catch(reject);
  });
}
