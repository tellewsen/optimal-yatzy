# Standard Yatzy CPU Solver — Design

## Context

This repo currently contains only the Norwegian **Maxi Yatzy** solver (6 dice,
20 categories), designed for CUDA/GPU execution. Per `README.md`, the CUDA
files (`kernel.cu`, `main.cu`) have never been compiled or run on real
hardware. The machine available for this work has a Quadro T2000 Max-Q
(4GB, low-power mobile GPU) — not worth targeting for this workload, so the
Maxi/GPU path is being shelved for now, left untouched, and can be revisited
later on better hardware.

Instead, this spec covers a **new, independent CPU-only solver for standard
Scandinavian Yatzy** (5 dice, 15 categories). It solves the exact same kind
of backward-induction DP as the Maxi version, but the state space is small
enough (2^15 masks × 64 upper-total buckets ≈ 2M states, 252 distinct dice
combos) that a single-threaded CPU solve completes in seconds — no GPU
needed.

This is phase one of a larger goal: the user wants to eventually build a
desktop GUI where they enter their current dice roll and get back the
optimal move. This spec's architecture is chosen specifically to make that
possible without a rewrite — see "Future GUI" below.

## Rules encoded (Standard Scandinavian Yatzy)

5 dice, 15 categories:

**Upper section** (bonus +50 if upper total ≥ 63):
- Ones, Twos, Threes, Fours, Fives, Sixes — sum of matching dice

**Lower section**:
- One Pair — highest pair × 2
- Two Pairs — two highest distinct pairs, summed × 2
- Three of a Kind — highest triple × 3
- Four of a Kind — highest quad × 4
- Small Straight (1-2-3-4-5) — 15 points
- Large Straight (2-3-4-5-6) — 20 points
- Full House (three-of-a-kind + pair) — sum of all 5 dice
- Chance — sum of all 5 dice
- Yatzy (all 5 dice equal) — 50 points flat

## Architecture

Two new, fully independent files/build target — **zero changes** to the
existing `precompute.h`, `kernel.cu`, `main.cu`, or `Makefile`'s existing
`all`/`maxi_solver_gpu` target.

```
precompute_std.h   — combinatorics + standard-Yatzy scoring (host-only C++,
                      no CUDA). Self-contained: duplicates the small generic
                      combo-generation helpers (factorial, multinomialWeight,
                      generateCombos, topFaces) rather than factoring out a
                      shared header, to avoid touching the existing Maxi code.

yatzy_engine.h/.cpp — the solver engine:
                      - buildFlatTables() (from precompute_std.h)
                      - solveDP(): full backward-induction DP over all
                        masks/popcount levels, single-threaded (state space
                        is small enough this runs in a few seconds)
                      - save/load DP table to yatzy_cpu_dp.bin (skip re-solve
                        on subsequent runs)
                      - query(usedMask, upperTotal, dice[5], rerollsLeft)
                        -> ranked recommendations (see below)

yatzy_cpu.cpp       — thin CLI: parses args, calls the engine, prints
                      results. No session/scorecard memory — every
                      invocation is a fresh, independent query.
```

Makefile gets one new target:

```
make yatzy_cpu   # g++ -O3 -std=c++17 yatzy_engine.cpp yatzy_cpu.cpp -o yatzy_cpu
```

## Engine query API

```
query(usedMask: uint16, upperTotal: int, dice: array[5] of int(1-6), rerollsLeft: 0|1|2)
```

- `usedMask` — bit i set means category i has already been scored (see
  category index list above, 0-14).
- `upperTotal` — running sum of upper-section points scored so far (raw,
  uncapped; the engine caps at 63 internally for DP lookups).
- `dice` — the 5 dice values currently showing.
- `rerollsLeft` — how many rerolls remain this turn (2 = right after the
  first roll, 1 = one reroll left, 0 = must score now).

Returns:
- If `rerollsLeft > 0`: a ranked list of `{ dice-to-hold (subset), expected
  value }`, including the "hold all 5 / stop rerolling now" option, sorted
  best-first.
- If `rerollsLeft == 0`: a ranked list of `{ category, resulting score,
  expected value }` for every currently-open category, sorted best-first.

## CLI interface

```
./yatzy_cpu --used 0,3,7 --upper 12 --dice 2,2,3,5,6 --rerolls 2
```

- Default output: human-readable ranked table.
- `--json` flag: machine-parseable JSON output instead — this is the seam
  the future GUI will use (spawn the process, parse stdout), so the GUI can
  be written in any language without linking against C++ directly.

## Testing / verification

- Sanity check: the DP's computed expected value for the full game (all 15
  categories open, upper total 0) should land close to the commonly-cited
  published optimal EV for standard Yatzy solitaire play (~248.75 points).
- Hand-verify a handful of `scoreCategory` results against known examples
  (e.g. a full house, a small straight, a Yatzy) — same style of check the
  Maxi version's `precompute.h` already received per the README.

## Out of scope for this spec

- The Maxi Yatzy GPU path (`kernel.cu`, `main.cu`, existing `Makefile`
  target) — left untouched, revisit later on suitable hardware.
- The desktop GUI itself — not built in this phase. The `--json` CLI output
  and the stateless, args-in/answer-out query design are chosen so the GUI
  (whatever language/toolkit is chosen later) can shell out to `yatzy_cpu`
  per query without needing a rewrite of the engine.
- Any persistent scorecard/session tracking in the CLI — deferred to
  whatever owns session state later (the GUI).
