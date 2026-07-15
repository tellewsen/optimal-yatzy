# Standard Yatzy — Win-Probability (Score-Aware) Strategy Engine

## Context

The engine (`yatzy_engine.h`/`.cpp`, spec'd in
[2026-07-12-standard-yatzy-cpu-solver-design.md](2026-07-12-standard-yatzy-cpu-solver-design.md))
and the GUI's vs-Computer mode (spec'd in
[2026-07-15-yatzy-gui-vs-computer-design.md](2026-07-15-yatzy-gui-vs-computer-design.md))
both currently optimize a single thing: expected score from the current
state onward, computed independently of the opponent. That's the correct
objective in solo play, and it's what makes the computer's play "optimal"
today. But in a two-player match, maximizing your own expected score is
**not** the same as maximizing your probability of winning — when you're
behind late in the game, the EV-maximizing move is often the wrong move;
you should be taking the higher-variance option that gives you a real shot
at catching up, even if it lowers your expected score.

A full two-player joint-state DP (`(mask₁,upper₁) × (mask₂,upper₂) × turn`)
is not on the table — it squares the existing ~2.1M-state space to ~4.4
trillion states, unrelated to and far beyond the never-yet-run-on-hardware
Maxi/GPU solver's own ~89M-state problem. This spec instead uses the fact
that the two players' turns don't interact except at the final score
comparison: precompute, once, for every single-player state, the *full
distribution* of achievable remaining score (not just its expectation) —
then combine two players' independently-looked-up distributions with a
cheap live calculation at decision time. No joint state is ever built.

## Scope

- **Engine-only.** This spec covers `yatzy_engine.h`/`.cpp`,
  `precompute_std.h` (read-only — no changes needed there), and a new set
  of CLI flags on `yatzy_cpu`. GUI wiring (a catch-up-aware computer
  opponent, or a "your win probability" readout for the human player) is a
  follow-up spec once this lands and its real solve-time/memory cost on
  actual hardware is known.
- **In scope:**
  - A new precomputed table giving, for every `(mask, upperTotal)` state,
    `P(remaining score ≥ t)` for every feasible `t`, under a policy that
    maximizes *that specific* probability (not expected value).
  - A new query function that, given both players' current states and
    banked totals, ranks the querying player's reroll/category options by
    win probability instead of expected value.
  - New CLI flags exposing this as an alternate query mode.
- **Out of scope:** GUI integration, Maxi (6-dice) support, any notion of
  difficulty/skill levels, saving/loading match state (unrelated to this
  layer), three-or-more-player matches.

## Why the naive joint DP is avoidable

The two players' dice rolls and category choices don't affect each other —
only the final score comparison couples them. So instead of a joint state,
precompute a **survival function** per single-player state:

```
winProbTable[mask][s][t] = P(a policy that maximizes exactly this
                              probability achieves remaining score ≥ t)
```

`t` ranges `0..MaxRemainingScore` (374 for standard Yatzy — the well-known
maximum possible game score: 105 upper-section pips + 50 bonus + 12 pair +
22 two pairs + 18 three-of-a-kind + 24 four-of-a-kind + 15 small straight +
20 large straight + 28 full house + 30 chance + 50 Yatzy). This is computed
**once**, independent of any opponent, exactly like today's `dp` table is
computed once independent of any specific game. At decision time, look up
*both* players' current-state rows from this one table and combine them
with an O(374) convolution — cheap, done live, per query.

Important subtlety: the policy that maximizes `P(≥ t)` for one `t` is
generally a *different* policy than the one that maximizes it for another
`t` (a "should I gun for Yatzy or bank the safe chance score" decision
depends on how much you still need). That's why the table has a full `t`
axis rather than one scalar per state — each `t` column is, in effect, an
independently-solved DP sharing the same transition structure as the
existing EV solve.

## Data model changes (`yatzy_engine.h`)

