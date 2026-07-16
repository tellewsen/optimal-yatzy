# Win-Probability Strategy Engine Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a win-probability-ranked query mode to the standard-Yatzy engine, alongside (not replacing) the existing expected-value query, per [2026-07-15-yatzy-win-probability-strategy-design.md](../specs/2026-07-15-yatzy-win-probability-strategy-design.md).

**Architecture:** A new precomputed table `winProb[mask][s][t] = P(remaining score >= t)` is built by the same backward-induction shape as the existing `solveDP` (`yatzy_engine.cpp:69-117`), with the combine rule changed from "add points, take expectation" to "shift by points, take elementwise max/weighted-sum over a threshold-indexed curve." At query time, `queryForWin` builds the querying player's resulting curve for each candidate action (reusing the same shift logic against the precomputed table) and combines it with a direct lookup of the opponent's curve via an O(numThresholds) convolution — no joint two-player state is ever built. Everything lives in the existing `yatzy_engine.h`/`.cpp` and `yatzy_cpu.cpp` — no new files, no Makefile changes.

**Tech Stack:** C++17, `std::thread` (already used by `solveDP`), no new dependencies.

## Global Constraints

- No new files — every change lands in `yatzy_engine.h`, `yatzy_engine.cpp`, `yatzy_cpu.cpp`, `test_yatzy_engine.cpp`, `test_yatzy_cli.sh`. `Makefile` is unchanged (same file set already builds these targets).
- **Deviation from the spec's storage plan, decided during implementation:** v1 uses `std::vector<float>` (not quantized `uint8`) for the `winProb` table, both in memory and on disk (~3.1GB for the full table: `32768 masks × 64 upper-buckets × 375 thresholds × 4 bytes`). Get the algorithm correct first with a simple representation; `uint8` quantization is explicit future work, not part of this plan.
- **Deviation from the spec's result-struct shape, decided during implementation:** `tieProb` lives on every `WinRerollOption`/`WinCategoryOption` (one per candidate action), not once per `WinQueryResult` — each candidate action has its own full outcome distribution, so a single top-level `tieProb` would be ambiguous about which action it describes.
- `solveWinProbDP` and `loadOrSolveWinProbDP` take an optional `maxPopcount` parameter (default `NumCats` = full real solve). Production callers (the CLI's normal path) never pass it. It exists so tests can solve a tiny slice of the table (e.g. `maxPopcount = 0` or `1`) instead of paying the full solve's cost in every `make test_yatzy` run. The CLI exposes it as `--winprob-max-popcount`, documented in the CLI's own usage text as an internal/testing knob — never use it for real play, since it leaves masks above the given popcount at zero (i.e. "impossible"), silently giving wrong answers for any query that needs them.
- The full-scale solve's real wall-clock/memory cost has **not** been measured on real hardware as of this plan (the spec flagged this explicitly). Task 5 is that benchmark, run manually — it is not part of the automated `make test_yatzy` path, matching how this repo already treats the Maxi/GPU code's "untested on real hardware" status.
- After every task: `make test_yatzy` must pass (existing tests untouched throughout — every new test is additive).

---

### Task 1: `solveWinProbDP` — curve-valued backward induction

**Files:**
- Modify: `yatzy_engine.h` (add `MaxRemainingScore`, `NumThresholds` constants and `solveWinProbDP` declaration)
- Modify: `yatzy_engine.cpp` (add `computeV0Curve`, `computeVFromSubsetsCurve` internal helpers and `solveWinProbDP`)
- Modify: `test_yatzy_engine.cpp` (new tests)

**Interfaces:**
- Consumes: `FlatTables`, `popcount` (existing, `yatzy_engine.cpp:10-14`), `NumCats`, `UpperCats`, `CapScore`, `Bonus`, `FullMask` (all existing).
- Produces: `constexpr int MaxRemainingScore = 374;`, `constexpr int NumThresholds = MaxRemainingScore + 1;`, `std::vector<float> solveWinProbDP(const FlatTables& t, int maxPopcount = NumCats);` — consumed by Task 2 and Task 3.

- [ ] **Step 1: Add constants and the declaration to `yatzy_engine.h`**

Add, immediately after `constexpr long long FullMask = (1LL << NumCats) - 1;` (`yatzy_engine.h:10`):

```cpp
// Theoretical maximum standard-Yatzy game score: 105 (upper pips, all
// categories maxed) + 50 (bonus) + 12 (one pair) + 22 (two pairs) + 18
// (three of a kind) + 24 (four of a kind) + 15 (small straight) + 20
// (large straight) + 28 (full house) + 30 (chance) + 50 (Yatzy) = 374.
constexpr int MaxRemainingScore = 374;
constexpr int NumThresholds = MaxRemainingScore + 1;

// winProb[(mask * (CapScore+1) + s) * NumThresholds + t] = P(a policy that
// maximizes exactly this probability achieves remaining score >= t), for
// t in 0..MaxRemainingScore. `maxPopcount` limits the backward induction to
// masks with at most this many open categories — production callers always
// use the default (full solve); tests pass a small value to solve only a
// cheap slice of the table. Masks above maxPopcount are left as zero.
std::vector<float> solveWinProbDP(const FlatTables& t, int maxPopcount = NumCats);
```

- [ ] **Step 2: Add the curve-valued transition helpers to `yatzy_engine.cpp`**

Add, inside the anonymous namespace, immediately after `computeVFromSubsets` (`yatzy_engine.cpp:44-57`):

```cpp
// Curve-valued analog of computeV0: V0curve[ci][t] = P(remaining >= t) for
// combo ci, given the caller must score into `mask` now, using already-
// solved child rows from `wp` (rows for masks with strictly fewer open
// categories than `mask`, already populated by an earlier popcount level).
void computeV0Curve(int mask, int s, const FlatTables& t, const std::vector<float>& wp,
                     std::vector<float>& V0curve) {
    size_t stride1 = (size_t)(CapScore + 1);
    size_t rowSize = (size_t)NumThresholds;
    for (int ci = 0; ci < t.numCombos; ci++) {
        float* out = &V0curve[(size_t)ci * rowSize];
        std::fill(out, out + NumThresholds, 0.0f);
        for (int m = mask; m; m &= (m - 1)) {
            int bit = m & (-m);
            int cat = __builtin_ctz((unsigned)bit);
            int pts = t.scoreTable[(size_t)ci * NumCats + cat];
            int ns = s;
            if (cat < UpperCats) {
                ns = s + pts;
                if (ns > CapScore) ns = CapScore;
            }
            int childMask = mask ^ bit;
            const float* child = &wp[((size_t)childMask * stride1 + ns) * rowSize];
            for (int tt = 0; tt < NumThresholds; tt++) {
                int need = tt - pts;
                // Banking `pts` alone already guarantees remaining >= tt
                // once need <= 0 — no need to consult the child at all.
                float val = (need <= 0) ? 1.0f : (need >= NumThresholds ? 0.0f : child[need]);
                if (val > out[tt]) out[tt] = val;
            }
        }
    }
}

// Curve-valued analog of computeVFromSubsets. Structurally identical to
// the scalar version — a probability-weighted sum of "P(success)" curves
// is still a valid "P(success)" curve (linearity of expectation over an
// indicator variable) — just widened from one float per combo to a
// NumThresholds-length row per combo.
void computeVFromSubsetsCurve(const FlatTables& t, const std::vector<float>& Vdown,
                               const std::vector<float>& V0curve, std::vector<float>& Vout) {
    size_t rowSize = (size_t)NumThresholds;
    for (int ci = 0; ci < t.numCombos; ci++) {
        float* out = &Vout[(size_t)ci * rowSize];
        const float* v0 = &V0curve[(size_t)ci * rowSize];
        std::copy(v0, v0 + NumThresholds, out);
        int ss = t.subsetStart[ci], se = t.subsetStart[ci + 1];
        for (int si = ss; si < se; si++) {
            int rs = t.subsetResultStart[si], re = t.subsetResultStart[si + 1];
            std::array<float, NumThresholds> val{};
            for (int ri = rs; ri < re; ri++) {
                float p = t.resultProb[ri];
                const float* childCurve = &Vdown[(size_t)t.resultComboID[ri] * rowSize];
                for (int tt = 0; tt < NumThresholds; tt++) val[tt] += p * childCurve[tt];
            }
            for (int tt = 0; tt < NumThresholds; tt++)
                if (val[tt] > out[tt]) out[tt] = val[tt];
        }
    }
}
```

- [ ] **Step 3: Add `solveWinProbDP` to `yatzy_engine.cpp`**

Add, immediately after `solveDP` (`yatzy_engine.cpp:117`, before `saveDP`):

```cpp
std::vector<float> solveWinProbDP(const FlatTables& t, int maxPopcount) {
    size_t totalMasks = (size_t)1 << NumCats;
    size_t stride1 = (size_t)(CapScore + 1);
    size_t rowSize = (size_t)NumThresholds;
    size_t dpSize = totalMasks * stride1 * rowSize;
    std::vector<float> wp(dpSize, 0.0f);

    // mask == 0: no categories left; remaining is a fixed value (the
    // bonus if the capped upper total reached CapScore, else 0) — not
    // random, so the curve is an exact step function.
    for (int s = 0; s <= CapScore; s++) {
        float* row = &wp[((size_t)0 * stride1 + s) * rowSize];
        int fixedRemaining = (s == CapScore) ? Bonus : 0;
        for (int tt = 0; tt < NumThresholds; tt++) row[tt] = (tt <= fixedRemaining) ? 1.0f : 0.0f;
    }

    std::vector<std::vector<int>> masksByPopcount(NumCats + 1);
    for (long long mask = 0; mask <= FullMask; mask++)
        masksByPopcount[popcount(mask)].push_back((int)mask);

    unsigned numThreads = std::max(1u, std::thread::hardware_concurrency());

    for (int p = 1; p <= maxPopcount; p++) {
        auto& levelMasks = masksByPopcount[p];
        size_t n = levelMasks.size();
        size_t chunk = (n + numThreads - 1) / numThreads;

        auto worker = [&](size_t begin, size_t end) {
            std::vector<float> V0((size_t)t.numCombos * rowSize);
            std::vector<float> V1((size_t)t.numCombos * rowSize);
            std::vector<float> V2((size_t)t.numCombos * rowSize);
            for (size_t idx = begin; idx < end; idx++) {
                int mask = levelMasks[idx];
                for (int s = 0; s <= CapScore; s++) {
                    computeV0Curve(mask, s, t, wp, V0);
                    computeVFromSubsetsCurve(t, V0, V0, V1);
                    computeVFromSubsetsCurve(t, V1, V0, V2);
                    float* out = &wp[((size_t)mask * stride1 + s) * rowSize];
                    for (int tt = 0; tt < NumThresholds; tt++) {
                        float acc = 0.0f;
                        for (int ci = 0; ci < t.numCombos; ci++)
                            acc += t.comboProb[ci] * V2[(size_t)ci * rowSize + tt];
                        out[tt] = acc;
                    }
                }
            }
        };

        std::vector<std::thread> threads;
        for (unsigned tIdx = 0; tIdx < numThreads; tIdx++) {
            size_t begin = std::min(n, (size_t)tIdx * chunk);
            size_t end = std::min(n, begin + chunk);
            if (begin >= end) continue;
            threads.emplace_back(worker, begin, end);
        }
        for (auto& th : threads) th.join();

        fprintf(stderr, "winprob popcount %2d/%d done (%zu masks)\n", p, maxPopcount, n);
    }
    return wp;
}
```

- [ ] **Step 4: Write the tests in `test_yatzy_engine.cpp`**

Add, before `int main()`:

```cpp
void test_winprob_terminal_and_yatzy_probability() {
    FlatTables t = buildFlatTables();
    // maxPopcount=1: solves mask==0 (terminal) plus every single-category
    // mask — cheap (16 masks x 64 s-values), no full 15-level solve needed.
    std::vector<float> wp = solveWinProbDP(t, 1);
    size_t stride1 = (size_t)(CapScore + 1);
    size_t rowSize = (size_t)NumThresholds;

    // Terminal exactness: mask==0, s==CapScore gets the bonus as a fixed
    // value; any other s gets 0.
    {
        const float* bonusRow = &wp[((size_t)0 * stride1 + CapScore) * rowSize];
        assert(bonusRow[Bonus] == 1.0f);
        assert(bonusRow[Bonus + 1] == 0.0f);
        const float* noBonusRow = &wp[((size_t)0 * stride1 + 0) * rowSize];
        assert(noBonusRow[0] == 1.0f);
        assert(noBonusRow[1] == 0.0f);
    }

    // Only Yatzy open (popcount 1): remaining is binary (0 or 50), so the
    // curve must be flat at whatever P(scoring a Yatzy this turn) is, for
    // every threshold 1..50, then exactly 0 above 50, and exactly 1 at 0.
    int yatzyOnlyMask = 1 << CatYatzy;
    const float* row = &wp[((size_t)yatzyOnlyMask * stride1 + 0) * rowSize];
    assert(row[0] == 1.0f);
    float pYatzy = row[50];
    assert(row[51] == 0.0f);
    for (int tt = 1; tt <= 50; tt++) assert(row[tt] == pYatzy);
    // Well-known trivia figure: optimal P(rolling a Yatzy, any face, in one
    // turn with up to 2 rerolls) is commonly cited around 4.6%. Wide
    // tolerance — this is a sanity check, not an exact-match regression
    // test (same spirit as test_full_game_ev's EV range check).
    printf("P(Yatzy this turn) = %f\n", pYatzy);
    assert(pYatzy > 0.03f && pYatzy < 0.07f);

    printf("test_winprob_terminal_and_yatzy_probability: passed\n");
}

void test_winprob_monotonic_and_consistent_with_ev() {
    FlatTables t = buildFlatTables();
    std::vector<float> dp = loadOrSolveDP(t, "test_dp_cache_shared.bin");
    // maxPopcount=3: covers every mask with <=3 open categories (576 masks
    // x 64 s-values) — fast, and deep enough to exercise 3 real levels of
    // the backward induction (each level depends on the one below it).
    std::vector<float> wp = solveWinProbDP(t, 3);
    size_t stride1 = (size_t)(CapScore + 1);
    size_t rowSize = (size_t)NumThresholds;

    std::vector<std::vector<int>> masksByPopcount(4);
    for (long long mask = 0; mask <= FullMask; mask++) {
        int pc = 0;
        for (long long m = mask; m; m &= (m - 1)) pc++;
        if (pc <= 3) masksByPopcount[pc].push_back((int)mask);
    }

    int checked = 0;
    for (int pc = 0; pc <= 3; pc++) {
        for (int mask : masksByPopcount[pc]) {
            for (int s : {0, 10, 40, CapScore}) {
                const float* row = &wp[((size_t)mask * stride1 + s) * rowSize];
                // Monotonicity: a survival function can't increase as the
                // threshold rises.
                for (int tt = 0; tt < NumThresholds - 1; tt++)
                    assert(row[tt] >= row[tt + 1]);
                // Consistency bound: summing P(>=t) over every t for the
                // per-threshold-optimal policy can only be >= the single
                // EV-maximizing policy's expected value (E[R] = sum_{t>=1}
                // P(R>=t) for ANY one fixed policy; the winProb table uses
                // a potentially-different, locally-optimal policy per t,
                // so its sum is an upper bound, never a lower one).
                double sum = 0.0;
                for (int tt = 1; tt < NumThresholds; tt++) sum += row[tt];
                float evValue = dp[(size_t)mask * stride1 + s];
                assert(sum >= (double)evValue - 1e-3);
                checked++;
            }
        }
    }
    printf("test_winprob_monotonic_and_consistent_with_ev: checked %d (mask,s) rows, passed\n", checked);
}
```

Add the two new calls to `main()`:

```cpp
int main() {
    test_full_game_ev();
    test_dp_cache_roundtrip();
    test_category_recommendation();
    test_reroll_recommendation();
    test_winprob_terminal_and_yatzy_probability();
    test_winprob_monotonic_and_consistent_with_ev();
    printf("test_yatzy_engine: all tests passed\n");
    return 0;
}
```

- [ ] **Step 5: Build and run**

Run: `make test_yatzy_engine && ./test_yatzy_engine`
Expected: all 6 tests print `passed`, including the printed `P(Yatzy this turn) = 0.0...` figure landing between 0.03 and 0.07.

- [ ] **Step 6: Commit**

```bash
git add yatzy_engine.h yatzy_engine.cpp test_yatzy_engine.cpp
git commit -m "feat(engine): add solveWinProbDP — per-state survival-function table"
```

---

### Task 2: Cache save/load for the win-prob table

**Files:**
- Modify: `yatzy_engine.h` (declarations)
- Modify: `yatzy_engine.cpp` (implementations, mirroring `saveDP`/`loadDP`/`loadOrSolveDP`)
- Modify: `test_yatzy_engine.cpp` (roundtrip test)

**Interfaces:**
- Consumes: `solveWinProbDP` (Task 1).
- Produces: `bool saveWinProbDP(const std::vector<float>&, const std::string&)`, `bool loadWinProbDP(std::vector<float>&, const std::string&, size_t)`, `std::vector<float> loadOrSolveWinProbDP(const FlatTables&, const std::string&, int maxPopcount = NumCats)` — consumed by Task 4 (CLI).

- [ ] **Step 1: Add declarations to `yatzy_engine.h`**

Add, immediately after the `solveWinProbDP` declaration from Task 1:

```cpp
bool saveWinProbDP(const std::vector<float>& wp, const std::string& path);
bool loadWinProbDP(std::vector<float>& wp, const std::string& path, size_t expectedSize);

// Loads wp from `path` if present and the right size; otherwise solves it
// (see solveWinProbDP's maxPopcount note) and saves it there for next time.
std::vector<float> loadOrSolveWinProbDP(const FlatTables& t, const std::string& path, int maxPopcount = NumCats);
```

- [ ] **Step 2: Add implementations to `yatzy_engine.cpp`**

Add, immediately after `loadOrSolveDP` (`yatzy_engine.cpp:140-147`):

```cpp
bool saveWinProbDP(const std::vector<float>& wp, const std::string& path) {
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return false;
    size_t n = wp.size();
    fwrite(&n, sizeof(size_t), 1, f);
    fwrite(wp.data(), sizeof(float), n, f);
    fclose(f);
    return true;
}

bool loadWinProbDP(std::vector<float>& wp, const std::string& path, size_t expectedSize) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    size_t n = 0;
    if (fread(&n, sizeof(size_t), 1, f) != 1 || n != expectedSize) { fclose(f); return false; }
    wp.resize(n);
    size_t read = fread(wp.data(), sizeof(float), n, f);
    fclose(f);
    return read == n;
}

std::vector<float> loadOrSolveWinProbDP(const FlatTables& t, const std::string& path, int maxPopcount) {
    size_t expectedSize = ((size_t)1 << NumCats) * (size_t)(CapScore + 1) * (size_t)NumThresholds;
    std::vector<float> wp;
    if (loadWinProbDP(wp, path, expectedSize)) return wp;
    wp = solveWinProbDP(t, maxPopcount);
    saveWinProbDP(wp, path);
    return wp;
}
```

- [ ] **Step 3: Write the roundtrip test in `test_yatzy_engine.cpp`**

Add, after `test_winprob_monotonic_and_consistent_with_ev`:

```cpp
void test_winprob_cache_roundtrip() {
    FlatTables t = buildFlatTables();
    const std::string path = "test_winprob_cache_roundtrip.bin";
    std::remove(path.c_str());

    std::vector<float> wp1 = loadOrSolveWinProbDP(t, path, 1);
    FILE* f = fopen(path.c_str(), "rb");
    assert(f != nullptr);
    fclose(f);

    std::vector<float> wp2 = loadOrSolveWinProbDP(t, path, 1);
    assert(wp1.size() == wp2.size());
    for (size_t i = 0; i < wp1.size(); i++) assert(wp1[i] == wp2[i]);

    std::remove(path.c_str());
    printf("test_winprob_cache_roundtrip: passed\n");
}
```

Add the call to `main()`, after `test_winprob_monotonic_and_consistent_with_ev()`.

- [ ] **Step 4: Build and run**

Run: `make test_yatzy_engine && ./test_yatzy_engine`
Expected: all 7 tests pass.

- [ ] **Step 5: Commit**

```bash
git add yatzy_engine.h yatzy_engine.cpp test_yatzy_engine.cpp
git commit -m "feat(engine): add save/load caching for the win-prob table"
```

---

### Task 3: `queryForWin` — combine two players' curves at query time

**Files:**
- Modify: `yatzy_engine.h` (structs + declaration)
- Modify: `yatzy_engine.cpp` (implementation)
- Modify: `test_yatzy_engine.cpp` (deterministic win/tie/loss tests)

**Interfaces:**
- Consumes: `solveWinProbDP`/`loadOrSolveWinProbDP` (Tasks 1-2), `countsToValues` (existing, `yatzy_engine.cpp:59-65`), `computeV0Curve`/`computeVFromSubsetsCurve` (Task 1).
- Produces: `WinRerollOption { heldValues, winProb, tieProb }`, `WinCategoryOption { category, resultingScore, winProb, tieProb }`, `WinQueryResult { isRerollDecision, rerollOptions, categoryOptions }`, `queryForWin(...)` — consumed by Task 4 (CLI).

- [ ] **Step 1: Add structs and declaration to `yatzy_engine.h`**

Add, at the end of the file (after the existing `query` declaration):

```cpp
struct WinRerollOption {
    std::vector<int> heldValues;
    float winProb;
    float tieProb;
};

struct WinCategoryOption {
    int category;
    int resultingScore;
    float winProb;
    float tieProb;
};

struct WinQueryResult {
    bool isRerollDecision;
    std::vector<WinRerollOption> rerollOptions;
    std::vector<WinCategoryOption> categoryOptions;
};

// myBankedTotal / oppBankedTotal: actual banked scores (not the capped
// upper-section bookkeeping value) — what the final comparison is decided
// on. myUsedMask/oppUsedMask follow the same "bit set = category already
// scored" convention as query()'s usedMask.
WinQueryResult queryForWin(const FlatTables& t, const std::vector<float>& wp,
                           int myUsedMask, int myUpperTotal, int myBankedTotal,
                           const std::array<int,5>& myDice, int myRerollsLeft,
                           int oppUsedMask, int oppUpperTotal, int oppBankedTotal);
```

- [ ] **Step 2: Implement `queryForWin` in `yatzy_engine.cpp`**

Add, at the end of the file (after the existing `query` function):

```cpp
namespace {

float survivalAt(const float* curve, int x) {
    if (x <= 0) return 1.0f;
    if (x > MaxRemainingScore) return 0.0f;
    return curve[x];
}

// O(NumThresholds) win/tie probability via the closed-form difference of
// two survival functions — see the design spec's "combining two
// independent curves" section for the derivation.
std::pair<float, float> winAndTie(const float* myCurve, const float* oppCurve, int gap) {
    float winP = 0.0f, tieP = 0.0f;
    for (int r = 0; r <= MaxRemainingScore; r++) {
        float pr = survivalAt(myCurve, r) - survivalAt(myCurve, r + 1);
        if (pr <= 0.0f) continue;
        int x = r - gap;
        float pOppLess = (x <= 0) ? 0.0f : (x > MaxRemainingScore ? 1.0f : 1.0f - oppCurve[x]);
        float pOppEq = survivalAt(oppCurve, x) - survivalAt(oppCurve, x + 1);
        winP += pr * pOppLess;
        tieP += pr * pOppEq;
    }
    return {winP, tieP};
}

} // namespace

WinQueryResult queryForWin(const FlatTables& t, const std::vector<float>& wp,
                           int myUsedMask, int myUpperTotal, int myBankedTotal,
                           const std::array<int,5>& myDice, int myRerollsLeft,
                           int oppUsedMask, int oppUpperTotal, int oppBankedTotal) {
    int openMask = (int)FullMask & ~myUsedMask;
    int cappedS = myUpperTotal > CapScore ? CapScore : myUpperTotal;
    int oppOpenMask = (int)FullMask & ~oppUsedMask;
    int oppCappedS = oppUpperTotal > CapScore ? CapScore : oppUpperTotal;
    size_t stride1 = (size_t)(CapScore + 1);
    size_t rowSize = (size_t)NumThresholds;
    const float* oppCurve = &wp[((size_t)oppOpenMask * stride1 + oppCappedS) * rowSize];
    int gap = oppBankedTotal - myBankedTotal;

    Counts c{};
    for (int v : myDice) c[v - 1]++;
    auto it = t.comboIndex.find(c);
    assert(it != t.comboIndex.end());
    int ci = it->second;

    WinQueryResult result;

    if (myRerollsLeft == 0) {
        result.isRerollDecision = false;
        for (int m = openMask; m; m &= (m - 1)) {
            int bit = m & (-m);
            int cat = __builtin_ctz((unsigned)bit);
            int pts = t.scoreTable[(size_t)ci * NumCats + cat];
            int ns = cappedS;
            if (cat < UpperCats) { ns = cappedS + pts; if (ns > CapScore) ns = CapScore; }
            int childMask = openMask ^ bit;
            const float* childCurve = &wp[((size_t)childMask * stride1 + ns) * rowSize];
            std::vector<float> myCurve(rowSize);
            for (int tt = 0; tt < NumThresholds; tt++) {
                int need = tt - pts;
                myCurve[tt] = (need <= 0) ? 1.0f : (need >= NumThresholds ? 0.0f : childCurve[need]);
            }
            auto [winP, tieP] = winAndTie(myCurve.data(), oppCurve, gap);
            result.categoryOptions.push_back({cat, pts, winP, tieP});
        }
        std::sort(result.categoryOptions.begin(), result.categoryOptions.end(),
                   [](const WinCategoryOption& a, const WinCategoryOption& b) { return a.winProb > b.winProb; });
        return result;
    }

    result.isRerollDecision = true;
    std::vector<float> V0((size_t)t.numCombos * rowSize);
    computeV0Curve(openMask, cappedS, t, wp, V0);

    std::vector<float> V1;
    const std::vector<float>* Vdown = &V0;
    if (myRerollsLeft == 2) {
        V1.resize((size_t)t.numCombos * rowSize);
        computeVFromSubsetsCurve(t, V0, V0, V1);
        Vdown = &V1;
    }

    int ss = t.subsetStart[ci], se = t.subsetStart[ci + 1];
    for (int si = ss; si < se; si++) {
        int rs = t.subsetResultStart[si], re = t.subsetResultStart[si + 1];
        std::vector<float> myCurve(rowSize, 0.0f);
        for (int ri = rs; ri < re; ri++) {
            float p = t.resultProb[ri];
            const float* childCurve = &(*Vdown)[(size_t)t.resultComboID[ri] * rowSize];
            for (int tt = 0; tt < NumThresholds; tt++) myCurve[tt] += p * childCurve[tt];
        }
        auto [winP, tieP] = winAndTie(myCurve.data(), oppCurve, gap);
        WinRerollOption opt;
        opt.heldValues = countsToValues(t.subsetCounts[si]);
        opt.winProb = winP;
        opt.tieProb = tieP;
        result.rerollOptions.push_back(opt);
    }
    std::sort(result.rerollOptions.begin(), result.rerollOptions.end(),
               [](const WinRerollOption& a, const WinRerollOption& b) { return a.winProb > b.winProb; });
    return result;
}
```

`countsToValues` is file-local (anonymous namespace, `yatzy_engine.cpp:59-65`) and already visible to any function defined later in the same translation unit — no header change needed for it.

- [ ] **Step 3: Write deterministic tests in `test_yatzy_engine.cpp`**

These use an opponent with **all** categories used (`oppOpenMask == 0`, the terminal step function) so the opponent's outcome is a fixed, known number — making win/tie/loss exactly computable by hand, no probability involved on either side except my own single deterministic score.

Add, after `test_winprob_cache_roundtrip`:

```cpp
void test_queryforwin_deterministic_outcomes() {
    FlatTables t = buildFlatTables();
    // Only Chance open for me (popcount 1) — queryForWin only ever reads
    // wp rows for masks strictly below my open mask's popcount, so a
    // maxPopcount=0 (terminal-only) solve is sufficient here.
    std::vector<float> wp = solveWinProbDP(t, 0);

    int myUsedMask = (int)FullMask & ~(1 << CatChance); // only Chance open
    std::array<int,5> dice = {3, 3, 3, 3, 3};            // Chance score = 15
    int oppUsedMask = (int)FullMask;                     // opponent fully done

    // My final = myBanked + 15. Opponent final = oppBanked (no pending
    // points: oppUpperTotal=0 means the terminal curve's fixed remainder
    // is 0, so oppBanked already reflects their true final score).
    struct Case { int myBanked, oppBanked; float expectWin, expectTie; };
    Case cases[] = {
        {190, 200, 1.0f, 0.0f}, // 205 > 200: win
        {190, 210, 0.0f, 0.0f}, // 205 < 210: loss
        {190, 205, 0.0f, 1.0f}, // 205 == 205: tie
    };
    for (const auto& tc : cases) {
        WinQueryResult r = queryForWin(t, wp, myUsedMask, 0, tc.myBanked, dice, 0,
                                        oppUsedMask, 0, tc.oppBanked);
        assert(!r.isRerollDecision);
        assert(r.categoryOptions.size() == 1);
        assert(r.categoryOptions[0].category == CatChance);
        assert(r.categoryOptions[0].resultingScore == 15);
        assert(r.categoryOptions[0].winProb == tc.expectWin);
        assert(r.categoryOptions[0].tieProb == tc.expectTie);
    }
    printf("test_queryforwin_deterministic_outcomes: passed\n");
}
```

Add the call to `main()`.

- [ ] **Step 4: Build and run**

Run: `make test_yatzy_engine && ./test_yatzy_engine`
Expected: all 8 tests pass.

- [ ] **Step 5: Commit**

```bash
git add yatzy_engine.h yatzy_engine.cpp test_yatzy_engine.cpp
git commit -m "feat(engine): add queryForWin — win-probability-ranked query"
```

---

### Task 4: CLI wiring (`yatzy_cpu.cpp`)

**Files:**
- Modify: `yatzy_cpu.cpp` (new flags, mode dispatch, output)
- Modify: `test_yatzy_cli.sh` (smoke test)

**Interfaces:**
- Consumes: `loadOrSolveWinProbDP`, `queryForWin`, `WinQueryResult` (Tasks 1-3).
- Produces: nothing new for later tasks (CLI is the outermost layer).

- [ ] **Step 1: Add new flags to `printUsageAndExit` and the parse loop**

Replace (`yatzy_cpu.cpp:23-32`):

```cpp
void printUsageAndExit(const char* prog) {
    fprintf(stderr,
        "Usage: %s --used <comma-separated category indices> --upper <int> "
        "--dice <5 comma-separated 1-6 values> --rerolls <0|1|2> [--json] [--dp-cache <path>]\n"
        "Categories: 0=Ones 1=Twos 2=Threes 3=Fours 4=Fives 5=Sixes 6=OnePair "
        "7=TwoPairs 8=ThreeOfAKind 9=FourOfAKind 10=SmallStraight 11=LargeStraight "
        "12=FullHouse 13=Chance 14=Yatzy\n",
        prog);
    exit(1);
}
```

with:

```cpp
void printUsageAndExit(const char* prog) {
    fprintf(stderr,
        "Usage: %s --used <comma-separated category indices> --upper <int> "
        "--dice <5 comma-separated 1-6 values> --rerolls <0|1|2> [--json] [--dp-cache <path>]\n"
        "  [--opp-used <indices> --opp-upper <int> --my-banked <int> --opp-banked <int> "
        "[--winprob-cache <path>]]\n"
        "Categories: 0=Ones 1=Twos 2=Threes 3=Fours 4=Fives 5=Sixes 6=OnePair "
        "7=TwoPairs 8=ThreeOfAKind 9=FourOfAKind 10=SmallStraight 11=LargeStraight "
        "12=FullHouse 13=Chance 14=Yatzy\n"
        "The --opp-*/--*-banked flags switch to win-probability-ranked output: give\n"
        "all four together to rank options by P(win) against a live opponent state\n"
        "instead of by expected value.\n"
        "--winprob-max-popcount is an internal testing knob (limits the win-prob\n"
        "solve depth) — never set it for real play, it silently zeroes states above\n"
        "the given popcount.\n",
        prog);
    exit(1);
}
```

Replace (`yatzy_cpu.cpp:36-50`):

```cpp
int main(int argc, char** argv) {
    std::string usedArg, diceArg, dpCachePath = "yatzy_cpu_dp.bin";
    int upperTotal = -1, rerollsLeft = -1;
    bool jsonOutput = false;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--used" && i + 1 < argc) usedArg = argv[++i];
        else if (arg == "--upper" && i + 1 < argc) upperTotal = std::atoi(argv[++i]);
        else if (arg == "--dice" && i + 1 < argc) diceArg = argv[++i];
        else if (arg == "--rerolls" && i + 1 < argc) rerollsLeft = std::atoi(argv[++i]);
        else if (arg == "--json") jsonOutput = true;
        else if (arg == "--dp-cache" && i + 1 < argc) dpCachePath = argv[++i];
        else printUsageAndExit(argv[0]);
    }
```

with:

```cpp
int main(int argc, char** argv) {
    std::string usedArg, diceArg, dpCachePath = "yatzy_cpu_dp.bin";
    std::string oppUsedArg, winProbCachePath = "yatzy_cpu_winprob.bin";
    int upperTotal = -1, rerollsLeft = -1;
    int oppUpperTotal = -1, myBankedTotal = -1, oppBankedTotal = -1;
    bool oppUsedProvided = false;
    int winProbMaxPopcount = NumCats;
    bool jsonOutput = false;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--used" && i + 1 < argc) usedArg = argv[++i];
        else if (arg == "--upper" && i + 1 < argc) upperTotal = std::atoi(argv[++i]);
        else if (arg == "--dice" && i + 1 < argc) diceArg = argv[++i];
        else if (arg == "--rerolls" && i + 1 < argc) rerollsLeft = std::atoi(argv[++i]);
        else if (arg == "--json") jsonOutput = true;
        else if (arg == "--dp-cache" && i + 1 < argc) dpCachePath = argv[++i];
        else if (arg == "--opp-used" && i + 1 < argc) { oppUsedArg = argv[++i]; oppUsedProvided = true; }
        else if (arg == "--opp-upper" && i + 1 < argc) oppUpperTotal = std::atoi(argv[++i]);
        else if (arg == "--my-banked" && i + 1 < argc) myBankedTotal = std::atoi(argv[++i]);
        else if (arg == "--opp-banked" && i + 1 < argc) oppBankedTotal = std::atoi(argv[++i]);
        else if (arg == "--winprob-cache" && i + 1 < argc) winProbCachePath = argv[++i];
        else if (arg == "--winprob-max-popcount" && i + 1 < argc) winProbMaxPopcount = std::atoi(argv[++i]);
        else printUsageAndExit(argv[0]);
    }

    bool winProbMode = oppUsedProvided || oppUpperTotal >= 0 || myBankedTotal >= 0 || oppBankedTotal >= 0;
    if (winProbMode && !(oppUsedProvided && oppUpperTotal >= 0 && myBankedTotal >= 0 && oppBankedTotal >= 0)) {
        fprintf(stderr, "error: --opp-used, --opp-upper, --my-banked, --opp-banked must all be given together\n");
        return 1;
    }
```

- [ ] **Step 2: Branch on `winProbMode` after the existing `FlatTables`/usedMask setup**

Replace (`yatzy_cpu.cpp:84-131`, everything from the "building tables" message to the end of `main`):

```cpp
    fprintf(stderr, "building tables and loading/solving DP (first run may take a minute)...\n");
    FlatTables t = buildFlatTables();
    std::vector<float> dp = loadOrSolveDP(t, dpCachePath);

    QueryResult r = query(t, dp, usedMask, upperTotal, dice, rerollsLeft);

    if (jsonOutput) {
        printf("{\"isRerollDecision\":%s,", r.isRerollDecision ? "true" : "false");
        if (r.isRerollDecision) {
            printf("\"rerollOptions\":[");
            for (size_t i = 0; i < r.rerollOptions.size(); i++) {
                const auto& opt = r.rerollOptions[i];
                printf("%s{\"holdValues\":[", i ? "," : "");
                for (size_t j = 0; j < opt.heldValues.size(); j++)
                    printf("%s%d", j ? "," : "", opt.heldValues[j]);
                printf("],\"expectedValue\":%f}", opt.expectedValue);
            }
            printf("]");
        } else {
            printf("\"categoryOptions\":[");
            for (size_t i = 0; i < r.categoryOptions.size(); i++) {
                const auto& opt = r.categoryOptions[i];
                printf("%s{\"category\":%d,\"categoryName\":\"%s\",\"resultingScore\":%d,\"expectedValue\":%f}",
                       i ? "," : "", opt.category, CategoryNames[opt.category], opt.resultingScore, opt.expectedValue);
            }
            printf("]");
        }
        printf("}\n");
    } else {
        if (r.isRerollDecision) {
            printf("Reroll recommendations (rerolls left: %d):\n", rerollsLeft);
            for (const auto& opt : r.rerollOptions) {
                printf("  hold [");
                for (size_t j = 0; j < opt.heldValues.size(); j++)
                    printf("%s%d", j ? "," : "", opt.heldValues[j]);
                printf("]%s -> expected value %.2f\n",
                       opt.heldValues.size() == 5 ? " (stop rerolling)" : "", opt.expectedValue);
            }
        } else {
            printf("Category recommendations:\n");
            for (const auto& opt : r.categoryOptions) {
                printf("  %-14s score %3d -> expected value %.2f\n",
                       CategoryNames[opt.category], opt.resultingScore, opt.expectedValue);
            }
        }
    }
    return 0;
}
```

with:

```cpp
    fprintf(stderr, "building tables and loading/solving DP (first run may take a minute)...\n");
    FlatTables t = buildFlatTables();

    if (winProbMode) {
        int oppUsedMask = 0;
        for (int cat : parseIntList(oppUsedArg)) {
            if (cat < 0 || cat >= NumCats) {
                fprintf(stderr, "error: opp category index must be 0-%d\n", NumCats - 1);
                return 1;
            }
            oppUsedMask |= (1 << cat);
        }

        std::vector<float> wp = loadOrSolveWinProbDP(t, winProbCachePath, winProbMaxPopcount);
        WinQueryResult r = queryForWin(t, wp, usedMask, upperTotal, myBankedTotal, dice, rerollsLeft,
                                        oppUsedMask, oppUpperTotal, oppBankedTotal);

        if (jsonOutput) {
            printf("{\"isRerollDecision\":%s,", r.isRerollDecision ? "true" : "false");
            if (r.isRerollDecision) {
                printf("\"rerollOptions\":[");
                for (size_t i = 0; i < r.rerollOptions.size(); i++) {
                    const auto& opt = r.rerollOptions[i];
                    printf("%s{\"holdValues\":[", i ? "," : "");
                    for (size_t j = 0; j < opt.heldValues.size(); j++)
                        printf("%s%d", j ? "," : "", opt.heldValues[j]);
                    printf("],\"winProb\":%f,\"tieProb\":%f}", opt.winProb, opt.tieProb);
                }
                printf("]");
            } else {
                printf("\"categoryOptions\":[");
                for (size_t i = 0; i < r.categoryOptions.size(); i++) {
                    const auto& opt = r.categoryOptions[i];
                    printf("%s{\"category\":%d,\"categoryName\":\"%s\",\"resultingScore\":%d,\"winProb\":%f,\"tieProb\":%f}",
                           i ? "," : "", opt.category, CategoryNames[opt.category], opt.resultingScore, opt.winProb, opt.tieProb);
                }
                printf("]");
            }
            printf("}\n");
        } else {
            if (r.isRerollDecision) {
                printf("Reroll recommendations (rerolls left: %d):\n", rerollsLeft);
                for (const auto& opt : r.rerollOptions) {
                    printf("  hold [");
                    for (size_t j = 0; j < opt.heldValues.size(); j++)
                        printf("%s%d", j ? "," : "", opt.heldValues[j]);
                    printf("]%s -> win %.2f%% (tie %.2f%%)\n",
                           opt.heldValues.size() == 5 ? " (stop rerolling)" : "", opt.winProb * 100.0f, opt.tieProb * 100.0f);
                }
            } else {
                printf("Category recommendations:\n");
                for (const auto& opt : r.categoryOptions) {
                    printf("  %-14s score %3d -> win %.2f%% (tie %.2f%%)\n",
                           CategoryNames[opt.category], opt.resultingScore, opt.winProb * 100.0f, opt.tieProb * 100.0f);
                }
            }
        }
        return 0;
    }

    std::vector<float> dp = loadOrSolveDP(t, dpCachePath);

    QueryResult r = query(t, dp, usedMask, upperTotal, dice, rerollsLeft);

    if (jsonOutput) {
        printf("{\"isRerollDecision\":%s,", r.isRerollDecision ? "true" : "false");
        if (r.isRerollDecision) {
            printf("\"rerollOptions\":[");
            for (size_t i = 0; i < r.rerollOptions.size(); i++) {
                const auto& opt = r.rerollOptions[i];
                printf("%s{\"holdValues\":[", i ? "," : "");
                for (size_t j = 0; j < opt.heldValues.size(); j++)
                    printf("%s%d", j ? "," : "", opt.heldValues[j]);
                printf("],\"expectedValue\":%f}", opt.expectedValue);
            }
            printf("]");
        } else {
            printf("\"categoryOptions\":[");
            for (size_t i = 0; i < r.categoryOptions.size(); i++) {
                const auto& opt = r.categoryOptions[i];
                printf("%s{\"category\":%d,\"categoryName\":\"%s\",\"resultingScore\":%d,\"expectedValue\":%f}",
                       i ? "," : "", opt.category, CategoryNames[opt.category], opt.resultingScore, opt.expectedValue);
            }
            printf("]");
        }
        printf("}\n");
    } else {
        if (r.isRerollDecision) {
            printf("Reroll recommendations (rerolls left: %d):\n", rerollsLeft);
            for (const auto& opt : r.rerollOptions) {
                printf("  hold [");
                for (size_t j = 0; j < opt.heldValues.size(); j++)
                    printf("%s%d", j ? "," : "", opt.heldValues[j]);
                printf("]%s -> expected value %.2f\n",
                       opt.heldValues.size() == 5 ? " (stop rerolling)" : "", opt.expectedValue);
            }
        } else {
            printf("Category recommendations:\n");
            for (const auto& opt : r.categoryOptions) {
                printf("  %-14s score %3d -> expected value %.2f\n",
                       CategoryNames[opt.category], opt.resultingScore, opt.expectedValue);
            }
        }
    }
    return 0;
}
```

- [ ] **Step 3: Build**

Run: `make yatzy_cpu`
Expected: builds clean.

- [ ] **Step 4: Add a smoke test to `test_yatzy_cli.sh`**

Append, before the final `echo "test_yatzy_cli: passed"`:

```bash
rm -f test_cli_winprob_cache.bin

# Only Chance open for me (dice 3,3,3,3,3 -> score 15, deterministic).
# Opponent fully done. --winprob-max-popcount 0 keeps this to a
# terminal-only solve (queryForWin only ever reads masks strictly below
# my open mask's popcount — here that's just mask 0).
win_output=$(./yatzy_cpu --used 0,1,2,3,4,5,6,7,8,9,10,11,12,14 --upper 0 \
  --dice 3,3,3,3,3 --rerolls 0 \
  --opp-used 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14 --opp-upper 0 \
  --my-banked 190 --opp-banked 200 \
  --winprob-cache test_cli_winprob_cache.bin --winprob-max-popcount 0 \
  --json 2>/dev/null)

echo "$win_output" | grep -q '"winProb":1.000000' || { echo "FAIL: expected winProb 1.0 when ahead"; exit 1; }

lose_output=$(./yatzy_cpu --used 0,1,2,3,4,5,6,7,8,9,10,11,12,14 --upper 0 \
  --dice 3,3,3,3,3 --rerolls 0 \
  --opp-used 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14 --opp-upper 0 \
  --my-banked 190 --opp-banked 210 \
  --winprob-cache test_cli_winprob_cache.bin --winprob-max-popcount 0 \
  --json 2>/dev/null)

echo "$lose_output" | grep -q '"winProb":0.000000' || { echo "FAIL: expected winProb 0.0 when behind"; exit 1; }

rm -f test_cli_winprob_cache.bin
```

- [ ] **Step 5: Run**

Run: `make test_yatzy`
Expected: `test_precompute_std`, `test_yatzy_engine` (8/8), and `test_yatzy_cli` (now including the win-prob checks) all pass.

- [ ] **Step 6: Commit**

```bash
git add yatzy_cpu.cpp test_yatzy_cli.sh
git commit -m "feat(cli): expose win-probability-ranked queries via --opp-*/--*-banked flags"
```

---

### Task 5: Full-scale benchmark (manual — not part of `make test_yatzy`)

**Files:** none (verification only).

**Interfaces:** none.

- [ ] **Step 1: Run the real full solve once**

```bash
rm -f bench_winprob.bin
time ./yatzy_cpu --used 0,3,7 --upper 12 --dice 2,2,3,5,6 --rerolls 2 \
  --opp-used 0,3,7 --opp-upper 12 --my-banked 0 --opp-banked 0 \
  --winprob-cache bench_winprob.bin
```

Record: wall-clock time, peak RSS (`/usr/bin/time -v` if available), and the resulting `bench_winprob.bin` file size (expect ~3.1GB per this plan's float32 choice).

- [ ] **Step 2: Apply the decision rule**

- If the full solve completes in well under the existing DP's "couple of minutes" order of magnitude scaled by the expected ~375x factor (i.e. comfortably under a few hours) **and** peak memory fits the development machine: ship as-is, no further work needed before a follow-up GUI-integration spec.
- If the solve is impractically slow (many hours) or memory is a real constraint: the follow-up work is (a) reduce `NumThresholds` resolution via a stride (e.g. every 2 points, halving both memory and compute) and re-run this benchmark, or (b) quantize the on-disk/in-memory representation to `uint8` per the original spec's proposal (this plan deliberately deferred that to keep the algorithm's first implementation simple). Do not attempt both at once — measure after each change.

- [ ] **Step 3: Record the result**

Add a short note to the top of [2026-07-15-yatzy-win-probability-strategy-design.md](../specs/2026-07-15-yatzy-win-probability-strategy-design.md) (the "Performance and storage" section) with the measured numbers, so the follow-up GUI-integration spec doesn't have to re-derive them.

```bash
rm -f bench_winprob.bin
```
