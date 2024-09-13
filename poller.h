#ifndef POLLER_H
#define POLLER_H

#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <string_view>

#include "absl/status/statusor.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "parser.h"
#include "scraper.h"
#include "shelly.h"

class Poller final {
 public:
  struct Options final {
    absl::Duration poll_period = absl::Seconds(15);
    std::function<absl::Time()> time_func = [] { return absl::Now(); };

    bool verbose_logging = false;

    std::function<void(absl::string_view name, const absl::Status& error)>
        error_callback;
    std::function<void(absl::string_view name,
                       const ::shelly::Metrics& metrics)>
        success_callback;
  };

  Poller() = delete;
  Poller(std::unique_ptr<Parser> parser, std::unique_ptr<Scraper> scraper,
         const Options& options);

  void AddTarget(std::string_view name, std::string_view hostname);

  void Run();
  void Kill();

 private:
  struct Target final {
    std::string name;
    std::string hostname;
  };

  std::unique_ptr<Parser> parser_;
  std::unique_ptr<Scraper> scraper_;
  const Options options_;

  std::vector<Target> targets_;

  bool alive_ = false;
  std::mutex alive_mutex_;
  std::mutex sleep_mutex_;
  std::condition_variable sleeper_;

  // TODO Worker thread pool

  void ProcessTarget(const Target& target);
  absl::StatusOr<::shelly::Metrics> RetrieveMetrics(const Target& target);
};

#endif  // POLLER_H
