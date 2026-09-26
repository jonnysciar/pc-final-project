# ============================================================
# Options
# ============================================================

set(PARTICLES_BUILD_HDF5 "AUTO" CACHE STRING "Build HDF5 support: OFF, ON, or AUTO")

set(PARTICLES_BUILD_OPENMP "OFF" CACHE STRING "Build OpenMP executable: OFF, ON, or AUTO")

set(PARTICLES_BUILD_CUDA "OFF" CACHE STRING "Build CUDA executable: OFF, ON, or AUTO")
set(PARTICLES_CUDA_ARCHITECTURES "61" CACHE STRING "CUDA architectures: AUTO, native, 80, 90, or list like 70;80;90")
option(PARTICLES_FAST_MATH_CUDA "Enable --use_fast_math for CUDA code" OFF)
option(PARTICLES_STRICT_CUDA "Fail if CUDA is requested but unavailable" OFF)
option(PARTICLES_NATIVE_ARCH "Enable native compilation flag" OFF)

set_property(CACHE PARTICLES_BUILD_HDF5 PROPERTY STRINGS OFF ON AUTO)
set_property(CACHE PARTICLES_BUILD_OPENMP PROPERTY STRINGS OFF ON AUTO)
set_property(CACHE PARTICLES_BUILD_CUDA PROPERTY STRINGS OFF ON AUTO)

# ============================================================
# Validate options
# ============================================================

if (NOT PARTICLES_BUILD_HDF5 MATCHES "^(OFF|ON|AUTO)$")
    message(FATAL_ERROR "PARTICLES_BUILD_HDF5 must be OFF, ON, or AUTO")
endif ()

if (NOT PARTICLES_BUILD_OPENMP MATCHES "^(OFF|ON|AUTO)$")
    message(FATAL_ERROR "PARTICLES_BUILD_OPENMP must be OFF, ON, or AUTO")
endif ()

if (NOT PARTICLES_BUILD_CUDA MATCHES "^(OFF|ON|AUTO)$")
    message(FATAL_ERROR "PARTICLES_BUILD_CUDA must be OFF, ON, or AUTO")
endif ()