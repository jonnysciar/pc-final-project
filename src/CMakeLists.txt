
add_subdirectory(serial)

if (PARTICLES_CUDA_AVAILABLE)
    add_subdirectory(cuda)
endif ()

if (PARTICLES_OPENMP_AVAILABLE)
    add_subdirectory(openmp)
endif ()