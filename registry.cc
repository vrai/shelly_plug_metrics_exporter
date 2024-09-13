#include "registry.h"

#include <string>

#include "absl/container/flat_hash_map.h"
#include "absl/log/log.h"
#include "absl/strings/substitute.h"
#include "prometheus/counter.h"
#include "prometheus/gauge.h"

namespace {

inline constexpr auto kTargetLabel = "target";

struct TargetMetrics final {
  ::prometheus::Gauge* const voltage;
  ::prometheus::Gauge* const apower;
  ::prometheus::Gauge* const current;
  ::prometheus::Gauge* const temp_c;
  ::prometheus::Gauge* const temp_f;
  ::prometheus::Counter* const success_queries;
  ::prometheus::Counter* const error_queries;
  ::prometheus::Gauge* const last_updated;
};

template <class T>
inline void IncrementIfNotNull(T* metric) {
  if (metric != nullptr) {
    metric->Increment();
  }
}

template <class T, typename V>
inline void SetIfNotNull(T* metric, V value) {
  if (metric != nullptr) {
    metric->Set(value);
  }
}

class RegistryImpl final : public Registry {
 public:
  RegistryImpl()
      : registry_(std::make_shared<::prometheus::Registry>()),
        voltage_(::prometheus::BuildGauge()
                     .Name("shelly_voltage")
                     .Help("Last observed voltage of the target")
                     .Register(*registry_)),
        apower_(::prometheus::BuildGauge()
                    .Name("shelly_apower")
                    .Help("Last observed power of the target")
                    .Register(*registry_)),
        current_(::prometheus::BuildGauge()
                     .Name("shelly_current")
                     .Help("Last observed current of the target")
                     .Register(*registry_)),
        temp_c_(::prometheus::BuildGauge()
                    .Name("shelly_temp_c")
                    .Help("Last observed temperature of the target")
                    .Register(*registry_)),
        temp_f_(::prometheus::BuildGauge()
                    .Name("shelly_temp_f")
                    .Help("Last observed temperature of the target")
                    .Register(*registry_)),
        success_queries_(
            ::prometheus::BuildCounter()
                .Name("shelly_success_counter")
                .Help("Number of successful metrics queries for the target")
                .Register(*registry_)),
        error_queries_(
            ::prometheus::BuildCounter()
                .Name("shelly_error_counter")
                .Help("Number of failed metrics queries for the target")
                .Register(*registry_)),
        last_updated_(
            ::prometheus::BuildGauge()
                .Name("shelly_last_updated")
                .Help("Timestamp for the most recent update for this target")
                .Register(*registry_)) {}

  std::shared_ptr<::prometheus::Registry> GetRegistry() override {
    return registry_;
  }

  absl::Status AddTarget(absl::string_view name) override {
    if (target_metrics_.contains(name)) {
      return absl::InvalidArgumentError(
          absl::Substitute("Duplicate target name \"$0\"", name));
    }

    const std::string name_str(name);
    TargetMetrics target_metrics = {
        .voltage = &(voltage_.Add({{kTargetLabel, name_str}})),
        .apower = &(apower_.Add({{kTargetLabel, name_str}})),
        .current = &(current_.Add({{kTargetLabel, name_str}})),
        .temp_c = &(temp_c_.Add({{kTargetLabel, name_str}})),
        .temp_f = &(temp_f_.Add({{kTargetLabel, name_str}})),
        .success_queries = &(success_queries_.Add({{kTargetLabel, name_str}})),
        .error_queries = &(error_queries_.Add({{kTargetLabel, name_str}})),
        .last_updated = &(last_updated_.Add({{kTargetLabel, name_str}})),
    };
    if (!target_metrics_
             .insert(std::make_pair(name_str, std::move(target_metrics)))
             .second) {
      return absl::InternalError(
          absl::Substitute("Failed to add target metrics for \"$0\"", name));
    }
    return absl::OkStatus();
  }

  void ErrorCallback(absl::string_view name,
                     const absl::Status& status) override {
    auto* const target_metrics = FindTargetMetricsOrNull(name);
    if (target_metrics == nullptr) {
      LOG(ERROR) << "Unknown target \"" << name << "\"";
      return;
    }

    IncrementIfNotNull(target_metrics->error_queries);
  }

  void SuccessCallback(absl::string_view name,
                       const ::shelly::Metrics& metrics) override {
    auto* const target_metrics = FindTargetMetricsOrNull(name);
    if (target_metrics == nullptr) {
      LOG(ERROR) << "Unknown target \"" << name << "\"";
      return;
    }

    SetIfNotNull(target_metrics->voltage, metrics.voltage);
    SetIfNotNull(target_metrics->current, metrics.current);
    SetIfNotNull(target_metrics->apower, metrics.apower);
    SetIfNotNull(target_metrics->temp_c, metrics.temp_c);
    SetIfNotNull(target_metrics->temp_f, metrics.temp_f);
    IncrementIfNotNull(target_metrics->success_queries);
    if (target_metrics->last_updated != nullptr) {
      target_metrics->last_updated->SetToCurrentTime();
    }
  }

 private:
  std::shared_ptr<::prometheus::Registry> registry_;

  ::prometheus::Family<::prometheus::Gauge>& voltage_;
  ::prometheus::Family<::prometheus::Gauge>& apower_;
  ::prometheus::Family<::prometheus::Gauge>& current_;
  ::prometheus::Family<::prometheus::Gauge>& temp_c_;
  ::prometheus::Family<::prometheus::Gauge>& temp_f_;
  ::prometheus::Family<::prometheus::Counter>& success_queries_;
  ::prometheus::Family<::prometheus::Counter>& error_queries_;
  ::prometheus::Family<::prometheus::Gauge>& last_updated_;

  absl::flat_hash_map<std::string, TargetMetrics> target_metrics_;

  TargetMetrics* FindTargetMetricsOrNull(absl::string_view name) {
    auto it = target_metrics_.find(name);
    if (it == target_metrics_.end()) {
      return nullptr;
    }
    return &(it->second);
  }
};

}  // namespace

std::unique_ptr<Registry> CreateRegistry() {
  return std::make_unique<RegistryImpl>();
}
