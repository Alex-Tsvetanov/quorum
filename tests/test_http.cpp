#include "check.hpp"
#include "quorum/http.hpp"

using namespace quorum;

TEST(http, "a request line and headers parse, including the body") {
    http::Request request;
    std::size_t consumed = 0;
    const char* raw =
        "POST /api/analyse?method=topsis HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Content-Length: 2\r\n"
        "\r\n"
        "{}";
    CHECK_TRUE(http::parse_request(raw, request, consumed) == http::Parse::Ok);
    CHECK_EQ(request.method, std::string("POST"));
    CHECK_EQ(request.path, std::string("/api/analyse"));
    CHECK_EQ(request.query, std::string("method=topsis"));
    CHECK_EQ(request.body, std::string("{}"));
    CHECK_EQ(consumed, std::string(raw).size());
    CHECK_EQ(request.header("Host"), std::string_view("localhost"));
}

TEST(http, "a split message waits until the headers and the body are both present") {
    const std::string whole =
        "GET /index.html HTTP/1.1\r\nHost: x\r\nContent-Length: 4\r\n\r\nabcd";
    http::Request request;
    std::size_t consumed = 0;
    for (std::size_t cut = 1; cut < whole.size(); ++cut) {
        const auto state = http::parse_request(whole.substr(0, cut), request, consumed);
        if (cut < whole.size()) CHECK_TRUE(state == http::Parse::Incomplete);
    }
    CHECK_TRUE(http::parse_request(whole, request, consumed) == http::Parse::Ok);
    CHECK_EQ(request.body, std::string("abcd"));
}

TEST(http, "consumed bytes let a second pipelined request be parsed from the rest") {
    const std::string two = "GET /a HTTP/1.1\r\n\r\nGET /b HTTP/1.1\r\n\r\n";
    http::Request request;
    std::size_t consumed = 0;
    CHECK_TRUE(http::parse_request(two, request, consumed) == http::Parse::Ok);
    CHECK_EQ(request.path, std::string("/a"));
    const std::string_view rest(two.data() + consumed, two.size() - consumed);
    CHECK_TRUE(http::parse_request(rest, request, consumed) == http::Parse::Ok);
    CHECK_EQ(request.path, std::string("/b"));
}

TEST(http, "malformed messages are rejected rather than half accepted") {
    http::Request request;
    std::size_t consumed = 0;
    CHECK_TRUE(http::parse_request("GET\r\n\r\n", request, consumed) == http::Parse::Error);
    CHECK_TRUE(http::parse_request("GET / HTTP/2.0\r\n\r\n", request, consumed) == http::Parse::Error);
    CHECK_TRUE(http::parse_request("GET / HTTP/1.1\r\nBadHeader\r\n\r\n", request, consumed) ==
               http::Parse::Error);
    CHECK_TRUE(http::parse_request("GET / HTTP/1.1\r\nContent-Length: 12x\r\n\r\n", request,
                                  consumed) == http::Parse::Error);
}

TEST(http, "HTTP/1.1 stays open unless the client asks to close") {
    http::Request request;
    std::size_t consumed = 0;
    http::parse_request("GET / HTTP/1.1\r\n\r\n", request, consumed);
    CHECK_TRUE(request.keep_alive());
    http::parse_request("GET / HTTP/1.1\r\nConnection: close\r\n\r\n", request, consumed);
    CHECK_FALSE(request.keep_alive());
    http::parse_request("GET / HTTP/1.0\r\n\r\n", request, consumed);
    CHECK_FALSE(request.keep_alive());
}

TEST(http, "percent escapes and query parameters decode") {
    CHECK_EQ(http::percent_decode("%2Fapi%20x"), std::string("/api x"));
    const auto value = http::query_param("method=ahp&samples=1000", "samples");
    CHECK_TRUE(value.has_value());
    CHECK_EQ(*value, std::string("1000"));
    CHECK_FALSE(http::query_param("method=ahp", "samples").has_value());
}

TEST(http, "a static path cannot escape the document root") {
    CHECK_TRUE(http::is_safe_path("/index.html"));
    CHECK_TRUE(http::is_safe_path("/app.css"));
    CHECK_FALSE(http::is_safe_path("/../secret"));
    CHECK_FALSE(http::is_safe_path("/a/../../b"));
    CHECK_FALSE(http::is_safe_path("/a/./b"));
    CHECK_FALSE(http::is_safe_path("index.html"));
    CHECK_FALSE(http::is_safe_path("/C:/windows"));
    CHECK_FALSE(http::is_safe_path("/a\\b"));
}

TEST(http, "a response carries a length and the documented status words") {
    http::Response response;
    response.status = 200;
    response.content_type = "text/plain";
    response.body = "ok";
    const std::string wired = http::serialize(response, false);
    CHECK_TRUE(wired.find("HTTP/1.1 200 OK") == 0);
    CHECK_TRUE(wired.find("Content-Length: 2") != std::string::npos);
    CHECK_TRUE(wired.find("Connection: close") != std::string::npos);
    CHECK_EQ(http::status_text(404), std::string_view("Not Found"));
    CHECK_EQ(http::mime_for("/app.js"), std::string_view("text/javascript; charset=utf-8"));
    CHECK_EQ(http::mime_for("/app.css"), std::string_view("text/css; charset=utf-8"));
}
