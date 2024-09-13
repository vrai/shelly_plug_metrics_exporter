#ifndef CONFIG_H
#define CONFIG_H

#include <string_view>
#include <vector>

#include "absl/status/statusor.h"
#include "target.h"

absl::StatusOr<std::vector<Target>> LoadTargetsFromFile(std::string_view filename); 

#endif  // CONFIG_H