# Copyright 2023-2024 DreamWorks Animation LLC
# SPDX-License-Identifier: Apache-2.0

set(component computation_engine_mcrt)

set(InstallIncludeDir ${PACKAGE_NAME}/engine/mcrt)
set(exportGroup ${PROJECT_NAME}Targets)

add_library(${component} SHARED "")
add_library(${PROJECT_NAME}::${component} ALIAS ${component})

target_sources(${component}
    PRIVATE
	DeltaImageCache.cc
	McrtDebugFeedback.cc
        McrtUpdate.cc
        MessageHistory.cc
        RenderContextDriver.cc
        RenderContextDriver_debugCommand.cc
        RenderContextDriver_enqMessage.cc
        RenderContextDriver_evalMessage.cc
	RenderContextDriver_feedback.cc
        RenderContextDriver_onIdle.cc
        RenderContextDriver_startStop.cc
        RenderContextDriverMaster.cc
        TimingRecorder.cc
	Watcher.cc
)

set_property(TARGET ${component}
    PROPERTY PUBLIC_HEADER
        McrtUpdate.h
        MessageHistory.h
        RenderContextDriver.h
        RenderContextDriverMaster.h
)

target_include_directories(${component}
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(${component}
    PUBLIC
        McrtDataio::engine_mcrt
        McrtDataio::engine_merger
        McrtDataio::share_util
        McrtMessages::mcrt_messages
	Moonray::rendering_mcrt_common
        Moonray::rendering_rndr
        Moonray::rendering_shading
        SceneRdl2::common_grid_util
        SceneRdl2::common_rec_time
	SceneRdl2::render_cache
        SceneRdl2::render_logging
        SceneRdl2::render_util
)

# If at Dreamworks add a SConscript stub file so others can use this library.
SConscript_Stub(${component})

# Set standard compile/link options
McrtComputation_cxx_compile_definitions(${component})
McrtComputation_cxx_compile_features(${component})
McrtComputation_cxx_compile_options(${component})
McrtComputation_link_options(${component})

# -------------------------------------
# Install the target and the export set
# -------------------------------------
include(GNUInstallDirs)

# install the target
install(TARGETS ${component}
    COMPONENT ${component}
    EXPORT ${exportGroup}
    LIBRARY
        DESTINATION ${CMAKE_INSTALL_LIBDIR}
        NAMELINK_SKIP
    RUNTIME
        DESTINATION ${CMAKE_INSTALL_BINDIR}
    ARCHIVE
        DESTINATION ${CMAKE_INSTALL_LIBDIR}
    PUBLIC_HEADER
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/${InstallIncludeDir}
)

# # install the export set
# install(
#     EXPORT ${exportGroup}
#     NAMESPACE ${PROJECT_NAME}::
#     DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME}-${PROJECT_VERSION}
# )
