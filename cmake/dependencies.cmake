# =============================================================================
# External Dependencies
# =============================================================================
include(FetchContent)

# ---------------------------------------------------------------------------
# Google Test (for unit tests)
# ---------------------------------------------------------------------------
if(GSST_BUILD_TESTS)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        v1.15.2
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
endif()

# ---------------------------------------------------------------------------
# Google Benchmark (for benchmarks)
# ---------------------------------------------------------------------------
if(GSST_BUILD_BENCHMARKS)
    FetchContent_Declare(
        googlebenchmark
        GIT_REPOSITORY https://github.com/google/benchmark.git
        GIT_TAG        v1.9.1
    )
    set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        set(BENCHMARK_ENABLE_LIBPFM ON CACHE BOOL "" FORCE)
    endif()
    FetchContent_MakeAvailable(googlebenchmark)
endif()
