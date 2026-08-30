// HTTP/1.1 request parsing and response serialising.
//
// Nothing here touches a socket. The parser is tested against buffers, which is
// the only way to pin split messages, pipelining and path traversal without
// depending on a live port.
#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace quorum::http {

struct Request {
    std::string method;
    std::string target;
    std::string path;
    std::string query;
    int minor_version = 1;
    std::vector<std::pair<std::string, std::string>> headers;
    std::string body;

    std::string_view header(std::string_view name) const;
    bool keep_alive() const;
};

enum class Parse { Incomplete, Ok, Error };

Parse parse_request(std::string_view buffer, Request& out, std::size_t& consumed);

struct Response {
    int status = 200;
    std::string content_type = "text/plain; charset=utf-8";
    std::string body;
    std::vector<std::pair<std::string, std::string>> extra_headers;
};

std::string serialize(const Response& response, bool keep_alive);
std::string_view status_text(int status);
std::string_view mime_for(std::string_view path);
std::string percent_decode(std::string_view text);
std::optional<std::string> query_param(std::string_view query, std::string_view key);

// True when the path can be joined to a document root without escaping it.
bool is_safe_path(std::string_view path);

}  // namespace quorum::http
