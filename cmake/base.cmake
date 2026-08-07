# ============================================================
# Optional HDF5
# ============================================================

set(PARTICLES_HDF5_AVAILABLE OFF)

if(NOT PARTICLES_BUILD_HDF5 STREQUAL "OFF")
    find_package(HDF5 QUIET COMPONENTS C CXX)

    if(HDF5_FOUND)
        set(PARTICLES_HDF5_AVAILABLE ON)
    elseif(PARTICLES_BUILD_HDF5 STREQUAL "ON")
        message(FATAL_ERROR
                "HDF5 support was requested with PARTICLES_BUILD_HDF5=ON, "
                "but HDF5 was not found."
        )
    else()
        message(WARNING
                "HDF5 not found. Building without HDF5 support."
        )
    endif()
endif()

# ============================================================
# Helper functions
# ============================================================

function(particles_apply_hdf5 target_name)
    if(NOT PARTICLES_HDF5_AVAILABLE)
        return()
    endif()

    target_compile_definitions(${target_name} PRIVATE USE_HDF5=1)

    if(TARGET hdf5::hdf5_cpp)
        target_link_libraries(${target_name} PRIVATE hdf5::hdf5_cpp)

        if(TARGET hdf5::hdf5)
            target_link_libraries(${target_name} PRIVATE hdf5::hdf5)
        endif()
    elseif(TARGET HDF5::HDF5)
        target_link_libraries(${target_name} PRIVATE HDF5::HDF5)
    elseif(TARGET hdf5::hdf5)
        target_link_libraries(${target_name} PRIVATE hdf5::hdf5)
    else()
        target_include_directories(${target_name} PRIVATE ${HDF5_INCLUDE_DIRS})
        target_compile_definitions(${target_name} PRIVATE ${HDF5_DEFINITIONS})

        if(HDF5_CXX_LIBRARIES)
            target_link_libraries(${target_name} PRIVATE ${HDF5_CXX_LIBRARIES})
        endif()

        if(HDF5_LIBRARIES)
            target_link_libraries(${target_name} PRIVATE ${HDF5_LIBRARIES})
        endif()
    endif()
endfunction()

function(particles_apply_warnings target_name)
    if(MSVC)
        target_compile_options(${target_name} PRIVATE
                $<$<COMPILE_LANGUAGE:CXX>:/W4>
                $<$<COMPILE_LANGUAGE:CUDA>:-Xcompiler=/W4>
        )
    else()
        target_compile_options(${target_name} PRIVATE
                $<$<COMPILE_LANGUAGE:CXX>:-Wall>
                $<$<COMPILE_LANGUAGE:CXX>:-Wextra>
                $<$<COMPILE_LANGUAGE:CXX>:-Wpedantic>
                $<$<COMPILE_LANGUAGE:CUDA>:-Xcompiler=-Wall,-Wextra,-Wno-pedantic>
        )
    endif()
endfunction()

function(particles_apply_common_settings target_name)
    particles_apply_hdf5(${target_name})
    particles_apply_warnings(${target_name})
endfunction()