```cpp
constexpr int MaxRemainingScore = 374; // documented constant; see derivation above

// winProb[mask * stride_s * numT + s * numT + t] = P(remaining >= t),
// uint8 fixed-point (0..255 representing 0.0..1.0), under the policy that
// maximizes exactly P(remaining >= t) from this (mask, s) state.
std::vector<uint8_t> solveWinProbDP(const FlatTables& t);

bool saveWinProbDP(const std::vector<uint8_t>& wp, const std::string& path);
bool loadWinProbDP(std::vector<uint8_t>& wp, const std::string& path, size_t expectedSize);
std::vector<uint8_t> loadOrSolveWinProbDP(const FlatTables& t, const std::string& path);

struct WinRerollOption {
    std::vector<int> heldValues;
    float winProb; // P(this player wins | holds these dice and plays optimally after)
};

struct WinCategoryOption {
    int category;
    int resultingScore;
    float winProb;
};

struct WinQueryResult {
    bool isRerollDecision;
    std::vector<WinRerollOption> rerollOptions;
    std::vector<WinCategoryOption> categoryOptions;
    float tieProb; // reported once per query, not per option — see below
};

// myBankedTotal / oppBankedTotal: actual scores banked so far (not the
// capped `s` used for bonus bookkeeping) — this is what the final
// comparison is actually decided on.
WinQueryResult queryForWin(const FlatTables& t, const std::vector<uint8_t>& winProb,
                           int myUsedMask, int myUpperTotal, int myBankedTotal,
                           const std::array<int,5>& myDice, int myRerollsLeft,
                           int oppUsedMask, int oppUpperTotal, int oppBankedTotal);
```

`FlatTables` and its combo/subset/reroll tables are reused unchanged — this
is purely an additional solve pass and an additional query path alongside
the existing `dp`/`query()`.

## `solveWinProbDP` — transition rule change

This mirrors `solveDP`'s backward induction over popcount levels
(`yatzy_engine.cpp:69-117`) with one change to the combine step. Today,
`computeV0` (`yatzy_engine.cpp:18-37`) does, per combo: `best over open cat
of (pts + dp[childMask][childS])` — an additive scalar recursion.

For the win-prob table, banking `pts` now doesn't add to a value, it
**reduces what's still needed**: achieving `remaining ≥ t` via category
`cat` means the child state must achieve `remaining ≥ t - pts`. So per
combo, per open category, the transition becomes a **shift of the child's
entire curve**, and `computeV0`'s existing "take the best over open
categories" loop becomes an elementwise max over shifted curves:

```cpp
for each open category cat:
    shifted[u] = childCurve[cat][clamp(u - pts[cat], 0, MaxRemainingScore)]
V0curve[ci][t] = max over cat of shifted[cat][t]
```

`computeVFromSubsets` (`yatzy_engine.cpp:44-57`) — the reroll-averaging
step — needs **no structural change**: `val += resultProb[ri] *
Vdown[comboID]` is a probability-weighted sum, and a probability-weighted
sum of "P(success)" values is still a valid P(success) (linearity of
expectation over an indicator variable). Only the element type changes
from `float` (one value) to `float[numT]` (a curve), so the same weighted-
sum code runs once per `t`.

Terminal case (`mask == 0`, `yatzy_engine.cpp:76`): today `dp[0][CapScore] =
Bonus`, all else 0. For the curve table, the terminal state's remaining
score is a *fixed* value (not random) — `Bonus` if `s == CapScore` else
`0` — so `curve[t] = 1.0` for `t <= thatFixedValue`, else `0.0`.

## Performance and storage

- **Storage:** `totalMasks(32768) × (CapScore+1=64) × numT(375)` entries.
  As `uint8` (fixed-point probability): **~786MB**. This dwarfs today's
  ~8MB `yatzy_cpu_dp.bin`. It's written to its own cache file
  (`yatzy_cpu_winprob.bin`, separate from and additive to the existing
  cache) and only solved/loaded when a win-probability query is actually
  requested — existing EV-only callers (today's `query()`, the CLI's
  default mode) pay no new cost.
- **Compute:** `computeV0`'s inner loop gains a `numT`-wide array operation
  per open category (was a single float compare). This multiplies solve
  work by roughly `numT` (~375x) over today's "couple of minutes" cold
  solve. The existing `std::thread` parallelism across popcount-level
  masks (`yatzy_engine.cpp:82-112`) still applies unchanged and the curve
  operations (shift + elementwise max, weighted-sum) are straightforward
  to keep cache-friendly/vectorizable. **This needs to be measured on real
  hardware before deciding whether the full 375-threshold resolution is
  practical for a "solve once, cache forever" desktop workflow, or whether
  a coarser stride (e.g. every 2 points, halving both memory and compute)
  is the right v1 default.** Flagging this explicitly rather than guessing
  — first implementation task should benchmark cold-solve wall time before
  any GUI-facing work builds on top of it.

## `queryForWin` — combining two independent curves at query time

Mirrors `query()`'s shape (`yatzy_engine.cpp:149-210`): same
reroll-vs-must-score branching, same combo lookup for the querying
player's live dice. The difference is in how each candidate option is
scored:

