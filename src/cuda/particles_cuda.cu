#include "constants.hpp"
#include "cuda_struct.hpp"
#include "h5.hpp"
#include "util.hpp"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

// CUDA Headers
#include <cuda_runtime.h>

#define BLOCKSIZE 256

// ============================================================
// MANDELBROT FIELD GENERATION
// ============================================================

void computeGeneratingField(Grid& g, std::size_t maxIter) {
    if (g.values.empty()) {
        throw std::runtime_error("computeGeneratingField: empty grid");
    }

    const double dx = (g.xe - g.xs) / static_cast<double>(g.nx - 1);
    const double dy = (g.ye - g.ys) / static_cast<double>(g.ny - 1);

    for (std::size_t j = 0; j < g.ny; ++j) {
        for (std::size_t i = 0; i < g.nx; ++i) {
            const double ca = g.xs + static_cast<double>(i) * dx;
            const double cb = g.ys + static_cast<double>(j) * dy;

            double za = 0.0;
            double zb = 0.0;

            std::size_t iter = 0;

            while (iter < maxIter) {
                const double a = za * za - zb * zb + ca;
                const double b = 2.0 * za * zb + cb;

                za = a;
                zb = b;

                if (za * za + zb * zb > 4.0) {
                    break;
                }

                ++iter;
            }

            g.values[idx2D(i, j, g.nx)] = static_cast<unsigned long long>(iter);
        }
    }
}


// ============================================================
// PARTICLE GENERATION
// ============================================================

Particles generateParticles(const Grid& g, const Grid& pg) {
    if (g.values.empty()) {
        throw std::runtime_error("generateParticles: empty generating field");
    }

    Particles P;

    const auto vmax = *std::max_element(g.values.begin(), g.values.end());
    auto vmin       = *std::min_element(g.values.begin(), g.values.end());

    // Select only grid points with iteration counts close to the maximum.
    // This keeps the particle count large enough for HPC benchmarking but
    // small enough for the O(N^2) dynamics to remain feasible.
    // This computes floor((29*vmax + vmin) / 30) without forming the
    // potentially overflowing product 29*vmax directly.
    {
        const unsigned long long qmax = vmax / 30ULL;
        const unsigned long long rmax = vmax % 30ULL;
        const unsigned long long qmin = vmin / 30ULL;
        const unsigned long long rmin = vmin % 30ULL;

        vmin = 29ULL * qmax + qmin + (29ULL * rmax + rmin) / 30ULL;
    }

    const std::size_t count = static_cast<std::size_t>(
        std::count_if(g.values.begin(), g.values.end(), [&](unsigned long long v) { return v >= vmin; })
    );

    if (count == 0) {
        throw std::runtime_error("No particles generated");
    }

    P.resize(count);

    std::size_t n = 0;

    for (std::size_t j = 0; j < g.ny; ++j) {
        for (std::size_t i = 0; i < g.nx; ++i) {
            const auto v = g.values[idx2D(i, j, g.nx)];

            if (v < vmin) {
                continue;
            }

            P.w[n] = std::max(1.0, 10.0 * static_cast<double>(v));
            P.x[n] = pg.xs + (pg.xe - pg.xs) * static_cast<double>(i) / static_cast<double>(g.nx - 1);
            P.y[n] = pg.ys + (pg.ye - pg.ys) * static_cast<double>(j) / static_cast<double>(g.ny - 1);
            P.vx[n] = 0.0;
            P.vy[n] = 0.0;

            ++n;
        }
    }

    if (n != count) {
        throw std::runtime_error("generateParticles: internal particle count mismatch");
    }

    return P;
}


// ============================================================
// PRIMARY HPC KERNEL: FORCE COMPUTATION
// ============================================================
// This O(N^2) all-pairs interaction loop is the main target for optimization
// and parallelization. It is possible to change the implementation strategy,
// data layout, blocking, MPI decomposition, GPU offload, exploit pair symmetry,
// but force law and the inclusion of all pair interactions must be preserved.
// ============================================================

