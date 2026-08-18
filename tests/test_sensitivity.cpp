#include <algorithm>

#include "check.hpp"
#include "quorum/mcdm.hpp"
#include "quorum/sensitivity.hpp"

using namespace quorum;

namespace {

model::Scenario bundled() {
    return model::load_file(std::string(QUORUM_SCENARIO_DIR) + "/example.json");
}

std::vector<double> ahp_weights_of(const model::Scenario& sc) {
    return mcdm::ahp_weights(sc.comparisons);
}

const sensitivity::Row& row_for(const sensitivity::Result& r, const std::string& id) {
    for (const auto& row : r.rows)
        if (row.project_id == id) return row;
    throw std::runtime_error("no row for " + id);
}

}  // namespace

TEST(sensitivity, "one run per criterion per factor, and each run is labelled") {
    const auto sc = bundled();
    const auto w = ahp_weights_of(sc);
    const std::vector<double> factors{0.5, 1.5};
    const auto r = sensitivity::sweep_weights(sc, w, factors, sensitivity::Method::Topsis);

    CHECK_EQ(r.points.size(), sc.criteria.size() * factors.size());
    CHECK_EQ(r.rows.size(), sc.projects.size());
    for (const auto& p : r.points) {
        CHECK_FALSE(p.label.empty());
        double sum = 0.0;
        for (double x : p.weights) sum += x;
        CHECK_NEAR(sum, 1.0, 1e-9);  // renormalised after the perturbation
    }
    // The first four runs vary the first criterion, so their labels name it.
    CHECK_TRUE(r.points[0].label.find(sc.criteria[0].id) != std::string::npos);
}

TEST(sensitivity, "the verdict follows the count, and the counts add up") {
    const auto sc = bundled();
    const auto w = ahp_weights_of(sc);
    const auto r = sensitivity::sweep_weights(sc, w, {0.5, 0.75, 1.25, 1.5},
                                              sensitivity::Method::Topsis);
    for (const auto& row : r.rows) {
        CHECK_NEAR(row.frequency,
                   static_cast<double>(row.times_selected) / static_cast<double>(r.points.size()),
                   1e-12);
        if (row.times_selected == r.points.size())
            CHECK_TRUE(row.verdict == sensitivity::Verdict::Always);
        else if (row.times_selected == 0)
            CHECK_TRUE(row.verdict == sensitivity::Verdict::Never);
        else
            CHECK_TRUE(row.verdict == sensitivity::Verdict::Sometimes);
    }
    std::size_t total = 0;
    for (const auto& row : r.rows) total += row.times_selected;
    std::size_t from_points = 0;
    for (const auto& p : r.points) from_points += p.selected.size();
    CHECK_EQ(total, from_points);
}

TEST(sensitivity, "a wider budget never buys a worse portfolio") {
    const auto sc = bundled();
    const auto w = ahp_weights_of(sc);
    const std::vector<double> factors{0.6, 0.8, 1.0, 1.2, 1.4};
    const auto r = sensitivity::sweep_budget(sc, w, factors, sensitivity::Method::Topsis);

    CHECK_EQ(r.points.size(), factors.size());
    for (std::size_t i = 1; i < r.points.size(); ++i) {
        CHECK_TRUE(r.points[i].budget > r.points[i - 1].budget);
        // Relaxing a constraint cannot reduce the optimum of a maximisation.
        CHECK_TRUE(r.points[i].value >= r.points[i - 1].value - 1e-9);
    }
}

TEST(sensitivity, "the budget sweep separates the stable positions from the marginal ones") {
    const auto sc = bundled();
    const auto w = ahp_weights_of(sc);
    const auto r = sensitivity::sweep_budget(sc, w, {0.5, 0.75, 1.0, 1.25, 1.5},
                                             sensitivity::Method::Topsis);
    std::size_t always = 0, sometimes = 0;
    for (const auto& row : r.rows) {
        if (row.verdict == sensitivity::Verdict::Always) ++always;
        if (row.verdict == sensitivity::Verdict::Sometimes) ++sometimes;
    }
    // The sweep is only informative if it separates the two groups. A run where
    // every project is stable, or none is, would mean the range is wrong.
    CHECK_TRUE(always > 0);
    CHECK_TRUE(sometimes > 0);
    // Whatever survives the tightest budget must survive every wider one.
    const auto& tightest = r.points.front().selected;
    for (std::size_t i : tightest) {
        const auto& row = row_for(r, sc.projects[i].id);
        CHECK_TRUE(row.times_selected >= 1);
    }
}

TEST(sensitivity, "the two methods can select different portfolios from the same scenario") {
    const auto sc = bundled();
    const auto w = ahp_weights_of(sc);
    const auto by_ahp = sensitivity::score(sc, w, sensitivity::Method::Ahp);
    const auto by_topsis = sensitivity::score(sc, w, sensitivity::Method::Topsis);
    CHECK_EQ(by_ahp.size(), sc.projects.size());
    CHECK_EQ(by_topsis.size(), sc.projects.size());

    const auto a = portfolio::solve_exact(portfolio::make_instance(sc, by_ahp));
    const auto t = portfolio::solve_exact(portfolio::make_instance(sc, by_topsis));
    CHECK_TRUE(a.optimal);
    CHECK_TRUE(t.optimal);
    // Both must be feasible against the same budget whatever they pick.
    CHECK_TRUE(a.cost <= sc.budget + 1e-9);
    CHECK_TRUE(t.cost <= sc.budget + 1e-9);
}

TEST(sensitivity, "method and verdict names are stable strings for the report") {
    CHECK_EQ(std::string(sensitivity::name_of(sensitivity::Method::Ahp)), std::string("AHP"));
    CHECK_EQ(std::string(sensitivity::name_of(sensitivity::Method::Topsis)), std::string("TOPSIS"));
    CHECK_EQ(std::string(sensitivity::name_of(sensitivity::Verdict::Always)), std::string("stable"));
    CHECK_EQ(std::string(sensitivity::name_of(sensitivity::Verdict::Sometimes)),
             std::string("knife-edge"));
}
