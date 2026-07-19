// yatzy_engine.h — backward-induction DP solver and query API for standard
// (5-dice, 15-category) Yatzy.
#pragma once
#include "precompute_std.h"
#include <vector>
#include <array>
#include <string>
#include <cstdio>

constexpr long long FullMask = (1LL << NumCats) - 1;

// Theoretical maximum standard-Yatzy game score: 105 (upper pips, all
// categories maxed) + 50 (bonus) + 12 (one pair) + 22 (two pairs) + 18
// (three of a kind) + 24 (four of a kind) + 15 (small straight) + 20
// (large straight) + 28 (full house) + 30 (chance) + 50 (Yatzy) = 374.
constexpr int MaxRemainingScore = 374;
constexpr int NumThresholds = MaxRemainingScore + 1;

// dp[mask * (CapScore+1) + s] = expected value of optimal play from here,
// given `mask` = categories still open and `s` = capped upper-section total
// so far (0..CapScore). Runs on CPU with std::thread parallelism across
// masks within each popcount level; may take up to a couple of minutes.
std::vector<float> solveDP(const FlatTables& t);

// winProb[(mask * (CapScore+1) + s) * NumThresholds + t] = P(a policy that
// maximizes exactly this probability achieves remaining score >= t), for
// t in 0..MaxRemainingScore. `maxPopcount` limits the backward induction to
// masks with at most this many open categories — production callers always
// use the default (full solve); tests pass a small value to solve only a
// cheap slice of the table. Masks above maxPopcount are left as zero.
std::vector<float> solveWinProbDP(const FlatTables& t, int maxPopcount = NumCats);

// The cache file stores the solved maxPopcount alongside the table data so
// loadOrSolveWinProbDP can detect and reject a shallower-than-needed solve
// (a partial solve is byte-size-identical to a full one — masks above its
// maxPopcount are simply left at zero — so size alone can't distinguish
// them; without this check, reusing a partial-solve cache at the default
// path would silently return zero/wrong answers for high-popcount states).
bool saveWinProbDP(const std::vector<float>& wp, int maxPopcount, const std::string& path);
bool loadWinProbDP(std::vector<float>& wp, int& savedMaxPopcount, const std::string& path, size_t expectedSize);

// Loads wp from `path` if present, the right size, AND solved to at least
// `maxPopcount`; otherwise solves it (see solveWinProbDP's maxPopcount
// note) and saves it there for next time.
std::vector<float> loadOrSolveWinProbDP(const FlatTables& t, const std::string& path, int maxPopcount = NumCats);

bool saveDP(const std::vector<float>& dp, const std::string& path);
bool loadDP(std::vector<float>& dp, const std::string& path, size_t expectedSize);

// Loads dp from `path` if present and the right size; otherwise solves it
// and saves it there for next time.
std::vector<float> loadOrSolveDP(const FlatTables& t, const std::string& path);

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

std::string queryResultToJson(const QueryResult& r);

// usedMask: bit i set means category i already scored.
// upperTotal: raw running upper-section total so far (uncapped).
// dice: five values, each 1-6.
// rerollsLeft: 2 right after the first roll, 1 after one reroll, 0 = must score.
QueryResult query(const FlatTables& t, const std::vector<float>& dp,
                   int usedMask, int upperTotal, const std::array<int,5>& dice,
                   int rerollsLeft);

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
