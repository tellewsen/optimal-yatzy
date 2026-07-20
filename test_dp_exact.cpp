// test_dp_exact.cpp — tight-tolerance checks on the solved DP table itself.
// Unlike test_yatzy_engine.cpp's wide-tolerance smoke test, these compare
// specific dp[] cells against closed-form probabilities derived by hand,
// independently of solveDP()'s own recursion — so a bug that's internally
// self-consistent (and would pass a pure regression check) can still be
// caught here. No RNG, no simulation: deterministic, runs in CI.
#include "precompute_std.h"
#include "yatzy_engine.h"
#include <cassert>
#include <cmath>
#include <cstdio>

namespace {

size_t Stride1 = (size_t)(CapScore + 1);

bool close(float actual, double expected, double tol) {
    return std::fabs((double)actual - expected) <= tol;
}

// Base case: with no categories left, dp[0][s] is just the bonus (or not) —
// no randomness, no recursion. Verifies solveDP's hard-coded base case and
// that the cap logic actually reaches CapScore for the "earned" case.
void test_base_case_bonus() {
    FlatTables t = buildFlatTables();
    std::vector<float> dp = loadOrSolveDP(t, "test_dp_cache_shared.bin");

    assert(dp[(size_t)0 * Stride1 + CapScore] == 50.0f);
    for (int s : {0, 30, 62}) {
        assert(dp[(size_t)0 * Stride1 + s] == 0.0f);
    }
    printf("test_base_case_bonus: passed\n");
}

// With only "Ones" open, holding every die that already shows 1 and
// rerolling the rest (for up to two rerolls) stochastically dominates any
// other strategy in count-of-1s, hence in score, hence in score+bonus —
// so it's optimal regardless of s. Each die then independently ends as a 1
// with probability 1-(5/6)^3 (three independent chances, since a held 1
// is never rerolled and a non-1 is rerolled every time). At s=0, scoring
// Ones (max 5 points) can never approach the s=63 bonus threshold, so the
// bonus term drops out entirely: dp[{Ones}][0] == 5 * (1-(5/6)^3).
void test_ones_category_no_bonus_interaction() {
    FlatTables t = buildFlatTables();
    std::vector<float> dp = loadOrSolveDP(t, "test_dp_cache_shared.bin");

    double pOne = 1.0 - std::pow(5.0 / 6.0, 3.0);
    double expected = 5.0 * pOne; // == 455/216 ~= 2.106481
    int mask = 1 << CatOnes;
    float actual = dp[(size_t)mask * Stride1 + 0];
    printf("dp[{Ones}][0] = %f (expected %f)\n", actual, expected);
    assert(close(actual, expected, 1e-3));
    printf("test_ones_category_no_bonus_interaction: passed\n");
}

// Same argument as above but for Sixes (score = count * 6); an independent
// spot check that the per-face scoring multiplier is wired correctly, not
// just the counting logic.
void test_sixes_category_no_bonus_interaction() {
    FlatTables t = buildFlatTables();
    std::vector<float> dp = loadOrSolveDP(t, "test_dp_cache_shared.bin");

    double pSix = 1.0 - std::pow(5.0 / 6.0, 3.0);
    double expected = 6.0 * 5.0 * pSix; // == 2730/216 ~= 12.638889
    int mask = 1 << CatSixes;
    float actual = dp[(size_t)mask * Stride1 + 0];
    printf("dp[{Sixes}][0] = %f (expected %f)\n", actual, expected);
    assert(close(actual, expected, 1e-3));
    printf("test_sixes_category_no_bonus_interaction: passed\n");
}

// At s=58, only rolling all five dice as 1s (raising the capped upper
// total from 58 to 63) earns the bonus; any other outcome (0-4 ones)
// leaves the bonus unearned. This exercises the s+pts capping/threshold
// logic in addition to the plain scoring check above: dp[{Ones}][58] ==
// 5*(1-(5/6)^3) + 50 * (1-(5/6)^3)^5.
void test_ones_category_bonus_threshold() {
    FlatTables t = buildFlatTables();
    std::vector<float> dp = loadOrSolveDP(t, "test_dp_cache_shared.bin");

    double pOne = 1.0 - std::pow(5.0 / 6.0, 3.0);
    double expected = 5.0 * pOne + 50.0 * std::pow(pOne, 5.0);
    int mask = 1 << CatOnes;
    float actual = dp[(size_t)mask * Stride1 + 58];
    printf("dp[{Ones}][58] = %f (expected %f)\n", actual, expected);
    assert(close(actual, expected, 1e-3));
    printf("test_ones_category_bonus_threshold: passed\n");
}

// Full-game expected value under optimal play, all 15 categories open.
// Tight-tolerance regression against a value measured directly from this
// solver (not an externally sourced constant) — this catches any future
// change that shifts the DP's answer, even slightly; test_full_game_ev in
// test_yatzy_engine.cpp keeps the wide sanity band for a from-scratch solve.
void test_full_game_ev_regression() {
    FlatTables t = buildFlatTables();
    std::vector<float> dp = loadOrSolveDP(t, "test_dp_cache_shared.bin");

    float actual = dp[(size_t)FullMask * Stride1 + 0];
    const double baseline = 248.440125; // measured 2026-07-20
    printf("dp[FullMask][0] = %f (regression baseline %f)\n", actual, baseline);
    assert(close(actual, baseline, 1e-3));
    printf("test_full_game_ev_regression: passed\n");
}

} // namespace

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    test_base_case_bonus();
    test_ones_category_no_bonus_interaction();
    test_sixes_category_no_bonus_interaction();
    test_ones_category_bonus_threshold();
    test_full_game_ev_regression();
    printf("all test_dp_exact checks passed\n");
    return 0;
}
