#include "parser.h"

#include "absl/status/status.h"
#include "absl/strings/substitute.h"
#include "nlohmann/json.hpp"
#include "status_macros/status_macros.h"

namespace {

using json = ::nlohmann::json;

std::string_view VersionString() {
  static const auto* const version = [] {
    return new std::string(absl::Substitute(
        "nlohmann json $0",
        json::meta()["version"]["string"].template get<std::string>()));
  }();
  return *version;
}

absl::StatusOr<json> GetField(const json& parent, std::string_view field) {
  const auto it = parent.find(field);
  if (it == parent.end()) {
    return absl::NotFoundError(absl::Substitute(
        "Missing JSON field \"$0\" in: $1", field, parent.dump()));
  }
  return *it;
}

absl::StatusOr<json> GetObjectField(const json& parent,
                                    std::string_view field) {
  ASSIGN_OR_RETURN(const auto value, GetField(parent, field));
  if (!value.is_object()) {
    return absl::InvalidArgumentError(absl::Substitute(
        "JSON field \"$0\" is not an object: $1", field, parent.dump()));
  }
  return value;
}

absl::StatusOr<double> GetDoubleField(const json& parent,
                                      std::string_view field) {
  ASSIGN_OR_RETURN(const auto value, GetField(parent, field));
  if (!value.is_number()) {
    return absl::InvalidArgumentError(absl::Substitute(
        "JSON field \"$0\" is not a number: $1", field, parent.dump()));
  }
  return value.template get<double>();
}

class ParserImpl final : public Parser {
 public:
  ParserImpl() = default;

  absl::StatusOr<::shelly::Metrics> Parse(const std::string& data) override {
    ::shelly::Metrics metrics;
    try {
      const json parsed = json::parse(data);

      ASSIGN_OR_RETURN(metrics.voltage, GetDoubleField(parsed, "voltage"));
      ASSIGN_OR_RETURN(metrics.apower, GetDoubleField(parsed, "apower"));
      ASSIGN_OR_RETURN(metrics.current, GetDoubleField(parsed, "current"));

      ASSIGN_OR_RETURN(const json temperature,
                       GetObjectField(parsed, "temperature"));
      ASSIGN_OR_RETURN(metrics.temp_c, GetDoubleField(temperature, "tC"));
      ASSIGN_OR_RETURN(metrics.temp_f, GetDoubleField(temperature, "tF"));
    } catch (const json::parse_error& e) {
      return absl::InvalidArgumentError(
          absl::Substitute("Failed to parse JSON: $0", e.what()));
    }
    return metrics;
  }

  std::string_view Version() const override { return VersionString(); }
};

}  // namespace

std::unique_ptr<Parser> CreateParser() {
  return std::make_unique<ParserImpl>();
}
