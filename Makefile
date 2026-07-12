# Adjust -arch to match your GPU's compute capability.
# RTX 4070 Ti Super (Ada Lovelace) = sm_89
NVCC := nvcc
ARCH := sm_89
CXXSTD := c++17

all: maxi_solver_gpu

maxi_solver_gpu: main.cu kernel.cu precompute.h
	$(NVCC) -O3 -std=$(CXXSTD) -arch=$(ARCH) -lineinfo main.cu kernel.cu -o maxi_solver_gpu

clean:
	rm -f maxi_solver_gpu checkpoint_maxi_gpu.bin checkpoint_maxi_gpu.bin.tmp

.PHONY: all clean
