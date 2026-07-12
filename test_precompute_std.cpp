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
