#ifndef PARSER_H
#define PARSER_H

#include <memory>
#include <string>

#include "absl/status/statusor.h"
#include "shelly.h"

class Parser {
 public:
  virtual ~Parser() = default;

  virtual absl::StatusOr<::shelly::Metrics> Parse(const std::string& data) = 0;

  virtual std::string_view Version() const = 0;

 protected:
  Parser() = default;
};

std::unique_ptr<Parser> CreateParser();

#endif  // PARSER_H
