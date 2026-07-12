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
