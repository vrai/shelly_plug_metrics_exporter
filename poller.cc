#include "poller.h"

#include <chrono>
#include <future>

#include "absl/log/check.h"
#include "absl/log/die_if_null.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/substitute.h"
#include "status_macros/status_macros.h"

namespace {

std::string CreateScrapeUrl(absl::string_view hostname) {
  return absl::Substitute("http://$0/rpc/Switch.GetStatus?id=0", hostname);
}

}  // namespace

Poller::Poller(std::unique_ptr<Parser> parser, std::unique_ptr<Scraper> scraper,
               const Options& options)
    : parser_(std::move(ABSL_DIE_IF_NULL(parser))),
      scraper_(std::move(ABSL_DIE_IF_NULL(scraper))),
      options_(options),
      alive_(false) {}

void Poller::AddTarget(std::string_view name, std::string_view hostname) {
  {
    std::unique_lock<std::mutex> lock(alive_mutex_);
    CHECK(!alive_) << "App::AddTarget must be called before App::Run";
  }
  targets_.push_back(Target{
      .name = std::string(name),
      .hostname = std::string(hostname),
  });
}

void Poller::Run() {
  {
    std::unique_lock<std::mutex> lock(alive_mutex_);
    CHECK(!alive_) << "App::Run called twice without first run being killed";
    alive_ = true;
  }

  LOG(INFO) << "Entered run loop, will poll every " << options_.poll_period;
  do {
    const auto start_time = options_.time_func();

    {
      std::unique_lock<std::mutex> lock(alive_mutex_);
      if (!alive_) {
        break;
      }
    }

    // Process th targets in parallel and then block this thread until they have
    // all completed.
    std::vector<std::future<void>> futures;
    futures.reserve(targets_.size());
    for (const auto& target : targets_) {
      futures.push_back(std::async(std::launch::async,
                                   [this, &target] { ProcessTarget(target); }));
    }
    for (auto& future : futures) {
      future.wait();
    }

    const auto delay =
        (start_time + options_.poll_period) - options_.time_func();
    if (delay > absl::ZeroDuration()) {
      std::unique_lock<std::mutex> lock(sleep_mutex_);
      sleeper_.wait_for(
          lock, std::chrono::milliseconds(absl::ToInt64Milliseconds(delay)));
    }

  } while (true);
  LOG(INFO) << "Exited run loop";
}

void Poller::Kill() {
  {
    std::unique_lock<std::mutex> lock(alive_mutex_);
    if (!alive_) {
      return;
    }
    alive_ = false;
  }
  sleeper_.notify_all();
}

bool Poller::Alive() const {
  std::unique_lock<std::mutex> lock(alive_mutex_);
  return alive_;
}

void Poller::ProcessTarget(const Target& target) {
  auto maybe_metrics = RetrieveMetrics(target);
  if (!maybe_metrics.ok()) {
    if (options_.error_callback) {
      options_.error_callback(target.name, maybe_metrics.status());
    }
    LOG(ERROR) << "Failed to retrieve metrics for target \"" << target.name
               << "\": " << maybe_metrics.status();
    return;
  }
  const auto metrics = std::move(maybe_metrics).value();

  if (options_.success_callback) {
    options_.success_callback(target.name, metrics);
  }
  if (options_.verbose_logging) {
    LOG(INFO) << "Got successful response for target \"" << target.name
              << "\": " << metrics.DebugString();
  }
}

absl::StatusOr<::shelly::Metrics> Poller::RetrieveMetrics(
    const Target& target) {
  const auto url = CreateScrapeUrl(target.hostname);
  ASSIGN_OR_RETURN(const auto scraper_result, scraper_->Scrape(url),
                   _ << "Failed to scraper " << url);
  if (scraper_result.code != 200) {
    return absl::InvalidArgumentError(absl::Substitute(
        "Got HTTP response code $0 for $1", scraper_result.code, url));
  }
  if (scraper_result.content_type != "application/json") {
    return absl::InvalidArgumentError(absl::Substitute(
        "Response content type \"$0\" is not supported, from $1",
        scraper_result.content_type, url));
  }

  ASSIGN_OR_RETURN(const auto metrics, parser_->Parse(scraper_result.content),
                   _ << "Failed to parse JSON from " << url);
  return metrics;
}
