// yatzy_cpu.cpp — thin CLI over the standard-Yatzy engine. Stateless: every
// invocation is an independent query, no session memory. This is the seam a
// future GUI will call through: spawn this process, parse --json output.
#include "precompute_std.h"
#include "yatzy_engine.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::vector<int> parseIntList(const std::string& s) {
    std::vector<int> out;
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, ',')) out.push_back(std::atoi(item.c_str()));
    return out;
}

void printUsageAndExit(const char* prog) {
    fprintf(stderr,
        "Usage: %s --used <comma-separated category indices> --upper <int> "
        "--dice <5 comma-separated 1-6 values> --rerolls <0|1|2> [--json] [--dp-cache <path>]\n"
        "Categories: 0=Ones 1=Twos 2=Threes 3=Fours 4=Fives 5=Sixes 6=OnePair "
        "7=TwoPairs 8=ThreeOfAKind 9=FourOfAKind 10=SmallStraight 11=LargeStraight "
        "12=FullHouse 13=Chance 14=Yatzy\n",
        prog);
    exit(1);
}

} // namespace

int main(int argc, char** argv) {
    std::string usedArg, diceArg, dpCachePath = "yatzy_cpu_dp.bin";
    int upperTotal = -1, rerollsLeft = -1;
    bool jsonOutput = false;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--used" && i + 1 < argc) usedArg = argv[++i];
        else if (arg == "--upper" && i + 1 < argc) upperTotal = std::atoi(argv[++i]);
        else if (arg == "--dice" && i + 1 < argc) diceArg = argv[++i];
        else if (arg == "--rerolls" && i + 1 < argc) rerollsLeft = std::atoi(argv[++i]);
        else if (arg == "--json") jsonOutput = true;
        else if (arg == "--dp-cache" && i + 1 < argc) dpCachePath = argv[++i];
        else printUsageAndExit(argv[0]);
    }

    if (diceArg.empty() || upperTotal < 0 || rerollsLeft < 0 || rerollsLeft > 2)
        printUsageAndExit(argv[0]);

    std::vector<int> diceVec = parseIntList(diceArg);
    if (diceVec.size() != 5) {
        fprintf(stderr, "error: --dice must have exactly 5 values\n");
        return 1;
    }
    std::array<int,5> dice;
    for (int i = 0; i < 5; i++) {
        if (diceVec[i] < 1 || diceVec[i] > 6) {
            fprintf(stderr, "error: dice values must be 1-6\n");
            return 1;
        }
        dice[i] = diceVec[i];
    }

    int usedMask = 0;
    if (!usedArg.empty()) {
        for (int cat : parseIntList(usedArg)) {
            if (cat < 0 || cat >= NumCats) {
                fprintf(stderr, "error: category index must be 0-%d\n", NumCats - 1);
                return 1;
            }
            usedMask |= (1 << cat);
        }
    }
    if (usedMask == (int)FullMask) {
        fprintf(stderr, "error: all categories already used, nothing to recommend\n");
        return 1;
    }

    fprintf(stderr, "building tables and loading/solving DP (first run may take a minute)...\n");
    FlatTables t = buildFlatTables();
    std::vector<float> dp = loadOrSolveDP(t, dpCachePath);

    QueryResult r = query(t, dp, usedMask, upperTotal, dice, rerollsLeft);

    if (jsonOutput) {
        printf("{\"isRerollDecision\":%s,", r.isRerollDecision ? "true" : "false");
        if (r.isRerollDecision) {
            printf("\"rerollOptions\":[");
            for (size_t i = 0; i < r.rerollOptions.size(); i++) {
                const auto& opt = r.rerollOptions[i];
                printf("%s{\"holdValues\":[", i ? "," : "");
                for (size_t j = 0; j < opt.heldValues.size(); j++)
                    printf("%s%d", j ? "," : "", opt.heldValues[j]);
                printf("],\"expectedValue\":%f}", opt.expectedValue);
            }
            printf("]");
        } else {
            printf("\"categoryOptions\":[");
            for (size_t i = 0; i < r.categoryOptions.size(); i++) {
                const auto& opt = r.categoryOptions[i];
                printf("%s{\"category\":%d,\"categoryName\":\"%s\",\"resultingScore\":%d,\"expectedValue\":%f}",
                       i ? "," : "", opt.category, CategoryNames[opt.category], opt.resultingScore, opt.expectedValue);
            }
            printf("]");
        }
        printf("}\n");
    } else {
        if (r.isRerollDecision) {
            printf("Reroll recommendations (rerolls left: %d):\n", rerollsLeft);
            for (const auto& opt : r.rerollOptions) {
                printf("  hold [");
                for (size_t j = 0; j < opt.heldValues.size(); j++)
                    printf("%s%d", j ? "," : "", opt.heldValues[j]);
                printf("]%s -> expected value %.2f\n",
                       opt.heldValues.size() == 5 ? " (stop rerolling)" : "", opt.expectedValue);
            }
        } else {
            printf("Category recommendations:\n");
            for (const auto& opt : r.categoryOptions) {
                printf("  %-14s score %3d -> expected value %.2f\n",
                       CategoryNames[opt.category], opt.resultingScore, opt.expectedValue);
            }
        }
    }
    return 0;
}
