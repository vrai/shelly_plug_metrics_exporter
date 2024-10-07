#include "config.h"

#include <gtest/gtest.h>

#include <fstream>
#include <string>

#include "absl/log/check.h"

std::string CreateTempFile(const std::string& content) {
  std::string filename = ::testing::TempDir() + "config_test_XXXXXX";
  const int fd = mkstemp(filename.data());
  CHECK(fd != -1) << "Failed to create temporary file: " << filename;
  std::ofstream file(filename);
  file << content;
  file.close();
  return filename;
}

TEST(LoadTargetsFromFileTest, Success) {
  const std::string json_content = R"(
    {
        "One": "192.168.1.1",
        "Two": "192.168.1.2"
    }
  )";
  const auto filename = CreateTempFile(json_content);
  const auto result = LoadTargetsFromFile(filename);

  ASSERT_TRUE(result.ok());
  ASSERT_EQ(result.value().size(), 2);
  EXPECT_EQ(result.value()[0].name, "One");
  EXPECT_EQ(result.value()[0].hostname, "192.168.1.1");
  EXPECT_EQ(result.value()[1].name, "Two");
  EXPECT_EQ(result.value()[1].hostname, "192.168.1.2");

  std::remove(filename.c_str());
}

TEST(LoadTargetsFromFileTest, FileNotFound) {
  const auto result = LoadTargetsFromFile("nonexistent.json");
  ASSERT_FALSE(result.ok());
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
}

TEST(LoadTargetsFromFileTest, InvalidJson) {
  const auto filename = CreateTempFile("not json");
  const auto result = LoadTargetsFromFile(filename);
  ASSERT_FALSE(result.ok());
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);

  std::remove(filename.c_str());
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}