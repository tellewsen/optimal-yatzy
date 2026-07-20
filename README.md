# Yatzy Optimal Strategy Solvers

Backward-induction DP solvers for optimal Yatzy play, plus a desktop app
(OptiYatzy) that uses one of them to give live move recommendations during a
real game. Three independent pieces live in this repo:

- **Standard Yatzy (5-dice, Scandinavian rules) — CPU solver.** The actively
  developed core: `yatzy_engine.h`/`.cpp`, `precompute_std.h`, `yatzy_cpu.cpp`.
  See [Standard Yatzy (CPU)](#standard-yatzy-cpu) below.
- **OptiYatzy** (`gui/`) — a Windows desktop app (Tauri) that wraps the
  standard-Yatzy CLI as a sidecar and adds a full turn-by-turn play UI,
  including a "vs Computer" mode where the computer plays optimally. See
  [OptiYatzy](#optiyatzy) below and [`gui/README.md`](gui/README.md).
- **Maxi Yatzy (6-dice, Norwegian rules) — GPU/CUDA solver.** Same algorithm
  family, redesigned for GPU execution: `kernel.cu`, `main.cu`, `precompute.h`.
  Experimental and never run on real hardware — see
  [Maxi Yatzy (6-dice) — GPU/CUDA solver](#maxi-yatzy-6-dice--gpucuda-solver)
  below.

## Background

A few years ago I read Michael Larsson and Kristofer Sjöberg's KTH bachelor's
thesis, ["Optimal Yatzy"](https://www.csc.kth.se/utbildning/kth/kurser/DD143X/dkand12/Group89Michael/report/Larsson+Sjoberg.pdf)
(DD143X, 2012), and have wanted to try building an optimal-strategy solver
myself ever since. This repo is that attempt: a backward-induction DP solver
for standard 5-dice Yatzy, an experimental port to 6-dice Maxi Yatzy on GPU,
and a desktop app to actually use the solver's recommendations while playing
a real game.

## Standard Yatzy (CPU)

The actively developed core: a backward-induction DP solver for standard
5-dice Scandinavian Yatzy (15 categories, upper bonus +50 at 63).
`precompute_std.h`, `yatzy_engine.h`/`.cpp`, `yatzy_cpu.cpp`. No GPU
required — plain CPU with `std::thread` parallelism.

Build and test:
```
make yatzy_cpu
make test_yatzy
```

Query the optimal move for a given game state:
```
./yatzy_cpu --used 0,3,7 --upper 12 --dice 2,2,3,5,6 --rerolls 2
```

- `--used`: comma-separated indices of categories already scored (run
  `./yatzy_cpu` with no args to see the full index list)
- `--upper`: running upper-section total so far
- `--dice`: the 5 dice currently showing
- `--rerolls`: rerolls remaining this turn (2, 1, or 0)
- `--json`: machine-parseable output instead of the human-readable table
- `--dp-cache`: override the DP cache file path (default `yatzy_cpu_dp.bin`)

The DP solve runs once and caches its result to the configured cache file; later
invocations load the cache instead of recomputing.

### Statistical verification

Two independent checks confirm the solver and its `query()` recommendations
actually produce the optimal-play statistics they claim to, not just that the
DP's own number looks plausible:

- `make test_dp_exact && ./test_dp_exact` — deterministic, tight-tolerance
  checks comparing solved `dp[]` cells against closed-form probabilities
  derived by hand (e.g. the expected count of 1s under the dominant
  hold-and-reroll strategy), independent of `solveDP()`'s own recursion.
  Folded into `make test_yatzy`.
- `make simulate_stats && ./simulate_stats --games 200000` — a Monte Carlo
  harness that plays full games by following `query()`'s own top-ranked
  recommendation with real dice rolls, then checks the simulated mean score
  converges to the DP's exact expected value. Multithreaded across games;
  run on demand (not part of `make test_yatzy`).

Measured over 200,000 simulated games (seed 42, 16 threads, ~70s): simulated
mean score **248.40** vs. the DP's exact expected value **248.44** — well
inside the 95% confidence interval `[248.23, 248.57]` — with the
upper-section bonus earned in **89.8%** of games.

## OptiYatzy

A Windows desktop app (`gui/`, built with Tauri — vanilla TypeScript/HTML/CSS,
no framework) that wraps `yatzy_cpu` as a sidecar binary and adds a full
turn-by-turn play UI: click each rolled die, get ranked reroll/category
recommendations back, and play either **Solo** or **vs Computer** (where the
computer plays every decision optimally, either instantly or step-by-step).
No engine logic is duplicated — the app calls the same CLI built above.

Solo mode also runs standalone in a plain web browser (no Tauri needed) —
the engine compiles to WebAssembly with a pre-solved DP table baked in. See
[`gui/README.md`](gui/README.md) for usage, building the sidecar, building
the browser (WASM) version, and running/developing on Windows (the Tauri
app itself only runs there — WSL can build and test the pure logic and the
browser build, but not launch the real Tauri app).

## Maxi Yatzy (6-dice) — GPU/CUDA solver

Same exact backward-induction algorithm as the CPU version, redesigned for GPU
execution: one CUDA block solves one (mask, upper-total) scorecard state, with threads
in the block cooperating over the 462 possible 6-dice rolls via shared memory.

### IMPORTANT — build status

The pure C++ combinatorics/scoring logic (`precompute.h`) has been compiled and
unit-tested (straight/house/tower/pair scoring verified by hand, roll probabilities
sum to 1.0, table sizes sane). **The CUDA-specific files (`kernel.cu`, `main.cu`) have
NOT been compiled** — the sandbox this was built in has no GPU and package installation
of a working `nvcc` failed on an unrelated dependency. Expect to do some first-compile
debugging (typical culprits: shared memory size limits, grid dimension limits, minor
CUDA API usage errors) — this is real, carefully-reasoned code, but untested on real
hardware.

### Design

- **Full DP table resident on GPU** the whole run: 2^20 masks × 85 upper-totals ×
  4 bytes (float32) ≈ 356 MB — trivial for a 16GB card.
- **One block per (mask, s) state.** Grid = (masks-in-level) × 85; blockDim = 256.
  Threads split the 462 dice-roll outcomes between them.
- **Shared memory** holds V0/V1/V2 (value at 0/1/2 rerolls remaining) — ~5.5KB per
  block, far under any modern GPU's shared memory limit, so this doesn't hurt occupancy.
- **Flattened CSR-style tables** (`subsetStart`, `subsetResultStart`, `resultComboID`,
  `resultProb`) instead of the Go version's slice-of-slices, for coalesced global
  memory access on the gather step.
- **Levels processed sequentially** (popcount 0 → 20), since level p's states depend
  on already-finished states with fewer categories filled. Within a level, all masks
  are solved in one kernel launch — fully parallel.
- **Checkpoint + overlap:** after each level, the DP table is copied device→host, and
  written to `checkpoint_maxi_gpu.bin` on a background CPU thread while the *next*
  level's kernel launch proceeds on the GPU. (This overlap is safe: the next level only
  writes NEW mask entries, never touches already-checkpointed ones.)

### Building

Requires the CUDA toolkit (`nvcc`) matching your driver, and a GPU with compute
capability matching the `-arch` flag in the Makefile (already set to `sm_89` for
Ada Lovelace / RTX 4070 Ti Super).

```
make
./maxi_solver_gpu
```

If you get an "insufficient shared memory" error, try reducing `BlockThreads` in
`kernel.cu`, or check `cudaFuncSetAttribute` is needed for opt-in large shared
memory allocations depending on your CUDA version.

### Rules encoded

Same as the CPU version: 6 dice, 20 categories, 84-point upper bonus worth +100.
See comments in `precompute.h` for the exact category list and scoring, including the
one open rule question (Full Straight = 21 vs 30 points — currently defaults to 21,
change `scoreFullStraight()` if your house rules differ).

### Resuming

Just re-run `./maxi_solver_gpu` — it checks for `checkpoint_maxi_gpu.bin` and resumes
from the last completed popcount level automatically.

## Further reading

Background on the algorithm class and prior work solving Yahtzee/Yatzy
optimally with the same backward-induction approach used here:

- Michael Larsson & Kristofer Sjöberg, ["Optimal Yatzy"](https://www.csc.kth.se/utbildning/kth/kurser/DD143X/dkand12/Group89Michael/report/Larsson+Sjoberg.pdf)
  (KTH Royal Institute of Technology, bachelor's thesis DD143X, 2012) — the
  paper that originally sparked this project; a from-scratch DP treatment
  of optimal Scandinavian Yatzy strategy.
- James Glenn, ["An Optimal Strategy for Yahtzee"](https://www.yahtzeemanifesto.com/an-optimal-strategy-for-yahtzee.php)
  (Loyola College in Maryland, Technical Report CS-TR-0002, 2006) — the
  standard reference for exact optimal-strategy solving of American
  Yahtzee via dynamic programming; the ~254.6 expected-score figure widely
  cited elsewhere traces back to this and Verhoeff's independent work below.
- Tom Verhoeff, ["Solving Solitaire Yahtzee"](https://wstomv.win.tue.nl/publications/yahtzee-report-unfinished.pdf)
  (Eindhoven University of Technology, 2004) — an independent derivation of
  the optimal solitaire strategy and expected score, with more detail on
  rule-variant sensitivity (bonus thresholds, joker rules, etc.).
- Richard S. Sutton & Andrew G. Barto, [*Reinforcement Learning: An Introduction*](http://incompleteideas.net/book/the-book-2nd.html),
  2nd ed., chapter 4 ("Dynamic Programming") — the general textbook
  treatment of the Bellman-equation/backward-induction machinery this
  repo's `solveDP()` is a finite-horizon instance of.
- Nicholas A. Pape, ["Yahtzee: Reinforcement Learning Techniques for Stochastic
  Combinatorial Games"](https://arxiv.org/abs/2601.00007) (2025) — trains
  policy-gradient RL agents (REINFORCE, A2C, PPO) on solitaire Yahtzee,
  reaching ~95% of the exact-DP optimum, and explicitly frames *multiplayer*
  Yahtzee's intractable joint state space as the reason to reach for
  approximation instead of exact DP — directly relevant background for
  [2026-07-15-yatzy-win-probability-strategy-design.md](docs/superpowers/specs/2026-07-15-yatzy-win-probability-strategy-design.md),
  which sidesteps that intractability by decomposing into independent
  per-player distributions instead of a joint state.
