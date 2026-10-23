cmake_minimum_required(VERSION 3.25)
project(algorithmic_trading_runtime LANGUAGES CXX)
set(CMAKE_CXX_EXTENSIONS OFF)

option(TRADING_WARNINGS_AS_ERRORS "Treat project compiler warnings as errors" ON)
option(TRADING_BUILD_DIAGNOSTICS "Build optional live data diagnostic tools" OFF)

function(configure_trading_target target)
    target_compile_features(${target} PUBLIC cxx_std_20)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
        if(TRADING_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
        if(TRADING_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
endfunction()

include(FetchContent)
set(JSON_BuildTests OFF CACHE BOOL "Build the JSON dependency's tests")
FetchContent_Declare(nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
    URL_HASH SHA256=42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa
    DOWNLOAD_EXTRACT_TIMESTAMP FALSE)
FetchContent_MakeAvailable(nlohmann_json)

add_library(trading_core
    src/market/minute_bar.cpp src/market/utc_timestamp.cpp
    src/storage/minute_bar_csv.cpp src/storage/artifact_io.cpp
    src/alpaca/alpaca_credentials.cpp src/alpaca/alpaca_bar_request.cpp src/alpaca/api_request_policy.cpp
    src/alpaca/alpaca_corporate_action_request.cpp)
target_include_directories(trading_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src/market"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/alpaca"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/storage")
configure_trading_target(trading_core)

add_library(alpaca_decoder src/alpaca/alpaca_bar_page.cpp src/alpaca/json_input.cpp src/market/market_calendar.cpp)
target_link_libraries(alpaca_decoder PUBLIC trading_core PRIVATE nlohmann_json::nlohmann_json)
configure_trading_target(alpaca_decoder)

add_library(artifact_integrity src/storage/content_digest.cpp)
target_link_libraries(artifact_integrity PUBLIC trading_core)
if(WIN32)
    target_sources(artifact_integrity PRIVATE src/platform/windows/content_digest_windows.cpp)
    target_link_libraries(artifact_integrity PRIVATE bcrypt)
    target_compile_definitions(artifact_integrity PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00)
else()
    find_package(OpenSSL REQUIRED COMPONENTS Crypto)
    target_sources(artifact_integrity PRIVATE src/platform/openssl/content_digest_openssl.cpp)
    target_link_libraries(artifact_integrity PRIVATE OpenSSL::Crypto)
endif()
configure_trading_target(artifact_integrity)

set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    research/intraday_momentum_protocol.md research/development_data_policy.json
    research/intraday_continuation_protocol.md)
file(SHA256 "${CMAKE_CURRENT_SOURCE_DIR}/research/intraday_momentum_protocol.md" protocol_sha256)
file(SHA256 "${CMAKE_CURRENT_SOURCE_DIR}/research/development_data_policy.json" data_policy_sha256)
file(SHA256 "${CMAKE_CURRENT_SOURCE_DIR}/research/intraday_continuation_protocol.md" continuation_protocol_sha256)
file(SHA256 "${CMAKE_CURRENT_SOURCE_DIR}/research/scheduled_releases_2022_2023.csv" release_calendar_sha256)

add_library(session_data
    src/storage/session_download.cpp src/storage/session_storage.cpp src/storage/session_gap_probe.cpp
    src/storage/session_batch.cpp src/storage/development_dataset.cpp src/storage/development_study_dataset.cpp)
target_link_libraries(session_data PUBLIC alpaca_decoder artifact_integrity PRIVATE nlohmann_json::nlohmann_json)
target_compile_definitions(session_data PRIVATE TRADING_DATA_POLICY_SHA256="${data_policy_sha256}")
configure_trading_target(session_data)

add_library(momentum_research
    src/research/intraday_momentum.cpp src/research/batch_momentum.cpp
    src/research/return_statistics.cpp src/research/development_study.cpp
    src/research/intraday_continuation.cpp src/research/continuation_study.cpp
    src/research/continuation_diagnostics.cpp src/research/intraday_reversion.cpp
    src/research/reversion_preflight.cpp src/research/reversion_uncertainty.cpp
    src/research/reversion_validation.cpp)
target_include_directories(momentum_research PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src/research")
target_link_libraries(momentum_research PUBLIC session_data PRIVATE nlohmann_json::nlohmann_json)
target_compile_definitions(momentum_research PRIVATE TRADING_PROTOCOL_SHA256="${protocol_sha256}")
target_compile_definitions(momentum_research PRIVATE
    TRADING_CONTINUATION_PROTOCOL_SHA256="${continuation_protocol_sha256}")
configure_trading_target(momentum_research)

add_library(accounting src/accounting/long_cash_account.cpp)
target_include_directories(accounting PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src/accounting")
configure_trading_target(accounting)

add_library(order_execution src/execution/order_runtime.cpp src/execution/runtime_input.cpp src/execution/execution_diagnostic.cpp)
target_include_directories(order_execution PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src/execution")
configure_trading_target(order_execution)

add_library(execution_persistence src/execution/journaled_runtime.cpp src/storage/runtime_json.cpp src/execution/recovery_diagnostic.cpp)
target_link_libraries(execution_persistence PUBLIC order_execution artifact_integrity nlohmann_json::nlohmann_json
    PRIVATE alpaca_decoder)
if(WIN32)
    target_sources(execution_persistence PRIVATE src/platform/windows/journal_storage_windows.cpp)
    target_compile_definitions(execution_persistence PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00)
else()
    target_sources(execution_persistence PRIVATE src/platform/posix/journal_storage_posix.cpp)
endif()
configure_trading_target(execution_persistence)

add_library(offline_execution src/execution/simulated_broker.cpp src/execution/offline_pipeline.cpp
    src/execution/momentum_order_adapter.cpp src/execution/pipeline_diagnostic.cpp)
target_link_libraries(offline_execution PUBLIC execution_persistence momentum_research)
configure_trading_target(offline_execution)

add_library(risk_inputs src/storage/risk_input_dataset.cpp src/accounting/corporate_action_history.cpp)
target_include_directories(risk_inputs PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src/accounting")
target_link_libraries(risk_inputs PUBLIC session_data PRIVATE nlohmann_json::nlohmann_json)
configure_trading_target(risk_inputs)

add_library(risk_preflight_core src/research/risk_preflight.cpp)
target_include_directories(risk_preflight_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src/research")
target_link_libraries(risk_preflight_core PUBLIC accounting session_data risk_inputs PRIVATE nlohmann_json::nlohmann_json)
configure_trading_target(risk_preflight_core)

add_library(daily_risk_research
    src/research/daily_risk_dataset.cpp src/research/daily_risk_study.cpp src/research/risk_measurement.cpp)
target_link_libraries(daily_risk_research PUBLIC risk_preflight_core momentum_research)
configure_trading_target(daily_risk_research)

# Actual source-file bytes and build identity accompany every generated study.
file(GLOB_RECURSE study_sources CONFIGURE_DEPENDS RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}"
    "src/*.cpp" "src/*.hpp" "apps/*.cpp" "apps/*.hpp" "research/*")
list(APPEND study_sources CMakeLists.txt CMakePresets.json)
list(SORT study_sources)
set(source_records "")
foreach(source_file IN LISTS study_sources)
    file(SHA256 "${CMAKE_CURRENT_SOURCE_DIR}/${source_file}" source_file_sha256)
    string(APPEND source_records "${source_file}:${source_file_sha256}\n")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${source_file}")
endforeach()
string(SHA256 source_sha256 "${source_records}")
# Embed the exact configure-time inventory with its aggregate. A rebuilt report
# can diagnose individual source-byte differences without opening any dataset.
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/source_inventory.txt" "${source_records}")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/generated/trading_source_inventory.hpp"
    "#pragma once\n#include <string_view>\nnamespace trading::build {\n"
    "inline constexpr std::string_view source_sha256{\"${source_sha256}\"};\n"
    "inline constexpr std::string_view source_records{R\"inventory(${source_records})inventory\"};\n}\n")

add_library(execution_reporting src/reporting/execution_diagnostic_report.cpp src/reporting/recovery_diagnostic_report.cpp)
target_include_directories(execution_reporting PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src/reporting"
    PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_link_libraries(execution_reporting PUBLIC execution_persistence trading_core PRIVATE nlohmann_json::nlohmann_json)
target_compile_definitions(execution_reporting PRIVATE
    TRADING_COMPILER_ID="${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}"
    TRADING_BUILD_CONFIGURATION="$<CONFIG>")
configure_trading_target(execution_reporting)
add_library(pipeline_reporting src/reporting/pipeline_diagnostic_report.cpp)
target_include_directories(pipeline_reporting PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src/reporting"
    PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_link_libraries(pipeline_reporting PUBLIC offline_execution)
target_compile_definitions(pipeline_reporting PRIVATE
    TRADING_COMPILER_ID="${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}"
    TRADING_BUILD_CONFIGURATION="$<CONFIG>")
configure_trading_target(pipeline_reporting)
add_library(reversion_reporting src/reporting/reversion_preflight_report.cpp
    src/reporting/reversion_validation_report.cpp)
target_include_directories(reversion_reporting PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src/reporting"
    PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_link_libraries(reversion_reporting PUBLIC momentum_research PRIVATE nlohmann_json::nlohmann_json)
target_compile_definitions(reversion_reporting PRIVATE
    TRADING_COMPILER_ID="${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}"
    TRADING_BUILD_CONFIGURATION="$<CONFIG>")
configure_trading_target(reversion_reporting)
add_library(continuation_reporting src/reporting/continuation_study_report.cpp)
target_include_directories(continuation_reporting PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src/reporting")
target_link_libraries(continuation_reporting PUBLIC momentum_research PRIVATE nlohmann_json::nlohmann_json)
target_compile_definitions(continuation_reporting PRIVATE
    TRADING_SOURCE_SHA256="${source_sha256}"
    TRADING_RELEASE_CALENDAR_SHA256="${release_calendar_sha256}"
    TRADING_BUILD_COMPILER="${CMAKE_CXX_COMPILER_ID}-${CMAKE_CXX_COMPILER_VERSION}"
    TRADING_BUILD_CONFIGURATION="$<CONFIG>")
configure_trading_target(continuation_reporting)

add_library(risk_preflight_reporting src/reporting/risk_preflight_report.cpp src/reporting/risk_input_report.cpp)
target_include_directories(risk_preflight_reporting PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src/reporting")
target_link_libraries(risk_preflight_reporting PUBLIC risk_preflight_core PRIVATE nlohmann_json::nlohmann_json)
target_compile_definitions(risk_preflight_reporting PRIVATE
    TRADING_SOURCE_SHA256="${source_sha256}"
    TRADING_COMPILER_ID="${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}"
    TRADING_BUILD_CONFIGURATION="$<CONFIG>")
configure_trading_target(risk_preflight_reporting)

add_library(daily_risk_reporting src/reporting/daily_risk_report.cpp)
target_include_directories(daily_risk_reporting PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src/reporting")
target_link_libraries(daily_risk_reporting PUBLIC daily_risk_research PRIVATE nlohmann_json::nlohmann_json)
target_compile_definitions(daily_risk_reporting PRIVATE
    TRADING_SOURCE_SHA256="${source_sha256}"
    TRADING_COMPILER_ID="${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}"
    TRADING_BUILD_CONFIGURATION="$<CONFIG>")
configure_trading_target(daily_risk_reporting)

function(add_trading_app target library)
    add_executable(${target} apps/${target}.cpp)
    target_link_libraries(${target} PRIVATE ${library})
    target_compile_definitions(${target} PRIVATE TRADING_PROJECT_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
    configure_trading_target(${target})
endfunction()
add_trading_app(development_finalize session_data)
add_trading_app(development_replay momentum_research)
add_trading_app(continuation_diagnostic momentum_research)
add_trading_app(continuation_development continuation_reporting)
add_trading_app(risk_preflight risk_preflight_reporting)
add_trading_app(risk_acquire risk_preflight_reporting)
add_trading_app(risk_development daily_risk_reporting)
add_trading_app(execution_diagnostic execution_reporting)
add_trading_app(recovery_diagnostic execution_reporting)
add_trading_app(pipeline_diagnostic pipeline_reporting)
add_trading_app(reversion_preflight reversion_reporting)
add_trading_app(reversion_validation reversion_reporting)
target_sources(continuation_diagnostic PRIVATE src/reporting/continuation_diagnostic_report.cpp)
target_include_directories(continuation_diagnostic PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/reporting")

if(WIN32)
    add_library(alpaca_market_data src/platform/windows/alpaca_market_data_client_winhttp.cpp)
    target_link_libraries(alpaca_market_data PUBLIC alpaca_decoder PRIVATE winhttp)
    target_compile_definitions(alpaca_market_data PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00)
    configure_trading_target(alpaca_market_data)

    add_library(alpaca_console
        src/platform/windows/alpaca_console_credentials.cpp
        src/platform/windows/alpaca_credential_store_windows.cpp)
    target_include_directories(alpaca_console PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/windows")
    target_link_libraries(alpaca_console PUBLIC trading_core PRIVATE advapi32)
    target_compile_definitions(alpaca_console PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    configure_trading_target(alpaca_console)

    add_trading_app(alpaca_development "alpaca_market_data;session_data;alpaca_console")
    target_link_libraries(risk_preflight PRIVATE alpaca_market_data alpaca_console)
    target_link_libraries(risk_acquire PRIVATE alpaca_market_data alpaca_console)
    if(TRADING_BUILD_DIAGNOSTICS)
        add_executable(alpaca_gap_probe apps/diagnostics/alpaca_gap_probe.cpp)
        target_link_libraries(alpaca_gap_probe PRIVATE alpaca_market_data session_data alpaca_console)
        target_compile_definitions(alpaca_gap_probe PRIVATE TRADING_PROJECT_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
        configure_trading_target(alpaca_gap_probe)
    endif()
endif()

include(CTest)
if(BUILD_TESTING)
    function(add_trading_contract target test_name libraries)
        add_executable(${target} tests/${target}.cpp)
        target_link_libraries(${target} PRIVATE ${libraries})
        target_compile_definitions(${target} PRIVATE TRADING_PROJECT_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
        configure_trading_target(${target})
        if(ARGV3)
            add_test(NAME ${test_name} COMMAND ${target} "${CMAKE_CURRENT_BINARY_DIR}/${target}-output")
        else()
            add_test(NAME ${test_name} COMMAND ${target})
        endif()
    endfunction()
    add_trading_contract(minute_bar_tests minute_bar_contracts trading_core)
    add_trading_contract(alpaca_credentials_tests alpaca_credentials_contracts trading_core)
    add_trading_contract(alpaca_bar_page_tests alpaca_bar_page_contracts "alpaca_decoder;nlohmann_json::nlohmann_json")
    add_trading_contract(session_download_tests session_download_contracts "session_data;nlohmann_json::nlohmann_json" OUTPUT)
    add_trading_contract(momentum_replay_tests momentum_replay_contracts momentum_research)
    add_trading_contract(intraday_continuation_tests intraday_continuation_contracts momentum_research)
    add_trading_contract(intraday_reversion_tests intraday_reversion_contracts "reversion_reporting;nlohmann_json::nlohmann_json" OUTPUT)
    add_trading_contract(reversion_validation_tests reversion_validation_contracts "reversion_reporting;nlohmann_json::nlohmann_json" OUTPUT)
    target_sources(intraday_continuation_tests PRIVATE src/reporting/continuation_diagnostic_report.cpp)
    target_include_directories(intraday_continuation_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/reporting")
    add_trading_contract(continuation_study_tests continuation_study_contracts "continuation_reporting;nlohmann_json::nlohmann_json")
    add_trading_contract(continuation_diagnostics_tests continuation_diagnostics_contracts "continuation_reporting;nlohmann_json::nlohmann_json")
    add_trading_contract(batch_pipeline_tests batch_pipeline_contracts "momentum_research;nlohmann_json::nlohmann_json" OUTPUT)
    add_trading_contract(return_statistics_tests return_statistics_contracts "momentum_research;nlohmann_json::nlohmann_json" OUTPUT)
    add_trading_contract(development_dataset_tests development_dataset_contracts "momentum_research;nlohmann_json::nlohmann_json" OUTPUT)
    add_trading_contract(long_cash_account_tests long_cash_account_contracts "accounting;momentum_research")
    add_trading_contract(risk_preflight_tests risk_preflight_contracts "risk_preflight_reporting;nlohmann_json::nlohmann_json" OUTPUT)
    add_trading_contract(risk_input_tests risk_input_contracts "risk_preflight_reporting;nlohmann_json::nlohmann_json" OUTPUT)
    add_trading_contract(risk_measurement_tests risk_measurement_contracts daily_risk_research)
    add_trading_contract(daily_risk_study_tests daily_risk_study_contracts "daily_risk_reporting;nlohmann_json::nlohmann_json" OUTPUT)
    add_trading_contract(order_runtime_tests order_runtime_contracts "execution_reporting;artifact_integrity;nlohmann_json::nlohmann_json" OUTPUT)
    add_trading_contract(offline_pipeline_tests offline_pipeline_contracts offline_execution OUTPUT)
    add_executable(journaled_runtime_tests tests/journaled_runtime_tests.cpp)
    target_link_libraries(journaled_runtime_tests PRIVATE execution_persistence)
    configure_trading_target(journaled_runtime_tests)
    add_test(NAME journaled_runtime_contracts COMMAND "${CMAKE_COMMAND}"
        "-DPROGRAM=$<TARGET_FILE:journaled_runtime_tests>" "-DROOT=${CMAKE_CURRENT_BINARY_DIR}/journaled_runtime_tests-output"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/journal_recovery_contracts.cmake")
    add_test(NAME development_full_period_integration COMMAND development_dataset_tests
        "${CMAKE_CURRENT_BINARY_DIR}/development-full-test-output" --full)
    set_tests_properties(development_full_period_integration PROPERTIES LABELS integration)
endif()
