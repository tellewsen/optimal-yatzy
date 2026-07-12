// main.cu — host driver: builds the flat tables once, uploads them to the GPU,
// then walks through popcount levels 0..NumCats launching one kernel per level.
// Checkpoints the full DP table to disk after each level, overlapping the disk
// write (CPU thread) with the next level's kernel launch (GPU).
#include "precompute.h"
#include <cuda_runtime.h>
#include <vector>
#include <thread>
#include <cstdio>
#include <cstring>
#include <chrono>

#define CUDA_CHECK(call) do { \
    cudaError_t err = (call); \
    if (err != cudaSuccess) { \
        fprintf(stderr, "CUDA error %s:%d: %s\n", __FILE__, __LINE__, cudaGetErrorString(err)); \
        exit(1); \
    } \
} while (0)

extern __global__ void solveLevelKernel(
    const int*, int, float*,
    const int*, const float*,
    const int*, const int*, const int*, const float*,
    int, int);

constexpr int BlockThreads = 256;
constexpr long long FullMask = (1LL << NumCats) - 1;
const char* checkpointFile = "checkpoint_maxi_gpu.bin";

int popcount(long long x) {
    int n = 0;
    while (x) { n += x & 1; x >>= 1; }
    return n;
}

// Writes the whole DP host buffer to disk, tagged with which popcount level just finished.
void writeCheckpoint(const std::vector<float>& hostDP, int lastCompletedLevel) {
    FILE* f = fopen((std::string(checkpointFile) + ".tmp").c_str(), "wb");
    if (!f) { fprintf(stderr, "failed to open checkpoint for writing\n"); return; }
    fwrite(&lastCompletedLevel, sizeof(int), 1, f);
    fwrite(hostDP.data(), sizeof(float), hostDP.size(), f);
    fclose(f);
    remove(checkpointFile);
    rename((std::string(checkpointFile) + ".tmp").c_str(), checkpointFile);
}

// Returns the last completed popcount level, or -1 if no checkpoint exists.
int loadCheckpoint(std::vector<float>& hostDP) {
    FILE* f = fopen(checkpointFile, "rb");
    if (!f) return -1;
    int lastCompletedLevel = -1;
    if (fread(&lastCompletedLevel, sizeof(int), 1, f) != 1) { fclose(f); return -1; }
    size_t n = fread(hostDP.data(), sizeof(float), hostDP.size(), f);
    fclose(f);
    if (n != hostDP.size()) return -1;
    return lastCompletedLevel;
}

