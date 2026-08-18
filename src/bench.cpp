// The measurements quoted in the report.
//
// Two questions, both answered by running the code rather than by reasoning
// about it. What does the exact search buy over the heuristic, in solution
// quality and in time, as the instance grows. And how many Monte Carlo samples
// are needed before the reported interval stops moving.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <numeric>
#include <random>
#include <string>
#include <vector>

#include "quorum/report.hpp"

namespace {

using namespace quorum;

constexpr std::int64_t kNodeLimit = 40'000'000;

// A synthetic instance with the same shape as a real one: costs and values of
// mixed density, two capacity dimensions, a sprinkling of prerequisites and a
// few mutual exclusions. Prerequisites always point at a lower index, so the
// natural order is already a valid funding order.
portfolio::Instance generate(std::size_t n, std::uint64_t seed) {
    std::mt19937_64 rng(seed);
    std::uniform_real_distribution<double> unit(0.0, 1.0);

    portfolio::Instance inst;
    inst.value.resize(n);
    inst.cost.resize(n);
    inst.usage.assign(2, std::vector<double>(n, 0.0));
    inst.prerequisites.assign(n, {});
    inst.exclusions.assign(n, {});
    inst.order.resize(n);
    std::iota(inst.order.begin(), inst.order.end(), std::size_t{0});

    double total_cost = 0.0;
    std::vector<double> total_usage(2, 0.0);
    for (std::size_t i = 0; i < n; ++i) {
        inst.value[i] = 0.02 + 0.18 * unit(rng);
        inst.cost[i] = 50.0 + 350.0 * unit(rng);
        total_cost += inst.cost[i];
        for (std::size_t r = 0; r < 2; ++r) {
            inst.usage[r][i] = 1.0 + 9.0 * unit(rng);
            total_usage[r] += inst.usage[r][i];
        }
        if (i > 0 && unit(rng) < 0.25) {
            const std::size_t dep = static_cast<std::size_t>(unit(rng) * static_cast<double>(i));
            inst.prerequisites[i].push_back(std::min(dep, i - 1));
        }
    }
    inst.budget = 0.40 * total_cost;
    inst.capacity = {0.40 * total_usage[0], 0.40 * total_usage[1]};

    for (std::size_t k = 0; k < n / 8; ++k) {
        const std::size_t a = static_cast<std::size_t>(unit(rng) * static_cast<double>(n));
        const std::size_t b = static_cast<std::size_t>(unit(rng) * static_cast<double>(n));
        if (a == b || a >= n || b >= n) continue;
        // An exclusion between a project and its own prerequisite would make the
        // dependant unfundable, which is a modelling error rather than an
        // instance, so those pairs are skipped.
        const auto& pre = inst.prerequisites[std::max(a, b)];
        if (std::find(pre.begin(), pre.end(), std::min(a, b)) != pre.end()) continue;
        inst.exclusions[a].push_back(b);
        inst.exclusions[b].push_back(a);
    }
    return inst;
}

// Exhaustive enumeration over every subset. Deliberately written out here rather
// than shared with the search: an independent answer is only independent if it
// shares no code with the thing it is checking.
double brute_force(const portfolio::Instance& inst) {
    const std::size_t n = inst.size();
    double best = 0.0;
    for (std::uint64_t mask = 0; mask < (std::uint64_t{1} << n); ++mask) {
        double value = 0.0, cost = 0.0;
        std::vector<double> used(inst.capacity.size(), 0.0);
        bool feasible = true;
        for (std::size_t i = 0; i < n && feasible; ++i) {
            if (!((mask >> i) & 1)) continue;
            value += inst.value[i];
            cost += inst.cost[i];
            for (std::size_t r = 0; r < inst.capacity.size(); ++r) used[r] += inst.usage[r][i];
            for (std::size_t p : inst.prerequisites[i])
                if (!((mask >> p) & 1)) feasible = false;
            for (std::size_t x : inst.exclusions[i])
                if ((mask >> x) & 1) feasible = false;
        }
        if (!feasible || cost > inst.budget + 1e-9) continue;
        for (std::size_t r = 0; r < inst.capacity.size(); ++r)
            if (used[r] > inst.capacity[r] + 1e-9) feasible = false;
        if (feasible) best = std::max(best, value);
    }
    return best;
}

void bench_correctness(std::ostream& os) {
    report::heading(os, "Branch and bound against exhaustive enumeration");
    os << "  twenty instances per size, every subset enumerated for the reference answer\n\n";

    report::Table t({"projects", "instances", "matched", "search ms", "enumeration ms",
                     "speed-up"});
    for (std::size_t n : {8u, 12u, 16u, 18u}) {
        constexpr int kInstances = 20;
        int matched = 0;
        double search_ms = 0.0, brute_ms = 0.0;
        for (int rep = 0; rep < kInstances; ++rep) {
            const auto inst = generate(n, 90000 + 31 * n + static_cast<std::uint64_t>(rep));
            const auto exact = portfolio::solve_exact(inst);
            const auto start = std::chrono::steady_clock::now();
            const double reference = brute_force(inst);
            brute_ms +=
                std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count() *
                1000.0;
            search_ms += exact.seconds * 1000.0;
            if (std::fabs(exact.value - reference) < 1e-9) ++matched;
        }
        t.row({std::to_string(n), std::to_string(kInstances),
               std::to_string(matched) + "/" + std::to_string(kInstances),
               report::number(search_ms / kInstances, 4),
               report::number(brute_ms / kInstances, 3),
               report::number(brute_ms / std::max(search_ms, 1e-9), 1) + "x"});
    }
    t.write(os);
}

void bench_selection(std::ostream& os) {
    report::heading(os, "Branch and bound against the greedy heuristic");
    os << "  five instances per size, two capacity dimensions, budget at 40 per cent of the ask\n\n";

    report::Table t({"projects", "exact value", "greedy value", "greedy of optimum", "nodes",
                     "exact ms", "greedy ms", "proved"});
    for (std::size_t n : {10u, 14u, 18u, 22u, 26u, 30u, 34u, 38u}) {
        double exact_value = 0.0, greedy_value = 0.0, exact_ms = 0.0, greedy_ms = 0.0;
        double nodes = 0.0, ratio = 0.0;
        int proved = 0;
        constexpr int kRepeats = 5;
        for (int rep = 0; rep < kRepeats; ++rep) {
            const auto inst = generate(n, 1000 + 17 * n + static_cast<std::uint64_t>(rep));
            const auto exact = portfolio::solve_exact(inst, kNodeLimit);
            const auto greedy = portfolio::solve_greedy(inst);
            exact_value += exact.value;
            greedy_value += greedy.value;
            nodes += static_cast<double>(exact.nodes);
            exact_ms += exact.seconds * 1000.0;
            greedy_ms += greedy.seconds * 1000.0;
            ratio += exact.value > 0.0 ? greedy.value / exact.value : 1.0;
            proved += exact.optimal ? 1 : 0;
        }
        const double r = kRepeats;
        t.row({std::to_string(n), report::number(exact_value / r, 4),
               report::number(greedy_value / r, 4),
               report::number(100.0 * ratio / r, 2) + " %",
               report::number(nodes / r, 0), report::number(exact_ms / r, 3),
               report::number(greedy_ms / r, 3), std::to_string(proved) + "/5"});
    }
    t.write(os);
}

void bench_montecarlo(std::ostream& os, const std::string& path) {
    const model::Scenario sc = model::load_file(path);
    std::vector<double> weights;
    if (!sc.comparisons.empty())
        weights = mcdm::ahp_weights(sc.comparisons);
    else
        for (const auto& c : sc.criteria) weights.push_back(c.weight);

    const auto values = sensitivity::score(sc, weights, sensitivity::Method::Topsis);
    const auto instance = portfolio::make_instance(sc, values);
    const auto exact = portfolio::solve_exact(instance);

    report::heading(os, "Monte Carlo convergence");
    os << "  scenario " << sc.name << ", " << exact.selected.size()
       << " funded projects, fixed seed\n\n";

    report::Table t({"samples", "mean cost", "p05 cost", "p95 cost", "p95 completion", "ms"});
    for (std::size_t n : {100u, 1000u, 10000u, 100000u, 1000000u}) {
        const auto r = risk::simulate(sc, exact.selected, n);
        t.row({std::to_string(n), report::number(r.cost.mean, 1), report::number(r.cost.p05, 1),
               report::number(r.cost.p95, 1), report::number(r.completion.p95, 3),
               report::number(r.seconds * 1000.0, 1)});
    }
    t.write(os);

    // Spread across independent streams at one sample count: the width of this
    // spread is the sampling error of the reported figure, which a single run
    // cannot show.
    report::heading(os, "Sampling error across seeds");
    report::Table s({"samples", "p95 cost, ten seeds", "spread", "relative spread"});
    for (std::size_t n : {1000u, 10000u, 100000u}) {
        double lo = 0.0, hi = 0.0, sum = 0.0;
        for (int k = 0; k < 10; ++k) {
            const auto r = risk::simulate(sc, exact.selected, n, 7000 + static_cast<std::uint64_t>(k));
            const double p = r.cost.p95;
            if (k == 0) {
                lo = hi = p;
            } else {
                lo = std::min(lo, p);
                hi = std::max(hi, p);
            }
            sum += p;
        }
        const double mean = sum / 10.0;
        s.row({std::to_string(n), report::number(mean, 2), report::number(hi - lo, 2),
               report::number(100.0 * (hi - lo) / mean, 3) + " %"});
    }
    s.write(os);
}

}  // namespace

int run_bench(const std::vector<std::string>& args, std::ostream& os) {
    std::string path = "scenarios/example.json";
    for (std::size_t i = 0; i + 1 < args.size(); ++i)
        if (args[i] == "--scenario") path = args[i + 1];

    os << "Quorum benchmarks. Every number below was produced by this run.\n";
    bench_correctness(os);
    bench_selection(os);
    bench_montecarlo(os, path);
    os << "\n";
    return 0;
}
