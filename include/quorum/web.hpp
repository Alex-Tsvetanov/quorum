// HTTP glue for the decision-support page: route a parsed request to static
// files or to the existing analysis pipeline. The server loop is here too;
// the handlers are a pure function of the request so the tests never bind a
// port.
#pragma once

#include <cstdint>
#include <string>

#include "quorum/http.hpp"

namespace quorum::web {

struct Config {
    std::string web_root = "web";
    std::string example_path = "scenarios/example.json";
};

http::Response handle(const http::Request& request, const Config& config);

struct ServeOptions {
    std::uint16_t port = 8080;
    Config config;
};

// Binds, prints the address, and serves until SIGINT or SIGTERM.
int serve(const ServeOptions& options);

}  // namespace quorum::web
