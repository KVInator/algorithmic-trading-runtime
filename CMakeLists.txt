cmake_minimum_required(VERSION 3.25)
project(algorithmic_trading_runtime LANGUAGES CXX)
set(CMAKE_CXX_EXTENSIONS OFF)

option(BUILD_TESTING "Build optional regression verification" OFF)
option(TRADING_WARNINGS_AS_ERRORS "Treat project compiler warnings as errors" ON)

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
    src/research/reversion_uncertainty.cpp
    src/research/reversion_validation.cpp src/research/reversion_strategy.cpp src/research/reversion_study.cpp)
target_include_directories(momentum_research PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src/research")
target_link_libraries(momentum_research PUBLIC session_data PRIVATE nlohmann_json::nlohmann_json)
target_compile_definitions(momentum_research PRIVATE TRADING_PROTOCOL_SHA256="${protocol_sha256}")
target_compile_definitions(momentum_research PRIVATE
    TRADING_CONTINUATION_PROTOCOL_SHA256="${continuation_protocol_sha256}")
configure_trading_target(momentum_research)

add_library(accounting src/accounting/long_cash_account.cpp)
target_include_directories(accounting PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src/accounting")
configure_trading_target(accounting)

add_library(order_execution src/execution/order_runtime.cpp src/execution/runtime_input.cpp)
target_include_directories(order_execution PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src/execution")
configure_trading_target(order_execution)

add_library(alpaca_paper_contract src/alpaca/broker_value.cpp src/alpaca/alpaca_paper_contract.cpp)
target_link_libraries(alpaca_paper_contract PUBLIC trading_core order_execution PRIVATE alpaca_decoder nlohmann_json::nlohmann_json)
configure_trading_target(alpaca_paper_contract)

add_library(execution_persistence src/execution/journaled_runtime.cpp src/storage/runtime_json.cpp src/storage/journal_frame.cpp)
target_link_libraries(execution_persistence PUBLIC order_execution artifact_integrity nlohmann_json::nlohmann_json
    PRIVATE alpaca_decoder)
if(WIN32)
    target_sources(execution_persistence PRIVATE src/platform/windows/journal_storage_windows.cpp)
    target_compile_definitions(execution_persistence PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00)
else()
    target_sources(execution_persistence PRIVATE src/platform/posix/journal_storage_posix.cpp)
endif()
configure_trading_target(execution_persistence)

add_library(paper_execution src/execution/paper_execution.cpp src/execution/journaled_paper_runtime.cpp)
target_link_libraries(paper_execution PUBLIC execution_persistence alpaca_paper_contract)
configure_trading_target(paper_execution)

add_library(paper_transport src/alpaca/paper_transport.cpp src/alpaca/paper_control.cpp)
target_link_libraries(paper_transport PUBLIC paper_execution)
configure_trading_target(paper_transport)
add_library(broker_simulation src/execution/simulated_broker.cpp)
target_link_libraries(broker_simulation PUBLIC order_execution)
configure_trading_target(broker_simulation)

add_library(offline_execution src/execution/offline_pipeline.cpp
    src/execution/momentum_order_adapter.cpp
    src/execution/strategy_order_adapters.cpp src/execution/strategy_execution_diagnostic.cpp)
target_link_libraries(offline_execution PUBLIC broker_simulation execution_persistence momentum_research accounting)
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

add_library(paper_transport_reporting src/reporting/paper_transport_report.cpp)
target_include_directories(paper_transport_reporting PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src/reporting" PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_link_libraries(paper_transport_reporting PUBLIC paper_transport)
target_compile_definitions(paper_transport_reporting PRIVATE TRADING_COMPILER_ID="${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}" TRADING_BUILD_CONFIGURATION="$<CONFIG>")
configure_trading_target(paper_transport_reporting)

add_library(simulated_paper_broker src/alpaca/simulated_paper_wire.cpp src/alpaca/paper_broker_diagnostic.cpp)
target_link_libraries(simulated_paper_broker PUBLIC paper_transport broker_simulation)
configure_trading_target(simulated_paper_broker)

add_library(execution_measurement_core src/execution/execution_measurement.cpp)
target_link_libraries(execution_measurement_core PUBLIC simulated_paper_broker)
configure_trading_target(execution_measurement_core)
add_library(execution_measurement_reporting src/reporting/execution_measurement_report.cpp)
target_include_directories(execution_measurement_reporting PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src/reporting" PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_link_libraries(execution_measurement_reporting PUBLIC execution_measurement_core)
target_compile_definitions(execution_measurement_reporting PRIVATE TRADING_COMPILER_ID="${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}" TRADING_BUILD_CONFIGURATION="$<CONFIG>")
configure_trading_target(execution_measurement_reporting)

add_library(runtime_benchmark_core src/execution/runtime_benchmark.cpp)
target_link_libraries(runtime_benchmark_core PUBLIC paper_execution)
if(WIN32)
    target_sources(runtime_benchmark_core PRIVATE src/platform/windows/runtime_resources_windows.cpp)
    target_link_libraries(runtime_benchmark_core PRIVATE psapi)
    target_compile_definitions(runtime_benchmark_core PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
else()
    target_sources(runtime_benchmark_core PRIVATE src/platform/posix/runtime_resources_posix.cpp)
endif()
configure_trading_target(runtime_benchmark_core)
add_library(runtime_benchmark_reporting src/reporting/runtime_benchmark_report.cpp)
target_include_directories(runtime_benchmark_reporting PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src/reporting" PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated/$<CONFIG>/runtime_benchmark_build.hpp"
    CONTENT "#pragma once\n#include <string_view>\nnamespace trading::build {\ninline constexpr std::string_view benchmark_common_flags{R\"flags(${CMAKE_CXX_FLAGS})flags\"};\ninline constexpr std::string_view benchmark_configuration_flags{R\"flags($<$<CONFIG:Debug>:${CMAKE_CXX_FLAGS_DEBUG}>$<$<CONFIG:Release>:${CMAKE_CXX_FLAGS_RELEASE}>$<$<CONFIG:RelWithDebInfo>:${CMAKE_CXX_FLAGS_RELWITHDEBINFO}>$<$<CONFIG:MinSizeRel>:${CMAKE_CXX_FLAGS_MINSIZEREL}>)flags\"};\ninline constexpr bool benchmark_warnings_as_errors{$<IF:$<BOOL:${TRADING_WARNINGS_AS_ERRORS}>,true,false>};\n}\n")
target_include_directories(runtime_benchmark_reporting PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated/$<CONFIG>")
target_link_libraries(runtime_benchmark_reporting PUBLIC runtime_benchmark_core)
target_compile_definitions(runtime_benchmark_reporting PRIVATE TRADING_COMPILER_ID="${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}" TRADING_BUILD_CONFIGURATION="$<CONFIG>")
configure_trading_target(runtime_benchmark_reporting)

add_library(strategy_execution_reporting src/reporting/strategy_execution_report.cpp)
target_include_directories(strategy_execution_reporting PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src/reporting"
    PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_link_libraries(strategy_execution_reporting PUBLIC offline_execution)
target_compile_definitions(strategy_execution_reporting PRIVATE
    TRADING_COMPILER_ID="${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}"
    TRADING_BUILD_CONFIGURATION="$<CONFIG>")
configure_trading_target(strategy_execution_reporting)
add_library(reversion_reporting src/reporting/reversion_validation_report.cpp src/reporting/reversion_study_report.cpp)
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

add_library(risk_input_reporting src/reporting/risk_input_report.cpp)
target_include_directories(risk_input_reporting PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src/reporting")
target_link_libraries(risk_input_reporting PUBLIC risk_preflight_core PRIVATE nlohmann_json::nlohmann_json)
target_compile_definitions(risk_input_reporting PRIVATE
    TRADING_SOURCE_SHA256="${source_sha256}"
    TRADING_COMPILER_ID="${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}"
    TRADING_BUILD_CONFIGURATION="$<CONFIG>")
configure_trading_target(risk_input_reporting)

add_library(daily_risk_reporting src/reporting/daily_risk_report.cpp)
target_include_directories(daily_risk_reporting PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src/reporting")
target_link_libraries(daily_risk_reporting PUBLIC daily_risk_research PRIVATE nlohmann_json::nlohmann_json)
target_compile_definitions(daily_risk_reporting PRIVATE
    TRADING_SOURCE_SHA256="${source_sha256}"
    TRADING_COMPILER_ID="${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}"
    TRADING_BUILD_CONFIGURATION="$<CONFIG>")
configure_trading_target(daily_risk_reporting)

add_library(momentum_reporting src/reporting/momentum_study_report.cpp)
target_include_directories(momentum_reporting PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src/reporting")
target_link_libraries(momentum_reporting PUBLIC momentum_research PRIVATE nlohmann_json::nlohmann_json)
target_compile_definitions(momentum_reporting PRIVATE TRADING_PROTOCOL_SHA256="${protocol_sha256}")
configure_trading_target(momentum_reporting)
add_library(project_demo_reporting src/reporting/project_demo.cpp)
target_include_directories(project_demo_reporting PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src/reporting")
target_link_libraries(project_demo_reporting PUBLIC strategy_execution_reporting simulated_paper_broker paper_transport_reporting)
configure_trading_target(project_demo_reporting)

function(add_trading_app target library)
    add_executable(${target} apps/${target}.cpp)
    target_link_libraries(${target} PRIVATE ${library})
    target_compile_definitions(${target} PRIVATE TRADING_PROJECT_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
    configure_trading_target(${target})
endfunction()
add_trading_app(project_demo project_demo_reporting)
add_trading_app(development_finalize session_data)
add_trading_app(development_replay momentum_reporting)
add_trading_app(continuation_development continuation_reporting)
add_trading_app(risk_acquire risk_input_reporting)
add_trading_app(risk_development daily_risk_reporting)
add_trading_app(runtime_benchmark runtime_benchmark_reporting)
target_compile_definitions(runtime_benchmark PRIVATE TRADING_BUILD_CONFIGURATION="$<CONFIG>")
add_trading_app(execution_measurement execution_measurement_reporting)
add_trading_app(strategy_reference_replay strategy_execution_reporting)
add_trading_app(reversion_validation reversion_reporting)
add_trading_app(reversion_development reversion_reporting)

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
    target_link_libraries(risk_acquire PRIVATE alpaca_market_data alpaca_console)

endif()

if(BUILD_TESTING)
    include(CTest)
    add_subdirectory(tests)
endif()
