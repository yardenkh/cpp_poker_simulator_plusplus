cmake_minimum_required(VERSION 3.16)

project(PokerEngine LANGUAGES CXX)

# The fast hand evaluator uses C++20 features such as std::popcount.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Release builds may optimize across files when supported by the compiler.
option(POKER_ENABLE_IPO "Enable interprocedural optimization in Release" ON)
if(POKER_ENABLE_IPO)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT POKER_IPO_SUPPORTED OUTPUT POKER_IPO_ERROR)
    if(POKER_IPO_SUPPORTED)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
    endif()
endif()
find_package(Threads REQUIRED)

# Path to the pre-generated hand lookup table.
set(HAND_TABLE_FILE
    ${CMAKE_CURRENT_SOURCE_DIR}/data/hand_tables.bin
)

# ------------------------------------------------------------
# poker_engine
#
# This is the actual poker engine library.
# Put engine .cpp files here, but NOT main.cpp or test files.
# ------------------------------------------------------------
add_library(poker_engine
    card/Card.cpp
    deck/Deck.cpp
    evaluation/HandEvaluator.cpp
    evaluation/FastHandEvaluator.cpp
    player/Player.cpp
    table/Table.cpp
    # Compile the placeholder AI so it is available to engine callers.
    decision/AIDecisionProvider.cpp
    decision/AIActionDistribution.cpp
    # Hand orchestration uses betting, contribution records, and player views.
    betting/Betting.cpp
    pot/PotManager.cpp
    view/PlayerView.cpp
    game/PokerGame.cpp
    simulation/Simulation.cpp
    # Caller-driven equity analysis; games never invoke it automatically.
    analysis/Analysis.cpp
    # Tournament orchestration wraps complete engine hands.
    tournament/Tournament.cpp
    simulation/TournamentSimulation.cpp
)

# Load the lookup table once by default. Applications can inject a different
# evaluator path; this source-tree path keeps tests independent of their cwd.
target_compile_definitions(poker_engine PRIVATE
    POKER_DEFAULT_HAND_TABLE="${HAND_TABLE_FILE}"
)
target_link_libraries(poker_engine PUBLIC Threads::Threads)

