#include "parser.h"

#include <gtest/gtest.h>

#include "absl/log/check.h"

TEST(ParseJson, EmptyString) {
  auto result = CreateParser()->Parse("");
  ASSERT_FALSE(result.ok());
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
}

TEST(ParseJson, NotJson) {
  auto result = CreateParser()->Parse(R"(not json)");
  ASSERT_FALSE(result.ok());
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
}

TEST(ParseJson, MissingTopLevelField) {
  auto result = CreateParser()->Parse(R"(
  {
    "apower": 100.0,
    "current": 12.0,
    "temperature": {
      "tC": 28.0,
      "tF": 82.0
    }
  }
  )");
  ASSERT_FALSE(result.ok());
  EXPECT_EQ(result.status().code(), absl::StatusCode::kNotFound);
}

TEST(ParseJson, MissingContainerField) {
  auto result = CreateParser()->Parse(R"(
  {
    "voltage": 120.0,
    "apower": 100.0,
    "current": 12.0
  }
  )");
  ASSERT_FALSE(result.ok());
  EXPECT_EQ(result.status().code(), absl::StatusCode::kNotFound);
}

TEST(ParseJson, Success) {
  auto result = CreateParser()->Parse(R"(
  {
    "voltage": 120.0,
    "apower": 100.0,
    "current": 12.0,
    "temperature": {
      "tC": 28.0,
      "tF": 82.0
    }
  }
  )");
  ASSERT_TRUE(result.ok());
  EXPECT_DOUBLE_EQ(result->voltage, 120.0);
  EXPECT_DOUBLE_EQ(result->apower, 100.0);
  EXPECT_DOUBLE_EQ(result->current, 12.0);
  EXPECT_DOUBLE_EQ(result->temp_c, 28.0);
  EXPECT_DOUBLE_EQ(result->temp_f, 82.0);
}