__global__ void computeForcesGPU(const GPUParticles GP, const GPUForce F) {
    const std::size_t N = GP.n;
    const std::size_t i = threadIdx.x + blockIdx.x * blockDim.x;

    if (i < N) {
        const double xi = GP.x[i];
        const double yi = GP.y[i];
        const double force_wi = kForce * GP.w[i];

        const double* const x = GP.x;
        const double* const y = GP.y;
        const double* const w = GP.w;

        double fxi = 0.0;
        double fyi = 0.0;

        for (int j = 0; j < N; ++j) {
            if (i != j) {

                double dx = x[j] - xi;
                double dy = y[j] - yi;
                double r2 = dx * dx + dy * dy + eps2;
                double invr  = 1.0 / std::sqrt(r2);
                invr = invr * invr * invr;
                double coeff = force_wi * w[j] * invr;

                fxi += coeff * dx;
                fyi += coeff * dy;
            }
        }

        F.fx[i] = fxi;
        F.fy[i] = fyi;
    }
}


// ============================================================
// VELOCITY-VERLET INTEGRATION
// ============================================================

__global__ void integrateVVGPU1(const GPUParticles GP, const GPUForce F, const double dt) {
    const std::size_t N = GP.n;
    const std::size_t i = threadIdx.x + blockIdx.x * blockDim.x;

    if (i < N) {
        assert(GP.w[i] > 0.0);

        const double invm = 1.0 / GP.w[i];

        GP.vx[i] += 0.5 * F.fx[i] * invm * dt;
        GP.vy[i] += 0.5 * F.fy[i] * invm * dt;
        GP.x[i]  += GP.vx[i] * dt;
        GP.y[i]  += GP.vy[i] * dt;
    }
}

__global__ void integrateVVGPU2(const GPUParticles GP, const GPUForce F, const double dt) {
    const std::size_t N = GP.n;
    const std::size_t i = threadIdx.x + blockIdx.x * blockDim.x;

    if (i < N) {
        assert(GP.w[i] > 0.0);

        const double invm = 1.0 / GP.w[i];

        GP.vx[i] += 0.5 * F.fx[i] * invm * dt;
        GP.vy[i] += 0.5 * F.fy[i] * invm * dt;
    }
}

void integrateVV(const GPUParticles &GP, GPUForce &F, GPUForce &FN, const double dt) {
    const std::size_t N = GP.n;
    dim3 bpg(ceil(static_cast<double>(N) / BLOCKSIZE));
    dim3 tpb(BLOCKSIZE);

    if (F.n != N || FN.n != N) {
        throw std::runtime_error("integrateVV: force array size mismatch");
    }

    integrateVVGPU1<<<bpg, tpb>>>(GP, F, dt);
    computeForcesGPU<<<bpg, tpb>>>(GP, FN);
    integrateVVGPU2<<<bpg, tpb>>>(GP, FN, dt);
}

// ============================================================
// MAIN
// ============================================================

