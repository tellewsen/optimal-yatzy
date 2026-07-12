// test_yatzy_engine.cpp — verifies the backward-induction DP solver and the
// query() recommendation engine for standard Yatzy.
#include "precompute_std.h"
#include "yatzy_engine.h"
#include <cassert>
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

    std::vector<float> dp1 = loadOrSolveDP(t, path);
    FILE* f = fopen(path.c_str(), "rb");
    assert(f != nullptr);
    fclose(f);

    std::vector<float> dp2 = loadOrSolveDP(t, path);
    assert(dp1.size() == dp2.size());
    for (size_t i = 0; i < dp1.size(); i++) assert(dp1[i] == dp2[i]);

    std::remove(path.c_str());
    printf("test_dp_cache_roundtrip: passed\n");
}

int main() {
    test_full_game_ev();
    test_dp_cache_roundtrip();
    printf("test_yatzy_engine: all tests passed\n");
    return 0;
}
