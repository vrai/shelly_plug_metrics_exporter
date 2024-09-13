#ifndef SCRAPER_H
#define SCRAPER_H

#include <memory>
#include <string>
#include <string_view>

#include "absl/status/statusor.h"

struct ScraperResult final {
  int code;
  std::string status;
  std::string content_type;
  std::string content;
};

class Scraper {
 public:
  struct Options final {
    bool verbose = false;
  };

  Scraper(const Scraper&) = delete;
  Scraper& operator=(const Scraper&) = delete;

  virtual ~Scraper() = default;

  virtual absl::StatusOr<ScraperResult> Scrape(const std::string& url) = 0;

  virtual std::string_view Version() const = 0;

 protected:
  Scraper() = default;
};

absl::StatusOr<std::unique_ptr<Scraper>> CreateScraper(const Scraper::Options& options);

#endif  // SCRAPER_H
