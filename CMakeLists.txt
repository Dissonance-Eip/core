cmake_minimum_required(VERSION 3.11)
project(dissonance-core LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(BUILD_NODE_ADDON "Build the Node.js addon (requires node_api.h)" OFF)
option(BUILD_CLI "Build the CLI executable" ON)
option(ENABLE_COVERAGE "Instrument the build for gcov/gcovr coverage (GCC or Clang)" OFF)

if(ENABLE_COVERAGE)
        add_compile_options(--coverage)
        string(APPEND CMAKE_EXE_LINKER_FLAGS " --coverage")
        string(APPEND CMAKE_SHARED_LINKER_FLAGS " --coverage")
endif()

# The addon is a shared library, so everything linked into it (kissfft
# included) must be position-independent.
if(BUILD_NODE_ADDON)
        set(CMAKE_POSITION_INDEPENDENT_CODE ON)
endif()

set(DISSONANCE_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/include)

include(FetchContent)
FetchContent_Declare(
        kissfft_src
        GIT_REPOSITORY https://github.com/mborgerding/kissfft.git
        GIT_TAG        master
        SOURCE_SUBDIR  "_none_"
)
FetchContent_MakeAvailable(kissfft_src)

add_library(kissfft STATIC ${kissfft_src_SOURCE_DIR}/kiss_fft.c)
target_include_directories(kissfft PUBLIC ${kissfft_src_SOURCE_DIR})
set_target_properties(kissfft PROPERTIES LINKER_LANGUAGE C)
target_compile_options(kissfft PRIVATE
        $<$<C_COMPILER_ID:MSVC>:/wd4267>
        $<$<C_COMPILER_ID:GNU,Clang>:-w>
)

set(DISSONANCE_SOURCES
        src/utils/WavParser.cpp
        src/utils/WavUtils.cpp
        src/audio/GainStage.cpp
        src/audio/PerturbationStage.cpp
        src/audio/Pipeline.cpp
        src/audio/WavProcessor.cpp
        src/audio/WindowedFFTStage.cpp
        src/audio/WindowFunctions.cpp
        src/audio/FFTProcessor.cpp
        src/audio/PsychoacousticModel.cpp
        src/audio/MaskingStage.cpp
        src/audio/MaskingThresholdStage.cpp
)

# CLI command handlers, shared by the executable and the unit tests.
set(DISSONANCE_CLI_SOURCES
        src/cli/Commands.cpp
        src/cli/ConsolePrinter.cpp
)

# Release builds of the addon use node-gyp (binding.gyp). This CMake target
# exists so the coverage build can instrument the addon and run its tests.
if(BUILD_NODE_ADDON)
        if(NOT DEFINED NODE_INCLUDE_DIR)
                execute_process(
                        COMMAND node -p "require('path').join(require('path').dirname(process.execPath), '..', 'include', 'node')"
                        OUTPUT_VARIABLE NODE_INCLUDE_DIR
                        OUTPUT_STRIP_TRAILING_WHITESPACE
                )
        endif()
        add_library(dissonance_core SHARED
                src/addon/addon.cpp
                ${DISSONANCE_SOURCES}
        )
        target_include_directories(dissonance_core PRIVATE
                ${DISSONANCE_INCLUDE_DIR}
                ${CMAKE_SOURCE_DIR}/node_modules/node-addon-api
                ${NODE_INCLUDE_DIR}
        )
        # Same exception mode as binding.gyp; node-addon-api requires one to be set.
        target_compile_definitions(dissonance_core PRIVATE NAPI_CPP_EXCEPTIONS)
        target_link_libraries(dissonance_core PRIVATE kissfft)
        set_target_properties(dissonance_core PROPERTIES PREFIX "" SUFFIX ".node")
        if(APPLE)
                # Node's symbols are resolved when the addon is loaded, not at link time.
                set_target_properties(dissonance_core PROPERTIES LINK_FLAGS "-undefined dynamic_lookup")
        endif()
endif()

if(BUILD_CLI)
        add_executable(dissonance.core
                src/cli/main.cpp
                ${DISSONANCE_CLI_SOURCES}
                ${DISSONANCE_SOURCES}
        )
        target_include_directories(dissonance.core PRIVATE ${DISSONANCE_INCLUDE_DIR})
        target_link_libraries(dissonance.core PRIVATE kissfft)
        set_target_properties(dissonance.core PROPERTIES
                RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        )
endif()

enable_testing()
# Tests mirror src/ (tests/audio, tests/cli, tests/utils) plus tests/integration;
# tests/support holds shared test helpers, and tests/bench holds the benchmark,
# which is built separately below.
file(GLOB_RECURSE TEST_SOURCES tests/*.cpp)
list(FILTER TEST_SOURCES EXCLUDE REGEX ".*/tests/bench/.*")
find_package(GTest QUIET)

if(GTest_FOUND AND TEST_SOURCES)
        add_executable(runTests ${TEST_SOURCES} ${DISSONANCE_CLI_SOURCES} ${DISSONANCE_SOURCES})
        target_include_directories(runTests PRIVATE ${DISSONANCE_INCLUDE_DIR})
        target_link_libraries(runTests PRIVATE kissfft GTest::GTest GTest::Main)
        set_target_properties(runTests PROPERTIES
                RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        )
        # Tests open fixtures such as ./test_files/sound.wav relative to the
        # working directory, so run them from the repo root, not the build dir.
        add_test(NAME runTests COMMAND runTests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
endif()

# Smoke tests for the CLI entry point (src/cli/main.cpp): usage, an unknown
# command, and each command's error path. They use a file that does not exist,
# so they finish instantly; the command handlers themselves are unit-tested in
# tests/cli.
if(BUILD_CLI)
        set(CLI_MISSING_WAV ${CMAKE_SOURCE_DIR}/test_files/does-not-exist.wav)
        add_test(NAME cli_usage COMMAND dissonance.core)
        set_tests_properties(cli_usage PROPERTIES PASS_REGULAR_EXPRESSION "Usage: ")
        add_test(NAME cli_unknown_command COMMAND dissonance.core nope ${CLI_MISSING_WAV})
        set_tests_properties(cli_unknown_command PROPERTIES
                PASS_REGULAR_EXPRESSION "Unknown command: nope"
        )
        foreach(command info process fft bark)
                add_test(NAME cli_${command}_missing_file
                        COMMAND dissonance.core ${command} ${CLI_MISSING_WAV}
                )
                set_tests_properties(cli_${command}_missing_file PROPERTIES
                        PASS_REGULAR_EXPRESSION "Error: Failed to open file"
                )
        endforeach()
endif()

option(BUILD_BENCH "Build benchmark executable" OFF)
if(BUILD_BENCH)
        add_executable(dissonance.bench tests/bench/bench_main.cpp ${DISSONANCE_SOURCES})
        target_include_directories(dissonance.bench PRIVATE ${DISSONANCE_INCLUDE_DIR})
        target_link_libraries(dissonance.bench PRIVATE kissfft)
        set_target_properties(dissonance.bench PROPERTIES
                RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        )
endif()
