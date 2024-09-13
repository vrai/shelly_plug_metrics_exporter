#include <csignal>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

#include "absl/base/log_severity.h"
#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/log/check.h"
#include "absl/log/globals.h"
#include "absl/log/initialize.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "config.h"
#include "parser.h"
#include "poller.h"
#include "prometheus/exposer.h"
#include "prometheus/registry.h"
#include "registry.h"
#include "scraper.h"
#include "shelly.h"
#include "target.h"

ABSL_FLAG(std::string, metrics_addr, "0.0.0.0:9100",
          "Address on which the metrics will be served. Defaults to the "
          "standard Prometheus node exporter port.");
ABSL_FLAG(std::string, metrics_path, "/metrics",
          "Path on which the metrics will be served.");
ABSL_FLAG(absl::Duration, poll_period, absl::Seconds(15),
          "How frequently the targets will be polled for new metrics.");
ABSL_FLAG(std::string, targets_config_file, "./targets.json",
          "File name of the JSON targets config file.");
ABSL_FLAG(bool, verbose_scraper, false, "If true, log verbose scraper output");

std::function<void(int)> signal_handler_func;
void SignalHandler(int signum) {
  if (signal_handler_func) {
    signal_handler_func(signum);
  }
}

template <class T>
T GetFlagOrDie(absl::Flag<T>& flag, const std::string& error,
               std::function<bool(const T&)> validation_func) {
  T val = absl::GetFlag(flag);
  if (validation_func != nullptr && !validation_func(val)) {
    LOG(QFATAL) << "Error with flag --" << flag.Name() << " : " << error;
  }
  return val;
}

std::unique_ptr<Scraper> CreateScraperOrDie() {
  auto maybe_scraper = CreateScraper(Scraper::Options{
      .verbose = absl::GetFlag(FLAGS_verbose_scraper),
  });
  if (!maybe_scraper.ok()) {
    LOG(FATAL) << maybe_scraper.status();
  }
  return std::move(maybe_scraper).value();
}

std::vector<Target> LoadTargetsOrDie(std::string_view filename) {
  auto maybe_targets = LoadTargetsFromFile(filename);
  if (!maybe_targets.ok()) {
    LOG(QFATAL) << "Failed to load targets file \"" << filename
                << "\": " << maybe_targets.status();
  }
  return *std::move(maybe_targets);
}

int main(int argc, char* argv[]) {
  absl::SetStderrThreshold(absl::LogSeverityAtLeast::kInfo);
  absl::InitializeLog();
  absl::ParseCommandLine(argc, argv);

  // Parse the command-line arguments
  const auto metrics_addr =
      GetFlagOrDie<std::string>(FLAGS_metrics_addr, "Must provide a value",
                                [](const auto& val) { return !val.empty(); });
  const auto metrics_path = GetFlagOrDie<std::string>(
      FLAGS_metrics_path, "Must be non-empty and start with a '/'",
      [](const auto& val) { return !val.empty() && val[0] == '/'; });
  const auto poll_period = GetFlagOrDie<absl::Duration>(
      FLAGS_poll_period, "Must be at least one second",
      [](const auto& val) { return val >= absl::Seconds(1); });
  const auto target_config_file = GetFlagOrDie<std::string>(
      FLAGS_targets_config_file, "File must exist", [](const auto& val) {
        return !val.empty() && std::filesystem::exists(val);
      });

  const auto targets = LoadTargetsOrDie(target_config_file);
  if (targets.empty()) {
    LOG(QFATAL) << "Targets file \"" << target_config_file
                << "\" contains no targets";
  }
  LOG(INFO) << "Loaded targets: " << targets.size();

  auto scraper = CreateScraperOrDie();
  LOG(INFO) << "Initialized scraper: " << scraper->Version();
  auto parser = CreateParser();
  LOG(INFO) << "Initialized parser: " << parser->Version();

  auto registry = CreateRegistry();

  Poller poller(
      std::move(parser), std::move(scraper),
      Poller::Options{
          .poll_period = poll_period,
          .error_callback =
              [&registry](absl::string_view name, const absl::Status& error) {
                registry->ErrorCallback(name, error);
              },
          .success_callback =
              [&registry](absl::string_view name,
                          const ::shelly::Metrics& metrics) {
                registry->SuccessCallback(name, metrics);
              },
      });

  for (const auto& target : targets) {
    poller.AddTarget(target.name, target.hostname);
    CHECK_OK(registry->AddTarget(target.name))
        << "Failed to add \"" << target.name << "\" to the registry";
  };

  ::prometheus::Exposer exposer(metrics_addr);
  exposer.RegisterCollectable(registry->GetRegistry(), metrics_path);

  // Setup the signal handlers to kill the poller gracefully.
  signal_handler_func = [&poller](int signum) {
    LOG(WARNING) << "Received signal " << signum << ", terminating";
    poller.Kill();
  };
  std::signal(SIGINT, SignalHandler);
  std::signal(SIGTERM, SignalHandler);

  poller.Run();
}
