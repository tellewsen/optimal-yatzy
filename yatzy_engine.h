// yatzy_engine.h — backward-induction DP solver and query API for standard
// (5-dice, 15-category) Yatzy.
#pragma once
#include "precompute_std.h"
#include <vector>
#include <array>
#include <string>
#include <cstdio>

constexpr long long FullMask = (1LL << NumCats) - 1;

// dp[mask * (CapScore+1) + s] = expected value of optimal play from here,
// given `mask` = categories still open and `s` = capped upper-section total
// so far (0..CapScore). Runs on CPU with std::thread parallelism across
// masks within each popcount level; may take up to a couple of minutes.
std::vector<float> solveDP(const FlatTables& t);

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

// usedMask: bit i set means category i already scored.
// upperTotal: raw running upper-section total so far (uncapped).
// dice: five values, each 1-6.
// rerollsLeft: 2 right after the first roll, 1 after one reroll, 0 = must score.
QueryResult query(const FlatTables& t, const std::vector<float>& dp,
                   int usedMask, int upperTotal, const std::array<int,5>& dice,
                   int rerollsLeft);
