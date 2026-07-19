// yatzy_engine.cpp
#include "yatzy_engine.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <sstream>
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

// The cache header is a fixed-width uint64_t (NOT `size_t`): `size_t` is
// 8 bytes on the native (x86-64) build but only 4 bytes on the wasm32
// build, so a header written by one and read by the other would
// silently misalign every float that follows it — same value shifted
// by 4 bytes, not a crash or an empty read, just quietly wrong DP
// lookups. A fixed-width header keeps the on-disk format identical
// across architectures.
bool saveDP(const std::vector<float>& dp, const std::string& path) {
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return false;
    uint64_t n = (uint64_t)dp.size();
    fwrite(&n, sizeof(uint64_t), 1, f);
    fwrite(dp.data(), sizeof(float), dp.size(), f);
    fclose(f);
    return true;
}

bool loadDP(std::vector<float>& dp, const std::string& path, size_t expectedSize) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    uint64_t n = 0;
    if (fread(&n, sizeof(uint64_t), 1, f) != 1 || n != (uint64_t)expectedSize) { fclose(f); return false; }
    dp.resize((size_t)n);
    size_t read = fread(dp.data(), sizeof(float), (size_t)n, f);
    fclose(f);
    return read == (size_t)n;
}

std::vector<float> loadOrSolveDP(const FlatTables& t, const std::string& path) {
    size_t expectedSize = ((size_t)1 << NumCats) * (size_t)(CapScore + 1);
    std::vector<float> dp;
    if (loadDP(dp, path, expectedSize)) return dp;
    dp = solveDP(t);
    saveDP(dp, path);
    return dp;
}

bool saveWinProbDP(const std::vector<float>& wp, int maxPopcount, const std::string& path) {
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return false;
    size_t n = wp.size();
    fwrite(&n, sizeof(size_t), 1, f);
    fwrite(&maxPopcount, sizeof(int), 1, f);
    fwrite(wp.data(), sizeof(float), n, f);
    fclose(f);
    return true;
}

bool loadWinProbDP(std::vector<float>& wp, int& savedMaxPopcount, const std::string& path, size_t expectedSize) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    size_t n = 0;
    if (fread(&n, sizeof(size_t), 1, f) != 1 || n != expectedSize) { fclose(f); return false; }
    if (fread(&savedMaxPopcount, sizeof(int), 1, f) != 1) { fclose(f); return false; }
    wp.resize(n);
    size_t read = fread(wp.data(), sizeof(float), n, f);
    fclose(f);
    return read == n;
}

std::vector<float> loadOrSolveWinProbDP(const FlatTables& t, const std::string& path, int maxPopcount) {
    size_t expectedSize = ((size_t)1 << NumCats) * (size_t)(CapScore + 1) * (size_t)NumThresholds;
    std::vector<float> wp;
    int savedMaxPopcount = -1;
    if (loadWinProbDP(wp, savedMaxPopcount, path, expectedSize) && savedMaxPopcount >= maxPopcount) return wp;
    wp = solveWinProbDP(t, maxPopcount);
    saveWinProbDP(wp, maxPopcount, path);
    return wp;
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

std::string queryResultToJson(const QueryResult& r) {
    char buf[64];
    std::ostringstream out;
    out << "{\"isRerollDecision\":" << (r.isRerollDecision ? "true" : "false") << ",";
    if (r.isRerollDecision) {
        out << "\"rerollOptions\":[";
        for (size_t i = 0; i < r.rerollOptions.size(); i++) {
            const auto& opt = r.rerollOptions[i];
            out << (i ? "," : "") << "{\"holdValues\":[";
            for (size_t j = 0; j < opt.heldValues.size(); j++)
                out << (j ? "," : "") << opt.heldValues[j];
            std::snprintf(buf, sizeof(buf), "%f", opt.expectedValue);
            out << "],\"expectedValue\":" << buf << "}";
        }
        out << "]";
    } else {
        out << "\"categoryOptions\":[";
        for (size_t i = 0; i < r.categoryOptions.size(); i++) {
            const auto& opt = r.categoryOptions[i];
            std::snprintf(buf, sizeof(buf), "%f", opt.expectedValue);
            out << (i ? "," : "") << "{\"category\":" << opt.category
                << ",\"categoryName\":\"" << CategoryNames[opt.category]
                << "\",\"resultingScore\":" << opt.resultingScore
                << ",\"expectedValue\":" << buf << "}";
        }
        out << "]";
    }
    out << "}";
    return out.str();
}
