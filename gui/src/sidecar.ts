// sidecar.ts — thin wrapper invoking the bundled yatzy_cpu sidecar. Not unit
// tested directly (Command.sidecar requires a running Tauri app); covered by
// manual verification instead, per the design spec.
import { Command } from "@tauri-apps/plugin-shell";
import { GameState } from "./state";
import { buildCliArgs } from "./cliArgs";
import { parseQueryResult, QueryResult } from "./parseResult";

export async function getRecommendation(state: GameState, dice: number[]): Promise<QueryResult> {
  const args = buildCliArgs(state, dice);
  const command = Command.sidecar("binaries/yatzy_cpu", args);
  const output = await command.execute();
  if (output.code !== 0) {
    throw new Error(`yatzy_cpu exited with code ${output.code}: ${output.stderr}`);
  }
  return parseQueryResult(output.stdout);
}
