# =============================================================================
# GSST - GPU Static Symbol Table Compression
# =============================================================================
# A high-performance GPU-accelerated string compression library based on FSST,
# providing both compression (74+ GB/s) and decompression on NVIDIA GPUs.
#
# Supports three decompression layouts:
#   - Blocks:   Simple 1-block-per-thread decompression
#   - Splits:   Sub-block parallelism with aligned I/O
#   - Coalesce: Interleaved symbols for coalesced memory access
# =============================================================================

cmake_minimum_required(VERSION 4.1)

project(gsst
    VERSION 0.1.0
    DESCRIPTION "GPU-accelerated FSST string compression and decompression"
    LANGUAGES CXX CUDA C
)

# ---------------------------------------------------------------------------
# Global options
# ---------------------------------------------------------------------------
option(GSST_BUILD_TESTS      "Build unit tests"               ON)
option(GSST_BUILD_BENCHMARKS "Build benchmarks"                ON)
option(GSST_BUILD_EXAMPLES   "Build example programs"          ON)
option(GSST_INSTALL          "Generate install targets"        ON)
option(GSST_USE_NVCOMP       "Enable nvCOMP baseline support"  OFF)
option(GSST_ENABLE_CLANG_TIDY "Run clang-tidy during build"    OFF)
option(GSST_ENABLE_CPPCHECK   "Run cppcheck during build"     OFF)

# ---------------------------------------------------------------------------
# C++ and CUDA standards
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_CUDA_STANDARD 20)
set(CMAKE_CUDA_STANDARD_REQUIRED ON)
set(CMAKE_CUDA_EXTENSIONS OFF)

set(CMAKE_CUDA_SEPARABLE_COMPILATION ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# CUDA architectures — override with -DCMAKE_CUDA_ARCHITECTURES="80;86;89;90"
if(NOT DEFINED CMAKE_CUDA_ARCHITECTURES)
    set(CMAKE_CUDA_ARCHITECTURES "80;86;89;90")
endif()

# ---------------------------------------------------------------------------
# Build type defaults
# ---------------------------------------------------------------------------
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE
        PROPERTY STRINGS "Debug" "Release" "RelWithDebInfo" "MinSizeRel")
endif()

# ---------------------------------------------------------------------------
# Compiler flags
# ---------------------------------------------------------------------------
include(CheckCXXCompilerFlag)

if(NOT MSVC)
    check_cxx_compiler_flag("-march=native" COMPILER_SUPPORTS_MARCH_NATIVE)
    if(COMPILER_SUPPORTS_MARCH_NATIVE)
        add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-march=native>)
    endif()

    add_compile_options(
        $<$<AND:$<COMPILE_LANGUAGE:CUDA>,$<CONFIG:Release>>:-O3>
        $<$<AND:$<COMPILE_LANGUAGE:CUDA>,$<CONFIG:Release>>:--generate-line-info>
        $<$<AND:$<COMPILE_LANGUAGE:CUDA>,$<CONFIG:Debug>>:-G>
        $<$<AND:$<COMPILE_LANGUAGE:CUDA>,$<CONFIG:Debug>>:--generate-line-info>
    )
else()
    add_compile_options(
        $<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Release>>:/O2>
    )
endif()

# ---------------------------------------------------------------------------
# Dependencies
# ---------------------------------------------------------------------------
# On Windows, help CMake find the CUDA toolkit if not already set
if(WIN32 AND NOT DEFINED CMAKE_CUDA_COMPILER)
    # Check common CUDA installation paths on Windows
    file(GLOB CUDA_PATHS "C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v*")
    if(CUDA_PATHS)
        list(SORT CUDA_PATHS)
        list(REVERSE CUDA_PATHS)  # Get the latest version first
        list(GET CUDA_PATHS 0 CUDA_TOOLKIT_PATH)
        set(CUDAToolkit_ROOT "${CUDA_TOOLKIT_PATH}" CACHE PATH "CUDA toolkit path")
        message(STATUS "Found CUDA toolkit at: ${CUDA_TOOLKIT_PATH}")
    endif()
endif()

find_package(CUDAToolkit REQUIRED)
find_package(Threads REQUIRED)

include(cmake/dependencies.cmake)

# ---------------------------------------------------------------------------
# Core library: gsst
# ---------------------------------------------------------------------------
add_library(gsst
    src/gsst.cu
    src/symbol_table.cu
    src/encoder.cu
    src/decoder.cu
    src/format.cu
)

add_library(gsst::gsst ALIAS gsst)

target_include_directories(gsst
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(gsst
    PUBLIC
        CUDA::cudart
        Threads::Threads
)

target_compile_features(gsst PUBLIC cxx_std_20 cuda_std_20)

set_target_properties(gsst PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    CUDA_RESOLVE_DEVICE_SYMBOLS ON
    VERSION   ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
)

# ---------------------------------------------------------------------------
# Install
# ---------------------------------------------------------------------------
if(GSST_INSTALL)
    include(GNUInstallDirs)
    include(CMakePackageConfigHelpers)

    install(TARGETS gsst
        EXPORT gsst-targets
        LIBRARY  DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE  DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME  DESTINATION ${CMAKE_INSTALL_BINDIR}
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )

    install(DIRECTORY include/gsst
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )

    install(EXPORT gsst-targets
        FILE gsst-targets.cmake
        NAMESPACE gsst::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/gsst
    )

    configure_package_config_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/cmake/gsst-config.cmake.in
        ${CMAKE_CURRENT_BINARY_DIR}/gsst-config.cmake
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/gsst
    )

    write_basic_package_version_file(
        ${CMAKE_CURRENT_BINARY_DIR}/gsst-config-version.cmake
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion
    )

    install(FILES
        ${CMAKE_CURRENT_BINARY_DIR}/gsst-config.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/gsst-config-version.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/gsst
    )
endif()

# ---------------------------------------------------------------------------
# Static analysis (applied to targets defined below, not third-party deps)
# ---------------------------------------------------------------------------
if(GSST_ENABLE_CLANG_TIDY)
    find_program(CLANG_TIDY_EXE NAMES clang-tidy clang-tidy-18 clang-tidy-19)
    if(CLANG_TIDY_EXE)
        message(STATUS "clang-tidy found: ${CLANG_TIDY_EXE}")
        set(CMAKE_CXX_CLANG_TIDY
            "${CLANG_TIDY_EXE}" "--config-file=${CMAKE_SOURCE_DIR}/.clang-tidy")
    else()
        message(WARNING "GSST_ENABLE_CLANG_TIDY is ON but clang-tidy not found")
    endif()
endif()

if(GSST_ENABLE_CPPCHECK)
    find_program(CPPCHECK_EXE NAMES cppcheck)
    if(CPPCHECK_EXE)
        message(STATUS "cppcheck found: ${CPPCHECK_EXE}")
        set(CMAKE_CXX_CPPCHECK
            "${CPPCHECK_EXE}"
            "--enable=warning,style,performance,portability"
            "--suppress=missingIncludeSystem"
            "--suppress=unmatchedSuppression"
            "--inline-suppr"
            "--std=c++20")
    else()
        message(WARNING "GSST_ENABLE_CPPCHECK is ON but cppcheck not found")
    endif()
endif()

# ---------------------------------------------------------------------------
# Sub-projects
# ---------------------------------------------------------------------------
if(GSST_BUILD_TESTS)
    enable_testing()
    add_subdirectory(test)
endif()

if(GSST_BUILD_BENCHMARKS)
    add_subdirectory(bench)
endif()

if(GSST_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()