int main(int argc, char** argv) {
#ifdef USE_HDF5
    H5::Exception::dontPrint();
#endif

    try {
        if (argc > 4) {
            printUsage(argv[0]);
            return EXIT_FAILURE;
        }

        const std::string inputFile = (argc > 1) ? argv[1] : "input/Particles.in";
        const std::string outputFile = (argc > 2) ? argv[2] : "none";
        const bool writeHdf5 = !isNoHdf5Token(outputFile);

#ifndef USE_HDF5
        if (writeHdf5) {
            throw std::runtime_error(
                "HDF5 output requested, but this executable was built without HDF5 support. "
                "Use 'none' as the output argument, or rebuild with -DUSE_HDF5."
            );
        }
#endif

        Grid gen;
        Grid screen;
        Config cfg = readInput(inputFile, gen, screen);

        if (argc > 3) {
            const long long outEvery = parseStrictLongLong(argv[3], "outputEvery");
            if (outEvery < 0) {
                throw std::runtime_error("outputEvery must be >= 0");
            }
            cfg.outputEvery = static_cast<std::size_t>(outEvery);
        }

        std::cout << std::setprecision(17);
        std::cout << "Input file:                 " << inputFile << "\n";

#ifdef USE_HDF5
        std::cout << "HDF5 compiled:              yes\n";
#else
        std::cout << "HDF5 compiled:               no\n";
#endif

        std::cout << "HDF5 output:                " << (writeHdf5 ? outputFile : "disabled") << "\n";
        std::cout << "Benchmark/no-output mode:   " << (!writeHdf5 ? "yes" : "no")           << "\n";
        std::cout << "Generating grid:            " << gen.nx << " x " << gen.ny             << "\n";
        std::cout << "Screen grid:                " << screen.nx << " x " << screen.ny       << "\n";
        std::cout << "Max iterations:             " << cfg.maxIters                          << "\n";
        std::cout << "Steps:                      " << cfg.maxSteps                          << "\n";
        std::cout << "dt:                         " << cfg.dt                                << "\n";

        if (cfg.outputEvery == 0) {
            std::cout << "Output policy:              final frame only, if HDF5 is enabled\n";
        } else {
            std::cout << "Output policy:              step 0, every "
                      << cfg.outputEvery << " step(s), and final step, if HDF5 is enabled\n";
        }

        // ----------------------------------------------------
        // 1. Generate Mandelbrot field
        // ----------------------------------------------------
        const auto mandelStart = std::chrono::steady_clock::now();
        computeGeneratingField(gen, cfg.maxIters);
        const auto mandelStop = std::chrono::steady_clock::now();
        const double mandelSeconds = std::chrono::duration<double>(mandelStop - mandelStart).count();

        // ----------------------------------------------------
        // 2. Generate particles
        // ----------------------------------------------------
        const auto particleStart = std::chrono::steady_clock::now();
        Particles P = generateParticles(gen, screen);
        const auto particleStop = std::chrono::steady_clock::now();
        const double particleGenerationSeconds = std::chrono::duration<double>(particleStop - particleStart).count();

        const std::size_t N = P.n;
        std::cout << "Particles:                  " << N << "\n";

        // ----------------------------------------------------
        // 3. Allocate force arrays
        // ----------------------------------------------------
        GPUParticles GP{P};
        GPUForce F{N}, FN{N};
        dim3 tpb(BLOCKSIZE);
        dim3 bpg(ceil(static_cast<double>(N) / BLOCKSIZE));

        cudaEvent_t start, finish;
        cudaEventCreate(&start);
        cudaEventCreate(&finish);

        // ----------------------------------------------------
        // 4. Initial force computation
        // ----------------------------------------------------

        cudaEventRecord(start);
        computeForcesGPU<<<bpg, tpb>>>(GP, F);
        cudaEventRecord(finish);
        cudaEventSynchronize(finish);
        float initForceSeconds;
        cudaEventElapsedTime(&initForceSeconds, start, finish);
        initForceSeconds /= 1000.0;

        // ----------------------------------------------------
        // 5. Weight range for screen output
        // ----------------------------------------------------
        const auto [wminIt, wmaxIt] = std::minmax_element(P.w.begin(), P.w.end());
        const double wmin = *wminIt;
        const double wmax = *wmaxIt;
        const double wr   = std::max(wmax - wmin, 1.0);

        // ----------------------------------------------------
        // 6. Optional HDF5 writer
        // ----------------------------------------------------
        std::unique_ptr<H5StreamWriter> h5;

        if (writeHdf5) {
            h5 = std::make_unique<H5StreamWriter>(outputFile, N, screen.nx, screen.ny);
            h5->writeMetadata(inputFile, cfg, gen, screen);
            h5->writeWeights(P);
        }

        std::size_t outputFrames = 0;
        bool hasLastWrittenStep  = false;
        std::size_t lastWrittenStep = 0;

        double screenBuildSeconds = 0.0;
        double hdf5WriteSeconds   = 0.0;

        auto writeOutputFrame = [&](std::size_t step) {
            if (!h5) {
                return;
            }

            if (hasLastWrittenStep && step == lastWrittenStep) {
                return;
            }

            GP.copy_to(P);

            const auto screenStart = std::chrono::steady_clock::now();
            buildScreen(screen, P, wmin, wr);
            const auto screenStop = std::chrono::steady_clock::now();

            screenBuildSeconds += std::chrono::duration<double>(screenStop - screenStart).count();

            const auto h5Start = std::chrono::steady_clock::now();
            h5->writeFrame(step, P, screen);
            const auto h5Stop = std::chrono::steady_clock::now();

            hdf5WriteSeconds += std::chrono::duration<double>(h5Stop - h5Start).count();

            hasLastWrittenStep = true;
            lastWrittenStep    = step;
            ++outputFrames;
        };

        // ----------------------------------------------------
        // 7. Simulation loop
        // ----------------------------------------------------
        cudaEvent_t dyn_start, dyn_finish;
        cudaEventCreate(&dyn_start);
        cudaEventCreate(&dyn_finish);

        double pureDynamicsSeconds = 0.0;
        float dynSeconds;

        cudaEventRecord(start);
        for (std::size_t step = 0; step < cfg.maxSteps; ++step) {
            if (h5 && shouldWriteStep(step, cfg.maxSteps, cfg.outputEvery)) {
                writeOutputFrame(step);
            }

            cudaEventRecord(dyn_start);
            if (step % 2 == 0) {
                integrateVV(GP, F, FN, cfg.dt);
            } else {
                integrateVV(GP, FN, F, cfg.dt);
            }
            cudaEventRecord(dyn_finish);

            cudaEventSynchronize(dyn_finish);
            cudaEventElapsedTime(&dynSeconds, dyn_start, dyn_finish);
            pureDynamicsSeconds += dynSeconds;
        }
        pureDynamicsSeconds /= 1000.0;

        cudaEventDestroy(dyn_start);
        cudaEventDestroy(dyn_finish);

        if (h5) {
            writeOutputFrame(cfg.maxSteps);
            h5->close();
        } else {
            GP.copy_to(P);
        }

        cudaEventRecord(finish);
        cudaEventSynchronize(finish);
        float loopWallSeconds;
        cudaEventElapsedTime(&loopWallSeconds, start, finish);
        loopWallSeconds /= 1000.0;

        cudaEventDestroy(start);
        cudaEventDestroy(finish);

        // ----------------------------------------------------
        // 8. Validation
        // ----------------------------------------------------
        const auto validationStart = std::chrono::steady_clock::now();
        const ValidationQuantities validation = computeValidationQuantities(P);
        const auto validationStop = std::chrono::steady_clock::now();
        const double validationSeconds = std::chrono::duration<double>(validationStop - validationStart).count();

        // ----------------------------------------------------
        // 9. Reporting
        // ----------------------------------------------------
        // This performance metric intentionally excludes the initial force
        // computation and reports only the repeated dynamics loop.
        const double interactions = static_cast<double>(N) * static_cast<double>(N - 1) * static_cast<double>(cfg.maxSteps);
        const double gigaInteractions = interactions / 1.0e9;

        std::cout << "Simulation completed successfully.\n";
        std::cout << "Output frames:                 " << outputFrames              << "  \n";
        std::cout << "Mandelbrot wall time:          " << mandelSeconds             << " s\n";
        std::cout << "Particle generation wall time: " << particleGenerationSeconds << " s\n";
        std::cout << "Initial force wall time:       " << initForceSeconds          << " s\n";
        std::cout << "Pure dynamics time:            " << pureDynamicsSeconds       << " s\n";
        std::cout << "Screen build time:             " << screenBuildSeconds        << " s\n";
        std::cout << "HDF5 write time:               " << hdf5WriteSeconds          << " s\n";
        std::cout << "Validation time:               " << validationSeconds         << " s\n";
        std::cout << "Loop wall time:                " << loopWallSeconds           << " s\n";

        if (cfg.maxSteps > 0 && pureDynamicsSeconds > 0.0) {
            std::cout << "Pure dynamics performance:  " << gigaInteractions / pureDynamicsSeconds << " GInteractions/s\n";
        }

        if (cfg.maxSteps > 0 && loopWallSeconds > 0.0) {
            std::cout << "Loop end-to-end performance: " << gigaInteractions / loopWallSeconds << " GInteractions/s\n";
        }

        printValidationQuantities(validation);

        GP.free();
        F.free();
        FN.free();

        return EXIT_SUCCESS;
    }
#ifdef USE_HDF5
    catch (const H5::Exception& e) {
        std::cerr << "HDF5 ERROR: " << e.getDetailMsg() << "\n";
        return EXIT_FAILURE;
    }
#endif
    catch (const std::exception& e) {
        std::cerr << "ERROR: " << e.what() << "\n";
        return EXIT_FAILURE;
    }
    catch (...) {
        std::cerr << "ERROR: unknown exception\n";
        return EXIT_FAILURE;
    }
}

