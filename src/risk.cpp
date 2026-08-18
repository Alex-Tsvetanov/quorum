#include "quorum/risk.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <numeric>
#include <random>
#include <unordered_map>

namespace quorum::risk {
namespace {

// A network reduced to what the forward pass needs. The Monte Carlo loop runs
// the schedule once per project per sample, so the topological order and the
// predecessor lists are computed once and reused rather than rebuilt from
// identifiers on every draw. Only the makespan is wanted here, so the backward
// pass is not run at all.
struct Compiled {
    std::vector<std::size_t> order;
    std::vector<std::vector<std::size_t>> predecessors;
};

Compiled compile(const cpm::Network& net) {
    Compiled c;
    c.order = cpm::topological_order(net);
    std::unordered_map<std::string, std::size_t> index;
    for (std::size_t i = 0; i < net.tasks.size(); ++i) index.emplace(net.tasks[i].id, i);
    c.predecessors.resize(net.tasks.size());
    for (std::size_t i = 0; i < net.tasks.size(); ++i)
        for (const auto& dep : net.tasks[i].depends_on)
            c.predecessors[i].push_back(index.at(dep));
    return c;
}

double forward_makespan(const Compiled& c, const std::vector<double>& durations,
                        std::vector<double>& finish_scratch) {
    double makespan = 0.0;
    for (std::size_t v : c.order) {
        double start = 0.0;
        for (std::size_t p : c.predecessors[v]) start = std::max(start, finish_scratch[p]);
        finish_scratch[v] = start + durations[v];
        makespan = std::max(makespan, finish_scratch[v]);
    }
    return makespan;
}

double percentile(const std::vector<double>& sorted, double q) {
    if (sorted.empty()) return 0.0;
    const double position = q * static_cast<double>(sorted.size() - 1);
    const std::size_t lo = static_cast<std::size_t>(std::floor(position));
    const std::size_t hi = std::min(lo + 1, sorted.size() - 1);
    const double frac = position - static_cast<double>(lo);
    return sorted[lo] * (1.0 - frac) + sorted[hi] * frac;
}

Summary summarise(std::vector<double> samples) {
    Summary s;
    if (samples.empty()) return s;
    const double n = static_cast<double>(samples.size());
    s.mean = std::accumulate(samples.begin(), samples.end(), 0.0) / n;
    double variance = 0.0;
    for (double x : samples) variance += (x - s.mean) * (x - s.mean);
    s.std_dev = std::sqrt(variance / n);
    std::sort(samples.begin(), samples.end());
    s.minimum = samples.front();
    s.maximum = samples.back();
    s.p05 = percentile(samples, 0.05);
    s.p50 = percentile(samples, 0.50);
    s.p95 = percentile(samples, 0.95);
    return s;
}

double share_at_most(const std::vector<double>& samples, double limit) {
    if (samples.empty()) return 0.0;
    const std::size_t count = static_cast<std::size_t>(
        std::count_if(samples.begin(), samples.end(), [&](double x) { return x <= limit; }));
    return static_cast<double>(count) / static_cast<double>(samples.size());
}

}  // namespace

double Result::cost_within(double limit) const { return share_at_most(cost_samples, limit); }
double Result::finished_by(double deadline) const {
    return share_at_most(completion_samples, deadline);
}

double triangular(double optimistic, double likely, double pessimistic, double u) {
    const double span = pessimistic - optimistic;
    if (span <= 0.0) return optimistic;
    u = std::clamp(u, 0.0, 1.0);
    const double split = (likely - optimistic) / span;
    if (u < split) return optimistic + std::sqrt(u * span * (likely - optimistic));
    return pessimistic - std::sqrt((1.0 - u) * span * (pessimistic - likely));
}

Result simulate(const model::Scenario& sc, const std::vector<std::size_t>& selected,
                std::size_t samples, std::uint64_t seed) {
    const auto start = std::chrono::steady_clock::now();

    // The triangular distribution is used rather than the beta that the PERT
    // expected value assumes. It is defined by exactly the three numbers the
    // scenario already carries, it has no shape parameter to invent, and its
    // inverse is closed form. The cost is that its variance is slightly larger
    // than the PERT one, so the interval reported here is a little wider than
    // the analytic PERT interval on the same input.
    Result out;
    out.samples = samples;
    out.cost_samples.reserve(samples);
    out.completion_samples.reserve(samples);
    if (selected.empty() || samples == 0) {
        out.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        return out;
    }

    const cpm::Network programme = model::programme_network(sc, selected);
    const Compiled programme_compiled = compile(programme);

    std::vector<Compiled> project_compiled;
    std::vector<cpm::Network> project_networks;
    project_compiled.reserve(selected.size());
    project_networks.reserve(selected.size());
    for (std::size_t i : selected) {
        project_networks.push_back(sc.projects[i].network());
        project_compiled.push_back(compile(project_networks.back()));
    }

    std::mt19937_64 rng(seed);
    std::uniform_real_distribution<double> uniform(0.0, 1.0);

    std::vector<double> project_durations(selected.size(), 0.0);
    std::vector<double> programme_finish(selected.size(), 0.0);
    std::vector<double> activity_durations;
    std::vector<double> activity_finish;

    for (std::size_t s = 0; s < samples; ++s) {
        double total_cost = 0.0;
        for (std::size_t k = 0; k < selected.size(); ++k) {
            const model::Project& p = sc.projects[selected[k]];
            activity_durations.assign(p.activities.size(), 0.0);
            activity_finish.assign(p.activities.size(), 0.0);
            for (std::size_t a = 0; a < p.activities.size(); ++a) {
                const model::Activity& act = p.activities[a];
                activity_durations[a] =
                    triangular(act.optimistic, act.likely, act.pessimistic, uniform(rng));
                total_cost += triangular(act.cost_optimistic, act.cost_likely,
                                         act.cost_pessimistic, uniform(rng));
            }
            project_durations[k] =
                forward_makespan(project_compiled[k], activity_durations, activity_finish);
        }
        out.cost_samples.push_back(total_cost);
        out.completion_samples.push_back(
            forward_makespan(programme_compiled, project_durations, programme_finish));
    }

    out.cost = summarise(out.cost_samples);
    out.completion = summarise(out.completion_samples);
    out.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    return out;
}

}  // namespace quorum::risk
