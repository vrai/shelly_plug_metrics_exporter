#include "scraper.h"

#include <regex>
#include <string>
#include <tuple>
#include <vector>

#include "absl/cleanup/cleanup.h"
#include "absl/log/die_if_null.h"
#include "absl/status/status.h"
#include "absl/strings/ascii.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_split.h"
#include "absl/strings/substitute.h"
#include "curl/curl.h"
#include "curl/curlver.h"

namespace {

std::string_view VersionString() {
  static const auto* const version = [] {
    return new std::string(absl::Substitute("libcurl $0", LIBCURL_VERSION));
  }();
  return *version;
}

const std::regex& HeaderRegex() {
  static const auto* const regex = [] {
    return new std::regex("^HTTP/(\\d)\\.(\\d)\\s+(\\d+)\\s+([^\\n^\\r]+)");
  }();
  return *regex;
}

// Holds the response state that's built up by the header and body callbacks.
struct State final {
  int code = -1;
  std::string status;
  int content_length = 0;
  std::string content_type;
  std::string content;

  // Used to pass back parsing errors.
  absl::Status error = absl::OkStatus();
};

absl::Status ParseStatusLine(State& state, const std::string& line) {
  std::smatch match;
  if (!std::regex_search(line, match, HeaderRegex())) {
    return absl::InvalidArgumentError(
        absl::Substitute("Invalid header: $0", line));
  }
  if (match.size() != 5) {
    return absl::InvalidArgumentError(
        absl::Substitute("Expected 5 parsed elements for header, got $0: $1",
                         match.size(), line));
  }
  if (!absl::SimpleAtoi(match[3].str(), &state.code)) {
    return absl::InvalidArgumentError(
        absl::Substitute("Status code in header not a number: $0", line));
  }
  state.status = match[4];
  return absl::OkStatus();
}

absl::StatusOr<std::tuple<std::string, std::string>> ParseHeaderLine(
    const std::string& line) {
  const std::vector<std::string> elements =
      absl::StrSplit(line, absl::MaxSplits(':', 1));
  if (elements.size() != 2) {
    return absl::InvalidArgumentError(
        absl::Substitute("Failed to parse header line: $0", line));
  }
  return std::make_tuple(
      std::string(absl::StripTrailingAsciiWhitespace(elements[0])),
      std::string(absl::StripLeadingAsciiWhitespace(elements[1])));
}

absl::Status HandleHeaderPair(State& state, std::string_view key,
                              std::string_view value) {
  const auto lower_key = absl::AsciiStrToLower(key);
  if (lower_key == "content-type") {
    state.content_type = absl::AsciiStrToLower(value);
  } else if (lower_key == "content-length") {
    if (!absl::SimpleAtoi(value, &state.content_length)) {
      return absl::InvalidArgumentError(
          absl::Substitute("Unable to parse content length value: $0", value));
    }
  }
  return absl::OkStatus();
}

size_t HeaderCallback(char* buffer, size_t size, size_t nitems,
                      void* user_data) {
  if (user_data == nullptr) {
    return 0;
  }
  State& state = *reinterpret_cast<State*>(user_data);

  std::string data(buffer, nitems);
  absl::StripAsciiWhitespace(&data);
  if (data.empty()) {
    return size * nitems;
  }

  if (state.code < 0) {
    const auto parse_status = ParseStatusLine(state, data);
    if (!parse_status.ok()) {
      state.error = parse_status;
      return 0;
    }
  } else {
    auto maybe_parsed = ParseHeaderLine(data);
    if (!maybe_parsed.ok()) {
      state.error = maybe_parsed.status();
      return 0;
    }
    auto [key, value] = std::move(maybe_parsed).value();
    const auto handle_status = HandleHeaderPair(state, key, value);
    if (!handle_status.ok()) {
      state.error = handle_status;
      return 0;
    }
  }
  return size * nitems;
}

size_t BodyCallback(char* buffer, size_t size, size_t nitems, void* user_data) {
  if (user_data == nullptr) {
    return 0;
  }
  State& state = *reinterpret_cast<State*>(user_data);
  if (state.content.capacity() < state.content_length) {
    state.content.reserve(state.content_length);
  }
  state.content += std::string(buffer, nitems);
  return size * nitems;
}

class ScraperImpl final : public Scraper {
 public:
  ScraperImpl() = delete;
  ScraperImpl(const Options& options) : options_(options) {}

  ~ScraperImpl() override { curl_global_cleanup(); }

  absl::StatusOr<ScraperResult> Scrape(const std::string& url) override {
    CURL* const curl = curl_easy_init();
    if (curl == nullptr) {
      return absl::InternalError("curl_easy_init failed");
    }
    auto curl_cleanup = absl::Cleanup([curl] { curl_easy_cleanup(curl); });

    State state;

    curl_easy_setopt(curl, CURLOPT_VERBOSE, options_.verbose ? 1 : 0);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Shelly Plug Metrics Exporter");
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &state);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, HeaderCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &state);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, BodyCallback);

    const CURLcode code = curl_easy_perform(curl);
    if (code != CURLE_OK) {
      if (!state.error.ok()) {
        return state.error;
      }
      return absl::InternalError(curl_easy_strerror(code));
    }
    if (!state.error.ok()) {
      return state.error;
    }

    if (state.code == 0 || state.status.empty()) {
      return absl::InvalidArgumentError("Missing status or status code");
    }
    if (state.content_type.empty()) {
      return absl::InvalidArgumentError("Missing content type");
    }

    return ScraperResult{
        .code = state.code,
        .status = state.status,
        .content_type = state.content_type,
        .content = state.content,
    };
  }

  std::string_view Version() const override { return VersionString(); }

 private:
  const Options options_;
};

}  // namespace

absl::StatusOr<std::unique_ptr<Scraper>> CreateScraper(const Scraper::Options& options) {
  const CURLcode code = curl_global_init(CURL_GLOBAL_ALL);
  if (code != CURLE_OK) {
    return absl::InternalError(curl_easy_strerror(code));
  }
  return std::make_unique<ScraperImpl>(options);
}
