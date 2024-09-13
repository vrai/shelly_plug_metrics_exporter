#ifndef TARGET_H
#define TARGET_H

#include <string>

struct Target final {
  std::string name;
  std::string hostname;
};

#endif  // TARGET_H