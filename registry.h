#ifndef REGISTRY_H
#define REGISTRY_H

#include <memory>
#include <string_view>

#include "absl/status/status.h"
#include "prometheus/registry.h"
#include "shelly.h"

class Registry {
 public:
  virtual ~Registry() = default;

  virtual std::shared_ptr<::prometheus::Registry> GetRegistry() = 0;

  virtual void ErrorCallback(absl::string_view name,
                             const absl::Status& status) = 0;
  virtual void SuccessCallback(absl::string_view name,
                               const ::shelly::Metrics& metrics) = 0;

  virtual absl::Status AddTarget(absl::string_view name) = 0;

 protected:
  Registry() = default;
};

std::unique_ptr<Registry> CreateRegistry();

#endif  // REGISTRY_H