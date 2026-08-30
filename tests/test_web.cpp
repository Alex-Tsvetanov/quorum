#include <string>

#include "check.hpp"
#include "quorum/analyse.hpp"
#include "quorum/http.hpp"
#include "quorum/web.hpp"

using namespace quorum;

namespace {

web::Config config() {
    web::Config c;
    c.web_root = QUORUM_WEB_ROOT;
    c.example_path = std::string(QUORUM_SCENARIO_DIR) + "/example.json";
    return c;
}

http::Request parse_raw(const std::string& raw) {
    http::Request request;
    std::size_t consumed = 0;
    CHECK_TRUE(http::parse_request(raw, request, consumed) == http::Parse::Ok);
    return request;
}

http::Request get(const std::string& target) {
    return parse_raw("GET " + target + " HTTP/1.1\r\nHost: localhost\r\n\r\n");
}

http::Request post(const std::string& target, const std::string& body) {
    return parse_raw("POST " + target + " HTTP/1.1\r\nContent-Length: " +
                     std::to_string(body.size()) + "\r\n\r\n" + body);
}

}  // namespace

TEST(web, "the bundled scenario produces the same funded set as the solver") {
    const std::string path = std::string(QUORUM_SCENARIO_DIR) + "/example.json";
    analyse::Options opt;
    opt.method = sensitivity::Method::Topsis;
    opt.risk_samples = 2000;
    const analyse::Result r = analyse::run_file(path, opt);

    CHECK_TRUE(r.exact.optimal);
    CHECK_EQ(r.exact.selected.size(), std::size_t{7});

    const json::Value doc = analyse::to_json(r);
    const auto& funded = doc.at("portfolio", "root").at("funded", "portfolio").as_array("funded");
    CHECK_EQ(funded.size(), r.exact.selected.size());
    for (std::size_t i = 0; i < funded.size(); ++i)
        CHECK_EQ(funded[i].at("id", "funded").as_string("id"),
                 r.scenario.projects[r.exact.selected[i]].id);

    const auto& rejected =
        doc.at("portfolio", "root").at("rejected", "portfolio").as_array("rejected");
    CHECK_EQ(rejected.size(), r.scenario.projects.size() - r.exact.selected.size());
    for (const auto& row : rejected) {
        const std::string id = row.at("id", "rejected").as_string("id");
        const std::size_t index = r.scenario.project_index(id);
        CHECK_EQ(row.at("reason", "rejected").as_string("reason"),
                 portfolio::rejection_reason(r.instance, r.scenario, r.exact, index));
    }

    const double from_json =
        doc.at("risk", "root").at("probability_within_budget", "risk").as_number("p");
    CHECK_NEAR(from_json, r.monte_carlo.cost_within(r.scenario.budget), 1e-15);
}

TEST(web, "POST /api/analyse returns the engine result for a small scenario") {
    const std::string body = R"({
      "name": "tiny",
      "budget": 100,
      "criteria": [
        { "id": "value", "direction": "max", "weight": 0.6 },
        { "id": "risk",  "direction": "min", "weight": 0.4 }
      ],
      "resources": [ { "id": "dev", "capacity": 5 } ],
      "projects": [
        {
          "id": "A", "scores": { "value": 8, "risk": 3 },
          "activities": [
            { "id": "a1", "duration": 3, "cost": 40, "demand": { "dev": 2 } }
          ]
        },
        {
          "id": "B", "scores": { "value": 5, "risk": 6 },
          "activities": [
            { "id": "b1", "duration": 2, "cost": 80, "demand": { "dev": 1 } }
          ]
        }
      ]
    })";

    const http::Response response = web::handle(post("/api/analyse?method=topsis&samples=200", body),
                                                config());
    CHECK_EQ(response.status, 200);
    const auto doc = json::parse(response.body);
    const auto& funded = doc.at("portfolio", "root").at("funded", "p").as_array("funded");
    CHECK_EQ(funded.size(), std::size_t{1});
    CHECK_EQ(funded[0].at("id", "funded").as_string("id"), std::string("A"));
    const auto& rejected = doc.at("portfolio", "root").at("rejected", "p").as_array("rejected");
    CHECK_EQ(rejected.size(), std::size_t{1});
    CHECK_EQ(rejected[0].at("id", "rejected").as_string("id"), std::string("B"));
    CHECK_FALSE(rejected[0].at("reason", "rejected").as_string("reason").empty());
    CHECK_TRUE(doc.find("risk") != nullptr);
}

TEST(web, "an empty or malformed body is a client error, not a crash") {
    const auto empty = web::handle(post("/api/analyse", ""), config());
    CHECK_EQ(empty.status, 400);
    CHECK_TRUE(json::parse(empty.body).find("error") != nullptr);

    const auto bad = web::handle(post("/api/analyse", "{"), config());
    CHECK_EQ(bad.status, 400);

    const auto unknown = web::handle(post("/api/analyse?method=saw", "{}"), config());
    CHECK_EQ(unknown.status, 400);
}

TEST(web, "GET /api/example returns the bundled scenario file") {
    const auto response = web::handle(get("/api/example"), config());
    CHECK_EQ(response.status, 200);
    const auto sc = model::load(json::parse(response.body));
    const auto from_disk = model::load_file(std::string(QUORUM_SCENARIO_DIR) + "/example.json");
    CHECK_EQ(sc.name, from_disk.name);
    CHECK_EQ(sc.projects.size(), from_disk.projects.size());
    CHECK_NEAR(sc.budget, from_disk.budget, 1e-12);
}

TEST(web, "GET / serves the in-tree page and GET /missing is 404") {
    const auto page = web::handle(get("/"), config());
    CHECK_EQ(page.status, 200);
    CHECK_TRUE(page.content_type.find("text/html") != std::string::npos);
    CHECK_TRUE(page.body.find("Quorum") != std::string::npos);

    const auto css = web::handle(get("/app.css"), config());
    CHECK_EQ(css.status, 200);
    CHECK_TRUE(css.content_type.find("text/css") != std::string::npos);

    const auto js = web::handle(get("/app.js"), config());
    CHECK_EQ(js.status, 200);
    CHECK_TRUE(js.content_type.find("javascript") != std::string::npos);

    const auto missing = web::handle(get("/no-such-file"), config());
    CHECK_EQ(missing.status, 404);

    const auto escape = web::handle(get("/../CMakeLists.txt"), config());
    CHECK_EQ(escape.status, 403);
}

TEST(web, "POST is refused on static paths and GET is refused on the analyse endpoint") {
    const auto post_root = web::handle(post("/", "{}"), config());
    CHECK_EQ(post_root.status, 405);
    const auto get_analyse = web::handle(get("/api/analyse"), config());
    CHECK_EQ(get_analyse.status, 405);
}
