cmake_minimum_required(VERSION 3.19.0)

project(
  shelly_plug_metrics_exporter
  VERSION 0.1.0
  LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(ABSL_PROPAGATE_CXX_STD ON)

include(cmake/CPM.cmake)
include(CTest)
include(FetchContent)

CPMAddPackage(
  NAME prometheus-cpp
  GITHUB_REPOSITORY jupp0r/prometheus-cpp
  VERSION 1.2.4
  OPTIONS "ENABLE_PUSH OFF" "ENABLE_TESTING OFF")

CPMAddPackage(
  NAME CURL
  GITHUB_REPOSITORY curl/curl
  GIT_TAG "curl-8_9_1")

CPMAddPackage(
  NAME nlohmann_json
  GITHUB_REPOSITORY nlohmann/json
  VERSION 3.11.3)

CPMAddPackage(
  NAME absl
  GITHUB_REPOSITORY abseil/abseil-cpp
  GIT_TAG "20230802.1")

CPMAddPackage(
  NAME googletest
  GITHUB_REPOSITORY google/googletest
  VERSION 1.15.2)

CPMAddPackage(
  NAME civetweb
  GITHUB_REPOSITORY civetweb/civetweb
  VERSION 1.16
  OPTIONS "CIVETWEB_ENABLE_CXX OFF"
          "CIVETWEB_BUILD_TESTING OFF"
          "CIVETWEB_ENABLE_SERVER_EXECUTABLE OFF"
          "CIVETWEB_ENABLE_ASAN OFF")

add_subdirectory(status_macros)

add_library(config STATIC config.h config.cc)
target_link_libraries(
  config
  status_macros
  target
  absl::statusor
  absl::strings
  nlohmann_json::nlohmann_json)

add_executable(config_test config_test.cc)
target_link_libraries(
  config_test
  absl::log
  config
  gtest_main
  gtest
  gmock
)

add_library(parser STATIC parser.h parser.cc)
target_link_libraries(
  parser
  shelly
  status_macros
  absl::statusor
  absl::strings
  nlohmann_json::nlohmann_json)

add_executable(parser_test parser_test.cc)
target_link_libraries(
  parser_test
  absl::log
  parser
  gtest_main
  gtest
  gmock
)

add_library(poller STATIC poller.h poller.cc)
target_link_libraries(
  poller
  parser
  scraper
  shelly
  status_macros
  absl::die_if_null
  absl::log
  absl::status
  absl::statusor
  absl::time)

add_executable(poller_test poller_test.cc)
target_link_libraries(
  poller_test
  absl::log
  absl::status
  absl::statusor
  poller
  gtest_main
  gtest
  gmock
)

add_library(registry STATIC registry.h registry.cc)
target_link_libraries(
  registry
  shelly
  absl::flat_hash_map
  absl::log
  absl::status
  absl::strings
  prometheus-cpp::core)

add_executable(registery_test registry_test.cc)
target_link_libraries(
  registery_test
  absl::flat_hash_map
  absl::log
  registry
  gtest_main
  gtest
  gmock
)

add_library(scraper STATIC scraper.h scraper.cc)
target_link_libraries(
  scraper
  absl::cleanup
  absl::die_if_null
  absl::status
  absl::statusor
  absl::strings
  CURL::libcurl)

add_executable(scraper_test scraper_test.cc)
target_link_libraries(
  scraper_test
  absl::log
  absl::strings
  civetweb-c-library
  scraper
  gtest_main
  gtest
  gmock
)

add_library(shelly STATIC shelly.h shelly.cc)
target_link_libraries(shelly absl::strings)

add_library(target INTERFACE target.h)

add_executable(shelly_plug_metrics_exporter main.cc)
target_link_libraries(
  shelly_plug_metrics_exporter
  config
  parser
  poller
  registry
  scraper
  shelly
  target
  absl::flags
  absl::flags_parse
  absl::log
  absl::log_initialize
  absl::log_severity
  absl::status
  absl::time
  prometheus-cpp::core
  prometheus-cpp::pull
  nlohmann_json::nlohmann_json
  CURL::libcurl)

  enable_testing()

  add_test(NAME ConfigTest COMMAND config_test)
  add_test(NAME ParserTest COMMAND parser_test)
  add_test(NAME PollerTest COMMAND poller_test)
  add_test(NAME ScraperTest COMMAND scraper_test)
  add_test(NAME RegistryTest COMMAND registery_test)