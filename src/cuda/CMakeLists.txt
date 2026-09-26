set(COMMON_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../common)

file(GLOB_RECURSE COMMON_HDRS "${COMMON_DIR}/*.hpp" "${COMMON_DIR}/*.h")
file(GLOB_RECURSE COMMON_SRCS "${COMMON_DIR}/*.cpp" "${COMMON_DIR}/*.c")

file(GLOB_RECURSE HDRS "${CMAKE_CURRENT_SOURCE_DIR}/*.hpp" "${CMAKE_CURRENT_SOURCE_DIR}/*.h")

set(TARGETS particles_cuda_basic particles_cuda_integrateVV particles_cuda_shared particles_cuda_fma)

foreach(TARGET IN LISTS TARGETS)
    add_executable(${TARGET} ${COMMON_HDRS} ${HDRS} ${COMMON_SRCS} "${CMAKE_CURRENT_SOURCE_DIR}/${TARGET}.cu")

    target_include_directories(${TARGET} PRIVATE ${COMMON_DIR})
    particles_apply_common_settings(${TARGET})

    set_target_properties(${TARGET} PROPERTIES
            CUDA_SEPARABLE_COMPILATION ON
            CUDA_RESOLVE_DEVICE_SYMBOLS ON
    )

    target_compile_options(${TARGET} PRIVATE $<$<COMPILE_LANGUAGE:CUDA>:${CUDA_NVCC_FLAGS}>)
    target_compile_definitions(${TARGET} PRIVATE PARTICLES_CUDA=1)

    if(PARTICLES_NATIVE_ARCH AND NOT MSVC)
        target_compile_options(${TARGET} PRIVATE
                $<$<COMPILE_LANGUAGE:CXX>:-march=native>
                $<$<COMPILE_LANGUAGE:CUDA>:-Xcompiler=-march=native>
        )
    endif()
endforeach()
