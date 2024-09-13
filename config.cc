#include "config.h"

#include <fstream>
#include <string>

#include "absl/status/status.h"
#include "absl/strings/substitute.h"
#include "nlohmann/json.hpp"
#include "status_macros/status_macros.h"

namespace {

using ::nlohmann::json;

absl::StatusOr<std::vector<Target>> ParseTargetsConfig(json& config) {
  if (!config.is_object()) {
    return absl::InvalidArgumentError(
        "Top-level configuration is not an object");
  }

  std::vector<Target> targets;
  targets.reserve(config.size());
  for (const auto& [key, value] : config.items()) {
    if (!value.is_string()) {
      return absl::InvalidArgumentError(
          absl::Substitute("Value for \"$0\" is not a string", key));
    }
    targets.push_back({
        .name = key,
        .hostname = value,
    });
  }
  return targets;
}

}  // namespace

absl::StatusOr<std::vector<Target>> LoadTargetsFromFile(
    std::string_view filename) {
  std::ifstream stream(std::string(filename).c_str());
  if (!stream.is_open()) {
    return absl::InvalidArgumentError(
        absl::Substitute("Failed to open file: $0", strerror(errno)));
  }
  json config;
  try {
    config = json::parse(stream);
  } catch (std::exception& e) {
    return absl::InvalidArgumentError(
        absl::Substitute("Failed to parse file as JSON: $0", e.what()));
  }
  ASSIGN_OR_RETURN(auto targets, ParseTargetsConfig(config),
                   _ << "Failed to parse file contents");
  return targets;
}
