#include <string>

#include "check.hpp"
#include "quorum/model.hpp"

using namespace quorum;

namespace {

// A two project scenario, small enough that every derived number can be checked
// by hand.
std::string document() {
    return R"({
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
        { "id": "a1", "duration": 3, "cost": 20, "demand": { "dev": 2 } },
        { "id": "a2", "duration": 4, "cost": 30, "depends_on": ["a1"], "demand": { "dev": 3 } },
        { "id": "a3", "duration": 2, "cost": 10, "demand": { "dev": 1 } }
      ]
    },
    {
      "id": "B", "scores": { "value": 5, "risk": 6 },
      "activities": [
        { "id": "b1", "optimistic": 2, "likely": 4, "pessimistic": 12,
          "cost_optimistic": 10, "cost_likely": 15, "cost_pessimistic": 26 }
      ]
    }
  ]
})";
}

model::Scenario load() { return model::load(json::parse(document())); }

}  // namespace

TEST(model, "a well formed scenario loads with its fields intact") {
    const auto sc = load();
    CHECK_EQ(sc.name, std::string("tiny"));
    CHECK_EQ(sc.projects.size(), std::size_t{2});
    CHECK_EQ(sc.criteria.size(), std::size_t{2});
    CHECK_TRUE(sc.criteria[0].maximise);
    CHECK_FALSE(sc.criteria[1].maximise);
    CHECK_NEAR(sc.budget, 100.0, 1e-12);
    CHECK_EQ(sc.resources.size(), std::size_t{1});
}

TEST(model, "criterion weights are normalised to sum to one") {
    const auto sc = load();
    CHECK_NEAR(sc.criteria[0].weight + sc.criteria[1].weight, 1.0, 1e-12);
    CHECK_NEAR(sc.criteria[0].weight, 0.6, 1e-12);
}

TEST(model, "a single point estimate collapses the three points onto itself") {
    const auto sc = load();
    const auto& a1 = sc.projects[0].activities[0];
    CHECK_NEAR(a1.optimistic, 3.0, 1e-12);
    CHECK_NEAR(a1.pessimistic, 3.0, 1e-12);
    CHECK_NEAR(a1.expected_duration(), 3.0, 1e-12);
    // The three point estimate on project B keeps its spread.
    const auto& b1 = sc.projects[1].activities[0];
    CHECK_NEAR(b1.expected_duration(), (2.0 + 16.0 + 12.0) / 6.0, 1e-12);
    CHECK_NEAR(b1.expected_cost(), (10.0 + 60.0 + 26.0) / 6.0, 1e-12);
}

TEST(model, "project cost, duration and peak demand are derived at load time") {
    const auto sc = load();
    const auto& a = sc.projects[0];
    CHECK_NEAR(a.expected_cost, 60.0, 1e-9);
    // a1 then a2 is seven periods, a3 runs alongside, so the project takes seven.
    CHECK_NEAR(a.expected_duration, 7.0, 1e-9);
    // a1 and a3 overlap in the first two periods, two engineers plus one.
    CHECK_NEAR(a.peak_demand[0], 3.0, 1e-9);
}

TEST(model, "a cyclic activity graph is rejected at load time") {
    const std::string doc = R"({
  "budget": 10, "criteria": [{"id": "v", "weight": 1}],
  "projects": [{ "id": "A", "scores": {"v": 1}, "activities": [
    {"id": "x", "duration": 1, "cost": 1, "depends_on": ["y"]},
    {"id": "y", "duration": 1, "cost": 1, "depends_on": ["x"]}
  ]}]})";
    CHECK_THROWS(model::load(json::parse(doc)), cpm::CycleError);
}

TEST(model, "a cyclic funding graph is rejected at load time") {
    const std::string doc = R"({
  "budget": 10, "criteria": [{"id": "v", "weight": 1}],
  "projects": [
    { "id": "A", "requires": ["B"], "scores": {"v": 1},
      "activities": [{"id": "x", "duration": 1, "cost": 1}] },
    { "id": "B", "requires": ["A"], "scores": {"v": 1},
      "activities": [{"id": "y", "duration": 1, "cost": 1}] }
  ]})";
    CHECK_THROWS(model::load(json::parse(doc)), cpm::CycleError);
}

TEST(model, "references to projects that do not exist are rejected") {
    const std::string doc = R"({
  "budget": 10, "criteria": [{"id": "v", "weight": 1}],
  "projects": [{ "id": "A", "requires": ["ghost"], "scores": {"v": 1},
    "activities": [{"id": "x", "duration": 1, "cost": 1}] }]})";
    CHECK_THROWS(model::load(json::parse(doc)), model::ModelError);
}

TEST(model, "a contradictory pair of constraints is caught rather than left to the solver") {
    const std::string doc = R"({
  "budget": 10, "criteria": [{"id": "v", "weight": 1}],
  "projects": [
    { "id": "A", "requires": ["B"], "excludes": ["B"], "scores": {"v": 1},
      "activities": [{"id": "x", "duration": 1, "cost": 1}] },
    { "id": "B", "scores": {"v": 1}, "activities": [{"id": "y", "duration": 1, "cost": 1}] }
  ]})";
    CHECK_THROWS(model::load(json::parse(doc)), model::ModelError);
}

