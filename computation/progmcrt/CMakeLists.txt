# Copyright 2023-2024 DreamWorks Animation LLC
# SPDX-License-Identifier: Apache-2.0

set(component computation_progmcrt)

add_library(${component} SHARED "")
add_library(${PROJECT_NAME}::${component} ALIAS ${component})

target_sources(${component}
    PRIVATE
        ProgMcrtComputation.cc
)

target_link_libraries(${component}
    PUBLIC
        ${PROJECT_NAME}::common_mcrt_logging
        ${PROJECT_NAME}::common_rec_load
        ${PROJECT_NAME}::computation_engine_mcrt
        ArrasCore::arras4_log
        ArrasCore::computation_api
	McrtDataio::engine_mcrt
        McrtMessages::mcrt_messages
        Moonray::common_geometry
        Moonray::grid_engine_tool
        Moonray::rendering_rndr
        Moonray::rendering_shading
        SceneRdl2::common_fb_util
        SceneRdl2::common_grid_util
        SceneRdl2::render_util
)

set(MOONRAY_EXEC_MODE_DEFAULT "AUTO" CACHE STRING "MoonRay's default execution mode")
set_property(CACHE MOONRAY_EXEC_MODE_DEFAULT PROPERTY STRINGS AUTO SCALAR VECTORIZED XPU)

target_compile_definitions(${component}
    PRIVATE
        MOONRAY_EXEC_MODE_DEFAULT=$CACHE{MOONRAY_EXEC_MODE_DEFAULT}
)

# Set standard compile/link options
McrtComputation_cxx_compile_definitions(${component})
McrtComputation_cxx_compile_features(${component})
McrtComputation_cxx_compile_options(${component})
McrtComputation_link_options(${component})

install(TARGETS ${component})
