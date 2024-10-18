#include "scraper.h"

#include <gtest/gtest.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>

#include "absl/log/check.h"
#include "absl/strings/substitute.h"
#include "civetweb.h"
#include "parser.h"

namespace {

inline constexpr bool kVerboseScraper = false;
inline constexpr auto kResponseType = "text/plain";
inline constexpr auto kResponseContent = R"(
  This is the content returned
  by the valid page request.
)";

int CivetWebHandler(mg_connection* conn, void*) {
  const std::string response = kResponseContent;
  mg_send_http_ok(conn, kResponseType, response.size());
  mg_write(conn, response.c_str(), response.size());
  return 200;
}

uint16_t FindUnusedPortOrDie() {
  int fd = socket(AF_INET, SOCK_STREAM, /*protocol=*/0);
  CHECK(fd != -1) << "Failed to create socket";

  // Bind the socket to any available port, which will result in the OS
  // assigning it an open port number.
  sockaddr_in addr;
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons(0);  // Bind to any available port.
  CHECK(bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0)
      << "Failed to bind socket";

  // Successfully bound the socket, now retrieve the port number.
  socklen_t addrlen = sizeof(addr);
  CHECK(getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &addrlen) == 0)
      << "Failed to get socket name";
  const uint16_t port = ntohs(addr.sin_port);
  CHECK(port != 0) << "Failed to get port number";

  // Clean up the socket so the port can be reused.
  shutdown(fd, SHUT_RDWR);
  CHECK(close(fd) == 0) << "Failed to close socket";
  return port;
}

}  // namespace

class Fixture final {
 public:
  Fixture() {
    // Use a random open port for the Civetweb server. Note that this is
    // susceptible to race conditions and should probably have retry logic.
    port_ = FindUnusedPortOrDie();
    std::string port_str = absl::Substitute("$0", port_);
    const char* options[] = {"listening_ports", port_str.c_str(), "num_threads",
                             "4", nullptr};

    // Setup Civetweb server to act as the endpoint for the scraper tests.
    mg_init_library(0);
    ctx_ = mg_start(nullptr, nullptr, options);
    CHECK(ctx_ != nullptr) << "Failed to initialize Civetweb server";
    mg_set_request_handler(ctx_, "/valid", CivetWebHandler, nullptr);

    auto scraper = CreateScraper({.verbose = kVerboseScraper});
    CHECK(scraper.ok()) << "Failed to create Scraper: " << scraper.status();
    scraper_ = std::move(*scraper);
  }

  ~Fixture() {
    mg_stop(ctx_);
    mg_exit_library();
  }

  Scraper& scraper() { return *scraper_; }
  int port() const { return port_; }

  std::string Host() const {
    return absl::Substitute("http://localhost:$0", port_);
  }

 private:
  mg_context* ctx_;
  int port_;
  std::unique_ptr<Scraper> scraper_;
};

TEST(ScrapeJson, InvalidHost) {
  Fixture fixture;

  const auto result = fixture.scraper().Scrape("http://invalid");
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInternal);
}

TEST(ScrapeJson, InvalidPage) {
  Fixture fixture;

  const auto result = fixture.scraper().Scrape(fixture.Host() + "/invalid");
  EXPECT_TRUE(result.ok());
  EXPECT_EQ(result->code, 404);
}

TEST(ScapeJson, ValidPage) {
  Fixture fixture;

  auto result = fixture.scraper().Scrape(fixture.Host() + "/valid");
  EXPECT_TRUE(result.ok());
  EXPECT_EQ(result->code, 200);
  EXPECT_EQ(result->content_type, kResponseType);
  EXPECT_EQ(result->content, kResponseContent);
}