TEST(model, "an out of order three point estimate is rejected") {
    const std::string doc = R"({
  "budget": 10, "criteria": [{"id": "v", "weight": 1}],
  "projects": [{ "id": "A", "scores": {"v": 1}, "activities": [
    {"id": "x", "optimistic": 9, "likely": 4, "pessimistic": 12, "cost": 1}]}]})";
    CHECK_THROWS(model::load(json::parse(doc)), model::ModelError);
}

TEST(model, "a project missing a score for one criterion is rejected") {
    const std::string doc = R"({
  "budget": 10,
  "criteria": [{"id": "v", "weight": 1}, {"id": "w", "weight": 1}],
  "projects": [{ "id": "A", "scores": {"v": 1},
    "activities": [{"id": "x", "duration": 1, "cost": 1}]}]})";
    CHECK_THROWS(model::load(json::parse(doc)), model::ModelError);
}

TEST(model, "duplicate identifiers are rejected at every level") {
    const std::string projects = R"({
  "budget": 10, "criteria": [{"id": "v", "weight": 1}],
  "projects": [
    {"id": "A", "scores": {"v": 1}, "activities": [{"id": "x", "duration": 1, "cost": 1}]},
    {"id": "A", "scores": {"v": 1}, "activities": [{"id": "y", "duration": 1, "cost": 1}]}
  ]})";
    CHECK_THROWS(model::load(json::parse(projects)), model::ModelError);

    const std::string activities = R"({
  "budget": 10, "criteria": [{"id": "v", "weight": 1}],
  "projects": [{"id": "A", "scores": {"v": 1}, "activities": [
    {"id": "x", "duration": 1, "cost": 1}, {"id": "x", "duration": 1, "cost": 1}]}]})";
    CHECK_THROWS(model::load(json::parse(activities)), model::ModelError);
}

TEST(model, "a scenario with neither weights nor a comparison matrix is rejected") {
    const std::string doc = R"({
  "budget": 10, "criteria": [{"id": "v"}],
  "projects": [{"id": "A", "scores": {"v": 1},
    "activities": [{"id": "x", "duration": 1, "cost": 1}]}]})";
    CHECK_THROWS(model::load(json::parse(doc)), model::ModelError);
}

TEST(model, "the funding order lists every prerequisite before its dependant") {
    const std::string doc = R"({
  "budget": 10, "criteria": [{"id": "v", "weight": 1}],
  "projects": [
    {"id": "C", "requires": ["B"], "scores": {"v": 1},
     "activities": [{"id": "z", "duration": 1, "cost": 1}]},
    {"id": "B", "requires": ["A"], "scores": {"v": 1},
     "activities": [{"id": "y", "duration": 1, "cost": 1}]},
    {"id": "A", "scores": {"v": 1}, "activities": [{"id": "x", "duration": 1, "cost": 1}]}
  ]})";
    const auto sc = model::load(json::parse(doc));
    const auto order = sc.funding_order();
    CHECK_EQ(order.size(), std::size_t{3});
    CHECK_EQ(sc.projects[order[0]].id, std::string("A"));
    CHECK_EQ(sc.projects[order[1]].id, std::string("B"));
    CHECK_EQ(sc.projects[order[2]].id, std::string("C"));
}

TEST(model, "the programme network carries project durations and prunes dropped edges") {
    const auto sc = load();
    const auto all = model::programme_network(sc, {0, 1});
    CHECK_EQ(all.tasks.size(), std::size_t{2});
    CHECK_NEAR(all.tasks[0].likely, sc.projects[0].expected_duration, 1e-9);

    // A prerequisite outside the selection cannot appear as an edge, or the
    // programme schedule would refer to a project nobody funded.
    const auto one = model::programme_network(sc, {1});
    CHECK_EQ(one.tasks.size(), std::size_t{1});
    CHECK_EQ(one.tasks[0].depends_on.size(), std::size_t{0});
}

TEST(model, "the bundled scenario loads and its derived figures are sane") {
    const auto sc = model::load_file(std::string(QUORUM_SCENARIO_DIR) + "/example.json");
    CHECK_EQ(sc.projects.size(), std::size_t{12});
    CHECK_EQ(sc.criteria.size(), std::size_t{4});
    CHECK_EQ(sc.comparisons.size(), std::size_t{4});
    double total = 0.0;
    for (const auto& p : sc.projects) {
        CHECK_TRUE(p.expected_cost > 0.0);
        CHECK_TRUE(p.expected_duration > 0.0);
        CHECK_EQ(p.peak_demand.size(), sc.resources.size());
        total += p.expected_cost;
    }
    // The scenario is only interesting if the money on the table is short of the
    // money asked for.
    CHECK_TRUE(sc.budget < total);
}
