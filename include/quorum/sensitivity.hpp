// Sensitivity analysis: rerun the whole pipeline over systematically varied
// assumptions and report which positions survive every variation.
//
// A recommendation that holds only at one particular budget or one particular
// weighting is not the same object as one that holds across the range, and a
// manager needs to see which of the two is on the table.
#pragma once

#include <string>
#include <vector>

#include "quorum/model.hpp"
#include "quorum/portfolio.hpp"

namespace quorum::sensitivity {

enum class Method { Ahp, Topsis };

const char* name_of(Method m);

// Scores every project with the given criteria weights and method.
std::vector<double> score(const model::Scenario& sc, const std::vector<double>& weights,
                          Method method);

// One run of the pipeline under one set of assumptions.
struct Point {
    std::string label;             // what was varied, in words
    std::vector<double> weights;
    double budget = 0.0;
    std::vector<std::size_t> selected;
    double value = 0.0;
};

enum class Verdict { Always, Sometimes, Never };

struct Row {
    std::string project_id;
    std::size_t times_selected = 0;
    double frequency = 0.0;
    Verdict verdict = Verdict::Never;
};

struct Result {
    std::vector<Point> points;
    std::vector<Row> rows;
    double seconds = 0.0;
};

const char* name_of(Verdict v);

// Multiplies one criterion weight at a time by each factor, renormalises, and
// reruns scoring and selection. Varying one weight at a time keeps every run
// attributable to a single assumption, which a joint sweep would not.
Result sweep_weights(const model::Scenario& sc, const std::vector<double>& base_weights,
                     const std::vector<double>& factors, Method method);

// Reruns selection over a range of budgets, expressed as multiples of the
// scenario budget. The scores do not change here, only the constraint.
Result sweep_budget(const model::Scenario& sc, const std::vector<double>& weights,
                    const std::vector<double>& factors, Method method);

}  // namespace quorum::sensitivity
