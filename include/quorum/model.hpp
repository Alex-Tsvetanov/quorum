// The scenario: candidate projects, their activities, the criteria they are
// judged by, and the limits the portfolio has to respect.
//
// Everything here is loaded from one JSON document and validated at load time.
// The invariants are checked once, in `load`, so no layer downstream has to ask
// whether the graph is acyclic or whether a dependency names a project that
// exists.
#pragma once

#include <stdexcept>
#include <string>
#include <vector>

#include "quorum/cpm.hpp"
#include "quorum/json.hpp"

namespace quorum::model {

// Raised for any input the model rejects. The message names the field.
class ModelError : public std::runtime_error {
public:
    explicit ModelError(const std::string& what) : std::runtime_error(what) {}
};

struct Criterion {
    std::string id;
    std::string label;
    bool maximise = true;  // false when a smaller score is better, such as risk
    double weight = 0.0;   // direct weight, normalised at load; may be unset
};

struct Resource {
    std::string id;
    std::string label;
    double capacity = 0.0;
};

struct Activity {
    std::string id;
    std::string name;
    double optimistic = 0.0, likely = 0.0, pessimistic = 0.0;
    double cost_optimistic = 0.0, cost_likely = 0.0, cost_pessimistic = 0.0;
    std::vector<std::string> depends_on;
    // Per-period demand while the activity runs, one entry per scenario
    // resource, in scenario order.
    std::vector<double> demand;

    double expected_duration() const { return (optimistic + 4.0 * likely + pessimistic) / 6.0; }
    double expected_cost() const {
        return (cost_optimistic + 4.0 * cost_likely + cost_pessimistic) / 6.0;
    }
};

struct Project {
    std::string id;
    std::string name;
    std::vector<Activity> activities;
    std::vector<std::string> requires_projects;  // must be funded before this one
    std::vector<std::string> excludes_projects;  // cannot be funded together
    std::vector<double> scores;                  // one per criterion, in scenario order

    // Derived at load time from the activity network.
    double expected_cost = 0.0;
    double expected_duration = 0.0;
    std::vector<double> peak_demand;  // one per resource
    cpm::Schedule schedule;

    cpm::Network network() const;
};

struct Scenario {
    std::string name;
    std::vector<Criterion> criteria;
    // Pairwise comparison matrix over the criteria, empty when the scenario
    // gives the weights directly.
    std::vector<std::vector<double>> comparisons;
    std::vector<Resource> resources;
    double budget = 0.0;
    std::vector<Project> projects;

    std::size_t project_index(const std::string& id) const;
    std::size_t criterion_index(const std::string& id) const;
    // Indices of `projects`, prerequisites before dependants.
    std::vector<std::size_t> funding_order() const;
    // The scores of every project against every criterion, projects by criteria.
    std::vector<std::vector<double>> score_matrix() const;
};

Scenario load(const json::Value& doc);
Scenario load_file(const std::string& path);

// The programme network of a selected portfolio: one node per selected project,
// its duration taken from its own critical path, its edges from the funding
// dependencies. This is what the critical path of the whole programme runs on.
cpm::Network programme_network(const Scenario& sc, const std::vector<std::size_t>& selected);

}  // namespace quorum::model
