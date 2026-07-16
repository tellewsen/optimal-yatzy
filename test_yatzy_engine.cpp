// test_yatzy_engine.cpp — verifies the backward-induction DP solver and the
// query() recommendation engine for standard Yatzy.
#include "precompute_std.h"
#include "yatzy_engine.h"
#include <array>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

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

void test_dp_cache_roundtrip() {
    FlatTables t = buildFlatTables();
    const std::string path = "test_dp_cache_roundtrip.bin";
    std::remove(path.c_str());

    auto t0 = std::chrono::steady_clock::now();
    std::vector<float> dp1 = loadOrSolveDP(t, path);
    auto t1 = std::chrono::steady_clock::now();
    double firstSeconds = std::chrono::duration<double>(t1 - t0).count();

    FILE* f = fopen(path.c_str(), "rb");
    assert(f != nullptr);
    fclose(f);

    auto t2 = std::chrono::steady_clock::now();
    std::vector<float> dp2 = loadOrSolveDP(t, path);
    auto t3 = std::chrono::steady_clock::now();
    double secondSeconds = std::chrono::duration<double>(t3 - t2).count();

    assert(dp1.size() == dp2.size());
    for (size_t i = 0; i < dp1.size(); i++) assert(dp1[i] == dp2[i]);

    printf("cache roundtrip: first solve %.2fs, second (cached) load %.2fs\n", firstSeconds, secondSeconds);
    // The cached load must be dramatically faster than the fresh solve —
    // this is what actually proves the cache path was taken, not just that
    // solveDP is deterministic.
    assert(secondSeconds < firstSeconds / 10.0);

    std::remove(path.c_str());
    printf("test_dp_cache_roundtrip: passed\n");
}

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
    // row[0] is a tt==0 reroll-probability-sum artifact: computeVFromSubsetsCurve
    // sums resultProb-weighted child values that should total exactly 1.0, but
    // float32 summation of several resultProb terms doesn't always land exactly
    // on 1.0 (observed overshoot ~1.19e-6 here) — tolerance is warranted only
    // for this specific tt==0 sum, not as a general floating-point caution.
    assert(fabsf(row[0] - 1.0f) < 1e-5f);
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
                // Verified necessary (not inherited caution): an exact
                // row[tt] >= row[tt+1] check fails in practice for masks with
                // popcount <= 3 — a standalone probe found violations up to
                // ~1e-6 in magnitude, consistent with float32 rounding noise
                // accumulating across the backward-induction levels.
                for (int tt = 0; tt < NumThresholds - 1; tt++)
                    assert(row[tt] >= row[tt + 1] - 1e-5f);
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

int main() {
    test_full_game_ev();
    test_dp_cache_roundtrip();
    test_category_recommendation();
    test_reroll_recommendation();
    test_winprob_terminal_and_yatzy_probability();
    test_winprob_monotonic_and_consistent_with_ev();
    test_winprob_cache_roundtrip();
    printf("test_yatzy_engine: all tests passed\n");
    return 0;
}
