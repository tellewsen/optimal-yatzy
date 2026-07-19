// wasm_bridge.cpp — Emscripten entry point exposing the standard-Yatzy
// engine's Solo-mode query to JS. The DP table ships pre-solved (baked in
// at build time via --preload-file), so this never solves — only looks up
// the table built once at first call.
#include "yatzy_engine.h"
#include <emscripten/bind.h>
#include <array>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {

FlatTables* g_tables = nullptr;
std::vector<float>* g_dp = nullptr;

void ensureLoaded() {
    if (g_tables) return;
    g_tables = new FlatTables(buildFlatTables());
    std::vector<float> dp;
    // This build has no thread support (deliberately, per the "no
    // client-side solving" design constraint), so falling through to a
    // solve here would silently freeze the browser tab for minutes. The
    // preloaded .data cache should always be present and correctly sized
    // (baked in via --preload-file), but if it's ever missing or corrupted,
    // fail fast and loud instead of silently solving.
    size_t expectedSize = ((size_t)1 << NumCats) * (size_t)(CapScore + 1);
    if (!loadDP(dp, "yatzy_cpu_dp.bin", expectedSize)) {
        throw std::runtime_error(
            "yatzy_cpu_dp.bin missing or wrong size — this WASM build requires a pre-solved DP cache baked in via --preload-file");
    }
    g_dp = new std::vector<float>(std::move(dp));
}

std::array<int, 5> parseDice(const std::string& csv) {
    std::array<int, 5> dice{};
    std::stringstream ss(csv);
    std::string item;
    int i = 0;
    while (std::getline(ss, item, ',') && i < 5) dice[i++] = std::atoi(item.c_str());
    return dice;
}

} // namespace

std::string queryJson(int usedMask, int upperTotal, std::string diceCsv, int rerollsLeft) {
    ensureLoaded();
    std::array<int, 5> dice = parseDice(diceCsv);
    QueryResult r = query(*g_tables, *g_dp, usedMask, upperTotal, dice, rerollsLeft);
    return queryResultToJson(r);
}

EMSCRIPTEN_BINDINGS(yatzy_module) {
    emscripten::function("queryJson", &queryJson);
}
