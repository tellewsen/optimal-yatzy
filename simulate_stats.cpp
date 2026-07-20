// simulate_stats.cpp — Monte Carlo end-to-end statistical validation of the
// standard-Yatzy engine. Unlike test_dp_exact.cpp (which reads dp[] cells
// directly), this actually plays full games by rolling real dice and
// following query()'s own top-ranked recommendation at every decision, then
// checks that the resulting score distribution's mean converges to the DP's
// exact predicted expected value. This is the strongest correctness check
// available: it validates that solveDP() + query() together produce a
// policy whose real-world performance matches what the DP claims, not just
// that the DP number itself looks plausible.
//
// Not part of `make test_yatzy` (that's fast, deterministic exact checks) —
// run manually: make simulate_stats && ./simulate_stats [--games N] [--seed S]
#include "precompute_std.h"
#include "yatzy_engine.h"
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <thread>
#include <vector>

namespace {

constexpr int HistBucketSize = 10;
constexpr int NumHistBuckets = (MaxRemainingScore + Bonus) / HistBucketSize + 2;

struct ThreadStats {
    long long n = 0;
    double sum = 0.0;
    double sumSq = 0.0;
    long long bonusCount = 0;
    std::array<double, NumCats> categorySum{};
    std::array<long long, NumHistBuckets> histogram{};
};

int rollDie(std::mt19937& rng) {
    static thread_local std::uniform_int_distribution<int> dist(1, 6);
    return dist(rng);
}

// Plays one full game by always taking query()'s top-ranked recommendation,
// rolling real dice via `rng`. Returns the final banked score (including
// the upper-section bonus, if earned).
int simulateOneGame(const FlatTables& t, const std::vector<float>& dp,
                     std::mt19937& rng, std::array<int, NumCats>& categoryScoreOut) {
    int usedMask = 0;
    int upperTotal = 0;
    categoryScoreOut.fill(0);

    while (usedMask != (int)FullMask) {
        std::array<int, 5> dice;
        for (int& d : dice) d = rollDie(rng);
        int rerollsLeft = 2;

        while (true) {
            QueryResult r = query(t, dp, usedMask, upperTotal, dice, rerollsLeft);
            if (r.isRerollDecision) {
                const RerollOption& top = r.rerollOptions[0];
                std::array<int, 5> newDice{};
                size_t numHeld = top.heldValues.size();
                for (size_t i = 0; i < numHeld; i++) newDice[i] = top.heldValues[i];
                for (size_t i = numHeld; i < 5; i++) newDice[i] = rollDie(rng);
                dice = newDice;
                rerollsLeft--;
            } else {
                const CategoryOption& top = r.categoryOptions[0];
                usedMask |= (1 << top.category);
                categoryScoreOut[top.category] = top.resultingScore;
                if (top.category < UpperCats) upperTotal += top.resultingScore;
                break;
            }
        }
    }

    int total = 0;
    for (int c = 0; c < NumCats; c++) total += categoryScoreOut[c];
    if (upperTotal >= CapScore) total += Bonus;
    return total;
}

ThreadStats runChunk(const FlatTables& t, const std::vector<float>& dp,
                      long long numGames, unsigned seed) {
    ThreadStats stats;
    std::mt19937 rng(seed);
    std::array<int, NumCats> categoryScore{};

    for (long long g = 0; g < numGames; g++) {
        int score = simulateOneGame(t, dp, rng, categoryScore);
        stats.n++;
        stats.sum += score;
        stats.sumSq += (double)score * score;
        int upperTotal = 0;
        for (int c = 0; c < UpperCats; c++) upperTotal += categoryScore[c];
        if (upperTotal >= CapScore) stats.bonusCount++;
        for (int c = 0; c < NumCats; c++) stats.categorySum[c] += categoryScore[c];
        int bucket = score / HistBucketSize;
        if (bucket < 0) bucket = 0;
        if (bucket >= NumHistBuckets) bucket = NumHistBuckets - 1;
        stats.histogram[bucket]++;
    }
    return stats;
}

void printUsageAndExit(const char* prog) {
    fprintf(stderr,
        "Usage: %s [--games N] [--seed S] [--dp-cache <path>]\n"
        "  --games N        total simulated games (default 50000)\n"
        "  --seed S         base RNG seed, for reproducibility (default 42)\n"
        "  --dp-cache PATH  DP cache file to load/solve (default yatzy_cpu_dp.bin)\n",
        prog);
    exit(1);
}

} // namespace

