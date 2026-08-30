#include "quorum/web.hpp"

#include <atomic>
#include <csignal>
#include <cstdio>
#include <exception>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>

#include "quorum/analyse.hpp"
#include "quorum/net.hpp"

namespace quorum::web {
namespace {

std::atomic<bool> g_stop{false};

extern "C" void on_interrupt(int) { g_stop.store(true); }

http::Response make_response(int status, std::string content_type, std::string body) {
    http::Response response;
    response.status = status;
    response.content_type = std::move(content_type);
    response.body = std::move(body);
    return response;
}

http::Response json_error(int status, const std::string& message) {
    json::Value body(json::Object{{"error", json::Value(message)}});
    return make_response(status, "application/json; charset=utf-8", json::dump(body));
}

std::string read_file(const std::string& path, bool& ok) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        ok = false;
        return {};
    }
    std::ostringstream buf;
    buf << in.rdbuf();
    ok = true;
    return buf.str();
}

std::string join_path(const std::string& root, const std::string& path) {
    std::string out = root;
    if (!out.empty() && (out.back() == '/' || out.back() == '\\')) out.pop_back();
    out += path;
    return out;
}

http::Response static_file(const Config& config, const std::string& path) {
    if (!http::is_safe_path(path)) return make_response(403, "text/plain; charset=utf-8", "Forbidden");
    const std::string relative = path == "/" ? std::string("/index.html") : path;
    bool ok = false;
    std::string body = read_file(join_path(config.web_root, relative), ok);
    if (!ok) return make_response(404, "text/plain; charset=utf-8", "Not Found");
    http::Response response;
    response.status = 200;
    response.content_type = std::string(http::mime_for(relative));
    response.body = std::move(body);
    response.extra_headers.emplace_back("Cache-Control", "no-store");
    return response;
}

analyse::Options options_from(const http::Request& request) {
    analyse::Options opt;
    opt.risk_samples = 20000;
    if (const auto method = http::query_param(request.query, "method"))
        opt.method = analyse::method_of(*method);
    if (const auto samples = http::query_param(request.query, "samples")) {
        const unsigned long n = std::stoul(*samples);
        if (n > 100000)
            throw model::ModelError("samples must be at most 100000");
        opt.risk_samples = static_cast<std::size_t>(n);
    }
    return opt;
}

http::Response analyse_body(const http::Request& request) {
    if (request.body.empty()) return json_error(400, "the request body is empty");
    try {
        const analyse::Result result = analyse::run_text(request.body, options_from(request));
        return make_response(200, "application/json; charset=utf-8", analyse::to_json_text(result));
    } catch (const std::invalid_argument&) {
        return json_error(400, "samples must be a non-negative integer");
    } catch (const std::out_of_range&) {
        return json_error(400, "samples is out of range");
    } catch (const std::exception& e) {
        return json_error(400, e.what());
    }
}

}  // namespace

http::Response handle(const http::Request& request, const Config& config) {
    if (request.method != "GET" && request.method != "HEAD" && request.method != "POST") {
        http::Response response = make_response(405, "text/plain; charset=utf-8",
                                                "Only GET, HEAD and POST are served.");
        response.extra_headers.emplace_back("Allow", "GET, HEAD, POST");
        return response;
    }

    if (request.path == "/api/analyse") {
        if (request.method != "POST") {
            http::Response response =
                make_response(405, "text/plain; charset=utf-8", "POST a scenario document.");
            response.extra_headers.emplace_back("Allow", "POST");
            return response;
        }
        return analyse_body(request);
    }

    if (request.path == "/api/example") {
        if (request.method == "POST") {
            http::Response response =
                make_response(405, "text/plain; charset=utf-8", "GET the bundled scenario.");
            response.extra_headers.emplace_back("Allow", "GET, HEAD");
            return response;
        }
        bool ok = false;
        std::string body = read_file(config.example_path, ok);
        if (!ok) return json_error(404, "the bundled scenario is not at " + config.example_path);
        return make_response(200, "application/json; charset=utf-8", std::move(body));
    }

    if (request.method == "POST") {
        http::Response response =
            make_response(405, "text/plain; charset=utf-8", "POST is only accepted at /api/analyse.");
        response.extra_headers.emplace_back("Allow", "GET, HEAD");
        return response;
    }

    return static_file(config, request.path);
}

int serve(const ServeOptions& options) {
    g_stop.store(false);
    net::Library library;
    std::string error;
    net::Socket listener = net::listen_on(options.port, error);
    if (!listener.valid()) {
        std::fprintf(stderr, "error: %s\n", error.c_str());
        return 1;
    }
    if (!net::set_nonblocking(listener.get(), true)) {
        std::fprintf(stderr, "error: could not set the listener non-blocking\n");
        return 1;
    }

    std::signal(SIGINT, on_interrupt);
#ifdef SIGTERM
    std::signal(SIGTERM, on_interrupt);
#endif

    const std::uint16_t port = net::local_port(listener.get());
    std::printf("Quorum is serving on http://localhost:%u\n", static_cast<unsigned>(port));
    std::printf("  client files  %s\n", options.config.web_root.c_str());
    std::printf("  example       %s\n", options.config.example_path.c_str());
    std::printf("Press Ctrl+C to stop.\n");
    std::fflush(stdout);

    while (!g_stop.load()) {
        fd_set read_set;
        FD_ZERO(&read_set);
        FD_SET(listener.get(), &read_set);
        timeval timeout{};
        timeout.tv_sec = 0;
        timeout.tv_usec = 250000;
#ifdef _WIN32
        const int nfds = 0;
#else
        const int nfds = listener.get() + 1;
#endif
        const int ready = ::select(nfds, &read_set, nullptr, nullptr, &timeout);
        if (ready < 0) {
            if (net::last_error_was_would_block()) continue;
            if (g_stop.load()) break;
            continue;
        }
        if (ready == 0) continue;

        net::Socket client = net::accept_on(listener.get());
        if (!client.valid()) continue;

        std::string incoming;
        char chunk[4096];
        http::Request request;
        std::size_t consumed = 0;
        http::Parse parsed = http::Parse::Incomplete;
        while (parsed == http::Parse::Incomplete) {
            const long n = net::recv_some(client.get(), chunk, sizeof chunk);
            if (n <= 0) break;
            incoming.append(chunk, static_cast<std::size_t>(n));
            parsed = http::parse_request(incoming, request, consumed);
        }
        http::Response response;
        const bool head = parsed == http::Parse::Ok && request.method == "HEAD";
        if (parsed != http::Parse::Ok) {
            response = make_response(400, "text/plain; charset=utf-8", "Bad Request");
        } else {
            response = handle(request, options.config);
        }
        const std::string wired = http::serialize(response, false);
        if (head) {
            const std::size_t cut = wired.find("\r\n\r\n");
            net::send_all(client.get(), wired.substr(0, cut + 4));
        } else {
            net::send_all(client.get(), wired);
        }
    }

    std::printf("Stopped.\n");
    return 0;
}

}  // namespace quorum::web
