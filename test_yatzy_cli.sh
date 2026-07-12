#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"

rm -f test_cli_dp_cache.bin

output=$(./yatzy_cpu --used 0,1,2,3,4,5,6,7,8,9,10,11,12,13 --upper 0 --dice 6,6,6,6,6 --rerolls 0 --dp-cache test_cli_dp_cache.bin 2>/dev/null)

echo "$output" | grep -q "Yatzy" || { echo "FAIL: expected Yatzy in output"; exit 1; }
echo "$output" | grep -qE "score[[:space:]]+50" || { echo "FAIL: expected score 50"; exit 1; }

rm -f test_cli_dp_cache.bin
echo "test_yatzy_cli: passed"