int main() {
    auto t0 = std::chrono::steady_clock::now();

    fprintf(stderr, "building flat combinatorics tables...\n");
    FlatTables t = buildFlatTables();
    fprintf(stderr, "  %d distinct rolls, %d categories, mask space %lld\n",
            t.numCombos, NumCats, (long long)1 << NumCats);

    // Upload flat tables once; they never change across levels.
    int   *d_scoreTable, *d_subsetStart, *d_subsetResultStart, *d_resultComboID;
    float *d_comboProb, *d_resultProb;
    CUDA_CHECK(cudaMalloc(&d_scoreTable, t.scoreTable.size() * sizeof(int)));
    CUDA_CHECK(cudaMalloc(&d_comboProb, t.comboProb.size() * sizeof(float)));
    CUDA_CHECK(cudaMalloc(&d_subsetStart, t.subsetStart.size() * sizeof(int)));
    CUDA_CHECK(cudaMalloc(&d_subsetResultStart, t.subsetResultStart.size() * sizeof(int)));
    CUDA_CHECK(cudaMalloc(&d_resultComboID, t.resultComboID.size() * sizeof(int)));
    CUDA_CHECK(cudaMalloc(&d_resultProb, t.resultProb.size() * sizeof(float)));

    CUDA_CHECK(cudaMemcpy(d_scoreTable, t.scoreTable.data(), t.scoreTable.size() * sizeof(int), cudaMemcpyHostToDevice));
    CUDA_CHECK(cudaMemcpy(d_comboProb, t.comboProb.data(), t.comboProb.size() * sizeof(float), cudaMemcpyHostToDevice));
    CUDA_CHECK(cudaMemcpy(d_subsetStart, t.subsetStart.data(), t.subsetStart.size() * sizeof(int), cudaMemcpyHostToDevice));
    CUDA_CHECK(cudaMemcpy(d_subsetResultStart, t.subsetResultStart.data(), t.subsetResultStart.size() * sizeof(int), cudaMemcpyHostToDevice));
    CUDA_CHECK(cudaMemcpy(d_resultComboID, t.resultComboID.data(), t.resultComboID.size() * sizeof(int), cudaMemcpyHostToDevice));
    CUDA_CHECK(cudaMemcpy(d_resultProb, t.resultProb.data(), t.resultProb.size() * sizeof(float), cudaMemcpyHostToDevice));

    // Full DP table, resident on the GPU for the whole run.
    size_t totalMasks = (size_t)1 << NumCats;
    size_t dpSize = totalMasks * (CapScore + 1);
    float* d_DP;
    CUDA_CHECK(cudaMalloc(&d_DP, dpSize * sizeof(float)));
    fprintf(stderr, "DP table: %zu states, %.1f MB on device\n", dpSize, dpSize * sizeof(float) / 1e6);

    std::vector<float> hostDP(dpSize);

    // Group masks by popcount (categories still open) and build the fixed processing order.
    std::vector<std::vector<int>> masksByPopcount(NumCats + 1);
    for (long long mask = 0; mask <= FullMask; mask++)
        masksByPopcount[popcount(mask)].push_back((int)mask);

    int resumeLevel = loadCheckpoint(hostDP);
    if (resumeLevel >= 0) {
        fprintf(stderr, "resuming from checkpoint: level %d already completed\n", resumeLevel);
        CUDA_CHECK(cudaMemcpy(d_DP, hostDP.data(), dpSize * sizeof(float), cudaMemcpyHostToDevice));
    } else {
        // mask == 0: no categories left. Value is just the bonus, if earned.
        std::vector<float> zeroRow(CapScore + 1, 0.0f);
        zeroRow[CapScore] = (float)Bonus;
        CUDA_CHECK(cudaMemcpy(d_DP, zeroRow.data(), zeroRow.size() * sizeof(float), cudaMemcpyHostToDevice));
        resumeLevel = 0;
    }

    std::thread writerThread;
    int startLevel = resumeLevel + 1;

    for (int p = startLevel; p <= NumCats; p++) {
        auto& levelMasks = masksByPopcount[p];
        int numMasksInLevel = (int)levelMasks.size();

        int* d_levelMasks;
        CUDA_CHECK(cudaMalloc(&d_levelMasks, levelMasks.size() * sizeof(int)));
        CUDA_CHECK(cudaMemcpy(d_levelMasks, levelMasks.data(), levelMasks.size() * sizeof(int), cudaMemcpyHostToDevice));

        dim3 grid(numMasksInLevel, CapScore + 1);
        size_t sharedBytes = 3 * (size_t)t.numCombos * sizeof(float);

        solveLevelKernel<<<grid, BlockThreads, sharedBytes>>>(
            d_levelMasks, numMasksInLevel, d_DP,
            d_scoreTable, d_comboProb,
            d_subsetStart, d_subsetResultStart, d_resultComboID, d_resultProb,
            t.numCombos, CapScore);
        CUDA_CHECK(cudaGetLastError());
        CUDA_CHECK(cudaDeviceSynchronize());
        CUDA_CHECK(cudaFree(d_levelMasks));

        auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        fprintf(stderr, "popcount %2d/%d done (%d masks) - elapsed %.1fs total\n",
                p, NumCats, numMasksInLevel, elapsed);

        // Snapshot DP to host, then write to disk on a background thread while the
        // next level's kernel launch proceeds. Join any previous writer first so we
        // never have two writes in flight or overwrite a buffer mid-write.
        if (writerThread.joinable()) writerThread.join();
        CUDA_CHECK(cudaMemcpy(hostDP.data(), d_DP, dpSize * sizeof(float), cudaMemcpyDeviceToHost));
        int completedLevel = p;
        writerThread = std::thread([&hostDP, completedLevel]() {
            writeCheckpoint(hostDP, completedLevel);
        });
    }
    if (writerThread.joinable()) writerThread.join();

    float finalValue;
    CUDA_CHECK(cudaMemcpy(&finalValue, d_DP + (size_t)FullMask * (CapScore + 1) + 0, sizeof(float), cudaMemcpyDeviceToHost));

    printf("DONE\n");
    printf("Expected optimal score (Norwegian Maxi Yatzy, solitaire): %f\n", finalValue);
    auto totalElapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    fprintf(stderr, "total time: %.1fs\n", totalElapsed);

    return 0;
}
