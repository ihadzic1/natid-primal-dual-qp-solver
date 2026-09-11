cmake_minimum_required(VERSION 3.17)

set(SOLUTION_NAME NatIDQP)
project(${SOLUTION_NAME} VERSION 1.0.0 LANGUAGES CXX)

set(SOURCE_ROOT ${CMAKE_CURRENT_LIST_DIR})

# Required by the natID IDE helper functions used by the GUI target. The
# optional override keeps local/CI installer builds isolated without changing
# the process HOME or USERPROFILE variables.
set(NATID_QP_HOME_ROOT "" CACHE PATH "Optional natID build-home override")
if(NATID_QP_HOME_ROOT)
    file(TO_CMAKE_PATH "${NATID_QP_HOME_ROOT}" HOME_ROOT)
elseif(WIN32)
    file(TO_CMAKE_PATH "$ENV{USERPROFILE}" HOME_ROOT)
else()
    file(TO_CMAKE_PATH "$ENV{HOME}" HOME_ROOT)
endif()

option(
    NATID_QP_PORTABLE_TEST_BACKEND
    "Use the bundled test-only natID compatibility backend (not for benchmarking)"
    OFF
)

if(NOT NATID_QP_PORTABLE_TEST_BACKEND)
    if(NOT DEFINED NATID_SDK_ROOT)
        if(DEFINED ENV{NATID_SDK_ROOT})
            file(TO_CMAKE_PATH "$ENV{NATID_SDK_ROOT}" NATID_SDK_ROOT)
        elseif(WIN32)
            file(TO_CMAKE_PATH "$ENV{USERPROFILE}/natID.SDK" NATID_SDK_ROOT)
        else()
            file(TO_CMAKE_PATH "$ENV{HOME}/natID.SDK" NATID_SDK_ROOT)
        endif()
    endif()

    set(NATID_SDK_ROOT "${NATID_SDK_ROOT}" CACHE PATH "Path to the natID.SDK folder")

    if(NOT EXISTS "${NATID_SDK_ROOT}/DevEnv/Common.cmake")
        message(FATAL_ERROR
            "natID SDK was not found at '${NATID_SDK_ROOT}'. "
            "Set -DNATID_SDK_ROOT=/path/to/natID.SDK."
        )
    endif()

    include("${NATID_SDK_ROOT}/DevEnv/Common.cmake")
    include("${NATID_SDK_ROOT}/DevEnv/MatrixLib.cmake")
    include("${NATID_SDK_ROOT}/DevEnv/SymbComp.cmake")
    include("${NATID_SDK_ROOT}/DevEnv/modSolver.cmake")
else()
    message(STATUS
        "Using portable test backend. This mode validates project logic only; "
        "use a real natID SDK build for submission and benchmarks."
    )
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(NATID_QP_CORE_SOURCES
    src/DTwinReferenceCommon.cpp
    src/InteriorPointSolver.cpp
    src/MatrixMarket.cpp
    src/QPProblem.cpp
)

add_library(natid_qp_core STATIC ${NATID_QP_CORE_SOURCES})
target_include_directories(natid_qp_core PUBLIC "${CMAKE_CURRENT_LIST_DIR}/include")

if(NATID_QP_PORTABLE_TEST_BACKEND)
    target_include_directories(
        natid_qp_core
        PUBLIC "${CMAKE_CURRENT_LIST_DIR}/tests/portable_natID/include"
    )
    target_compile_definitions(natid_qp_core PUBLIC NATID_QP_PORTABLE_TEST_BACKEND)
else()
    target_link_libraries(
        natid_qp_core
        PUBLIC
            debug ${MU_LIB_DEBUG}
            optimized ${MU_LIB_RELEASE}
            debug ${MATRIX_LIB_DEBUG}
            optimized ${MATRIX_LIB_RELEASE}
    )
endif()

if(NOT NATID_QP_PORTABLE_TEST_BACKEND)
    # The adapter is separate from the portable core because it uses the
    # native dTwin/modelSolver runtime shipped with the natID SDK.
    add_library(natid_qp_dtwin STATIC src/DTwinReferenceSolver.cpp)
    target_include_directories(
        natid_qp_dtwin
        PUBLIC "${CMAKE_CURRENT_LIST_DIR}/include"
    )
    target_link_libraries(
        natid_qp_dtwin
        PUBLIC natid_qp_core
        PRIVATE
            debug ${MODSOLVER_LIB_DEBUG}
            optimized ${MODSOLVER_LIB_RELEASE}
            debug ${SYMBCOMP_LIB_DEBUG}
            optimized ${SYMBCOMP_LIB_RELEASE}
            debug ${MU_LIB_DEBUG}
            optimized ${MU_LIB_RELEASE}
    )
endif()

add_executable(natid_qp src/main.cpp)
target_link_libraries(natid_qp PRIVATE natid_qp_core)

if(NOT NATID_QP_PORTABLE_TEST_BACKEND)
    setIDEPropertiesForExecutable(natid_qp)
endif()

add_subdirectory(gui)

include(CTest)
if(BUILD_TESTING)
    add_executable(natid_qp_tests tests/test_solver.cpp)
    target_link_libraries(natid_qp_tests PRIVATE natid_qp_core)
    target_compile_definitions(
        natid_qp_tests
        PRIVATE NATID_QP_TEST_DATA_DIR="${CMAKE_CURRENT_LIST_DIR}/data"
    )
    add_test(NAME natid_qp_solver_tests COMMAND natid_qp_tests)
endif()

if(MSVC)
    target_compile_options(natid_qp_core PRIVATE /W4 /permissive-)
    target_compile_options(natid_qp PRIVATE /W4 /permissive-)
    if(TARGET natid_qp_dtwin)
        target_compile_options(natid_qp_dtwin PRIVATE /W4 /permissive-)
    endif()
else()
    target_compile_options(natid_qp_core PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(natid_qp PRIVATE -Wall -Wextra -Wpedantic)
    if(TARGET natid_qp_dtwin)
        target_compile_options(natid_qp_dtwin PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endif()
