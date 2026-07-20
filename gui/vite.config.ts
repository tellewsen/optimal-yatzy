import { fileURLToPath } from "node:url";
import { defineConfig } from "vite";

// @ts-expect-error process is a nodejs global
const host = process.env.TAURI_DEV_HOST;

// https://vite.dev/config/
export default defineConfig(async ({ mode }) => ({

  resolve: {
    alias: {
      // engine.ts imports "virtual:wasm-engine" rather than "./wasmEngine"
      // directly so this alias — not a runtime check — decides which file
      // Rollup's module graph ever sees. In `tauri` mode (see package.json's
      // "build:tauri" script) it resolves to the stub, so the real
      // wasmEngine.ts — and the ~8.4MB pre-solved DP table it pulls in from
      // src/wasm/ — never enters the desktop build at all.
      "virtual:wasm-engine": fileURLToPath(
        new URL(mode === "tauri" ? "./src/wasmEngineStub.ts" : "./src/wasmEngine.ts", import.meta.url)
      ),
    },
  },

  // Vite options tailored for Tauri development and only applied in `tauri dev` or `tauri build`
  //
  // 1. prevent Vite from obscuring rust errors
  clearScreen: false,
  // 2. tauri expects a fixed port, fail if that port is not available
  server: {
    port: 1420,
    strictPort: true,
    host: host || false,
    hmr: host
      ? {
          protocol: "ws",
          host,
          port: 1421,
        }
      : undefined,
    watch: {
      // 3. tell Vite to ignore watching `src-tauri`
      ignored: ["**/src-tauri/**"],
    },
  },
}));
