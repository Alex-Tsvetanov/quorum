#include <algorithm>
#include <random>

#include "check.hpp"
#include "quorum/portfolio.hpp"

using namespace quorum;

namespace {

// Exhaustive enumeration over every subset. Only usable for small instances,
// which is exactly the point: it is the independent answer the search is checked
// against, and it shares no code with the search.
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

portfolio::Instance random_instance(std::size_t n, std::uint64_t seed) {
    std::mt19937_64 rng(seed);
    std::uniform_real_distribution<double> unit(0.0, 1.0);
    portfolio::Instance inst;
    inst.value.resize(n);
    inst.cost.resize(n);
    inst.usage.assign(1, std::vector<double>(n, 0.0));
    inst.prerequisites.assign(n, {});
    inst.exclusions.assign(n, {});
    inst.order.resize(n);
    for (std::size_t i = 0; i < n; ++i) inst.order[i] = i;

    double total_cost = 0.0, total_usage = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        inst.value[i] = 0.05 + unit(rng);
        inst.cost[i] = 10.0 + 90.0 * unit(rng);
        inst.usage[0][i] = 1.0 + 5.0 * unit(rng);
        total_cost += inst.cost[i];
        total_usage += inst.usage[0][i];
        if (i > 0 && unit(rng) < 0.3)
            inst.prerequisites[i].push_back(
                std::min(static_cast<std::size_t>(unit(rng) * static_cast<double>(i)), i - 1));
    }
    inst.budget = 0.45 * total_cost;
    inst.capacity = {0.55 * total_usage};
    if (n >= 4) {
        // One exclusion, placed where it cannot contradict a prerequisite.
        const auto& pre = inst.prerequisites[n - 1];
        if (std::find(pre.begin(), pre.end(), std::size_t{0}) == pre.end()) {
            inst.exclusions[0].push_back(n - 1);
            inst.exclusions[n - 1].push_back(0);
        }
    }
    return inst;
}

bool feasible(const portfolio::Instance& inst, const portfolio::Solution& sol) {
    if (sol.cost > inst.budget + 1e-9) return false;
    for (std::size_t r = 0; r < inst.capacity.size(); ++r)
        if (sol.used[r] > inst.capacity[r] + 1e-9) return false;
    for (std::size_t i = 0; i < inst.size(); ++i) {
        if (!sol.chosen[i]) continue;
        for (std::size_t p : inst.prerequisites[i])
            if (!sol.chosen[p]) return false;
        for (std::size_t x : inst.exclusions[i])
            if (sol.chosen[x]) return false;
    }
    return true;
}

}  // namespace

TEST(portfolio, "the search matches exhaustive enumeration on small instances") {
    for (std::uint64_t seed = 1; seed <= 40; ++seed) {
        const auto inst = random_instance(12, seed);
        const auto exact = portfolio::solve_exact(inst);
        CHECK_TRUE(exact.optimal);
        CHECK_NEAR(exact.value, brute_force(inst), 1e-9);
    }
}

TEST(portfolio, "the returned portfolio satisfies every constraint") {
    for (std::uint64_t seed = 1; seed <= 40; ++seed) {
        const auto inst = random_instance(14, 500 + seed);
        CHECK_TRUE(feasible(inst, portfolio::solve_exact(inst)));
        CHECK_TRUE(feasible(inst, portfolio::solve_greedy(inst)));
    }
}

TEST(portfolio, "the heuristic never beats the exact search") {
    // The validity condition of the whole experiment. A greedy result above the
    // exact one means the model is wrong, not that the solver is slow.
    for (std::uint64_t seed = 1; seed <= 60; ++seed) {
        const auto inst = random_instance(16, 900 + seed);
        const auto exact = portfolio::solve_exact(inst);
        const auto greedy = portfolio::solve_greedy(inst);
        CHECK_TRUE(greedy.value <= exact.value + 1e-9);
    }
}

TEST(portfolio, "a prerequisite that does not fit keeps its dependant out") {
    portfolio::Instance inst;
    inst.value = {1.0, 10.0};      // the dependant is worth ten times the prerequisite
    inst.cost = {80.0, 30.0};      // together they cost 110 against a budget of 100
    inst.budget = 100.0;
    inst.usage.clear();
    inst.capacity.clear();
    inst.prerequisites = {{}, {0}};
    inst.exclusions = {{}, {}};
    inst.order = {0, 1};

    const auto sol = portfolio::solve_exact(inst);
    CHECK_TRUE(sol.optimal);
    CHECK_FALSE(sol.chosen[1]);
    CHECK_TRUE(sol.chosen[0]);
    CHECK_NEAR(sol.value, 1.0, 1e-9);
}

