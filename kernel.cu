// kernel.cu — the actual GPU compute: one block per (mask, s) scorecard state.
// Threads within a block cooperate over the ~462 dice-roll outcomes, using shared
// memory for the three reroll-depth value arrays (V0 = 0 rerolls left, V1 = 1, V2 = 2).
#include "precompute.h"
#include <cuda_runtime.h>
#include <cstdio>

#define CUDA_CHECK(call) do { \
    cudaError_t err = (call); \
    if (err != cudaSuccess) { \
        fprintf(stderr, "CUDA error %s:%d: %s\n", __FILE__, __LINE__, cudaGetErrorString(err)); \
        exit(1); \
    } \
} while (0)

// Block dimension used to sweep over combos; must be a power of two for the reduction step.
constexpr int BlockThreads = 256;

__global__ void solveLevelKernel(
    const int* __restrict__ levelMasks, int numMasksInLevel,
    float* __restrict__ DP,
    const int* __restrict__ scoreTable,
    const float* __restrict__ comboProb,
    const int* __restrict__ subsetStart,
    const int* __restrict__ subsetResultStart,
    const int* __restrict__ resultComboID,
    const float* __restrict__ resultProb,
    int numCombos, int capScore)
{
    int maskIdx = blockIdx.x;
    int s = blockIdx.y;
    if (maskIdx >= numMasksInLevel) return;
    int mask = levelMasks[maskIdx];

    extern __shared__ float shared[];
    float* V0 = shared;
    float* V1 = shared + numCombos;
    float* V2 = shared + 2 * numCombos;
    __shared__ float reduceBuf[BlockThreads];

    int tid = threadIdx.x;
    size_t stride1 = (size_t)(capScore + 1);

    // ---- V0: value if this were the final roll (0 rerolls left) ----
    for (int ci = tid; ci < numCombos; ci += BlockThreads) {
        float best = -1.0f;
        for (int m = mask; m; m &= (m - 1)) {
            int bit = m & (-m);
            int cat = __ffs(bit) - 1;
            int pts = scoreTable[(size_t)ci * NumCats + cat];
            int ns = s;
            if (cat < UpperCats) {
                ns = s + pts;
                if (ns > capScore) ns = capScore;
            }
            int childMask = mask ^ bit;
            float val = (float)pts + DP[(size_t)childMask * stride1 + ns];
            if (val > best) best = val;
        }
        V0[ci] = best;
    }
    __syncthreads();

    // ---- V1: value with 1 reroll left, using V0 as the downstream value ----
    for (int ci = tid; ci < numCombos; ci += BlockThreads) {
        float best = V0[ci];
        int ss = subsetStart[ci], se = subsetStart[ci + 1];
        for (int si = ss; si < se; si++) {
            int rs = subsetResultStart[si], re = subsetResultStart[si + 1];
            float val = 0.0f;
            for (int ri = rs; ri < re; ri++)
                val += resultProb[ri] * V0[resultComboID[ri]];
            if (val > best) best = val;
        }
        V1[ci] = best;
    }
    __syncthreads();

    // ---- V2: value with 2 rerolls left (the initial-roll situation), using V1 ----
    for (int ci = tid; ci < numCombos; ci += BlockThreads) {
        float best = V0[ci];
        int ss = subsetStart[ci], se = subsetStart[ci + 1];
        for (int si = ss; si < se; si++) {
            int rs = subsetResultStart[si], re = subsetResultStart[si + 1];
            float val = 0.0f;
            for (int ri = rs; ri < re; ri++)
                val += resultProb[ri] * V1[resultComboID[ri]];
            if (val > best) best = val;
        }
        V2[ci] = best;
    }
    __syncthreads();

    // ---- Expectation over the initial roll, block-wide reduction ----
    float partial = 0.0f;
    for (int ci = tid; ci < numCombos; ci += BlockThreads)
        partial += comboProb[ci] * V2[ci];
    reduceBuf[tid] = partial;
    __syncthreads();
    for (int stride = BlockThreads / 2; stride > 0; stride >>= 1) {
        if (tid < stride) reduceBuf[tid] += reduceBuf[tid + stride];
        __syncthreads();
    }
    if (tid == 0)
        DP[(size_t)mask * stride1 + s] = reduceBuf[0];
}
