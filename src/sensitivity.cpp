#include "quorum/sensitivity.hpp"

#include <chrono>
#include <cstdio>
#include <numeric>

#include "quorum/mcdm.hpp"

namespace quorum::sensitivity {
namespace {

mcdm::Directions directions_of(const model::Scenario& sc) {
    mcdm::Directions dirs;
    dirs.reserve(sc.criteria.size());
    for (const auto& c : sc.criteria) dirs.push_back(c.maximise);
    return dirs;
}

std::string format(double x, int decimals) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.*f", decimals, x);
    return buf;
}

// Tallies how often each project appears across the runs and turns the count
// into a verdict.
void tally(const model::Scenario& sc, Result& result) {
    result.rows.clear();
    if (result.points.empty()) return;
    std::vector<std::size_t> count(sc.projects.size(), 0);
    for (const auto& p : result.points)
        for (std::size_t i : p.selected) ++count[i];

    const double runs = static_cast<double>(result.points.size());
    for (std::size_t i = 0; i < sc.projects.size(); ++i) {
        Row row;
        row.project_id = sc.projects[i].id;
        row.times_selected = count[i];
        row.frequency = static_cast<double>(count[i]) / runs;
        if (count[i] == result.points.size())
            row.verdict = Verdict::Always;
        else if (count[i] == 0)
            row.verdict = Verdict::Never;
        else
            row.verdict = Verdict::Sometimes;
        result.rows.push_back(std::move(row));
    }
}

std::vector<double> renormalised(std::vector<double> w) {
    const double sum = std::accumulate(w.begin(), w.end(), 0.0);
    if (sum > 0.0)
        for (double& x : w) x /= sum;
    return w;
}

}  // namespace

const char* name_of(Method m) { return m == Method::Ahp ? "AHP" : "TOPSIS"; }

const char* name_of(Verdict v) {
    switch (v) {
        case Verdict::Always: return "stable";
        case Verdict::Sometimes: return "knife-edge";
        default: return "never";
    }
}

std::vector<double> score(const model::Scenario& sc, const std::vector<double>& weights,
                          Method method) {
    const auto matrix = sc.score_matrix();
    const auto dirs = directions_of(sc);
    return method == Method::Ahp ? mcdm::ahp_scores(matrix, weights, dirs)
                                 : mcdm::topsis_scores(matrix, weights, dirs);
}

Result sweep_weights(const model::Scenario& sc, const std::vector<double>& base_weights,
                     const std::vector<double>& factors, Method method) {
    const auto start = std::chrono::steady_clock::now();
    Result result;

    for (std::size_t k = 0; k < base_weights.size(); ++k) {
        for (double f : factors) {
            std::vector<double> w = base_weights;
            w[k] *= f;
            w = renormalised(std::move(w));

            Point point;
            point.label = sc.criteria[k].id + " x" + format(f, 2);
            point.weights = w;
            point.budget = sc.budget;
            const auto values = score(sc, w, method);
            const auto instance = portfolio::make_instance(sc, values);
            const auto solution = portfolio::solve_exact(instance);
            point.selected = solution.selected;
            point.value = solution.value;
            result.points.push_back(std::move(point));
        }
    }

    tally(sc, result);
    result.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    return result;
}

Result sweep_budget(const model::Scenario& sc, const std::vector<double>& weights,
                    const std::vector<double>& factors, Method method) {
    const auto start = std::chrono::steady_clock::now();
    Result result;
    const auto values = score(sc, weights, method);

    for (double f : factors) {
        model::Scenario varied = sc;
        varied.budget = sc.budget * f;

        Point point;
        point.label = "budget x" + format(f, 2);
        point.weights = weights;
        point.budget = varied.budget;
        const auto instance = portfolio::make_instance(varied, values);
        const auto solution = portfolio::solve_exact(instance);
        point.selected = solution.selected;
        point.value = solution.value;
        result.points.push_back(std::move(point));
    }

    tally(sc, result);
    result.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    return result;
}

}  // namespace quorum::sensitivity
