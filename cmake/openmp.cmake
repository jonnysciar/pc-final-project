# ============================================================
# Optional OpenMP
# ============================================================

set(PARTICLES_OPENMP_AVAILABLE OFF)

if(NOT PARTICLES_BUILD_OPENMP STREQUAL "OFF")
    find_package(OpenMP QUIET COMPONENTS CXX)

    if(OpenMP_CXX_FOUND)
        set(PARTICLES_OPENMP_AVAILABLE ON)
    elseif(PARTICLES_BUILD_OPENMP STREQUAL "ON")
        message(FATAL_ERROR
                "OpenMP was requested, but OpenMP was not found."
        )
    else()
        message(WARNING
                "OpenMP not found. OpenMP target will not be built."
        )
    endif()
endif()