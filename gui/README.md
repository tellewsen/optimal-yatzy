# OptiYatzy

A Windows desktop app for standard (5-dice, Scandinavian-rules) Yatzy: enter
your rolls during a real game and get the optimal move back, powered by the
`yatzy_cpu` DP engine from the repo root (built for this app via
`scripts/build-sidecar.sh`, no engine logic duplicated here).

## Using it

1. Click each die to cycle it through 1→6 to match what you physically
   rolled (it wraps back to 1 after 6), or click "🎲 Roll remaining dice" to
   randomize whichever dice aren't currently held.
2. Once all 5 are filled, the app calls the engine and shows either:
   - a list of **reroll options** ("Hold [...] — expected value ..."), or
   - a list of **category options** once rerolls are exhausted.
3. Click the option you want. Clicking a reroll option keeps those dice and
   clears the rest for you to physically re-roll and re-enter. Clicking a
   category option scores that turn and starts the next one.
4. "New Game" opens a Solo / vs Computer picker before starting a fresh
   scorecard.

The very first query in a session solves the DP fresh (a couple of minutes);
after that it's loaded from a cached table (`yatzy_cpu_dp.bin`, ~8MB) in
Tauri's app-data directory, so it's near-instant on every later query and
every future app launch.

## Playing vs Computer

In vs Computer mode you always go first. After you score a category, the
computer takes its turn automatically — click "Play Computer's Turn" (or
"Next →" in step-by-step mode) to let it play. It always plays the
top-ranked (optimal) move; there's no adjustable difficulty. Once both
scorecards have a score in the same category, the higher one is highlighted
gold, and the match ends with a banner showing both totals and the winner
(or a tie).

The ⚙️ settings icon next to "New Game" opens a panel with two toggles:
hiding the recommendation list if you'd rather play without hints, and
switching the computer between playing its turn instantly or one
roll/hold/reroll decision at a time ("Step-by-step").

## Rebuilding the sidecar

After any change to `yatzy_engine.cpp`, `yatzy_cpu.cpp`, or `precompute_std.h`
in the repo root:

```bash
./scripts/build-sidecar.sh
```

## Building the browser (WASM) version

Solo mode also runs standalone in a plain web browser (no Tauri, no
installed app) — the engine is compiled to WebAssembly with a pre-solved DP
table baked in, so there's no client-side solve. vs Computer mode is
desktop-only (its win-probability data is ~3GB, too large to ship to a
browser) and is automatically disabled when the page isn't running inside
Tauri.

One-time setup — install [Emscripten](https://emscripten.org/):

```bash
git clone https://github.com/emscripten-core/emsdk.git ~/emsdk
cd ~/emsdk && ./emsdk install latest && ./emsdk activate latest
```

Every time you build the WASM module, `emcc` needs to be on `PATH` in that
shell — if you haven't added `source ~/emsdk/emsdk_env.sh` to your shell
startup file (e.g. `~/.bashrc`), run it manually first:

```bash
source ~/emsdk/emsdk_env.sh   # must run un-piped in the SAME shell as build-wasm.sh —
                                # piping it (e.g. `| tail`) runs it in a subshell and the
                                # exported PATH is lost, so `emcc` won't be found after
```

The build script needs a pre-solved `yatzy_cpu_dp.bin` at the repo root. If
you don't already have one (check `ls ../yatzy_cpu_dp.bin`), generate it —
building the native CLI first if you haven't already (`../yatzy_cpu` won't
exist on a fresh checkout), then running any query, which triggers a
one-time cold solve (up to a couple of minutes):

```bash
cd ..
make yatzy_cpu                # skip if ./yatzy_cpu already exists
./yatzy_cpu --used 0 --upper 0 --dice 1,1,1,1,1 --rerolls 2
cd gui
```

Then build and preview:

```bash
./scripts/build-wasm.sh   # compiles to src/wasm/ (gitignored — this is build
                            # output, rerun after any yatzy_engine.cpp change)
npm run dev                # open the printed URL in a plain browser tab
```

## Developing/running on Windows

Windows can't execute a binary sitting on this repo's WSL UNC path, so sync
to a native Windows folder first:

```bash
./scripts/sync-to-windows.sh
```

Then, from Windows (PowerShell, or via `powershell.exe` from WSL):

```powershell
cd C:\Users\<you>\yatzy-gui-build
npm install
npm run tauri dev    # iterate
npm run tauri build   # produce a distributable
```

## Testing

Pure logic (game-state transitions, CLI-arg building, JSON-result parsing)
has unit tests, runnable directly in WSL (no Windows/Tauri needed):

```bash
npm test
```

The sidecar call itself and the rendered UI require a running Tauri app on
Windows (`Command.sidecar` only works through Tauri's IPC bridge) — verify
those by playing a full game in `npm run tauri dev`.
