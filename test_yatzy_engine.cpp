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

    // Checksum wp1 rather than keeping it around for a full element-wise
    // comparison against wp2 — the win-prob table is ~3.1GB, so holding two
    // copies simultaneously would double this test's peak memory to ~6.2GB.
    // A double-precision sum over the exact same float values, read back
    // byte-identical from disk, is exact (no reordering, no arithmetic
    // differences) — not weaker in practice than an element-wise compare
    // for a file-roundtrip bug, and it lets wp1 be freed before wp2 loads.
    size_t size1 = wp1.size();
    double checksum1 = 0.0;
    for (float v : wp1) checksum1 += v;
    wp1.clear();
    wp1.shrink_to_fit();

    std::vector<float> wp2 = loadOrSolveWinProbDP(t, path, 1);
    assert(wp2.size() == size1);
    double checksum2 = 0.0;
    for (float v : wp2) checksum2 += v;
    assert(checksum1 == checksum2);

    std::remove(path.c_str());
    printf("test_winprob_cache_roundtrip: passed\n");
}

void test_winprob_cache_rejects_shallower_solve() {
    FlatTables t = buildFlatTables();
    const std::string path = "test_winprob_cache_depth_check.bin";
    std::remove(path.c_str());

    // Solve+cache at a shallow depth first.
    loadOrSolveWinProbDP(t, path, 1);

    // Requesting a deeper solve against the same cache path must NOT
    // silently reuse the shallower cache (it's byte-size-identical to a
    // full solve, since solveWinProbDP always allocates the full-size
    // array regardless of maxPopcount) — it must detect the depth
    // mismatch and re-solve to the requested depth.
    std::vector<float> deeper = loadOrSolveWinProbDP(t, path, 3);

    // Spot-check a mask that only gets meaningfully solved at popcount 3
    // (three open categories) — before this fix, this would still show
    // the shallow solve's all-zero-past-threshold-0 pattern, because the
    // stale cache file would have been silently reused.
    int mask3 = (1 << CatOnes) | (1 << CatTwos) | (1 << CatThrees);
    size_t stride1 = (size_t)(CapScore + 1);
    size_t rowSize = (size_t)NumThresholds;
    const float* row = &deeper[((size_t)mask3 * stride1 + 0) * rowSize];
    // row[0] is NOT bit-exact here (unlike the mask==0 terminal case): for
    // any mask with real reroll choices, V1/V2's tt==0 column sums several
    // resultProb-weighted 1.0 terms that don't always land exactly on 1.0
    // in float32 — the same mechanism found for the single-Yatzy-open case
    // during Task 1's review, just general to any multi-category mask.
    // Empirically verified via a standalone probe: observed overshoot
    // ~1.19e-6 here, comfortably inside this tolerance.
    assert(fabsf(row[0] - 1.0f) < 1e-4f);
    bool foundNonzero = false;
    for (int tt = 1; tt < NumThresholds; tt++) if (row[tt] > 0.0f) { foundNonzero = true; break; }
    assert(foundNonzero);

    std::remove(path.c_str());
    printf("test_winprob_cache_rejects_shallower_solve: passed\n");
}

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

int main() {
    test_full_game_ev();
    test_dp_cache_roundtrip();
    test_category_recommendation();
    test_reroll_recommendation();
    test_winprob_terminal_and_yatzy_probability();
    test_winprob_monotonic_and_consistent_with_ev();
    test_winprob_cache_roundtrip();
    test_winprob_cache_rejects_shallower_solve();
    test_queryforwin_deterministic_outcomes();
    printf("test_yatzy_engine: all tests passed\n");
    return 0;
}
