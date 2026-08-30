// One run of the pipeline that both the command line and the web handler drive.
// Scoring, selection, the programme schedule and the Monte Carlo pass live here
// so a second language cannot invent a second answer.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "quorum/cpm.hpp"
#include "quorum/json.hpp"
#include "quorum/mcdm.hpp"
#include "quorum/model.hpp"
#include "quorum/portfolio.hpp"
#include "quorum/risk.hpp"
#include "quorum/sensitivity.hpp"

namespace quorum::analyse {

struct Options {
    sensitivity::Method method = sensitivity::Method::Topsis;
    // Zero skips the Monte Carlo pass, which is what `quorum solve` wants.
    std::size_t risk_samples = 0;
};

struct Result {
    sensitivity::Method method = sensitivity::Method::Topsis;
    model::Scenario scenario;
    mcdm::AhpResult ahp;
    std::vector<double> weights;
    std::vector<double> ahp_scores;
    std::vector<double> topsis_scores;
    portfolio::Instance instance;
    portfolio::Solution exact;
    portfolio::Solution greedy;
    cpm::Schedule programme;
    risk::Result monte_carlo;
};

Result run(model::Scenario scenario, const Options& options);
Result run_file(const std::string& path, const Options& options);
Result run_text(const std::string& text, const Options& options);

json::Value to_json(const Result& result);
std::string to_json_text(const Result& result);

sensitivity::Method method_of(const std::string& name);

}  // namespace quorum::analyse
