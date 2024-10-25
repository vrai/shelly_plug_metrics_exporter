#include "registry.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <optional>
#include <string_view>

#include "absl/container/flat_hash_map.h"
#include "absl/log/check.h"
#include "absl/types/span.h"
#include "prometheus/client_metric.h"
#include "prometheus/metric_family.h"

namespace {

using ::testing::AllOf;
using ::testing::Contains;
using ::testing::DoubleEq;
using ::testing::Pair;
using ::testing::UnorderedElementsAre;

absl::flat_hash_map<std::string, absl::flat_hash_map<std::string, double>>
GetMetricsAsDoubles(absl::Span<const ::prometheus::MetricFamily> families) {
  absl::flat_hash_map<std::string, absl::flat_hash_map<std::string, double>>
      results;

  for (const auto& family : families) {
    for (const auto& metric : family.metric) {
      CHECK(metric.label.size() == 1)
          << "Expected metric \"" << family.name << "\" to have exactly one "
          << "label";
      results[metric.label.at(0).value][family.name] =
          metric.counter.value != 0 ? metric.counter.value : metric.gauge.value;
    }
  }

  return results;
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
  EXPECT_THAT(
      GetMetricsAsDoubles(registry->GetRegistry()->Collect()),
      UnorderedElementsAre(Pair(
          "target", Contains(Pair("shelly_error_counter", DoubleEq(0.0))))));
}

TEST(ErrorCallback, UpdatesMetric) {
  auto registry = CreateRegistry();

  // Create two targets and confirm that their error count is zero.
  ASSERT_TRUE(registry->AddTarget("target_one").ok());
  ASSERT_TRUE(registry->AddTarget("target_two").ok());
  EXPECT_THAT(GetMetricsAsDoubles(registry->GetRegistry()->Collect()),
              UnorderedElementsAre(
                  Pair("target_one",
                       Contains(Pair("shelly_error_counter", DoubleEq(0.0)))),
                  Pair("target_two",
                       Contains(Pair("shelly_error_counter", DoubleEq(0.0))))));

  // Call error callback for the first target and ensure that its error count
  // increments, but the second target's error count remains unchanged.
  registry->ErrorCallback("target_one", absl::InternalError("expected error"));
  EXPECT_THAT(GetMetricsAsDoubles(registry->GetRegistry()->Collect()),
              UnorderedElementsAre(
                  Pair("target_one",
                       Contains(Pair("shelly_error_counter", DoubleEq(1.0)))),
                  Pair("target_two",
                       Contains(Pair("shelly_error_counter", DoubleEq(0.0))))));
}

TEST(SuccessCallback, NoTargets) {
  auto registry = CreateRegistry();
  registry->SuccessCallback("missing_target", {.voltage = 120.0});
}

TEST(SuccessCallback, UnknownTarget) {
  auto registry = CreateRegistry();
  ASSERT_TRUE(registry->AddTarget("target").ok());

  registry->SuccessCallback("missing_target", {.voltage = 120.0});
  EXPECT_THAT(GetMetricsAsDoubles(registry->GetRegistry()->Collect()),
              UnorderedElementsAre(Pair(
                  "target",
                  AllOf(Contains(Pair("shelly_success_counter", DoubleEq(0.0))),
                        Contains(Pair("shelly_voltage", DoubleEq(0.0)))))));
}

TEST(SuccessCallback, UpdateMetrics) {
  auto registry = CreateRegistry();
  ASSERT_TRUE(registry->AddTarget("target_one").ok());
  ASSERT_TRUE(registry->AddTarget("target_two").ok());

  // Update the voltage for the first target and confirm it's applied without
  // affecting the second target.
  registry->SuccessCallback("target_one", {.voltage = 120.0});

  EXPECT_THAT(
      GetMetricsAsDoubles(registry->GetRegistry()->Collect()),
      UnorderedElementsAre(
          Pair("target_one",
               AllOf(Contains(Pair("shelly_success_counter", DoubleEq(1.0))),
                     Contains(Pair("shelly_voltage", DoubleEq(120.0))))),
          Pair("target_two",
               AllOf(Contains(Pair("shelly_success_counter", DoubleEq(0.0))),
                     Contains(Pair("shelly_voltage", DoubleEq(0.0)))))));
}