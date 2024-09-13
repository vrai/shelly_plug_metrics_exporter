#ifndef SHELLY_H
#define SHELLY_H

#include <string>

namespace shelly {

struct Metrics final {
  double apower;
  double voltage;
  double current;
  double temp_c;
  double temp_f;

  std::string DebugString() const;
};

}  // namespace shelly

#endif  // SHELLY_H