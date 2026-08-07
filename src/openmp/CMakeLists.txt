set(TARGET particles_omp)
set(COMMON_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../common)

file(GLOB_RECURSE COMMON_HDRS "${COMMON_DIR}/*.hpp" "${COMMON_DIR}/*.h")
file(GLOB_RECURSE COMMON_SRCS "${COMMON_DIR}/*.cpp" "${COMMON_DIR}/*.c")

file(GLOB_RECURSE HDRS "${CMAKE_CURRENT_SOURCE_DIR}/*.hpp" "${CMAKE_CURRENT_SOURCE_DIR}/*.h")
file(GLOB_RECURSE SRCS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/*.c")

add_executable(${TARGET} ${COMMON_HDRS} ${HDRS} ${COMMON_SRCS} ${SRCS})

target_compile_features(${TARGET} PRIVATE cxx_std_17)
particles_apply_common_settings(${TARGET})
target_link_libraries(${TARGET} PRIVATE OpenMP::OpenMP_CXX)
target_compile_definitions(${TARGET} PRIVATE PARTICLES_OPENMP=1)