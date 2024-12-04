#include "poller.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <barrier>
#include <thread>

#include "absl/strings/substitute.h"

MATCHER_P(MetricsEq, cmp,
          absl::Substitute("Equals Metrics: $0", cmp.DebugString())) {
  return arg.apower == cmp.apower && arg.voltage == cmp.voltage &&
         arg.current == cmp.current && arg.temp_c == cmp.temp_c &&
         arg.temp_f == cmp.temp_f;
}

class MockParser : public Parser {
 public:
  MOCK_METHOD(absl::StatusOr<::shelly::Metrics>, Parse, (const std::string&),
              (override));
  MOCK_METHOD(std::string_view, Version, (), (const, override));
};

class MockScraper : public Scraper {
 public:
  MOCK_METHOD(absl::StatusOr<ScraperResult>, Scrape, (const std::string&),
              (override));
  MOCK_METHOD(std::string_view, Version, (), (const, override));
};

class FakeClock final {
 public:
  FakeClock() = delete;
  FakeClock(const absl::Time& time) : time_(time) {}

  absl::Time Now() const { return time_; }

 private:
  absl::Time time_;
};

void WaitUntilPollerIsAlive(Poller& poller) {
  while (!poller.Alive()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
}

class Fixture final {
 public:
  Fixture() = delete;

  Fixture(std::function<void(absl::string_view, const absl::Status&)>
              error_callback,
          std::function<void(absl::string_view, const ::shelly::Metrics&)>
              success_callback)
      : clock_(absl::FromUnixSeconds(0)) {
    auto parser = std::make_unique<MockParser>();
    parser_ptr_ = parser.get();

    auto scraper = std::make_unique<MockScraper>();
    scraper_ptr_ = scraper.get();

    poller_ = std::make_unique<Poller>(
        std::move(parser), std::move(scraper),
        Poller::Options{
            .poll_period = absl::Milliseconds(100),
            .time_func = [this] { return clock_.Now(); },
            .error_callback = error_callback,
            .success_callback = success_callback,
        });
  }

  ~Fixture() { Stop(); }

  void Run() {
    run_thread_ = std::thread([this] { poller_->Run(); });
    WaitUntilPollerIsAlive(*poller_);
  }

  void Stop() {
    if (poller_->Alive()) {
      poller_->Kill();
      run_thread_.join();
    }
  }

  Poller& poller() { return *poller_; }
  MockParser& parser() { return *parser_ptr_; }
  MockScraper& scraper() { return *scraper_ptr_; }

 private:
  FakeClock clock_;
  MockParser* parser_ptr_;
  MockScraper* scraper_ptr_;
  std::unique_ptr<Poller> poller_;
  std::thread run_thread_;
};

TEST(Run, NoTargets) {
  Fixture fixture(/*error_callback=*/nullptr, /*success_callback=*/nullptr);
  EXPECT_FALSE(fixture.poller().Alive());
  fixture.Run();
  EXPECT_TRUE(fixture.poller().Alive());
  fixture.Stop();
  EXPECT_FALSE(fixture.poller().Alive());
}

template <typename TestTraits>
class BarrierTest : public ::testing::Test {
 public:
  BarrierTest()
      : fixture_(
            [this](absl::string_view name, const absl::Status& error) {
              error_ = error;
              barrier_.arrive_and_wait();
            },
            [this](absl::string_view name, const ::shelly::Metrics& metrics) {
              success_name_ = std::string(name);
              success_metrics_ = metrics;
              barrier_.arrive_and_wait();
            }),
        barrier_(2, [] {}) {}

 protected:
  using BarrierCompleteFn = std::function<void()>;

  Fixture fixture_;
  std::barrier<BarrierCompleteFn> barrier_;
  absl::Status error_;
  std::string success_name_;
  ::shelly::Metrics success_metrics_;

  void Run() {
    fixture_.Run();
    barrier_.arrive_and_wait();
    fixture_.Stop();
  }
};
TYPED_TEST_SUITE_P(BarrierTest);

TYPED_TEST_P(BarrierTest, Test) {
  TypeParam::SetExpectations(this->fixture_);
  TypeParam::AddTargets(this->fixture_);
  this->Run();
  if (this->error_.ok()) {
    EXPECT_THAT(this->success_name_, TypeParam::kExpectedName);
    EXPECT_THAT(this->success_metrics_, MetricsEq(TypeParam::kExpectedMetrics));
  } else {
    EXPECT_EQ(this->error_.code(), TypeParam::kExpectedStatusCode);
    EXPECT_THAT(this->error_.message(),
                testing::HasSubstr(TypeParam::kExpectedMessage));
  }
}

REGISTER_TYPED_TEST_SUITE_P(BarrierTest, Test);

struct ScraperErrorTest final {
  static constexpr absl::StatusCode kExpectedStatusCode =
      absl::StatusCode::kPermissionDenied;
  static constexpr absl::string_view kExpectedMessage = "expected error";
  static constexpr absl::string_view kExpectedName = "";
  static constexpr shelly::Metrics kExpectedMetrics = {};

  static void SetExpectations(Fixture& fixture) {
    EXPECT_CALL(fixture.scraper(), Scrape(testing::_))
        .WillOnce(
            testing::Return(absl::PermissionDeniedError("expected error")));
  }

  static void AddTargets(Fixture& fixture) {
    fixture.poller().AddTarget("test_target", "localhost:80");
  }
};
INSTANTIATE_TYPED_TEST_SUITE_P(ScraperError, BarrierTest, ScraperErrorTest);

struct ScraperReturnsHttpError final {
  static constexpr absl::StatusCode kExpectedStatusCode =
      absl::StatusCode::kInvalidArgument;
  static constexpr absl::string_view kExpectedMessage = "404";
  static constexpr absl::string_view kExpectedName = "";
  static constexpr shelly::Metrics kExpectedMetrics = {};

  static void SetExpectations(Fixture& fixture) {
    EXPECT_CALL(fixture.scraper(), Scrape(testing::_))
        .WillOnce(testing::Return(
            ScraperResult{.code = 404, .content = "Not Found"}));
  }

  static void AddTargets(Fixture& fixture) {
    fixture.poller().AddTarget("test_target", "localhost:80");
  }
};
INSTANTIATE_TYPED_TEST_SUITE_P(ScraperReturnsHttp, BarrierTest,
                               ScraperReturnsHttpError);

struct ScraperReturnsNonJsonTest final {
  static constexpr absl::StatusCode kExpectedStatusCode =
      absl::StatusCode::kInvalidArgument;
  static constexpr absl::string_view kExpectedMessage = "text/plain";
  static constexpr absl::string_view kExpectedName = "";
  static constexpr shelly::Metrics kExpectedMetrics = {};

  static void SetExpectations(Fixture& fixture) {
    EXPECT_CALL(fixture.scraper(), Scrape(testing::_))
        .WillOnce(testing::Return(ScraperResult{
            .code = 200, .content_type = "text/plain", .content = "Not JSON"}));
  }

  static void AddTargets(Fixture& fixture) {
    fixture.poller().AddTarget("test_target", "localhost:80");
  }
};
INSTANTIATE_TYPED_TEST_SUITE_P(ScraperReturnsNonJson, BarrierTest,
                               ScraperReturnsNonJsonTest);

struct ParserReturnsErrorTest final {
  static constexpr absl::StatusCode kExpectedStatusCode =
      absl::StatusCode::kInternal;
  static constexpr absl::string_view kExpectedMessage = "expected error";
  static constexpr absl::string_view kExpectedName = "";
  static constexpr shelly::Metrics kExpectedMetrics = {};

  static void SetExpectations(Fixture& fixture) {
    EXPECT_CALL(fixture.scraper(), Scrape(testing::_))
        .WillOnce(testing::Return(ScraperResult{
            .code = 200, .content_type = "application/json", .content = "{}"}));
    EXPECT_CALL(fixture.parser(), Parse(testing::_))
        .WillOnce(testing::Return(absl::InternalError("expected error")));
  }

  static void AddTargets(Fixture& fixture) {
    fixture.poller().AddTarget("test_target", "localhost:80");
  }
};
INSTANTIATE_TYPED_TEST_SUITE_P(ParserReturnsError, BarrierTest,
                               ParserReturnsErrorTest);

struct ParserReturnsMetricsTest final {
  static constexpr absl::StatusCode kExpectedStatusCode = absl::StatusCode::kOk;
  static constexpr absl::string_view kExpectedMessage = "";
  static constexpr absl::string_view kExpectedName = "test_target";
  static constexpr shelly::Metrics kExpectedMetrics = {
      .apower = 115.0,
      .voltage = 230.0,
      .current = 0.5,
      .temp_c = 28.0,
      .temp_f = 82.0,
  };

  static void SetExpectations(Fixture& fixture) {
    EXPECT_CALL(fixture.scraper(), Scrape(testing::_))
        .WillOnce(testing::Return(ScraperResult{
            .code = 200, .content_type = "application/json", .content = "{}"}));
    EXPECT_CALL(fixture.parser(), Parse(testing::_))
        .WillOnce(testing::Return(kExpectedMetrics));
  }

  static void AddTargets(Fixture& fixture) {
    fixture.poller().AddTarget("test_target", "localhost:80");
  }
};
INSTANTIATE_TYPED_TEST_SUITE_P(ParserReturnsMetrics, BarrierTest,
                               ParserReturnsMetricsTest);