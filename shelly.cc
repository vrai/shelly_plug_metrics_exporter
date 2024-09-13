#include "shelly.h"

#include "absl/strings/substitute.h"

namespace shelly {

std::string Metrics::DebugString() const {
  return absl::Substitute(
      "Metrics{apower=$0, voltage=$1, current=$2, temp_c=$3, temp_f=$4}",
      apower, voltage, current, temp_c, temp_f);
}

}  // namespace shelly