int main(int argc, char** argv) {
    long long totalGames = 50000;
    unsigned baseSeed = 42;
    std::string dpCachePath = "yatzy_cpu_dp.bin";

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--games" && i + 1 < argc) totalGames = std::atoll(argv[++i]);
        else if (arg == "--seed" && i + 1 < argc) baseSeed = (unsigned)std::atoll(argv[++i]);
        else if (arg == "--dp-cache" && i + 1 < argc) dpCachePath = argv[++i];
        else printUsageAndExit(argv[0]);
    }
    if (totalGames <= 0) printUsageAndExit(argv[0]);

    fprintf(stderr, "building tables and loading/solving DP (first run may take a minute)...\n");
    FlatTables t = buildFlatTables();
    std::vector<float> dp = loadOrSolveDP(t, dpCachePath);
    size_t stride1 = (size_t)(CapScore + 1);
    float exactEV = dp[(size_t)FullMask * stride1 + 0];

    unsigned numThreads = std::max(1u, std::thread::hardware_concurrency());
    if ((long long)numThreads > totalGames) numThreads = (unsigned)totalGames;

    std::vector<std::thread> threads;
    std::vector<ThreadStats> results(numThreads);
    long long base = totalGames / numThreads;
    long long remainder = totalGames % numThreads;

    auto t0 = std::chrono::steady_clock::now();
    for (unsigned i = 0; i < numThreads; i++) {
        long long chunk = base + (i < (unsigned)remainder ? 1 : 0);
        threads.emplace_back([&, i, chunk]() {
            results[i] = runChunk(t, dp, chunk, baseSeed + i * 7919u + 1u);
        });
    }
    for (auto& th : threads) th.join();
    auto t1 = std::chrono::steady_clock::now();
    double elapsedSec = std::chrono::duration<double>(t1 - t0).count();

    ThreadStats merged;
    for (const auto& r : results) {
        merged.n += r.n;
        merged.sum += r.sum;
        merged.sumSq += r.sumSq;
        merged.bonusCount += r.bonusCount;
        for (int c = 0; c < NumCats; c++) merged.categorySum[c] += r.categorySum[c];
        for (int b = 0; b < NumHistBuckets; b++) merged.histogram[b] += r.histogram[b];
    }

    double n = (double)merged.n;
    double mean = merged.sum / n;
    double variance = merged.sumSq / n - mean * mean;
    if (variance < 0.0) variance = 0.0; // guards against float roundoff
    double stddev = std::sqrt(variance);
    double stderrOfMean = stddev / std::sqrt(n);
    double ciHalfWidth = 1.96 * stderrOfMean;
    double ciLow = mean - ciHalfWidth, ciHigh = mean + ciHalfWidth;
    bool ok = exactEV >= ciLow && exactEV <= ciHigh;

    printf("=== Monte Carlo validation (%lld games, %u threads, %.2fs) ===\n",
           merged.n, numThreads, elapsedSec);
    printf("simulated mean score:  %.4f\n", mean);
    printf("simulated std dev:      %.4f\n", stddev);
    printf("95%% CI of the mean:    [%.4f, %.4f]\n", ciLow, ciHigh);
    printf("DP exact expected value: %.4f  -> %s\n", exactEV, ok ? "inside CI (PASS)" : "OUTSIDE CI (FAIL)");
    printf("\n");
    printf("upper-section bonus achieved: %.2f%% of games\n", 100.0 * merged.bonusCount / n);
    printf("\n");
    printf("per-category average score:\n");
    for (int c = 0; c < NumCats; c++)
        printf("  %-14s %6.3f\n", CategoryNames[c], merged.categorySum[c] / n);
    printf("\n");
    printf("score histogram (bucket size %d):\n", HistBucketSize);
    long long maxCount = 0;
    for (int b = 0; b < NumHistBuckets; b++) maxCount = std::max(maxCount, merged.histogram[b]);
    const int barWidth = 60;
    for (int b = 0; b < NumHistBuckets; b++) {
        if (merged.histogram[b] == 0) continue;
        int bars = (int)((double)merged.histogram[b] / maxCount * barWidth);
        if (bars == 0 && merged.histogram[b] > 0) bars = 1;
        printf("  %3d-%3d: %6lld ", b * HistBucketSize, b * HistBucketSize + HistBucketSize - 1, merged.histogram[b]);
        for (int k = 0; k < bars; k++) putchar('#');
        putchar('\n');
    }

    return ok ? 0 : 1;
}
