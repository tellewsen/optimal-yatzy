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