1. Build the querying player's **resulting curve** for each candidate
   action (hold subset, or category choice) using the same
   shift-and-max/weighted-sum logic as `solveWinProbDP`, seeded from the
   *already-precomputed* child rows in `winProb` — this is query-time work
   over live dice, exactly as `computeV0`/`computeVFromSubsets` already do
   in `query()` today, just curve-valued.
2. Look up the **opponent's curve directly** from `winProb` at their
   current `(oppUsedMask, cappedOppUpperTotal)` — no computation, a single
   row read.
3. Convolve: let `gap = oppBankedTotal - myBankedTotal`. For each possible
   value `r` I might still score (derived from consecutive differences of
   my resulting curve: `P(remaining == r) = curve[r] - curve[r+1]`), the
   probability the opponent's remaining score is strictly less than
   `r - gap` comes from their curve the same way. Sum
   `P(my remaining == r) * P(opp remaining < r - gap)` over all `r` for
   win probability; the tie term (`opp remaining == r - gap`) is summed
   separately into `tieProb`. This is an `O(numT²)` double sum in the
   simplest form (~140K operations at numT=375) — cheap at query time,
   done once per candidate action, never precomputed per state pair.

`tieProb` is reported once per `WinQueryResult` rather than per option
because it reflects the *overall* match state given the current action
under consideration is what the caller is choosing between — each option's
`winProb` already accounts for its own resulting distribution, and
`1 - winProb - tieProb` (per option) recovers the loss probability if
needed for display.

## CLI changes (`yatzy_cpu.cpp`)

New flags, layered onto the existing usage (`yatzy_cpu.cpp:23-32`),
required together as a group to switch modes:

```
--opp-used <comma-separated category indices>
--opp-upper <int>
--my-banked <int>
--opp-banked <int>
--winprob-cache <path>      (default: yatzy_cpu_winprob.bin)
```

If none of the `--opp-*`/`--*-banked` flags are given, behavior is
byte-for-byte unchanged (today's EV-ranked `query()` path — no new cost).
If the full group is given, the CLI solves/loads `winProb` (via
`loadOrSolveWinProbDP`) instead of/alongside `dp`, calls `queryForWin`, and
emits `winProb`/`tieProb` fields in place of `expectedValue` in both
text and `--json` output. Loading both caches is fine when both are
requested (e.g. a future GUI wanting both the EV ranking and the win-prob
ranking in one call) — the two tables are independent and additive.

## Testing

New C++ test binary/section (matching the existing `test_yatzy_engine`
convention), covering correctness invariants rather than golden output
(there's no independent reference implementation to compare against):

- **Terminal correctness:** `mask == 0` curves are exact step functions at
  `0` or `Bonus` depending on `s`.
- **Monotonicity:** `winProb[mask][s][t]` is non-increasing in `t` for
  every state (a survival function can't increase as the threshold rises).
- **Consistency bound against the existing EV table:** for every
  `(mask, s)`, `Σ_t winProb[mask][s][t] ≥ dp[mask][s]`. This follows from
  `E[R] = Σ_{t≥1} P(R ≥ t)` for any single fixed policy, while the
  win-prob table's sum uses a *different, locally-optimal* policy per `t`
  — so the sum can only be greater than or equal to the (single-policy)
  expected value, never less. Equality only at states with no real
  decision (e.g. one category left, one dice combo).
- **Small-scenario brute force:** for a truncated game (e.g. 3-4 open
  categories, few turns), brute-force enumerate all rolls/holds/choices
  for both a fixed "EV-optimal" policy and the win-prob table's policy,
  confirming the latter's `P(≥t)` is never lower for any `t`, and is
  strictly higher for at least one `t` in some constructed scenario (the
  whole point of this feature — otherwise it isn't doing anything the
  existing EV table doesn't already do).
- **CLI smoke test** (extends `test_yatzy_cli.sh`): a query with the new
  `--opp-*`/`--*-banked` flags returns valid JSON with `winProb` fields
  summing sensibly (`winProb + tieProb + implied loss ≈ 1` per option).

## Out of scope for this spec

- GUI wiring (computer opponent playing catch-up, human-facing win% readout).
- Adaptive/ragged threshold ranges per popcount level (storage optimization
  — the flat `0..374` range wastes some space at low-popcount/late-game
  states where the feasible max remaining is much smaller, but isn't worth
  the complexity until the flat version's memory/solve cost is measured).
- Maxi (6-dice) equivalent.
- Any strategy weaker than "exactly optimal for the chosen objective" (no
  difficulty levels).
