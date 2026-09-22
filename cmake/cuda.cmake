# ============================================================
# Optional CUDA
# ============================================================

set(PARTICLES_CUDA_AVAILABLE OFF)

if(NOT PARTICLES_BUILD_CUDA STREQUAL "OFF")
    include(CheckLanguage)
    check_language(CUDA)

    if(CMAKE_CUDA_COMPILER)
        enable_language(CUDA)

        if(NOT PARTICLES_CUDA_ARCHITECTURES STREQUAL "AUTO"
                AND NOT PARTICLES_CUDA_ARCHITECTURES STREQUAL "")
            set(CMAKE_CUDA_ARCHITECTURES "${PARTICLES_CUDA_ARCHITECTURES}")
        endif()

        set(CMAKE_CUDA_STANDARD 17)
        set(CMAKE_CUDA_STANDARD_REQUIRED ON)
        set(CMAKE_CUDA_EXTENSIONS OFF)
        set(CMAKE_EXE_LINKER_FLAGS "-L/usr/lib/gcc/x86_64-linux-gnu/14")
        set(CMAKE_SHARED_LINKER_FLAGS "-L/usr/lib/gcc/x86_64-linux-gnu/14")
        set(CUDA_NVCC_FLAGS -fmad=false -Xptxas -v)
        set(CMAKE_CUDA_RUNTIME_LIBRARY Shared)

        if(PARTICLES_FAST_MATH_CUDA)
            set(CUDA_NVCC_FLAGS ${CUDA_NVCC_FLAGS} -use_fast_math)
        endif()

        set(PARTICLES_CUDA_AVAILABLE ON)
    elseif(PARTICLES_BUILD_CUDA STREQUAL "ON" OR PARTICLES_STRICT_CUDA)
        message(FATAL_ERROR
                "CUDA was requested, but no CUDA compiler was found."
        )
    else()
        message(WARNING
                "CUDA compiler not found. CUDA target will not be built."
        )
    endif()
endif()