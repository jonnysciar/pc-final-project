cmake_minimum_required(VERSION 3.25...3.30)
project(ParticlesSimulation LANGUAGES CXX)

include(cmake/settings.cmake)
include(cmake/options.cmake)
include(cmake/base.cmake)
include(cmake/openmp.cmake)
include(cmake/cuda.cmake)

add_subdirectory(src)

# ============================================================
# Summary
# ============================================================

if(TARGET particles_omp)
    set(PARTICLES_OMP_TARGET_BUILT ON)
else()
    set(PARTICLES_OMP_TARGET_BUILT OFF)
endif()

if(TARGET particles_cuda)
    set(PARTICLES_CUDA_TARGET_BUILT ON)
else()
    set(PARTICLES_CUDA_TARGET_BUILT OFF)
endif()

if(DEFINED CMAKE_CUDA_ARCHITECTURES)
    set(PARTICLES_CUDA_ARCH_SUMMARY "${CMAKE_CUDA_ARCHITECTURES}")
else()
    set(PARTICLES_CUDA_ARCH_SUMMARY "not set")
endif()

message(STATUS "")
message(STATUS "ParticlesSimulation configuration")
message(STATUS "  System:                   ${CMAKE_SYSTEM_NAME}")
message(STATUS "  Processor:                ${CMAKE_SYSTEM_PROCESSOR}")
message(STATUS "  C++ compiler:             ${CMAKE_CXX_COMPILER}")
if(PARTICLES_CUDA_AVAILABLE)
    message(STATUS "  CUDA compiler:            ${CMAKE_CUDA_COMPILER}")
endif()
message(STATUS "  Build type:               ${CMAKE_BUILD_TYPE}")
message(STATUS "  Serial target built:      ON")
message(STATUS "  HDF5 requested:           ${PARTICLES_BUILD_HDF5}")
message(STATUS "  HDF5 available:           ${PARTICLES_HDF5_AVAILABLE}")
message(STATUS "  OpenMP requested:         ${PARTICLES_BUILD_OPENMP}")
message(STATUS "  OpenMP available:         ${PARTICLES_OPENMP_AVAILABLE}")
message(STATUS "  OpenMP target built:      ${PARTICLES_OMP_TARGET_BUILT}")
message(STATUS "  CUDA requested:           ${PARTICLES_BUILD_CUDA}")
message(STATUS "  CUDA available:           ${PARTICLES_CUDA_AVAILABLE}")
message(STATUS "  CUDA target built:        ${PARTICLES_CUDA_TARGET_BUILT}")
message(STATUS "  Native host architecture: ${PARTICLES_NATIVE_ARCH}")
message(STATUS "  CUDA architectures:       ${PARTICLES_CUDA_ARCH_SUMMARY}")
message(STATUS "")
