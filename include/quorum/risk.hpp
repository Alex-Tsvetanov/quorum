// Monte Carlo over the cost and duration estimates of a selected portfolio.
//
// The three-point estimates in the scenario describe uncertainty that the
// expected value throws away. Sampling them and rerunning the schedule turns the
// single number back into a distribution, which is what a confidence interval on
// the total cost and on the completion date needs.
#pragma once

#include <cstdint>
#include <vector>

#include "quorum/model.hpp"

namespace quorum::risk {

struct Summary {
    double mean = 0.0;
    double std_dev = 0.0;
    double minimum = 0.0;
    double p05 = 0.0;
    double p50 = 0.0;
    double p95 = 0.0;
    double maximum = 0.0;
};

struct Result {
    Summary cost;
    Summary completion;
    std::size_t samples = 0;
    double seconds = 0.0;
    std::vector<double> cost_samples;
    std::vector<double> completion_samples;

    // Share of samples whose total cost stays within the limit.
    double cost_within(double limit) const;
    // Share of samples whose programme finishes by the deadline.
    double finished_by(double deadline) const;
};

// The seed is fixed by default so a reported figure can be reproduced by
// rerunning the same command. Pass a different seed to check that a conclusion
// does not depend on the stream.
inline constexpr std::uint64_t kDefaultSeed = 20260819ULL;

Result simulate(const model::Scenario& sc, const std::vector<std::size_t>& selected,
                std::size_t samples, std::uint64_t seed = kDefaultSeed);

// Inverse transform sample of a triangular distribution on the three points.
// Exposed because the tests pin its quantiles, and because it is the one place
// where the shape assumption of the simulation lives.
double triangular(double optimistic, double likely, double pessimistic, double u);

}  // namespace quorum::risk
