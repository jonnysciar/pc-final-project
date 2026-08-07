set(TARGET particles_cuda)
set(COMMON_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../common)

file(GLOB_RECURSE COMMON_HDRS "${COMMON_DIR}/*.hpp" "${COMMON_DIR}/*.h")
file(GLOB_RECURSE COMMON_SRCS "${COMMON_DIR}/*.cpp" "${COMMON_DIR}/*.c")

file(GLOB_RECURSE HDRS "${CMAKE_CURRENT_SOURCE_DIR}/*.hpp" "${CMAKE_CURRENT_SOURCE_DIR}/*.h")
file(GLOB_RECURSE SRCS
        "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/*.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/*.cu"
)

add_executable(${TARGET} ${COMMON_HDRS} ${HDRS} ${COMMON_SRCS} ${SRCS})

target_compile_features(${TARGET} PRIVATE cxx_std_17)
particles_apply_common_settings(${TARGET})

set_target_properties(${TARGET} PROPERTIES
        CUDA_SEPARABLE_COMPILATION ON
        CUDA_RESOLVE_DEVICE_SYMBOLS ON
)

target_compile_options(${TARGET} PRIVATE $<$<COMPILE_LANGUAGE:CUDA>:${CUDA_NVCC_FLAGS}>)
target_compile_definitions(${TARGET} PRIVATE PARTICLES_CUDA=1)