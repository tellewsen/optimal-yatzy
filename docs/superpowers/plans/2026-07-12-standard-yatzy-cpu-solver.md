# Standard Yatzy CPU Solver Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a fully independent CPU-only solver for standard 5-dice Scandinavian Yatzy (15 categories), exposing a stateless query engine that answers "what's the optimal move given this game state?" — architected so a future desktop GUI can call the same engine via subprocess + JSON without a rewrite.

**Architecture:** Backward-induction DP identical in spirit to the existing (untested, GPU-only) Maxi solver, but for the much smaller standard-Yatzy state space (252 dice combos, 2^15 masks × 64 upper-total buckets), solved single-process on CPU with `std::thread` parallelism across masks within each popcount level. A stateless `query()` function reuses the DP table to answer both "which dice to hold" and "which category to score" questions on demand. A thin CLI wraps the engine with plain args-in/stdout-out semantics.

**Tech Stack:** C++17, g++, POSIX threads (`-pthread`). No CUDA, no external libraries, no test framework — plain `assert()`-based test binaries (matches this repo's existing "hand-verified" testing style noted in `README.md`).

## Global Constraints

- Do **not** modify `precompute.h`, `kernel.cu`, `main.cu`, or the existing `all`/`maxi_solver_gpu` Makefile target — the Maxi/GPU path is untested and explicitly out of scope (see `docs/superpowers/specs/2026-07-12-standard-yatzy-cpu-solver-design.md`).
- All new files are fully self-contained: `precompute_std.h` duplicates the small generic combinatorics helpers rather than sharing a header with `precompute.h`.
- C++17 throughout; compiled with `g++ -O3 -std=c++17 -Wall -pthread`.
- Category indices 0–14 and their names must exactly match the `CategoryNames` array defined in Task 1 — every later task's `--used` parsing, test cases, and output rely on this exact ordering: `0 Ones, 1 Twos, 2 Threes, 3 Fours, 4 Fives, 5 Sixes, 6 OnePair, 7 TwoPairs, 8 ThreeOfAKind, 9 FourOfAKind, 10 SmallStraight, 11 LargeStraight, 12 FullHouse, 13 Chance, 14 Yatzy`.
- The DP solve is expensive enough (~2.1M `(mask, s)` states) that test runs invoking `solveDP()` may take up to a couple of minutes on first run; this is expected, not a bug.

---

### Task 1: Standard-Yatzy combinatorics and scoring (`precompute_std.h`)

**Files:**
- Create: `precompute_std.h`
- Test: `test_precompute_std.cpp`

**Interfaces:**
- Produces: `NumDice=5`, `UpperCats=6`, `CapScore=63`, `Bonus=50`, `NumCats=15`, `enum Category {...}`, `CategoryNames[NumCats]`, `using Counts = std::array<int8_t,6>`, `struct CountsHash`, `scoreCategory(int cat, const Counts&) -> int`, `generateCombos(int n) -> std::vector<Counts>`, `multinomialWeight(const Counts&, int) -> double`, `struct FlatTables { int numCombos; std::vector<float> comboProb; std::vector<int> scoreTable; std::vector<int> subsetStart; std::vector<Counts> subsetCounts; std::vector<int> subsetResultStart; std::vector<int> resultComboID; std::vector<float> resultProb; std::unordered_map<Counts,int,CountsHash> comboIndex; }`, `buildFlatTables() -> FlatTables`.

- [ ] **Step 1: Write the failing test**

Create `test_precompute_std.cpp`:

```cpp
// test_precompute_std.cpp — hand-checked unit tests for standard-Yatzy
// scoring and combinatorics.
#include "precompute_std.h"
#include <cassert>
#include <cstdio>
#include <cmath>
#include <initializer_list>

static Counts makeCounts(std::initializer_list<int> faces1to6) {
    Counts c{};
    for (int f : faces1to6) c[f - 1]++;
    return c;
}

int main() {
    {
        Counts c = makeCounts({1,1,1,4,5});
        assert(scoreCategory(CatOnes, c) == 3);
    }
    {
        Counts c = makeCounts({4,4,4,2,2});
        assert(scoreCategory(CatFullHouse, c) == 16);
        assert(scoreCategory(CatThreeKind, c) == 12);
        assert(scoreCategory(CatOnePair, c) == 8);
    }
    {
        Counts c = makeCounts({4,4,4,2,6});
        assert(scoreCategory(CatFullHouse, c) == 0);
    }
    {
        Counts c = makeCounts({1,2,3,4,5});
        assert(scoreCategory(CatSmallStraight, c) == 15);
        assert(scoreCategory(CatLargeStraight, c) == 0);
    }
    {
        Counts c = makeCounts({2,3,4,5,6});
        assert(scoreCategory(CatLargeStraight, c) == 20);
        assert(scoreCategory(CatSmallStraight, c) == 0);
    }
    {
        Counts c = makeCounts({6,6,6,6,6});
        assert(scoreCategory(CatYatzy, c) == 50);
        assert(scoreCategory(CatChance, c) == 30);
    }
    {
        Counts c = makeCounts({5,5,3,3,1});
        assert(scoreCategory(CatTwoPairs, c) == 16);
    }

    auto combos = generateCombos(NumDice);
    assert(combos.size() == 252);

    double totalRolls = std::pow(6.0, NumDice);
    double probSum = 0.0;
    for (auto& c : combos) probSum += multinomialWeight(c, NumDice) / totalRolls;
    assert(std::fabs(probSum - 1.0) < 1e-9);

    FlatTables t = buildFlatTables();
    assert(t.numCombos == 252);
    assert((int)t.comboProb.size() == 252);
    assert((int)t.scoreTable.size() == 252 * NumCats);
    assert((int)t.subsetCounts.size() == t.subsetStart[t.numCombos]);
    float comboProbSum = 0.0f;
    for (float p : t.comboProb) comboProbSum += p;
    assert(std::fabs(comboProbSum - 1.0f) < 1e-4f);

    printf("test_precompute_std: all tests passed\n");
    return 0;
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `g++ -O3 -std=c++17 -Wall test_precompute_std.cpp -o test_precompute_std`
Expected: FAIL — `precompute_std.h: No such file or directory`

- [ ] **Step 3: Write the implementation**

Create `precompute_std.h`:

```cpp
// precompute_std.h — host-side combinatorics and scoring for standard
// Scandinavian Yatzy (5 dice, 15 categories). Fully independent of
// precompute.h (Norwegian Maxi Yatzy) — the generic combinatorics helpers
// are duplicated here rather than shared, so the untested Maxi/GPU code is
// never touched.
#pragma once
#include <vector>
#include <array>
#include <unordered_map>
#include <cmath>
#include <cstdint>
#include <functional>

constexpr int NumDice = 5;
constexpr int UpperCats = 6;
constexpr int CapScore = 63;
constexpr int Bonus = 50;

enum Category {
    CatOnes = 0, CatTwos, CatThrees, CatFours, CatFives, CatSixes,
    CatOnePair, CatTwoPairs,
    CatThreeKind, CatFourKind,
    CatSmallStraight, CatLargeStraight,
    CatFullHouse,
    CatChance, CatYatzy,
    NumCats
};

constexpr const char* CategoryNames[NumCats] = {
    "Ones", "Twos", "Threes", "Fours", "Fives", "Sixes",
    "OnePair", "TwoPairs",
    "ThreeOfAKind", "FourOfAKind",
    "SmallStraight", "LargeStraight",
    "FullHouse",
    "Chance", "Yatzy"
};

using Counts = std::array<int8_t, 6>;

struct CountsHash {
    size_t operator()(const Counts& c) const {
        size_t h = 0;
        for (auto v : c) h = h * 7 + v;
        return h;
    }
};

inline double factorial(int n) {
    double f = 1.0;
    for (int i = 2; i <= n; i++) f *= i;
    return f;
}

inline double multinomialWeight(const Counts& c, int n) {
    double w = factorial(n);
    for (auto v : c) w /= factorial(v);
    return w;
}

inline void generateCombosRec(int face, int remaining, Counts& cur, std::vector<Counts>& out) {
    if (face == 5) {
        cur[5] = (int8_t)remaining;
        out.push_back(cur);
        return;
    }
    for (int i = 0; i <= remaining; i++) {
        cur[face] = (int8_t)i;
        generateCombosRec(face + 1, remaining - i, cur, out);
    }
}

inline std::vector<Counts> generateCombos(int n) {
    std::vector<Counts> out;
    Counts cur{};
    generateCombosRec(0, n, cur, out);
    return out;
}

inline int sumDice(const Counts& c) {
    int s = 0;
    for (int i = 0; i < 6; i++) s += (i + 1) * c[i];
    return s;
}

// highest-first list of faces (1-indexed) whose count >= minCount
inline std::vector<int> topFaces(const Counts& c, int minCount) {
    std::vector<int> res;
    for (int f = 5; f >= 0; f--)
        if (c[f] >= minCount) res.push_back(f + 1);
    return res;
}

inline int scoreCategory(int cat, const Counts& c) {
    switch (cat) {
        case CatOnes:   return c[0] * 1;
        case CatTwos:   return c[1] * 2;
        case CatThrees: return c[2] * 3;
        case CatFours:  return c[3] * 4;
        case CatFives:  return c[4] * 5;
        case CatSixes:  return c[5] * 6;
        case CatOnePair: {
            auto f = topFaces(c, 2);
            return f.empty() ? 0 : f[0] * 2;
        }
        case CatTwoPairs: {
            auto f = topFaces(c, 2);
            return f.size() >= 2 ? (f[0] + f[1]) * 2 : 0;
        }
        case CatThreeKind: {
            auto f = topFaces(c, 3);
            return f.empty() ? 0 : f[0] * 3;
        }
        case CatFourKind: {
            auto f = topFaces(c, 4);
            return f.empty() ? 0 : f[0] * 4;
        }
        case CatSmallStraight: {
            for (int f = 0; f <= 4; f++) if (c[f] < 1) return 0;
            return 15;
        }
        case CatLargeStraight: {
            for (int f = 1; f <= 5; f++) if (c[f] < 1) return 0;
            return 20;
        }
        case CatFullHouse: {
            bool has3 = false, has2 = false;
            for (int f = 0; f < 6; f++) { if (c[f] == 3) has3 = true; if (c[f] == 2) has2 = true; }
            return (has3 && has2) ? sumDice(c) : 0;
        }
        case CatChance: return sumDice(c);
        case CatYatzy: {
            for (int f = 0; f < 6; f++) if (c[f] == 5) return 50;
            return 0;
        }
    }
    return 0;
}

// Flattened tables ready for the DP solver, built once at startup.
struct FlatTables {
    int numCombos;
    std::vector<float> comboProb;         // [numCombos]
    std::vector<int>   scoreTable;        // [numCombos * NumCats]
    std::vector<int>   subsetStart;       // [numCombos + 1]
    std::vector<Counts> subsetCounts;     // [numSubsets] — dice held per subset
    std::vector<int>   subsetResultStart; // [numSubsets + 1]
    std::vector<int>   resultComboID;     // [numResults]
    std::vector<float> resultProb;        // [numResults]
    std::unordered_map<Counts, int, CountsHash> comboIndex;
};

inline FlatTables buildFlatTables() {
    FlatTables t;
    auto allCombos = generateCombos(NumDice);
    t.numCombos = (int)allCombos.size();

    t.comboIndex.reserve(allCombos.size() * 2);
    for (int i = 0; i < t.numCombos; i++) t.comboIndex[allCombos[i]] = i;

    double totalRolls = std::pow(6.0, NumDice);
    t.comboProb.resize(t.numCombos);
    t.scoreTable.resize((size_t)t.numCombos * NumCats);
    for (int i = 0; i < t.numCombos; i++) {
        t.comboProb[i] = (float)(multinomialWeight(allCombos[i], NumDice) / totalRolls);
        for (int cat = 0; cat < NumCats; cat++)
            t.scoreTable[(size_t)i * NumCats + cat] = scoreCategory(cat, allCombos[i]);
    }

    std::vector<std::vector<Counts>> rerollCombos(NumDice + 1);
    std::vector<std::vector<float>>  rerollProb(NumDice + 1);
    for (int k = 0; k <= NumDice; k++) {
        rerollCombos[k] = generateCombos(k);
        double total = (k == 0) ? 1.0 : std::pow(6.0, k);
        rerollProb[k].resize(rerollCombos[k].size());
        for (size_t i = 0; i < rerollCombos[k].size(); i++)
            rerollProb[k][i] = (float)(multinomialWeight(rerollCombos[k][i], k) / total);
    }

    t.subsetStart.assign(t.numCombos + 1, 0);
    std::vector<std::vector<Counts>> keepSubsetsPerCombo(t.numCombos);

    for (int ci = 0; ci < t.numCombos; ci++) {
        const Counts& c = allCombos[ci];
        std::vector<Counts>& subs = keepSubsetsPerCombo[ci];
        Counts cur{};
        std::function<void(int)> rec = [&](int face) {
            if (face == 6) { subs.push_back(cur); return; }
            for (int8_t i = 0; i <= c[face]; i++) { cur[face] = i; rec(face + 1); }
        };
        rec(0);
        t.subsetStart[ci + 1] = t.subsetStart[ci] + (int)subs.size();
    }

    int numSubsets = t.subsetStart[t.numCombos];
    t.subsetCounts.resize(numSubsets);
    for (int ci = 0; ci < t.numCombos; ci++) {
        const auto& subs = keepSubsetsPerCombo[ci];
        for (size_t si = 0; si < subs.size(); si++)
            t.subsetCounts[t.subsetStart[ci] + si] = subs[si];
    }

    t.subsetResultStart.assign(numSubsets + 1, 0);

    std::vector<std::vector<std::pair<int,float>>> perSubsetResults(numSubsets);
    for (int ci = 0; ci < t.numCombos; ci++) {
        const auto& subs = keepSubsetsPerCombo[ci];
        for (size_t si = 0; si < subs.size(); si++) {
            int globalSubsetIdx = t.subsetStart[ci] + (int)si;
            int kept = 0;
            for (auto v : subs[si]) kept += v;
            int k = NumDice - kept;
            std::unordered_map<int, float> agg;
            agg.reserve(rerollCombos[k].size() * 2);
            for (size_t oi = 0; oi < rerollCombos[k].size(); oi++) {
                Counts nc = subs[si];
                for (int f = 0; f < 6; f++) nc[f] += rerollCombos[k][oi][f];
                int id = t.comboIndex[nc];
                agg[id] += rerollProb[k][oi];
            }
            auto& dst = perSubsetResults[globalSubsetIdx];
            dst.reserve(agg.size());
            for (auto& kv : agg) dst.push_back(kv);
            t.subsetResultStart[globalSubsetIdx + 1] = (int)dst.size();
        }
    }
    for (int i = 0; i < numSubsets; i++)
        t.subsetResultStart[i + 1] += t.subsetResultStart[i];

    int numResults = t.subsetResultStart[numSubsets];
    t.resultComboID.resize(numResults);
    t.resultProb.resize(numResults);
    for (int si = 0; si < numSubsets; si++) {
        int base = t.subsetResultStart[si];
        for (size_t j = 0; j < perSubsetResults[si].size(); j++) {
            t.resultComboID[base + j] = perSubsetResults[si][j].first;
            t.resultProb[base + j]    = perSubsetResults[si][j].second;
        }
    }

    return t;
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `g++ -O3 -std=c++17 -Wall test_precompute_std.cpp -o test_precompute_std && ./test_precompute_std`
Expected: `test_precompute_std: all tests passed`

- [ ] **Step 5: Commit**

```bash
git add precompute_std.h test_precompute_std.cpp
git commit -m "feat: add standard-Yatzy combinatorics and scoring tables"
```

---

### Task 2: Backward-induction DP solver (`yatzy_engine.h`/`.cpp`)

**Files:**
- Create: `yatzy_engine.h`
- Create: `yatzy_engine.cpp`
- Test: `test_yatzy_engine.cpp`

**Interfaces:**
- Consumes: `FlatTables`, `buildFlatTables()`, `NumCats`, `CapScore`, `Bonus` from `precompute_std.h` (Task 1).
- Produces: `constexpr long long FullMask`, `std::vector<float> solveDP(const FlatTables&)`.

- [ ] **Step 1: Write the failing test**

Create `test_yatzy_engine.cpp`:

```cpp
// test_yatzy_engine.cpp — verifies the backward-induction DP solver and the
// query() recommendation engine for standard Yatzy.
#include "precompute_std.h"
#include "yatzy_engine.h"
#include <cassert>
#include <cstdio>

void test_full_game_ev() {
    FlatTables t = buildFlatTables();
    std::vector<float> dp = solveDP(t);
    size_t stride1 = (size_t)(CapScore + 1);
    float ev = dp[(size_t)FullMask * stride1 + 0];
    printf("full-game expected value: %f\n", ev);
    // Sanity range around the commonly-cited ~248.75 optimal EV for
    // standard Scandinavian Yatzy solitaire play. Wide tolerance: this
    // catches gross bugs, it is not an exact-match regression test.
    assert(ev > 240.0f && ev < 255.0f);
    printf("test_full_game_ev: passed\n");
}

int main() {
    test_full_game_ev();
    printf("test_yatzy_engine: all tests passed\n");
    return 0;
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `g++ -O3 -std=c++17 -Wall -pthread test_yatzy_engine.cpp -o test_yatzy_engine`
Expected: FAIL — `yatzy_engine.h: No such file or directory`

- [ ] **Step 3: Write the implementation**

Create `yatzy_engine.h`:

```cpp
// yatzy_engine.h — backward-induction DP solver and query API for standard
// (5-dice, 15-category) Yatzy.
#pragma once
#include "precompute_std.h"
#include <vector>
#include <array>
#include <string>

constexpr long long FullMask = (1LL << NumCats) - 1;

// dp[mask * (CapScore+1) + s] = expected value of optimal play from here,
// given `mask` = categories still open and `s` = capped upper-section total
// so far (0..CapScore). Runs on CPU with std::thread parallelism across
// masks within each popcount level; may take up to a couple of minutes.
std::vector<float> solveDP(const FlatTables& t);

struct RerollOption {
    std::vector<int> heldValues; // sorted dice values kept; reroll the rest
    float expectedValue;
};

struct CategoryOption {
    int category;
    int resultingScore;
    float expectedValue; // resultingScore + DP value of the state after scoring
};

struct QueryResult {
    bool isRerollDecision;
    std::vector<RerollOption> rerollOptions;     // populated iff isRerollDecision
    std::vector<CategoryOption> categoryOptions; // populated iff !isRerollDecision
};

// usedMask: bit i set means category i already scored.
// upperTotal: raw running upper-section total so far (uncapped).
// dice: five values, each 1-6.
// rerollsLeft: 2 right after the first roll, 1 after one reroll, 0 = must score.
QueryResult query(const FlatTables& t, const std::vector<float>& dp,
                   int usedMask, int upperTotal, const std::array<int,5>& dice,
                   int rerollsLeft);
```

Create `yatzy_engine.cpp`:

```cpp
// yatzy_engine.cpp
#include "yatzy_engine.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <thread>

namespace {

int popcount(long long x) {
    int n = 0;
    while (x) { n += x & 1; x >>= 1; }
    return n;
}

// V0[ci] = value of combo ci if this were the final roll: best over open
// categories in `mask` of (points + DP value of the resulting state).
void computeV0(int mask, int s, const FlatTables& t, const std::vector<float>& dp, std::vector<float>& V0) {
    size_t stride1 = (size_t)(CapScore + 1);
    for (int ci = 0; ci < t.numCombos; ci++) {
        float best = -1.0f;
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
            float val = (float)pts + dp[(size_t)childMask * stride1 + ns];
            if (val > best) best = val;
        }
        V0[ci] = best;
    }
}

// Value of each combo when you may hold everything (falling back to V0) or
// pick one of its precomputed count-subsets to hold, rerolling the rest and
// taking the probability-weighted value from `Vdown` (V0 when computing V1,
// V1 when computing V2 — this matches the two-step reroll structure of
// Yatzy: at most 2 rerolls after the initial roll).
void computeVFromSubsets(const FlatTables& t, const std::vector<float>& Vdown, const std::vector<float>& V0, std::vector<float>& Vout) {
    for (int ci = 0; ci < t.numCombos; ci++) {
        float best = V0[ci];
        int ss = t.subsetStart[ci], se = t.subsetStart[ci + 1];
        for (int si = ss; si < se; si++) {
            int rs = t.subsetResultStart[si], re = t.subsetResultStart[si + 1];
            float val = 0.0f;
            for (int ri = rs; ri < re; ri++)
                val += t.resultProb[ri] * Vdown[t.resultComboID[ri]];
            if (val > best) best = val;
        }
        Vout[ci] = best;
    }
}

std::vector<int> countsToValues(const Counts& c) {
    std::vector<int> values;
    for (int f = 0; f < 6; f++)
        for (int k = 0; k < c[f]; k++)
            values.push_back(f + 1);
    return values;
}

} // namespace

std::vector<float> solveDP(const FlatTables& t) {
    size_t totalMasks = (size_t)1 << NumCats;
    size_t stride1 = (size_t)(CapScore + 1);
    size_t dpSize = totalMasks * stride1;
    std::vector<float> dp(dpSize, 0.0f);

    // mask == 0: no categories left; value is just the bonus, if earned.
    dp[(size_t)0 * stride1 + CapScore] = (float)Bonus;

    std::vector<std::vector<int>> masksByPopcount(NumCats + 1);
    for (long long mask = 0; mask <= FullMask; mask++)
        masksByPopcount[popcount(mask)].push_back((int)mask);

    unsigned numThreads = std::max(1u, std::thread::hardware_concurrency());

    for (int p = 1; p <= NumCats; p++) {
        auto& levelMasks = masksByPopcount[p];
        size_t n = levelMasks.size();
        size_t chunk = (n + numThreads - 1) / numThreads;

        auto worker = [&](size_t begin, size_t end) {
            std::vector<float> V0(t.numCombos), V1(t.numCombos), V2(t.numCombos);
            for (size_t idx = begin; idx < end; idx++) {
                int mask = levelMasks[idx];
                for (int s = 0; s <= CapScore; s++) {
                    computeV0(mask, s, t, dp, V0);
                    computeVFromSubsets(t, V0, V0, V1);
                    computeVFromSubsets(t, V1, V0, V2);
                    float expectation = 0.0f;
                    for (int ci = 0; ci < t.numCombos; ci++)
                        expectation += t.comboProb[ci] * V2[ci];
                    dp[(size_t)mask * stride1 + s] = expectation;
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

        fprintf(stderr, "popcount %2d/%d done (%zu masks)\n", p, NumCats, n);
    }
    return dp;
}

QueryResult query(const FlatTables& t, const std::vector<float>& dp,
                   int usedMask, int upperTotal, const std::array<int,5>& dice,
                   int rerollsLeft) {
    int openMask = (int)FullMask & ~usedMask;
    int cappedS = upperTotal > CapScore ? CapScore : upperTotal;

    Counts c{};
    for (int v : dice) c[v - 1]++;
    auto it = t.comboIndex.find(c);
    assert(it != t.comboIndex.end());
    int ci = it->second;

    QueryResult result;

    if (rerollsLeft == 0) {
        result.isRerollDecision = false;
        size_t stride1 = (size_t)(CapScore + 1);
        for (int m = openMask; m; m &= (m - 1)) {
            int bit = m & (-m);
            int cat = __builtin_ctz((unsigned)bit);
            int pts = t.scoreTable[(size_t)ci * NumCats + cat];
            int ns = cappedS;
            if (cat < UpperCats) {
                ns = cappedS + pts;
                if (ns > CapScore) ns = CapScore;
            }
            int childMask = openMask ^ bit;
            float ev = (float)pts + dp[(size_t)childMask * stride1 + ns];
            result.categoryOptions.push_back({cat, pts, ev});
        }
        std::sort(result.categoryOptions.begin(), result.categoryOptions.end(),
                   [](const CategoryOption& a, const CategoryOption& b) { return a.expectedValue > b.expectedValue; });
        return result;
    }

    result.isRerollDecision = true;
    std::vector<float> V0(t.numCombos);
    computeV0(openMask, cappedS, t, dp, V0);

    std::vector<float> V1;
    const std::vector<float>* Vdown = &V0;
    if (rerollsLeft == 2) {
        V1.resize(t.numCombos);
        computeVFromSubsets(t, V0, V0, V1);
        Vdown = &V1;
    }

    int ss = t.subsetStart[ci], se = t.subsetStart[ci + 1];
    for (int si = ss; si < se; si++) {
        int rs = t.subsetResultStart[si], re = t.subsetResultStart[si + 1];
        float val = 0.0f;
        for (int ri = rs; ri < re; ri++)
            val += t.resultProb[ri] * (*Vdown)[t.resultComboID[ri]];
        RerollOption opt;
        opt.heldValues = countsToValues(t.subsetCounts[si]);
        opt.expectedValue = val;
        result.rerollOptions.push_back(opt);
    }
    std::sort(result.rerollOptions.begin(), result.rerollOptions.end(),
               [](const RerollOption& a, const RerollOption& b) { return a.expectedValue > b.expectedValue; });
    return result;
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `g++ -O3 -std=c++17 -Wall -pthread test_yatzy_engine.cpp yatzy_engine.cpp -o test_yatzy_engine && ./test_yatzy_engine`
Expected: prints `full-game expected value: <number between 240 and 255>`, then `test_full_game_ev: passed` and `test_yatzy_engine: all tests passed`. May take up to a couple of minutes.

- [ ] **Step 5: Commit**

```bash
git add yatzy_engine.h yatzy_engine.cpp test_yatzy_engine.cpp
git commit -m "feat: add backward-induction DP solver for standard Yatzy"
```

---

### Task 3: DP table disk cache

**Files:**
- Modify: `yatzy_engine.h`
- Modify: `yatzy_engine.cpp`
- Modify: `test_yatzy_engine.cpp`

**Interfaces:**
- Consumes: `solveDP` (Task 2).
- Produces: `bool saveDP(const std::vector<float>&, const std::string&)`, `bool loadDP(std::vector<float>&, const std::string&, size_t)`, `std::vector<float> loadOrSolveDP(const FlatTables&, const std::string&)`.

- [ ] **Step 1: Write the failing test**

Add to `test_yatzy_engine.cpp` (above `int main()`):

```cpp
void test_dp_cache_roundtrip() {
    FlatTables t = buildFlatTables();
    const std::string path = "test_dp_cache_roundtrip.bin";
    std::remove(path.c_str());

    std::vector<float> dp1 = loadOrSolveDP(t, path);
    FILE* f = fopen(path.c_str(), "rb");
    assert(f != nullptr);
    fclose(f);

    std::vector<float> dp2 = loadOrSolveDP(t, path);
    assert(dp1.size() == dp2.size());
    for (size_t i = 0; i < dp1.size(); i++) assert(dp1[i] == dp2[i]);

    std::remove(path.c_str());
    printf("test_dp_cache_roundtrip: passed\n");
}
```

Update `main()`:

```cpp
int main() {
    test_full_game_ev();
    test_dp_cache_roundtrip();
    printf("test_yatzy_engine: all tests passed\n");
    return 0;
}
```

Also add `#include <cstdlib>` and `#include <string>` to the top of `test_yatzy_engine.cpp`.

- [ ] **Step 2: Run test to verify it fails**

Run: `g++ -O3 -std=c++17 -Wall -pthread test_yatzy_engine.cpp yatzy_engine.cpp -o test_yatzy_engine`
Expected: FAIL — `'loadOrSolveDP' was not declared in this scope`

- [ ] **Step 3: Write the implementation**

Add to `yatzy_engine.h` (below the `solveDP` declaration):

```cpp
bool saveDP(const std::vector<float>& dp, const std::string& path);
bool loadDP(std::vector<float>& dp, const std::string& path, size_t expectedSize);

// Loads dp from `path` if present and the right size; otherwise solves it
// and saves it there for next time.
std::vector<float> loadOrSolveDP(const FlatTables& t, const std::string& path);
```

Add `#include <cstdio>` to `yatzy_engine.h` if not already present via `precompute_std.h` transitively — add it explicitly to be safe.

Add to `yatzy_engine.cpp` (after `solveDP`, before `query`):

```cpp
bool saveDP(const std::vector<float>& dp, const std::string& path) {
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return false;
    size_t n = dp.size();
    fwrite(&n, sizeof(size_t), 1, f);
    fwrite(dp.data(), sizeof(float), n, f);
    fclose(f);
    return true;
}

bool loadDP(std::vector<float>& dp, const std::string& path, size_t expectedSize) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    size_t n = 0;
    if (fread(&n, sizeof(size_t), 1, f) != 1 || n != expectedSize) { fclose(f); return false; }
    dp.resize(n);
    size_t read = fread(dp.data(), sizeof(float), n, f);
    fclose(f);
    return read == n;
}

std::vector<float> loadOrSolveDP(const FlatTables& t, const std::string& path) {
    size_t expectedSize = ((size_t)1 << NumCats) * (size_t)(CapScore + 1);
    std::vector<float> dp;
    if (loadDP(dp, path, expectedSize)) return dp;
    dp = solveDP(t);
    saveDP(dp, path);
    return dp;
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `g++ -O3 -std=c++17 -Wall -pthread test_yatzy_engine.cpp yatzy_engine.cpp -o test_yatzy_engine && ./test_yatzy_engine`
Expected: both `test_full_game_ev: passed` and `test_dp_cache_roundtrip: passed`, then `test_yatzy_engine: all tests passed`.

- [ ] **Step 5: Commit**

```bash
git add yatzy_engine.h yatzy_engine.cpp test_yatzy_engine.cpp
git commit -m "feat: cache the solved DP table to disk"
```

---

### Task 4: Category recommendation query (rerolls remaining == 0)

**Files:**
- Modify: `test_yatzy_engine.cpp`

**Interfaces:**
- Consumes: `query()`, `QueryResult`, `CategoryOption`, `loadOrSolveDP` (Tasks 2-3), `CatYatzy`, `CatChance`, `FullMask`.

`query()`'s `rerollsLeft == 0` branch and the category-ranking logic were already written in Task 2's `yatzy_engine.cpp` (they're part of the same function as the reroll branch, which Task 5 exercises) — this task adds the test that proves that branch correct in isolation before Task 5 builds on it.

- [ ] **Step 1: Write the failing test**

Add to `test_yatzy_engine.cpp` (above `int main()`), and add `#include <array>` to the top of the file:

```cpp
void test_category_recommendation() {
    FlatTables t = buildFlatTables();
    std::vector<float> dp = loadOrSolveDP(t, "test_dp_cache_shared.bin");

    // Only Yatzy (14) and Chance (13) open; a rolled Yatzy should recommend
    // scoring the Yatzy category over Chance.
    int usedMask = (int)FullMask & ~((1 << CatYatzy) | (1 << CatChance));
    std::array<int,5> dice = {6,6,6,6,6};
    QueryResult r = query(t, dp, usedMask, 0, dice, 0);

    assert(!r.isRerollDecision);
    assert(r.categoryOptions.size() == 2);
    assert(r.categoryOptions[0].category == CatYatzy);
    assert(r.categoryOptions[0].resultingScore == 50);

    printf("test_category_recommendation: passed\n");
}
```

Update `main()`:

```cpp
int main() {
    test_full_game_ev();
    test_dp_cache_roundtrip();
    test_category_recommendation();
    printf("test_yatzy_engine: all tests passed\n");
    return 0;
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `g++ -O3 -std=c++17 -Wall -pthread test_yatzy_engine.cpp yatzy_engine.cpp -o test_yatzy_engine`
Expected: FAIL — `'CatYatzy' was not declared in this scope` (only if `precompute_std.h` isn't included — it is, transitively via `yatzy_engine.h`, so this should actually compile; if it does, this step's "expected failure" is instead a runtime assertion failure if the query logic were wrong. Compile first to confirm no build errors, then treat this step as verifying the test *can* fail by temporarily checking `r.categoryOptions[0].category == CatChance` — flip it back before Step 3's real run.)

- [ ] **Step 3: Confirm implementation already satisfies the test**

`query()`'s `rerollsLeft == 0` branch already exists from Task 2. No production code changes needed.

- [ ] **Step 4: Run test to verify it passes**

Run: `g++ -O3 -std=c++17 -Wall -pthread test_yatzy_engine.cpp yatzy_engine.cpp -o test_yatzy_engine && ./test_yatzy_engine`
Expected: `test_category_recommendation: passed` among the output, then `test_yatzy_engine: all tests passed`.

- [ ] **Step 5: Commit**

```bash
git add test_yatzy_engine.cpp
git commit -m "test: verify category recommendation query"
```

---

### Task 5: Reroll recommendation query (rerolls remaining > 0)

**Files:**
- Modify: `test_yatzy_engine.cpp`

**Interfaces:**
- Consumes: `query()`, `QueryResult`, `RerollOption`, `loadOrSolveDP`, `CatYatzy`, `FullMask`.

Same situation as Task 4: `query()`'s `rerollsLeft > 0` branch was written in Task 2. This task adds the test proving it.

- [ ] **Step 1: Write the failing test**

Add to `test_yatzy_engine.cpp` (above `int main()`):

```cpp
void test_reroll_recommendation() {
    FlatTables t = buildFlatTables();
    std::vector<float> dp = loadOrSolveDP(t, "test_dp_cache_shared.bin");

    // Only Yatzy open, already rolled a Yatzy (6,6,6,6,6): holding all 5
    // dice must be the top-ranked choice for both reroll counts, since
    // rerolling can only ever match or destroy the Yatzy, never improve it.
    int usedMask = (int)FullMask & ~(1 << CatYatzy);
    std::array<int,5> dice = {6,6,6,6,6};

    for (int rerollsLeft : {2, 1}) {
        QueryResult r = query(t, dp, usedMask, 0, dice, rerollsLeft);
        assert(r.isRerollDecision);
        assert(!r.rerollOptions.empty());
        assert(r.rerollOptions[0].heldValues.size() == 5);
    }

    std::remove("test_dp_cache_shared.bin");
    printf("test_reroll_recommendation: passed\n");
}
```

Update `main()`:

```cpp
int main() {
    test_full_game_ev();
    test_dp_cache_roundtrip();
    test_category_recommendation();
    test_reroll_recommendation();
    printf("test_yatzy_engine: all tests passed\n");
    return 0;
}
```

- [ ] **Step 2: Run test to verify it fails**

Temporarily change the assertion to `assert(r.rerollOptions[0].heldValues.size() != 5);`, run:
Run: `g++ -O3 -std=c++17 -Wall -pthread test_yatzy_engine.cpp yatzy_engine.cpp -o test_yatzy_engine && ./test_yatzy_engine`
Expected: FAIL — assertion triggers (proving the test can actually fail). Revert the assertion back to `== 5` before continuing.

- [ ] **Step 3: Confirm implementation already satisfies the test**

`query()`'s `rerollsLeft > 0` branch already exists from Task 2. No production code changes needed.

- [ ] **Step 4: Run test to verify it passes**

Run: `g++ -O3 -std=c++17 -Wall -pthread test_yatzy_engine.cpp yatzy_engine.cpp -o test_yatzy_engine && ./test_yatzy_engine`
Expected: all five test lines print `passed`, ending with `test_yatzy_engine: all tests passed`.

- [ ] **Step 5: Commit**

```bash
git add test_yatzy_engine.cpp
git commit -m "test: verify reroll recommendation query"
```

---

### Task 6: CLI (`yatzy_cpu.cpp`)

**Files:**
- Create: `yatzy_cpu.cpp`
- Test: `test_yatzy_cli.sh`

**Interfaces:**
- Consumes: `buildFlatTables`, `loadOrSolveDP`, `query`, `QueryResult`, `CategoryNames`, `FullMask`, `NumCats` (Tasks 1-3).

- [ ] **Step 1: Write the failing test**

Create `test_yatzy_cli.sh`:

```bash
#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"

rm -f test_cli_dp_cache.bin

output=$(./yatzy_cpu --used 0,1,2,3,4,5,6,7,8,9,10,11,12,13 --upper 0 --dice 6,6,6,6,6 --rerolls 0 --dp-cache test_cli_dp_cache.bin 2>/dev/null)

echo "$output" | grep -q "Yatzy" || { echo "FAIL: expected Yatzy in output"; exit 1; }
echo "$output" | grep -qE "score[[:space:]]+50" || { echo "FAIL: expected score 50"; exit 1; }

rm -f test_cli_dp_cache.bin
echo "test_yatzy_cli: passed"
```

Run: `chmod +x test_yatzy_cli.sh && ./test_yatzy_cli.sh`
Expected: FAIL — `./yatzy_cpu: No such file or directory`

- [ ] **Step 2: Write the implementation**

Create `yatzy_cpu.cpp`:

```cpp
// yatzy_cpu.cpp — thin CLI over the standard-Yatzy engine. Stateless: every
// invocation is an independent query, no session memory. This is the seam a
// future GUI will call through: spawn this process, parse --json output.
#include "precompute_std.h"
#include "yatzy_engine.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::vector<int> parseIntList(const std::string& s) {
    std::vector<int> out;
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, ',')) out.push_back(std::atoi(item.c_str()));
    return out;
}

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

} // namespace

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

    if (diceArg.empty() || upperTotal < 0 || rerollsLeft < 0 || rerollsLeft > 2)
        printUsageAndExit(argv[0]);

    std::vector<int> diceVec = parseIntList(diceArg);
    if (diceVec.size() != 5) {
        fprintf(stderr, "error: --dice must have exactly 5 values\n");
        return 1;
    }
    std::array<int,5> dice;
    for (int i = 0; i < 5; i++) {
        if (diceVec[i] < 1 || diceVec[i] > 6) {
            fprintf(stderr, "error: dice values must be 1-6\n");
            return 1;
        }
        dice[i] = diceVec[i];
    }

    int usedMask = 0;
    if (!usedArg.empty()) {
        for (int cat : parseIntList(usedArg)) {
            if (cat < 0 || cat >= NumCats) {
                fprintf(stderr, "error: category index must be 0-%d\n", NumCats - 1);
                return 1;
            }
            usedMask |= (1 << cat);
        }
    }
    if (usedMask == (int)FullMask) {
        fprintf(stderr, "error: all categories already used, nothing to recommend\n");
        return 1;
    }

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

Build it directly for this step (Makefile target comes in Task 7):

Run: `g++ -O3 -std=c++17 -Wall -pthread yatzy_engine.cpp yatzy_cpu.cpp -o yatzy_cpu`

- [ ] **Step 3: Run test to verify it passes**

Run: `chmod +x test_yatzy_cli.sh && ./test_yatzy_cli.sh`
Expected: `test_yatzy_cli: passed` (first run solves the DP fresh into `test_cli_dp_cache.bin` before deleting it — may take up to a couple of minutes).

- [ ] **Step 4: Commit**

```bash
git add yatzy_cpu.cpp test_yatzy_cli.sh
git commit -m "feat: add stateless CLI for the standard-Yatzy query engine"
```

---

### Task 7: Build integration, docs, and final verification

**Files:**
- Modify: `Makefile`
- Modify: `README.md`

**Interfaces:**
- Consumes: everything from Tasks 1-6.

- [ ] **Step 1: Update the Makefile**

Replace the full contents of `Makefile` with:

```makefile
# Adjust -arch to match your GPU's compute capability.
# RTX 4070 Ti Super (Ada Lovelace) = sm_89
NVCC := nvcc
ARCH := sm_89
CXXSTD := c++17

CXX := g++
CXXFLAGS_STD := -O3 -std=$(CXXSTD) -Wall -pthread

all: maxi_solver_gpu

maxi_solver_gpu: main.cu kernel.cu precompute.h
	$(NVCC) -O3 -std=$(CXXSTD) -arch=$(ARCH) -lineinfo main.cu kernel.cu -o maxi_solver_gpu

# --- standard (5-dice) Yatzy CPU solver — independent of the Maxi/GPU code above ---

yatzy_cpu: yatzy_cpu.cpp yatzy_engine.cpp yatzy_engine.h precompute_std.h
	$(CXX) $(CXXFLAGS_STD) yatzy_engine.cpp yatzy_cpu.cpp -o yatzy_cpu

test_precompute_std: test_precompute_std.cpp precompute_std.h
	$(CXX) $(CXXFLAGS_STD) test_precompute_std.cpp -o test_precompute_std

test_yatzy_engine: test_yatzy_engine.cpp yatzy_engine.cpp yatzy_engine.h precompute_std.h
	$(CXX) $(CXXFLAGS_STD) test_yatzy_engine.cpp yatzy_engine.cpp -o test_yatzy_engine

test_yatzy: test_precompute_std test_yatzy_engine yatzy_cpu
	./test_precompute_std
	./test_yatzy_engine
	./test_yatzy_cli.sh

clean:
	rm -f maxi_solver_gpu checkpoint_maxi_gpu.bin checkpoint_maxi_gpu.bin.tmp
	rm -f yatzy_cpu test_precompute_std test_yatzy_engine
	rm -f yatzy_cpu_dp.bin test_cli_dp_cache.bin

.PHONY: all clean test_yatzy
```

- [ ] **Step 2: Update the README**

Add a new section to `README.md`, after the existing `## Resuming` section:

```markdown

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
```

- [ ] **Step 3: Run the full test suite from a clean state**

```bash
rm -f yatzy_cpu test_precompute_std test_yatzy_engine yatzy_cpu_dp.bin test_cli_dp_cache.bin test_dp_cache_roundtrip.bin test_dp_cache_shared.bin
make test_yatzy
```

Expected: all of `test_precompute_std`, `test_yatzy_engine` (all 4 sub-tests), and `test_yatzy_cli.sh` print `passed`/`all tests passed`, exit code 0.

- [ ] **Step 4: Commit**

```bash
git add Makefile README.md
git commit -m "build: wire up standard-Yatzy CPU solver targets and docs"
```
