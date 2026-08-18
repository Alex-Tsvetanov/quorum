// Portfolio selection: choose the subset of candidate projects with the largest
// total value that fits the budget and the resource capacities, honours the
// funding prerequisites and respects the mutual exclusions.
//
// Two solvers over the same instance. Branch and bound with a linear relaxation
// bound returns the optimum. The greedy heuristic returns a feasible portfolio
// in a fraction of the time. Running both is not redundancy: the heuristic is
// the control value that catches a wrong model, because an exact solver that
// returns less than a feasible heuristic is not slow, it is wrong.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "quorum/model.hpp"

namespace quorum::portfolio {

struct Instance {
    std::vector<double> value;                       // one per project
    std::vector<double> cost;                        // one per project
    std::vector<std::vector<double>> usage;          // usage[resource][project]
    std::vector<double> capacity;                    // one per resource
    double budget = 0.0;
    std::vector<std::vector<std::size_t>> prerequisites;  // direct, by project index
    std::vector<std::vector<std::size_t>> exclusions;     // symmetric, by project index
    std::vector<std::size_t> order;                       // prerequisites first

    std::size_t size() const { return value.size(); }
};

struct Solution {
    std::vector<char> chosen;            // one flag per project
    std::vector<std::size_t> selected;   // indices of the chosen projects, ascending
    double value = 0.0;
    double cost = 0.0;
    std::vector<double> used;            // one per resource
    bool optimal = false;                // proved optimal, as opposed to merely feasible
    std::int64_t nodes = 0;              // search nodes explored
    double seconds = 0.0;
};

// Builds the selection instance. `value` is the score of each project from the
// chosen multi-criteria method, in scenario order.
Instance make_instance(const model::Scenario& sc, const std::vector<double>& value);

// Branch and bound with a linear relaxation bound. `node_limit` below zero means
// no limit; when a positive limit is hit the search stops and the returned
// solution is feasible but not marked optimal.
Solution solve_exact(const Instance& inst, std::int64_t node_limit = -1);

// Greedy by value density, pulling in each candidate's prerequisite closure.
Solution solve_greedy(const Instance& inst);

// Why a project is not in the portfolio. Returns an empty string when it is.
std::string rejection_reason(const Instance& inst, const model::Scenario& sc,
                             const Solution& sol, std::size_t project);

}  // namespace quorum::portfolio
