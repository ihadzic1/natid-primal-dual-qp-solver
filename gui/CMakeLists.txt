if(NATID_QP_PORTABLE_TEST_BACKEND)
    message(STATUS "Skipping natid_qp_gui with the portable test backend.")
    return()
endif()

if(NOT TARGET natid_qp_core OR NOT TARGET natid_qp_dtwin)
    message(FATAL_ERROR
        "natid_qp_gui must be added from the NatIDQP root project, "
        "after the natid_qp_core and natid_qp_dtwin targets are created."
    )
endif()

include("${NATID_SDK_ROOT}/DevEnv/natGUI.cmake")

set(NATID_QP_GUI_SOURCES
    src/main.cpp
)

set(NATID_QP_GUI_HEADERS
    src/Application.h
    src/ConvergenceCanvas.h
    src/DashboardView.h
    src/MainWindow.h
    src/ObjectiveChartPanel.h
    src/ObjectiveCanvas.h
)

set(NATID_QP_GUI_PLIST "${CMAKE_CURRENT_LIST_DIR}/src/Info.plist")

add_executable(
    natid_qp_gui
    ${NATID_QP_GUI_SOURCES}
    ${NATID_QP_GUI_HEADERS}
)

target_link_libraries(
    natid_qp_gui
    PRIVATE
        natid_qp_core
        natid_qp_dtwin
        debug ${NATGUI_LIB_DEBUG}
        optimized ${NATGUI_LIB_RELEASE}
)

# Used by the problem quick-select in development builds. Deployed builds also
# search the natID application resource directory populated by the installer.
target_compile_definitions(
    natid_qp_gui
    PRIVATE NATID_QP_DATA_DIR="${CMAKE_CURRENT_LIST_DIR}/../data"
)

source_group("src" FILES ${NATID_QP_GUI_SOURCES})
source_group("inc" FILES ${NATID_QP_GUI_HEADERS})

setTargetPropertiesForGUIApp(natid_qp_gui "${NATID_QP_GUI_PLIST}")
setIDEPropertiesForGUIExecutable(natid_qp_gui "${CMAKE_CURRENT_LIST_DIR}")

# Common.cmake from some natID SDK packages does not forward the function's
# devResPath argument into the Visual Studio debugger property. Set it
# explicitly so F5 works immediately after CMake reconfiguration.
if(WIN32)
    set_target_properties(
        natid_qp_gui
        PROPERTIES
            VS_DEBUGGER_COMMAND_ARGUMENTS
                "-devResPath=${CMAKE_CURRENT_LIST_DIR}"
    )
endif()

setPlatformDLLPath(natid_qp_gui)

if(MSVC)
    target_compile_options(natid_qp_gui PRIVATE /W4 /permissive-)
else()
    target_compile_options(natid_qp_gui PRIVATE -Wall -Wextra -Wpedantic)
endif()
