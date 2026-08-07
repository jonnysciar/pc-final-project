#pragma once

#include "util.hpp"
#include <cuda_runtime.h>

struct GPUForce {
    std::size_t n{};

    double *fx = nullptr;
    double *fy = nullptr;

    GPUForce(const std::size_t n) : n(n) {
        cudaMalloc(&this->fx, this->n * sizeof(double));
        cudaMalloc(&this->fy, this->n * sizeof(double));

        cudaMemset(fx, 0, this->n * sizeof(double));
        cudaMemset(fy, 0, this->n * sizeof(double));

        cudaDeviceSynchronize();
    }
    GPUForce(const double *fx, const double *fy, const std::size_t n) : n(n) {
        cudaMalloc(&this->fx, this->n * sizeof(double));
        cudaMalloc(&this->fy, this->n * sizeof(double));

        cudaMemcpy(this->fx, fx, n * sizeof(double), cudaMemcpyHostToDevice);
        cudaMemcpy(this->fy, fy, n * sizeof(double), cudaMemcpyHostToDevice);
    }

    void copy_from(double *fx, double *fy) const {
        cudaMemcpy(this->fx, fx, n * sizeof(double), cudaMemcpyHostToDevice);
        cudaMemcpy(this->fy, fy, n * sizeof(double), cudaMemcpyHostToDevice);
    }

    void copy_to(double *fx, double *fy, const std::size_t n) const {
        cudaMemcpy(fx, this->fx, n * sizeof(double), cudaMemcpyDeviceToHost);
        cudaMemcpy(fy, this->fy, n * sizeof(double), cudaMemcpyDeviceToHost);
    }

    void free() const {
        cudaFree(fx);
        cudaFree(fy);
    }
};

struct GPUParticles {
    std::size_t n{};

    // Structure-of-arrays layout.
    // Friendly to SIMD, cache blocking, CUDA/OpenACC offload, and MPI packing.
    double* w = nullptr;
    double* x = nullptr;
    double* y = nullptr;
    double* vx = nullptr;
    double *vy = nullptr;


    GPUParticles(const Particles &P) : n(P.n) {
        cudaMalloc(&w, n * sizeof(double));
        cudaMalloc(&x, n * sizeof(double));
        cudaMalloc(&y, n * sizeof(double));
        cudaMalloc(&vx, n * sizeof(double));
        cudaMalloc(&vy, n * sizeof(double));

        cudaMemcpy(w, P.w.data(), n * sizeof(double), cudaMemcpyHostToDevice);
        cudaMemcpy(x, P.x.data(), n * sizeof(double), cudaMemcpyHostToDevice);
        cudaMemcpy(y, P.y.data(), n * sizeof(double), cudaMemcpyHostToDevice);
        cudaMemcpy(vx, P.vx.data(), n * sizeof(double), cudaMemcpyHostToDevice);
        cudaMemcpy(vy, P.vy.data(), n * sizeof(double), cudaMemcpyHostToDevice);
    }

    void copy_to(Particles &P) const {
        cudaMemcpy(P.w.data(), w, P.n * sizeof(double), cudaMemcpyDeviceToHost);
        cudaMemcpy(P.x.data(), x, P.n * sizeof(double), cudaMemcpyDeviceToHost);
        cudaMemcpy(P.y.data(), y, P.n * sizeof(double), cudaMemcpyDeviceToHost);
        cudaMemcpy(P.vx.data(), vx, P.n * sizeof(double), cudaMemcpyDeviceToHost);
        cudaMemcpy(P.vy.data(), vy, P.n * sizeof(double), cudaMemcpyDeviceToHost);
    }

    void copy_from(Particles &P) const {
        cudaMemcpy(w, P.w.data(), n * sizeof(double), cudaMemcpyHostToDevice);
        cudaMemcpy(x, P.x.data(), n * sizeof(double), cudaMemcpyHostToDevice);
        cudaMemcpy(y, P.y.data(), n * sizeof(double), cudaMemcpyHostToDevice);
        cudaMemcpy(vx, P.vx.data(), n * sizeof(double), cudaMemcpyHostToDevice);
        cudaMemcpy(vy, P.vy.data(), n * sizeof(double), cudaMemcpyHostToDevice);
    }

    void free() const {
        cudaFree(w);
        cudaFree(x);
        cudaFree(y);
        cudaFree(vx);
        cudaFree(vy);
    }
};