TEST(portfolio, "a mutual exclusion admits the better of the pair and no more") {
    portfolio::Instance inst;
    inst.value = {4.0, 7.0};
    inst.cost = {10.0, 10.0};
    inst.budget = 100.0;
    inst.usage.clear();
    inst.capacity.clear();
    inst.prerequisites = {{}, {}};
    inst.exclusions = {{1}, {0}};
    inst.order = {0, 1};

    const auto sol = portfolio::solve_exact(inst);
    CHECK_EQ(sol.selected.size(), std::size_t{1});
    CHECK_TRUE(sol.chosen[1]);
    CHECK_NEAR(sol.value, 7.0, 1e-9);
}

TEST(portfolio, "a capacity limit binds independently of the budget") {
    portfolio::Instance inst;
    inst.value = {5.0, 5.0, 5.0};
    inst.cost = {1.0, 1.0, 1.0};        // the budget is nowhere near binding
    inst.budget = 1000.0;
    inst.usage = {{6.0, 6.0, 6.0}};
    inst.capacity = {13.0};             // room for two, not three
    inst.prerequisites = {{}, {}, {}};
    inst.exclusions = {{}, {}, {}};
    inst.order = {0, 1, 2};

    const auto sol = portfolio::solve_exact(inst);
    CHECK_EQ(sol.selected.size(), std::size_t{2});
    CHECK_NEAR(sol.value, 10.0, 1e-9);
}

TEST(portfolio, "the search stops and says so when the node limit is reached") {
    const auto inst = random_instance(30, 4242);
    const auto stopped = portfolio::solve_exact(inst, 50);
    CHECK_FALSE(stopped.optimal);
    CHECK_TRUE(stopped.nodes <= 51);
    const auto complete = portfolio::solve_exact(inst);
    CHECK_TRUE(complete.optimal);
    CHECK_TRUE(complete.value >= stopped.value - 1e-9);
}

TEST(portfolio, "the instance built from a scenario carries costs, demand and dependencies") {
    const auto sc = model::load_file(std::string(QUORUM_SCENARIO_DIR) + "/example.json");
    std::vector<double> value(sc.projects.size(), 1.0);
    const auto inst = portfolio::make_instance(sc, value);

    CHECK_EQ(inst.size(), sc.projects.size());
    CHECK_NEAR(inst.budget, sc.budget, 1e-12);
    CHECK_EQ(inst.capacity.size(), sc.resources.size());
    for (std::size_t i = 0; i < sc.projects.size(); ++i) {
        CHECK_NEAR(inst.cost[i], sc.projects[i].expected_cost, 1e-9);
        for (std::size_t r = 0; r < sc.resources.size(); ++r)
            CHECK_NEAR(inst.usage[r][i], sc.projects[i].peak_demand[r], 1e-9);
    }
    // P02 requires P01, and P05 and P06 exclude each other. The exclusion is
    // recorded on both sides whatever the file says.
    const std::size_t p01 = sc.project_index("P01"), p02 = sc.project_index("P02");
    const std::size_t p05 = sc.project_index("P05"), p06 = sc.project_index("P06");
    CHECK_EQ(inst.prerequisites[p02].size(), std::size_t{1});
    CHECK_EQ(inst.prerequisites[p02][0], p01);
    CHECK_TRUE(std::find(inst.exclusions[p05].begin(), inst.exclusions[p05].end(), p06) !=
               inst.exclusions[p05].end());
    CHECK_TRUE(std::find(inst.exclusions[p06].begin(), inst.exclusions[p06].end(), p05) !=
               inst.exclusions[p06].end());

    CHECK_THROWS(portfolio::make_instance(sc, {1.0}), model::ModelError);
}

TEST(portfolio, "a rejected project comes back with the constraint that rejected it") {
    const auto sc = model::load_file(std::string(QUORUM_SCENARIO_DIR) + "/example.json");
    // Value everything equally and starve the budget, so only the cheapest
    // handful fit and every rejection has a stated cause.
    model::Scenario poor = sc;
    poor.budget = 120.0;
    const std::vector<double> value(sc.projects.size(), 1.0);
    const auto inst = portfolio::make_instance(poor, value);
    const auto sol = portfolio::solve_exact(inst);

    for (std::size_t i = 0; i < poor.projects.size(); ++i) {
        const std::string reason = portfolio::rejection_reason(inst, poor, sol, i);
        if (sol.chosen[i])
            CHECK_TRUE(reason.empty());
        else
            CHECK_FALSE(reason.empty());
    }
    // P02 cannot be funded while P01 is not, and the message has to say so.
    const std::size_t p02 = poor.project_index("P02");
    if (!sol.chosen[p02] && !sol.chosen[poor.project_index("P01")]) {
        const std::string reason = portfolio::rejection_reason(inst, poor, sol, p02);
        CHECK_TRUE(reason.find("P01") != std::string::npos);
    }
}
