
/*
================================================================================
Particle System Solver - Serial C++17 Baseline
================================================================================

Course final project baseline for parallel programming with:
OpenMP, MPI, CUDA, OpenACC, or hybrid approaches.

Primary objective
-----------------
Students should optimize and parallelize the particle dynamics phase.

Benchmark/no-output mode
---------------------------------
HDF5 output disabled. No screen generation.
Final validation quantities are still printed.

Recommended benchmark run:

  ./path/to/particles_serial input/Particles.in none 0

Use the external validator on /step,/weight,/pos,/vel.
The /screen dataset is intended mainly for visualization/debugging.

HDF5 support is optional at compile time.

Without HDF5:

  g++ -O3 -std=c++17 -Wall -Wextra -pedantic particles.cpp -o particles_serial

With HDF5:

  g++ -O3 -std=c++17 -Wall -Wextra -pedantic -DUSE_HDF5 particles.cpp -o particles_serial -lhdf5_cpp -lhdf5

or, depending on the system:

  h5c++ -O3 -std=c++17 -Wall -Wextra -pedantic -DUSE_HDF5 particles.cpp -o particles_serial

Command line:

  ./particles_serial [inputFile] [h5File|none|--no-hdf5] [outputEvery]

Examples:

  ./particles_serial input/Particles.in none 0
  ./particles_serial input/Particles.in output/Particles_cpp.h5
  ./particles_serial input/Particles.in output/Particles_cpp.h5 1000

Input file format, after removing empty lines and comment-only lines beginning
with '#':

  generatingGridNx
  generatingGridNy
  generatingGridXs
  generatingGridXe
  generatingGridYs
  generatingGridYe
  screenGridNx
  screenGridNy
  screenGridXs
  screenGridXe
  screenGridYs
  screenGridYe
  maxFractalIterations
  timeSteps
  dt
  outputEvery

outputEvery:
  0  means final HDF5 frame only, if HDF5 output is enabled.
  >0 means step 0, every outputEvery steps, and the final step.

================================================================================
*/

#include "h5.hpp"
#include "constants.hpp"
#include "util.hpp"

#include <omp.h>

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
#include <utility>
#include <vector>

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

void computeForces(const Particles& P, double* fx, double* fy) {
    const std::size_t N = P.n;

    if (!fx || !fy) {
        throw std::runtime_error("computeForces: null output pointer");
    }

    const double* const x = P.x.data();
    const double* const y = P.y.data();
    const double* const w = P.w.data();

#pragma omp parallel for collapse(1) shared(N,x,y,w)
    for (std::size_t i = 0; i < N; ++i) {
        const double xi = x[i];
        const double yi = y[i];
        const double wi = w[i];

        double fxi = 0.0;
        double fyi = 0.0;

        for (std::size_t j = 0; j < N; ++j) {
            if (i != j) {
                const double dx = x[j] - xi;
                const double dy = y[j] - yi;
                const double r2 = dx * dx + dy * dy + eps2;
                const double invr = 1.0 / std::sqrt(r2);
                const double invr2 = invr * invr;
                const double invr3 = invr2 * invr;
                const double coeff = kForce * wi * w[j] * invr3;

                fxi += coeff * dx;
                fyi += coeff * dy;
            }
        }

        fx[i] = fxi;
        fy[i] = fyi;
    }
}


// ============================================================
// VELOCITY-VERLET INTEGRATION
// ============================================================

void integrateVV(
    Particles& P,
    std::vector<double>& fx,
    std::vector<double>& fy,
    std::vector<double>& fx_new,
    std::vector<double>& fy_new,
    double dt
) {
    const std::size_t N = P.n;

    if (fx.size() != N || fy.size() != N || fx_new.size() != N || fy_new.size() != N) {
        throw std::runtime_error("integrateVV: force array size mismatch");
    }

    for (std::size_t i = 0; i < N; ++i) {
        assert(P.w[i] > 0.0);

        const double invm = 1.0 / P.w[i];

        P.vx[i] += 0.5 * fx[i] * invm * dt;
        P.vy[i] += 0.5 * fy[i] * invm * dt;
        P.x[i]  += P.vx[i] * dt;
        P.y[i]  += P.vy[i] * dt;
    }

    computeForces(P, fx_new.data(), fy_new.data());

    for (std::size_t i = 0; i < N; ++i) {
        assert(P.w[i] > 0.0);

        const double invm = 1.0 / P.w[i];

        P.vx[i] += 0.5 * fx_new[i] * invm * dt;
        P.vy[i] += 0.5 * fy_new[i] * invm * dt;
    }

    fx.swap(fx_new);
    fy.swap(fy_new);
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
        std::vector<double> fx(N, 0.0);
        std::vector<double> fy(N, 0.0);
        std::vector<double> fx_new(N, 0.0);
        std::vector<double> fy_new(N, 0.0);

        // ----------------------------------------------------
        // 4. Initial force computation
        // ----------------------------------------------------
        const auto initForceStart = std::chrono::steady_clock::now();
        computeForces(P, fx.data(), fy.data());
        const auto initForceStop = std::chrono::steady_clock::now();
        const double initForceSeconds = std::chrono::duration<double>(initForceStop - initForceStart).count();

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
        const auto loopStart = std::chrono::steady_clock::now();
        double pureDynamicsSeconds = 0.0;

        for (std::size_t step = 0; step < cfg.maxSteps; ++step) {
            if (h5 && shouldWriteStep(step, cfg.maxSteps, cfg.outputEvery)) {
                writeOutputFrame(step);
            }

            const auto dynStart = std::chrono::steady_clock::now();
            integrateVV(P, fx, fy, fx_new, fy_new, cfg.dt);
            const auto dynStop = std::chrono::steady_clock::now();

            pureDynamicsSeconds += std::chrono::duration<double>(dynStop - dynStart).count();
        }

        if (h5) {
            writeOutputFrame(cfg.maxSteps);
            h5->close();
        }

        const auto loopStop = std::chrono::steady_clock::now();
        const double loopWallSeconds = std::chrono::duration<double>(loopStop - loopStart).count();

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

