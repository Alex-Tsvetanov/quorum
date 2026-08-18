#include <algorithm>
#include <cmath>

#include "check.hpp"
#include "quorum/risk.hpp"

using namespace quorum;

namespace {

model::Scenario bundled() {
    return model::load_file(std::string(QUORUM_SCENARIO_DIR) + "/example.json");
}

}  // namespace

TEST(risk, "the triangular inverse hits the endpoints and the mode quantile") {
    CHECK_NEAR(risk::triangular(2, 5, 11, 0.0), 2.0, 1e-12);
    CHECK_NEAR(risk::triangular(2, 5, 11, 1.0), 11.0, 1e-12);
    // The distribution function at the mode is (m - o) / (p - o), so drawing
    // exactly that uniform value must return the mode.
    CHECK_NEAR(risk::triangular(2, 5, 11, 3.0 / 9.0), 5.0, 1e-9);
    // Monotone in the uniform draw.
    double previous = -1.0;
    for (int k = 0; k <= 100; ++k) {
        const double x = risk::triangular(2, 5, 11, k / 100.0);
        CHECK_TRUE(x >= previous);
        previous = x;
    }
    // A degenerate estimate has no spread to sample.
    CHECK_NEAR(risk::triangular(4, 4, 4, 0.37), 4.0, 1e-12);
}

TEST(risk, "the sampled mean sits near the analytic triangular mean") {
    // The mean of a triangular distribution is the average of its three points.
    // Ten thousand draws put the sample mean within a few thousandths of it.
    const double o = 2.0, m = 5.0, p = 11.0;
    const double analytic = (o + m + p) / 3.0;
    double sum = 0.0;
    const int n = 200000;
    for (int k = 0; k < n; ++k)
        sum += risk::triangular(o, m, p, (k + 0.5) / static_cast<double>(n));
    CHECK_NEAR(sum / n, analytic, 0.01);
}

TEST(risk, "the simulation is reproducible with a fixed seed and moves with a new one") {
    const auto sc = bundled();
    const std::vector<std::size_t> selected{0, 1, 2};
    const auto a = risk::simulate(sc, selected, 2000);
    const auto b = risk::simulate(sc, selected, 2000);
    CHECK_NEAR(a.cost.mean, b.cost.mean, 1e-12);
    CHECK_NEAR(a.completion.p95, b.completion.p95, 1e-12);

    const auto c = risk::simulate(sc, selected, 2000, risk::kDefaultSeed + 1);
    CHECK_TRUE(std::fabs(c.cost.mean - a.cost.mean) > 1e-9);
}

TEST(risk, "the percentiles are ordered and bracket the median") {
    const auto sc = bundled();
    const auto r = risk::simulate(sc, {0, 1, 2, 3}, 5000);
    CHECK_EQ(r.samples, std::size_t{5000});
    CHECK_TRUE(r.cost.minimum <= r.cost.p05);
    CHECK_TRUE(r.cost.p05 < r.cost.p50);
    CHECK_TRUE(r.cost.p50 < r.cost.p95);
    CHECK_TRUE(r.cost.p95 <= r.cost.maximum);
    CHECK_TRUE(r.completion.p05 <= r.completion.p50);
    CHECK_TRUE(r.completion.p50 <= r.completion.p95);
    CHECK_TRUE(r.cost.std_dev > 0.0);
}

TEST(risk, "the sampled cost centres on the sum of the expected costs") {
    // The PERT expected value weights the mode by four, the triangular mean
    // weights all three points equally, so the two differ whenever the estimate
    // is skewed. They stay within a few per cent on this scenario, which is the
    // check that the simulation samples the same estimates the schedule uses.
    const auto sc = bundled();
    const std::vector<std::size_t> selected{0, 2, 6};
    double deterministic = 0.0;
    for (std::size_t i : selected) deterministic += sc.projects[i].expected_cost;

    const auto r = risk::simulate(sc, selected, 20000);
    CHECK_TRUE(std::fabs(r.cost.mean - deterministic) / deterministic < 0.05);
}

TEST(risk, "the sampled completion respects the programme dependencies") {
    const auto sc = bundled();
    // P01 then P02: the programme has to be at least as long as the sum of the
    // two project durations, not the longer of the two.
    const std::size_t p01 = sc.project_index("P01"), p02 = sc.project_index("P02");
    const auto chained = risk::simulate(sc, {p01, p02}, 4000);
    const auto alone = risk::simulate(sc, {p01}, 4000);
    CHECK_TRUE(chained.completion.p50 > alone.completion.p50);
    CHECK_TRUE(chained.completion.p50 >=
               sc.projects[p01].expected_duration + sc.projects[p02].expected_duration - 2.0);
}

TEST(risk, "the tail probabilities agree with the sample shares") {
    const auto sc = bundled();
    const auto r = risk::simulate(sc, {0, 1, 2}, 5000);
    CHECK_NEAR(r.cost_within(r.cost.p50), 0.5, 0.02);
    CHECK_NEAR(r.finished_by(r.completion.p95), 0.95, 0.02);
    CHECK_NEAR(r.cost_within(r.cost.minimum - 1.0), 0.0, 1e-12);
    CHECK_NEAR(r.cost_within(r.cost.maximum + 1.0), 1.0, 1e-12);
}

TEST(risk, "an empty portfolio or a zero sample count returns an empty result") {
    const auto sc = bundled();
    const auto none = risk::simulate(sc, {}, 100);
    CHECK_EQ(none.cost_samples.size(), std::size_t{0});
    CHECK_NEAR(none.cost.mean, 0.0, 1e-12);
    const auto zero = risk::simulate(sc, {0}, 0);
    CHECK_EQ(zero.cost_samples.size(), std::size_t{0});
}
