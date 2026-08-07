#pragma once

#include <charconv>
#include <cmath>
#include <string>
#include <stdexcept>
#include <limits>
#include <iostream>
#include <vector>
#include <sstream>
#include <fstream>
#include <iomanip>

// ============================================================
// UTILITIES
// ============================================================

inline std::size_t idx2D(std::size_t i, std::size_t j, std::size_t nx) noexcept {
    return i + j * nx;
}

std::size_t safeMul(std::size_t a, std::size_t b, const std::string& what) {
    if (a != 0 && b > std::numeric_limits<std::size_t>::max() / a) {
        throw std::overflow_error("Overflow while computing " + what);
    }
    return a * b;
}

std::size_t safeGridSize(std::size_t nx, std::size_t ny) {
    if (nx == 0 || ny == 0) {
        throw std::invalid_argument("Grid dimensions must be > 0");
    }
    return safeMul(nx, ny, "grid size");
}

long long parseStrictLongLong(const std::string& s, const std::string& what) {
    long long value = 0;
    const char* begin = s.data();
    const char* end   = s.data() + s.size();
    const auto [ptr, ec] = std::from_chars(begin, end, value);

    if (ec != std::errc{} || ptr != end) {
        throw std::runtime_error("Invalid " + what + ": '" + s + "'");
    }
    return value;
}

bool isNoHdf5Token(const std::string& text) {
    return text == "none" ||
           text == "NONE" ||
           text == "-" ||
           text == "--no-hdf5";
}

void printUsage(const char* prog) {
    std::cerr
        << "Usage: " << prog << " [inputFile] [h5File|none|--no-hdf5] [outputEvery]\n"
        << "Examples:\n"
        << "  " << prog << " input/Particles.in none 0\n"
        << "  " << prog << " input/Particles.in output/Particles_cpp.h5 1000\n";
}

bool shouldWriteStep(std::size_t step, std::size_t finalStep, std::size_t outputEvery) {
    if (step == finalStep) {
        return true;
    }
    if (outputEvery == 0) {
        return false;
    }
    if (step == 0) {
        return true;
    }
    return (step % outputEvery) == 0;
}

// ============================================================
// DATA STRUCTURES
// ============================================================

struct Grid {
    std::size_t nx{};
    std::size_t ny{};

    double xs{};
    double xe{};
    double ys{};
    double ye{};

    std::vector<unsigned long long> values;

    void allocate() {
        values.assign(safeGridSize(nx, ny), 0ULL);
    }
};

struct Particles {
    std::size_t n{};

    // Structure-of-arrays layout.
    // Friendly to SIMD, cache blocking, CUDA/OpenACC offload, and MPI packing.
    std::vector<double> w;
    std::vector<double> x;
    std::vector<double> y;
    std::vector<double> vx;
    std::vector<double> vy;

    void resize(std::size_t N) {
        if (N == 0) {
            throw std::runtime_error("Particles.resize: N must be > 0");
        }
        n = N;
        w.resize(N);
        x.resize(N);
        y.resize(N);
        vx.assign(N, 0.0);
        vy.assign(N, 0.0);
    }
};

struct Config {
    std::size_t maxIters{};
    std::size_t maxSteps{};
    std::size_t outputEvery{0};
    double dt{};
};

struct ValidationQuantities {
    double sum_x{};
    double sum_y{};
    double sum_vx{};
    double sum_vy{};

    double weighted_sum_x{};
    double weighted_sum_y{};

    double momentum_x{};
    double momentum_y{};

    double kinetic_energy{};
    double potential_like{};
    double energy_like{};
};

// ============================================================
// INPUT PARSER
// ============================================================

template <typename T>
bool parseLine(std::istream& in, T& value) {
    std::string line;

    while (std::getline(in, line)) {
        const auto first = line.find_first_not_of(" \t\r\n");

        if (first == std::string::npos || line[first] == '#') {
            continue;
        }

        std::istringstream iss(line);

        if (!(iss >> value)) {
            throw std::runtime_error("Parse error: " + line);
        }

        iss >> std::ws;

        if (!iss.eof()) {
            if (iss.peek() == '#') {
                return true;
            }
            throw std::runtime_error("Trailing junk: " + line);
        }

        return true;
    }

    return false;
}

