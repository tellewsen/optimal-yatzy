#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"

rm -f test_cli_dp_cache.bin

output=$(./yatzy_cpu --used 0,1,2,3,4,5,6,7,8,9,10,11,12,13 --upper 0 --dice 6,6,6,6,6 --rerolls 0 --dp-cache test_cli_dp_cache.bin 2>/dev/null)

echo "$output" | grep -q "Yatzy" || { echo "FAIL: expected Yatzy in output"; exit 1; }
echo "$output" | grep -qE "score[[:space:]]+50" || { echo "FAIL: expected score 50"; exit 1; }

json_output=$(./yatzy_cpu --used 0,1,2,3,4,5,6,7,8,9,10,11,12,13 --upper 0 --dice 6,6,6,6,6 --rerolls 0 --dp-cache test_cli_dp_cache.bin --json 2>/dev/null)

echo "$json_output" | grep -q '"isRerollDecision":false' || { echo "FAIL: expected isRerollDecision:false in --json output"; exit 1; }
echo "$json_output" | grep -q '"categoryName":"Yatzy"' || { echo "FAIL: expected Yatzy category in --json output"; exit 1; }
echo "$json_output" | grep -q '"resultingScore":50' || { echo "FAIL: expected resultingScore 50 in --json output"; exit 1; }

rm -f test_cli_dp_cache.bin

rm -f test_cli_winprob_cache.bin

# Only Chance open for me (dice 3,3,3,3,3 -> score 15, deterministic).
# Opponent fully done. --winprob-max-popcount 0 keeps this to a
# terminal-only solve (queryForWin only ever reads masks strictly below
# my open mask's popcount — here that's just mask 0).
win_output=$(./yatzy_cpu --used 0,1,2,3,4,5,6,7,8,9,10,11,12,14 --upper 0 \
  --dice 3,3,3,3,3 --rerolls 0 \
  --opp-used 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14 --opp-upper 0 \
  --my-banked 190 --opp-banked 200 \
  --winprob-cache test_cli_winprob_cache.bin --winprob-max-popcount 0 \
  --json 2>/dev/null)

echo "$win_output" | grep -q '"winProb":1.000000' || { echo "FAIL: expected winProb 1.0 when ahead"; exit 1; }

lose_output=$(./yatzy_cpu --used 0,1,2,3,4,5,6,7,8,9,10,11,12,14 --upper 0 \
  --dice 3,3,3,3,3 --rerolls 0 \
  --opp-used 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14 --opp-upper 0 \
  --my-banked 190 --opp-banked 210 \
  --winprob-cache test_cli_winprob_cache.bin --winprob-max-popcount 0 \
  --json 2>/dev/null)

echo "$lose_output" | grep -q '"winProb":0.000000' || { echo "FAIL: expected winProb 0.0 when behind"; exit 1; }

rm -f test_cli_winprob_cache.bin

echo "test_yatzy_cli: passed"