# Let source files include headers from the project root.
#
# For example:
#   #include "card/Card.hpp"
#   #include "deck/Deck.hpp"
#   #include "evaluation/FastHandEvaluator.hpp"
target_include_directories(poker_engine PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Enable useful compiler warnings.
if(MSVC)
    target_compile_options(poker_engine PRIVATE
        /W4
    )
else()
    target_compile_options(poker_engine PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
endif()

# ------------------------------------------------------------
# poker_cli
#
# This is the normal executable that starts from main.cpp.
# It uses the poker_engine library.
# ------------------------------------------------------------
# Terminal adapters are separate so headless users link only poker_engine.
add_library(poker_terminal ui/TerminalUI.cpp decision/HumanDecisionProvider.cpp)
target_link_libraries(poker_terminal PUBLIC poker_engine)
if(MSVC)
    target_compile_options(poker_terminal PRIVATE /W4)
else()
    target_compile_options(poker_terminal PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(poker_cli
    main.cpp
)

target_link_libraries(poker_cli PRIVATE
    poker_terminal
)

# Copy the already-generated hand table next to poker_cli.
#
# This does NOT regenerate or compile the table.
# It only copies the binary file when necessary.
add_custom_command(
    TARGET poker_cli
    POST_BUILD

    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${HAND_TABLE_FILE}
        $<TARGET_FILE_DIR:poker_cli>/hand_tables.bin

    COMMENT "Copying hand_tables.bin for poker_cli"
    VERBATIM
)

# ------------------------------------------------------------
# poker_tests
#
# Separate executable containing your tests.
# It also uses the poker_engine library.
# ------------------------------------------------------------
add_executable(poker_tests
    tests/PokerTests.cpp
)

target_link_libraries(poker_tests PRIVATE
    poker_engine
)

# The tests may also use FastHandEvaluator, so give them their own
# copy of the lookup table next to the test executable.
add_custom_command(
    TARGET poker_tests
    POST_BUILD

    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${HAND_TABLE_FILE}
        $<TARGET_FILE_DIR:poker_tests>/hand_tables.bin

    COMMENT "Copying hand_tables.bin for poker_tests"
    VERBATIM
)

# ------------------------------------------------------------
# poker_validation
#
# Optional but explicit random differential validation. This is intentionally
# excluded from the default build because it evaluates one million random hands.
# ------------------------------------------------------------
add_executable(poker_validation EXCLUDE_FROM_ALL
    tools/RandomValidation.cpp
)

target_link_libraries(poker_validation PRIVATE
    poker_engine
)

target_include_directories(poker_validation PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

add_custom_command(
    TARGET poker_validation
    POST_BUILD

    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${HAND_TABLE_FILE}
        $<TARGET_FILE_DIR:poker_validation>/hand_tables.bin

    COMMENT "Copying hand_tables.bin for poker_validation"
    VERBATIM
)

add_custom_target(run_random_validation
    COMMAND ${CMAKE_COMMAND} -E chdir ${CMAKE_CURRENT_BINARY_DIR} $<TARGET_FILE:poker_validation>
    DEPENDS poker_validation
    COMMENT "Running 1,000,000-hand differential validation"
    VERBATIM
)

# ------------------------------------------------------------
# poker_benchmark
#
# Optional benchmark target for comparing the fast lookup evaluator against the
# reference evaluator. It is excluded from normal builds to keep default CI
# output focused on correctness.
# ------------------------------------------------------------
add_executable(poker_benchmark EXCLUDE_FROM_ALL
    tools/BenchmarkFastEvaluator.cpp
)

target_link_libraries(poker_benchmark PRIVATE
    poker_engine
)

target_include_directories(poker_benchmark PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

add_custom_command(
    TARGET poker_benchmark
    POST_BUILD

    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${HAND_TABLE_FILE}
        $<TARGET_FILE_DIR:poker_benchmark>/hand_tables.bin

    COMMENT "Copying hand_tables.bin for poker_benchmark"
    VERBATIM
)

add_custom_target(run_poker_benchmark
    COMMAND ${CMAKE_COMMAND} -E chdir ${CMAKE_CURRENT_BINARY_DIR} $<TARGET_FILE:poker_benchmark>
    DEPENDS poker_benchmark
    COMMENT "Benchmarking fast evaluator performance"
    VERBATIM
)

# ------------------------------------------------------------
# generate_hand_tables
#
# Utility executable used ONLY when we intentionally want to
# regenerate hand_tables.bin.
#
# EXCLUDE_FROM_ALL is important:
#
#   cmake --build build
#
# will NOT build this executable and will NOT regenerate the table.
# ------------------------------------------------------------
add_executable(generate_hand_tables EXCLUDE_FROM_ALL
    tools/GenerateFastHandTables.cpp
)

target_include_directories(generate_hand_tables PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# ------------------------------------------------------------
# regenerate_hand_tables
#
# Explicit target for rebuilding the ~15 MB lookup table.
#
# Run:
#
#   cmake --build build --target regenerate_hand_tables
#
# Normal builds do not run this.
# ------------------------------------------------------------
add_custom_target(regenerate_hand_tables
    COMMAND
        $<TARGET_FILE:generate_hand_tables>
        ${HAND_TABLE_FILE}

    DEPENDS
        generate_hand_tables

    WORKING_DIRECTORY
        ${CMAKE_CURRENT_SOURCE_DIR}

    COMMENT
        "Regenerating data/hand_tables.bin"

    VERBATIM
)

# ------------------------------------------------------------
# CTest
# ------------------------------------------------------------
enable_testing()

# Independent toy game for future CFR/CFR+ validation. No Hold'em evaluator
# tables or poker_engine dependencies are required by this library/test.
add_library(poker_kuhn research/games/kuhn/KuhnGame.cpp)
# Export the repository root so consumers can include the module by its full
# project-relative header path. Link poker_kuhn into the future solver library.
target_include_directories(poker_kuhn PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
if(MSVC)
    target_compile_options(poker_kuhn PRIVATE /W4)
else()
    target_compile_options(poker_kuhn PRIVATE -Wall -Wextra -Wpedantic)
endif()
# Keep the executable beside the toy game's sources, and register it with the
# normal CTest suite. Linking only poker_kuhn checks the module stays independent.
add_executable(poker_kuhn_tests research/games/kuhn/KuhnGameTests.cpp)
target_link_libraries(poker_kuhn_tests PRIVATE poker_kuhn)
add_test(NAME poker_kuhn_tests COMMAND poker_kuhn_tests)

add_executable(poker_tournament_tests tests/TournamentTests.cpp)
target_link_libraries(poker_tournament_tests PRIVATE poker_engine)
add_test(NAME poker_tournament_tests COMMAND poker_tournament_tests)

# Parallel tournament episodes have their own runner and determinism checks.
add_executable(poker_tournament_simulation_tests tests/TournamentSimulationTests.cpp)
target_link_libraries(poker_tournament_simulation_tests PRIVATE poker_engine)
add_test(NAME poker_tournament_simulation_tests COMMAND poker_tournament_simulation_tests)

# Build explicitly for large experiments: each episode is a complete tournament.
add_executable(poker_tournament_simulation EXCLUDE_FROM_ALL tools/TournamentSimulation.cpp)
target_link_libraries(poker_tournament_simulation PRIVATE poker_engine)

# Seeded equity calculations, invalid observations, and exact reference cases.
add_executable(poker_analysis_tests tests/AnalysisTests.cpp)
target_link_libraries(poker_analysis_tests PRIVATE poker_engine)
add_test(NAME poker_analysis_tests COMMAND poker_analysis_tests)

# Stream-based checks exercise human input without an interactive session.
add_executable(poker_terminal_tests tests/TerminalTests.cpp)
target_link_libraries(poker_terminal_tests PRIVATE poker_terminal)
add_test(NAME poker_terminal_tests COMMAND poker_terminal_tests)

# Full-hand regressions use scripted providers and the shared lookup evaluator.
add_executable(poker_game_tests tests/GameTests.cpp)
target_link_libraries(poker_game_tests PRIVATE poker_engine)
add_test(NAME poker_game_tests COMMAND poker_game_tests)

# Resumable games, batched inference, replay, and parallel determinism.
add_executable(poker_research_tests tests/ResearchTests.cpp)
target_link_libraries(poker_research_tests PRIVATE poker_engine)
add_test(NAME poker_research_tests COMMAND poker_research_tests)

# Permanent end-to-end throughput benchmark; excluded from routine builds.
add_executable(poker_simulation_benchmark EXCLUDE_FROM_ALL tools/BenchmarkSimulation.cpp)
target_link_libraries(poker_simulation_benchmark PRIVATE poker_engine)

add_test(
    NAME poker_tests
    COMMAND poker_tests
)

# Make the test executable's directory the working directory.
# This means opening "hand_tables.bin" works regardless of whether
# the generator uses a single-config or multi-config build layout.
set_tests_properties(poker_tests PROPERTIES
    WORKING_DIRECTORY $<TARGET_FILE_DIR:poker_tests>
)

add_executable(poker_ai_tests tests/AIDecisionProviderTests.cpp)
target_link_libraries(poker_ai_tests PRIVATE poker_engine)
add_test(NAME poker_ai_tests COMMAND poker_ai_tests)

# Runtime policy inference is reusable independently of the trainer.
add_library(poker_policy policy/Observation.cpp policy/FrozenPolicy.cpp policy/TrainedAIDecisionProvider.cpp)
target_link_libraries(poker_policy PUBLIC poker_engine)
add_library(poker_mccfr research/mccfr/TrainingConfig.cpp research/mccfr/StrategyTable.cpp
    research/mccfr/HoldemTrainingGame.cpp research/mccfr/Checkpoint.cpp)
target_link_libraries(poker_mccfr PUBLIC poker_policy)
foreach(target poker_policy poker_mccfr)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endforeach()
add_executable(poker_train_mccfr tools/TrainMCCFR.cpp)
target_link_libraries(poker_train_mccfr PRIVATE poker_mccfr)
add_library(poker_leduc research/games/leduc/LeducGame.cpp)
target_link_libraries(poker_leduc PUBLIC poker_policy)
add_executable(poker_mccfr_tests tests/MCCFRTests.cpp)
target_link_libraries(poker_mccfr_tests PRIVATE poker_mccfr poker_kuhn poker_leduc)
add_test(NAME poker_mccfr_tests COMMAND poker_mccfr_tests)
add_test(NAME poker_policy_fresh_process COMMAND poker_mccfr_tests --read-policy mccfr-fresh-process.policy)
set_tests_properties(poker_policy_fresh_process PROPERTIES DEPENDS poker_mccfr_tests)
target_sources(poker_mccfr PRIVATE research/mccfr/Evaluation.cpp)
add_executable(poker_evaluate_mccfr tools/EvaluateMCCFR.cpp)
target_link_libraries(poker_evaluate_mccfr PRIVATE poker_mccfr)

# Keep warnings enabled on adapter/test executables too.
foreach(target poker_leduc poker_train_mccfr poker_evaluate_mccfr poker_mccfr_tests)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endforeach()