template <typename T>
void requireLine(std::istream& in, T& value, const std::string& name) {
    if (!parseLine(in, value)) {
        throw std::runtime_error("Unexpected EOF while reading " + name);
    }
}

Config readInput(const std::string& file, Grid& g, Grid& pg) {
    std::ifstream in(file);

    if (!in) {
        throw std::runtime_error("Cannot open input file: " + file);
    }

    long long raw_g_nx  = 0;
    long long raw_g_ny  = 0;
    long long raw_pg_nx = 0;
    long long raw_pg_ny = 0;

    requireLine(in, raw_g_nx, "g.nx");
    requireLine(in, raw_g_ny, "g.ny");

    requireLine(in, g.xs, "g.xs");
    requireLine(in, g.xe, "g.xe");
    requireLine(in, g.ys, "g.ys");
    requireLine(in, g.ye, "g.ye");

    requireLine(in, raw_pg_nx, "pg.nx");
    requireLine(in, raw_pg_ny, "pg.ny");

    requireLine(in, pg.xs, "pg.xs");
    requireLine(in, pg.xe, "pg.xe");
    requireLine(in, pg.ys, "pg.ys");
    requireLine(in, pg.ye, "pg.ye");

    long long raw_max_iters    = 0;
    long long raw_max_steps    = 0;
    long long raw_output_every = 0;

    Config cfg{};

    requireLine(in, raw_max_iters, "maxIters");
    requireLine(in, raw_max_steps, "maxSteps");
    requireLine(in, cfg.dt, "dt");
    requireLine(in, raw_output_every, "outputEvery");

    if (raw_g_nx < 2 || raw_g_ny < 2 || raw_pg_nx < 2 || raw_pg_ny < 2) {
        throw std::runtime_error("Grids must have at least 2 points in each direction");
    }

    if (g.xe <= g.xs || g.ye <= g.ys) {
        throw std::runtime_error("Invalid generating domain");
    }

    if (pg.xe <= pg.xs || pg.ye <= pg.ys) {
        throw std::runtime_error("Invalid particle/screen domain");
    }

    if (cfg.dt <= 0.0) {
        throw std::runtime_error("dt must be > 0");
    }

    if (raw_max_iters <= 0 || raw_max_steps <= 0) {
        throw std::runtime_error("maxIters and maxSteps must be > 0");
    }

    if (raw_output_every < 0) {
        throw std::runtime_error("outputEvery must be >= 0");
    }

    g.nx  = static_cast<std::size_t>(raw_g_nx);
    g.ny  = static_cast<std::size_t>(raw_g_ny);
    pg.nx = static_cast<std::size_t>(raw_pg_nx);
    pg.ny = static_cast<std::size_t>(raw_pg_ny);

    cfg.maxIters    = static_cast<std::size_t>(raw_max_iters);
    cfg.maxSteps    = static_cast<std::size_t>(raw_max_steps);
    cfg.outputEvery = static_cast<std::size_t>(raw_output_every);

    g.allocate();
    pg.allocate();

    return cfg;
}


// ============================================================
// SCREEN BUILDING
// Visualization/debugging only. Official benchmark runs should disable HDF5,
// so this function should not affect benchmark timing.
// ============================================================

