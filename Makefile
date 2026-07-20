# Adjust -arch to match your GPU's compute capability.
# RTX 4070 Ti Super (Ada Lovelace) = sm_89
NVCC := nvcc
ARCH := sm_89
CXXSTD := c++17

CXX := g++
CXXFLAGS_STD := -O3 -std=$(CXXSTD) -Wall -pthread

all: maxi_solver_gpu

maxi_solver_gpu: main.cu kernel.cu precompute.h
	$(NVCC) -O3 -std=$(CXXSTD) -arch=$(ARCH) -lineinfo main.cu kernel.cu -o maxi_solver_gpu

# --- standard (5-dice) Yatzy CPU solver — independent of the Maxi/GPU code above ---

yatzy_cpu: yatzy_cpu.cpp yatzy_engine.cpp yatzy_engine.h precompute_std.h
	$(CXX) $(CXXFLAGS_STD) yatzy_engine.cpp yatzy_cpu.cpp -o yatzy_cpu

test_precompute_std: test_precompute_std.cpp precompute_std.h
	$(CXX) $(CXXFLAGS_STD) test_precompute_std.cpp -o test_precompute_std

test_yatzy_engine: test_yatzy_engine.cpp yatzy_engine.cpp yatzy_engine.h precompute_std.h
	$(CXX) $(CXXFLAGS_STD) test_yatzy_engine.cpp yatzy_engine.cpp -o test_yatzy_engine

# Tight-tolerance, deterministic checks on solved dp[] cells against
# closed-form probabilities — fast enough to run every time, unlike
# simulate_stats below.
test_dp_exact: test_dp_exact.cpp yatzy_engine.cpp yatzy_engine.h precompute_std.h
	$(CXX) $(CXXFLAGS_STD) test_dp_exact.cpp yatzy_engine.cpp -o test_dp_exact

# Monte Carlo end-to-end validation: plays real games via query()'s own
# recommendations and checks the simulated mean against the DP's exact EV.
# Not part of `test_yatzy` — run manually (~15s for the 50k-game default).
simulate_stats: simulate_stats.cpp yatzy_engine.cpp yatzy_engine.h precompute_std.h
	$(CXX) $(CXXFLAGS_STD) simulate_stats.cpp yatzy_engine.cpp -o simulate_stats

test_yatzy: test_precompute_std test_yatzy_engine test_dp_exact yatzy_cpu
	./test_precompute_std
	./test_yatzy_engine
	./test_dp_exact
	./test_yatzy_cli.sh

clean:
	rm -f maxi_solver_gpu checkpoint_maxi_gpu.bin checkpoint_maxi_gpu.bin.tmp
	rm -f yatzy_cpu test_precompute_std test_yatzy_engine test_dp_exact simulate_stats
	rm -f yatzy_cpu_dp.bin test_cli_dp_cache.bin

.PHONY: all clean test_yatzy
