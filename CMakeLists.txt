cmake_minimum_required(VERSION 3.19.0)

project(shelly_plug_metrics_exporter VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(ABSL_PROPAGATE_CXX_STD ON)

include(FetchContent)

FetchContent_Declare(
  prometheus-cpp
  GIT_REPOSITORY "https://github.com/jupp0r/prometheus-cpp.git"
  GIT_TAG "v1.2.4")
FetchContent_MakeAvailable(prometheus-cpp)

FetchContent_Declare(
  CURL
  GIT_REPOSITORY "https://github.com/curl/curl.git"
  GIT_TAG "curl-8_9_1"
)
FetchContent_MakeAvailable(CURL)

FetchContent_Declare(
  nlohmann_json
  GIT_REPOSITORY "https://github.com/nlohmann/json.git"
  GIT_TAG "v3.11.3"
)
FetchContent_MakeAvailable(nlohmann_json)

FetchContent_Declare(
  absl
  GIT_REPOSITORY "https://github.com/abseil/abseil-cpp.git"
  GIT_TAG "20230802.1")
FetchContent_MakeAvailable(absl)

add_subdirectory(status_macros)

add_library(config STATIC config.h config.cc)
target_link_libraries(
  config
  status_macros
  target
  absl::statusor
  absl::strings
  nlohmann_json::nlohmann_json
)

add_library(parser STATIC parser.h parser.cc)
target_link_libraries(
  parser
  shelly
  status_macros
  absl::statusor
  absl::strings
  nlohmann_json::nlohmann_json
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
  absl::time
)

add_library(registry STATIC registry.h registry.cc)
target_link_libraries(
  registry
  shelly
  absl::flat_hash_map
  absl::log
  absl::status
  absl::strings
  prometheus-cpp::core
)

add_library(scraper STATIC scraper.h scraper.cc)
target_link_libraries(
  scraper
  absl::cleanup
  absl::die_if_null
  absl::status
  absl::statusor
  absl::strings
  CURL::libcurl
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
  CURL::libcurl
)