void buildScreen(Grid& screen, const Particles& P, double wmin, double wr) {
    if (screen.values.empty()) {
        throw std::runtime_error("buildScreen: empty screen grid");
    }

    if (wr <= 0.0) {
        throw std::runtime_error("buildScreen: invalid weight range");
    }

    std::fill(screen.values.begin(), screen.values.end(), 0ULL);

    const double invdx = static_cast<double>(screen.nx - 1) / (screen.xe - screen.xs);
    const double invdy = static_cast<double>(screen.ny - 1) / (screen.ye - screen.ys);

    for (std::size_t n = 0; n < P.n; ++n) {
        int ix = static_cast<int>((P.x[n] - screen.xs) * invdx);
        int iy = static_cast<int>((P.y[n] - screen.ys) * invdy);

        if (ix < 0) {
            ix = 0;
        } else if (ix > static_cast<int>(screen.nx - 1)) {
            ix = static_cast<int>(screen.nx - 1);
        }

        if (iy < 0) {
            iy = 0;
        } else if (iy > static_cast<int>(screen.ny - 1)) {
            iy = static_cast<int>(screen.ny - 1);
        }

        int wp_i = static_cast<int>(10.0 * (P.w[n] - wmin) / wr);

        if (wp_i < 0) {
            wp_i = 0;
        } else if (wp_i > 1000) {
            wp_i = 1000;
        }

        const unsigned long long wp = static_cast<unsigned long long>(wp_i);

        for (int dj = -1; dj <= 1; ++dj) {
            const int jy = iy + dj;
            if (jy < 0 || jy >= static_cast<int>(screen.ny)) {
                continue;
            }

            for (int di = -1; di <= 1; ++di) {
                const int jx = ix + di;
                if (jx < 0 || jx >= static_cast<int>(screen.nx)) {
                    continue;
                }

                const std::size_t p = static_cast<std::size_t>(jx) + static_cast<std::size_t>(jy) * screen.nx;

                screen.values[p] += wp;
            }
        }
    }
}


// ============================================================
// VALIDATION QUANTITIES
// ============================================================

ValidationQuantities computeValidationQuantities(const Particles& P) {
    ValidationQuantities q{};
    const std::size_t N = P.n;

    for (std::size_t i = 0; i < N; ++i) {
        const double wi  = P.w[i];
        const double xi  = P.x[i];
        const double yi  = P.y[i];
        const double vxi = P.vx[i];
        const double vyi = P.vy[i];

        q.sum_x  += xi;
        q.sum_y  += yi;
        q.sum_vx += vxi;
        q.sum_vy += vyi;

        q.weighted_sum_x += wi * xi;
        q.weighted_sum_y += wi * yi;

        q.momentum_x += wi * vxi;
        q.momentum_y += wi * vyi;

        q.kinetic_energy += 0.5 * wi * (vxi * vxi + vyi * vyi);
    }

    // O(N^2), computed only once at the end. Useful because it depends on the
    // final particle distribution and catches many classes of errors.
    for (std::size_t i = 0; i < N; ++i) {
        for (std::size_t j = i + 1; j < N; ++j) {
            const double dx = P.x[j] - P.x[i];
            const double dy = P.y[j] - P.y[i];
            const double r2 = dx * dx + dy * dy + eps2;

            q.potential_like += kForce * P.w[i] * P.w[j] / std::sqrt(r2);
        }
    }

    q.energy_like = q.kinetic_energy + q.potential_like;
    return q;
}

void printValidationQuantities(const ValidationQuantities& q) {
    std::cout << std::setprecision(17);
    std::cout << "Final validation quantities:\n";
    std::cout << "  sum_x:            " << q.sum_x          << "\n";
    std::cout << "  sum_y:            " << q.sum_y          << "\n";
    std::cout << "  sum_vx:           " << q.sum_vx         << "\n";
    std::cout << "  sum_vy:           " << q.sum_vy         << "\n";
    std::cout << "  weighted_sum_x:   " << q.weighted_sum_x << "\n";
    std::cout << "  weighted_sum_y:   " << q.weighted_sum_y << "\n";
    std::cout << "  momentum_x:       " << q.momentum_x     << "\n";
    std::cout << "  momentum_y:       " << q.momentum_y     << "\n";
    std::cout << "  kinetic_energy:   " << q.kinetic_energy << "\n";
    std::cout << "  potential_like:   " << q.potential_like << "\n";
    std::cout << "  energy_like:      " << q.energy_like    << "\n";
}