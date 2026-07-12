# Norwegian Maxi Yatzy Optimal Strategy Solver — CUDA/GPU version

Same exact backward-induction algorithm as the CPU (Go) version, redesigned for GPU
execution: one CUDA block solves one (mask, upper-total) scorecard state, with threads
in the block cooperating over the 462 possible 6-dice rolls via shared memory.

## IMPORTANT — build status

The pure C++ combinatorics/scoring logic (`precompute.h`) has been compiled and
unit-tested (straight/house/tower/pair scoring verified by hand, roll probabilities
sum to 1.0, table sizes sane). **The CUDA-specific files (`kernel.cu`, `main.cu`) have
NOT been compiled** — the sandbox this was built in has no GPU and package installation
of a working `nvcc` failed on an unrelated dependency. Expect to do some first-compile
debugging (typical culprits: shared memory size limits, grid dimension limits, minor
CUDA API usage errors) — this is real, carefully-reasoned code, but untested on real
hardware.

## Design

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

## Building

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

## Rules encoded

Same as the CPU version: 6 dice, 20 categories, 84-point upper bonus worth +100.
See comments in `precompute.h` for the exact category list and scoring, including the
one open rule question (Full Straight = 21 vs 30 points — currently defaults to 21,
change `scoreFullStraight()` if your house rules differ).

## Resuming

Just re-run `./maxi_solver_gpu` — it checks for `checkpoint_maxi_gpu.bin` and resumes
from the last completed popcount level automatically.

## Standard Yatzy (CPU)

A second, fully independent solver for standard 5-dice Scandinavian Yatzy
(15 categories, upper bonus +50 at 63) lives alongside the Maxi/GPU code:
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

The DP solve runs once and caches its result to `yatzy_cpu_dp.bin`; later
invocations load the cache instead of recomputing.
