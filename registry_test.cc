#include "registry.h"

#include <gtest/gtest.h>

#include <optional>
#include <string_view>

#include "absl/log/check.h"
#include "absl/types/span.h"
#include "prometheus/client_metric.h"
#include "prometheus/metric_family.h"

namespace {

::prometheus::MetricFamily FindMetricFamilyOrDie(
    absl::Span<const ::prometheus::MetricFamily> families,
    std::string_view name) {
  std::optional<::prometheus::MetricFamily> result;
  for (const auto& family : families) {
    if (family.name == name) {
      result = family;
      break;
    }
  }
  CHECK(result.has_value())
      << "Could not find metric family \"" << name << "\"";
  return std::move(result).value();
}

::prometheus::ClientMetric FindClientMetricOrDie(
    absl::Span<const ::prometheus::MetricFamily> families,
    std::string_view name, std::string_view target) {
  const auto family = FindMetricFamilyOrDie(families, name);

  std::optional<::prometheus::ClientMetric> result;
  for (const auto& metric : family.metric) {
    CHECK(metric.label.size() == 1)
        << "Expected metric \"" << name << "\" to have exactly one label";
    if (metric.label.at(0).value == target) {
      result = metric;
      break;
    }
  }
  CHECK(result.has_value()) << "Could not find metric \"" << name
                            << "\" for target \"" << target << "\"";
  return std::move(result).value();
}

}  // namespace

TEST(AddTargets, CreatesMetrics) {
  auto registry = CreateRegistry();
  EXPECT_TRUE(registry->GetRegistry()->Collect().empty());

  EXPECT_TRUE(registry->AddTarget("target_one").ok());
  ASSERT_FALSE(registry->GetRegistry()->Collect().empty());
  const size_t families_size = registry->GetRegistry()->Collect().size();
  EXPECT_EQ(registry->GetRegistry()->Collect().front().metric.size(), 1);

  // Adding a second target should not increase the number of families, as all
  // targets should have the same metrics. However it should increase the
  // metrics per family by one.
  EXPECT_TRUE(registry->AddTarget("target_two").ok());
  EXPECT_EQ(registry->GetRegistry()->Collect().size(), families_size);
  EXPECT_EQ(registry->GetRegistry()->Collect().front().metric.size(), 2);
}

TEST(AddTargets, Duplicate) {
  auto registry = CreateRegistry();

  EXPECT_TRUE(registry->AddTarget("target").ok());
  const auto result = registry->AddTarget("target");
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.code(), absl::StatusCode::kInvalidArgument);
}

TEST(ErrorCallback, NoTargets) {
  auto registry = CreateRegistry();
  registry->ErrorCallback("missing_target",
                          absl::InternalError("expected error"));
}

TEST(ErrorCallback, UnknownTarget) {
  auto registry = CreateRegistry();
  ASSERT_TRUE(registry->AddTarget("target").ok());

  registry->ErrorCallback("missing_target",
                          absl::InternalError("expected error"));
  EXPECT_EQ(FindClientMetricOrDie(registry->GetRegistry()->Collect(),
                                  "shelly_error_counter", "target")
                .counter.value,
            0);
}

TEST(ErrorCallback, UpdatesMetric) {
  auto registry = CreateRegistry();

  // Create two targets and confirm that their error count is zero.
  ASSERT_TRUE(registry->AddTarget("target_one").ok());
  EXPECT_EQ(FindClientMetricOrDie(registry->GetRegistry()->Collect(),
                                  "shelly_error_counter", "target_one")
                .counter.value,
            0);
  ASSERT_TRUE(registry->AddTarget("target_two").ok());
  EXPECT_EQ(FindClientMetricOrDie(registry->GetRegistry()->Collect(),
                                  "shelly_error_counter", "target_two")
                .counter.value,
            0);

  // Call error callback for the first target and ensure that its error count
  // increments, but the second target's error count remains unchanged.
  registry->ErrorCallback("target_one", absl::InternalError("expected error"));
  EXPECT_EQ(FindClientMetricOrDie(registry->GetRegistry()->Collect(),
                                  "shelly_error_counter", "target_one")
                .counter.value,
            1);
  EXPECT_EQ(FindClientMetricOrDie(registry->GetRegistry()->Collect(),
                                  "shelly_error_counter", "target_two")
                .counter.value,
            0);
}

TEST(SuccessCallback, NoTargets) {
  auto registry = CreateRegistry();
  registry->SuccessCallback("missing_target", {.voltage = 120.0});
}

TEST(SuccessCallback, UnknownTarget) {
  auto registry = CreateRegistry();
  ASSERT_TRUE(registry->AddTarget("target").ok());

  registry->SuccessCallback("missing_target", {.voltage = 120.0});
  EXPECT_DOUBLE_EQ(FindClientMetricOrDie(registry->GetRegistry()->Collect(),
                                         "shelly_voltage", "target")
                       .gauge.value,
                   0.0);
  EXPECT_EQ(FindClientMetricOrDie(registry->GetRegistry()->Collect(),
                                  "shelly_success_counter", "target")
                .counter.value,
            0);
}

TEST(SuccessCallback, UpdateMetrics) {
  auto registry = CreateRegistry();
  ASSERT_TRUE(registry->AddTarget("target_one").ok());
  ASSERT_TRUE(registry->AddTarget("target_two").ok());

  // Update the voltage for the first target and confirm it's applied.
  registry->SuccessCallback("target_one", {.voltage = 120.0});
  EXPECT_DOUBLE_EQ(FindClientMetricOrDie(registry->GetRegistry()->Collect(),
                                         "shelly_voltage", "target_one")
                       .gauge.value,
                   120.0);
  EXPECT_EQ(FindClientMetricOrDie(registry->GetRegistry()->Collect(),
                                  "shelly_success_counter", "target_one")
                .counter.value,
            1);

  // Confirm that the second target remains unchanged.
  EXPECT_DOUBLE_EQ(FindClientMetricOrDie(registry->GetRegistry()->Collect(),
                                         "shelly_voltage", "target_two")
                       .gauge.value,
                   0.0);
  EXPECT_EQ(FindClientMetricOrDie(registry->GetRegistry()->Collect(),
                                  "shelly_success_counter", "target_two")
                .counter.value,
            0);
}