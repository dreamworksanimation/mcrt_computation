# Copyright 2023-2024 DreamWorks Animation LLC
# SPDX-License-Identifier: Apache-2.0

set(component computation_progmcrt_merge)

add_library(${component} SHARED "")
add_library(${PROJECT_NAME}::${component} ALIAS ${component})

target_sources(${component}
    PRIVATE
        ProgMcrtMergeClockDeltaDriver.cc
        ProgMcrtMergeComputation.cc 
        ProgMcrtMergeDebugFeedback.cc
)

target_link_libraries(${component}
    PUBLIC
        ${PROJECT_NAME}::common_mcrt_logging
        McrtDataio::engine_mcrt
        McrtDataio::engine_merger
        McrtMessages::mcrt_messages
        Moonray::grid_engine_tool 
        SceneRdl2::common_grid_util
        SceneRdl2::render_util
        SceneRdl2::common_fb_util
        ArrasCore::arras4_log 
        ArrasCore::computation_api
)

# Set standard compile/link options
McrtComputation_cxx_compile_definitions(${component})
McrtComputation_cxx_compile_features(${component})
McrtComputation_cxx_compile_options(${component})
McrtComputation_link_options(${component})

install(TARGETS ${component})
