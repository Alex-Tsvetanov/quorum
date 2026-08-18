#include "quorum/portfolio.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <limits>
#include <numeric>
#include <set>

namespace quorum::portfolio {
namespace {

// Tolerance on the bound comparison. The scores are relative closeness values in
// the unit interval, so absolute epsilon at this scale is safe and avoids
// pruning the optimum away on a rounding difference.
constexpr double kEpsilon = 1e-9;

std::string rounded(double x, int decimals) {
    char buf[48];
    std::snprintf(buf, sizeof buf, "%.*f", decimals, x);
    return buf;
}

double now_seconds(std::chrono::steady_clock::time_point start) {
    const auto end = std::chrono::steady_clock::now();
    return std::chrono::duration<double>(end - start).count();
}

void finalise(Solution& sol, const Instance& inst) {
    sol.selected.clear();
    sol.value = 0.0;
    sol.cost = 0.0;
    sol.used.assign(inst.capacity.size(), 0.0);
    for (std::size_t i = 0; i < inst.size(); ++i) {
        if (!sol.chosen[i]) continue;
        sol.selected.push_back(i);
        sol.value += inst.value[i];
        sol.cost += inst.cost[i];
        for (std::size_t r = 0; r < inst.capacity.size(); ++r) sol.used[r] += inst.usage[r][i];
    }
}

// The search. Kept as a struct so the recursion carries state without a long
// parameter list, and so the node counter has one owner.
class Search {
public:
    Search(const Instance& inst, std::int64_t node_limit)
        : inst_(inst),
          limit_(node_limit),
          n_(inst.size()),
          position_(inst.size(), 0),
          chosen_(inst.size(), 0),
          best_(inst.size(), 0) {
        for (std::size_t p = 0; p < n_; ++p) position_[inst_.order[p]] = p;
        build_density_orders();
    }

    Solution run() {
        const auto start = std::chrono::steady_clock::now();
        std::vector<double> remaining = inst_.capacity;
        descend(0, 0.0, inst_.budget, remaining);

        Solution sol;
        sol.chosen = best_;
        sol.optimal = !aborted_;
        sol.nodes = nodes_;
        finalise(sol, inst_);
        sol.seconds = now_seconds(start);
        return sol;
    }

private:
    const Instance& inst_;
    std::int64_t limit_;
    std::size_t n_;
    std::vector<std::size_t> position_;   // project index -> position in the order
    std::vector<char> chosen_;
    std::vector<char> best_;
    double best_value_ = -1.0;
    std::int64_t nodes_ = 0;
    bool aborted_ = false;
    // For each constraint dimension, the projects sorted by decreasing value per
    // unit of that dimension. Dimension zero is the budget, the rest are the
    // resources. Computed once: sorting at every node would dominate the run.
    std::vector<std::vector<std::size_t>> density_order_;

    const std::vector<double>& dimension(std::size_t d) const {
        return d == 0 ? inst_.cost : inst_.usage[d - 1];
    }

    void build_density_orders() {
        const std::size_t dims = 1 + inst_.capacity.size();
        density_order_.resize(dims);
        for (std::size_t d = 0; d < dims; ++d) {
            const std::vector<double>& use = dimension(d);
            std::vector<std::size_t> idx(n_);
            std::iota(idx.begin(), idx.end(), std::size_t{0});
            std::stable_sort(idx.begin(), idx.end(), [&](std::size_t a, std::size_t b) {
                // Zero usage means unlimited density: such a project can be
                // taken in this dimension for free, so it sorts first.
                const bool za = use[a] <= 0.0, zb = use[b] <= 0.0;
                if (za != zb) return za;
                if (za && zb) return inst_.value[a] > inst_.value[b];
                return inst_.value[a] / use[a] > inst_.value[b] / use[b];
            });
            density_order_[d] = std::move(idx);
        }
    }

    // Upper bound on what the undecided projects can still add.
    //
    // For a single dimension the linear relaxation of a knapsack is solved by
    // filling in decreasing density order and taking a fraction of the item that
    // straddles the limit. Relaxing away every constraint but one gives a valid
    // bound, so the smallest such bound over the dimensions is also valid, and
    // it is tighter than any one of them. The prerequisite and exclusion
    // constraints are dropped as well, which only relaxes the problem further.
    //LSTBEGINbound
    double relaxation_bound(std::size_t from, double budget_left,
                            const std::vector<double>& capacity_left) const {
        double best = std::numeric_limits<double>::infinity();
        const std::size_t dims = 1 + inst_.capacity.size();
        for (std::size_t d = 0; d < dims; ++d) {
            const std::vector<double>& use = dimension(d);
            double room = d == 0 ? budget_left : capacity_left[d - 1];
            double total = 0.0;
            for (std::size_t i : density_order_[d]) {
                if (position_[i] < from) continue;  // already decided
                if (use[i] <= 0.0) {
                    total += inst_.value[i];
                    continue;
                }
                if (use[i] <= room) {
                    room -= use[i];
                    total += inst_.value[i];
                } else {
                    total += inst_.value[i] * room / use[i];
                    break;
                }
            }
            best = std::min(best, total);
        }
        return best;
    }
    //LSTENDbound

    bool can_include(std::size_t item, double budget_left,
                     const std::vector<double>& capacity_left) const {
        if (inst_.cost[item] > budget_left + kEpsilon) return false;
        for (std::size_t r = 0; r < inst_.capacity.size(); ++r)
            if (inst_.usage[r][item] > capacity_left[r] + kEpsilon) return false;
        // Prerequisites come earlier in the order, so they are already decided.
        for (std::size_t p : inst_.prerequisites[item])
            if (!chosen_[p]) return false;
        for (std::size_t x : inst_.exclusions[item])
            if (chosen_[x]) return false;
        return true;
    }

    void descend(std::size_t from, double value_so_far, double budget_left,
                 std::vector<double>& capacity_left) {
        if (aborted_) return;
        if (limit_ >= 0 && nodes_ >= limit_) {
            aborted_ = true;
            return;
        }
        ++nodes_;

        if (from == n_) {
            if (value_so_far > best_value_ + kEpsilon) {
                best_value_ = value_so_far;
                best_ = chosen_;
            }
            return;
        }
        if (value_so_far + relaxation_bound(from, budget_left, capacity_left) <
            best_value_ + kEpsilon)
            return;

        const std::size_t item = inst_.order[from];

        // Include first. A good incumbent found early makes the bound prune more
        // of what follows, and the order is by value density, so the first dive
        // is close to the greedy solution.
        if (can_include(item, budget_left, capacity_left)) {
            chosen_[item] = 1;
            for (std::size_t r = 0; r < inst_.capacity.size(); ++r)
                capacity_left[r] -= inst_.usage[r][item];
            descend(from + 1, value_so_far + inst_.value[item], budget_left - inst_.cost[item],
                    capacity_left);
            for (std::size_t r = 0; r < inst_.capacity.size(); ++r)
                capacity_left[r] += inst_.usage[r][item];
            chosen_[item] = 0;
        }

        // Exclude. Nothing decided so far can depend on this project, because
        // dependants come later in the order, so no repair is needed here.
        descend(from + 1, value_so_far, budget_left, capacity_left);
    }
};

std::vector<std::size_t> prerequisite_closure(const Instance& inst, std::size_t item,
                                              const std::vector<char>& chosen) {
    std::vector<std::size_t> closure;
    std::vector<char> seen(inst.size(), 0);
    std::vector<std::size_t> stack{item};
    while (!stack.empty()) {
        const std::size_t v = stack.back();
        stack.pop_back();
        if (seen[v] || chosen[v]) continue;
        seen[v] = 1;
        closure.push_back(v);
        for (std::size_t p : inst.prerequisites[v]) stack.push_back(p);
    }
    return closure;
}

}  // namespace

Instance make_instance(const model::Scenario& sc, const std::vector<double>& value) {
    if (value.size() != sc.projects.size())
        throw model::ModelError("the value vector does not match the number of projects");

    Instance inst;
    inst.value = value;
    inst.budget = sc.budget;
    inst.cost.reserve(sc.projects.size());
    for (const auto& p : sc.projects) inst.cost.push_back(p.expected_cost);

    inst.capacity.reserve(sc.resources.size());
    for (const auto& r : sc.resources) inst.capacity.push_back(r.capacity);
    inst.usage.assign(sc.resources.size(), std::vector<double>(sc.projects.size(), 0.0));
    for (std::size_t r = 0; r < sc.resources.size(); ++r)
        for (std::size_t i = 0; i < sc.projects.size(); ++i)
            inst.usage[r][i] = sc.projects[i].peak_demand[r];

    inst.prerequisites.assign(sc.projects.size(), {});
    inst.exclusions.assign(sc.projects.size(), {});
    for (std::size_t i = 0; i < sc.projects.size(); ++i) {
        for (const auto& dep : sc.projects[i].requires_projects)
            inst.prerequisites[i].push_back(sc.project_index(dep));
        for (const auto& ex : sc.projects[i].excludes_projects) {
            const std::size_t j = sc.project_index(ex);
            inst.exclusions[i].push_back(j);
            inst.exclusions[j].push_back(i);  // exclusion is symmetric whatever the file says
        }
    }
    for (auto& list : inst.exclusions) {
        std::sort(list.begin(), list.end());
        list.erase(std::unique(list.begin(), list.end()), list.end());
    }

    inst.order = sc.funding_order();
    return inst;
}

Solution solve_exact(const Instance& inst, std::int64_t node_limit) {
    return Search(inst, node_limit).run();
}

Solution solve_greedy(const Instance& inst) {
    const auto start = std::chrono::steady_clock::now();
    const std::size_t n = inst.size();

    std::vector<std::size_t> by_density(n);
    std::iota(by_density.begin(), by_density.end(), std::size_t{0});
    std::stable_sort(by_density.begin(), by_density.end(), [&](std::size_t a, std::size_t b) {
        const double da = inst.cost[a] > 0.0 ? inst.value[a] / inst.cost[a]
                                             : std::numeric_limits<double>::infinity();
        const double db = inst.cost[b] > 0.0 ? inst.value[b] / inst.cost[b]
                                             : std::numeric_limits<double>::infinity();
        return da > db;
    });

    Solution sol;
    sol.chosen.assign(n, 0);
    double budget_left = inst.budget;
    std::vector<double> capacity_left = inst.capacity;

    for (std::size_t candidate : by_density) {
        if (sol.chosen[candidate]) continue;
        // Taking a project means taking everything it depends on, so the whole
        // closure is costed and accepted or rejected together. This is where the
        // heuristic gives ground: a high density project can drag in cheap value
        // prerequisites that an exact search would have declined.
        const auto closure = prerequisite_closure(inst, candidate, sol.chosen);
        double cost = 0.0;
        std::vector<double> usage(inst.capacity.size(), 0.0);
        bool blocked = false;
        for (std::size_t i : closure) {
            cost += inst.cost[i];
            for (std::size_t r = 0; r < inst.capacity.size(); ++r) usage[r] += inst.usage[r][i];
            for (std::size_t x : inst.exclusions[i]) {
                if (sol.chosen[x]) blocked = true;
                if (std::find(closure.begin(), closure.end(), x) != closure.end()) blocked = true;
            }
        }
        if (blocked || cost > budget_left + kEpsilon) continue;
        bool fits = true;
        for (std::size_t r = 0; r < inst.capacity.size(); ++r)
            if (usage[r] > capacity_left[r] + kEpsilon) fits = false;
        if (!fits) continue;

        for (std::size_t i : closure) sol.chosen[i] = 1;
        budget_left -= cost;
        for (std::size_t r = 0; r < inst.capacity.size(); ++r) capacity_left[r] -= usage[r];
    }

    finalise(sol, inst);
    sol.optimal = false;
    sol.seconds = now_seconds(start);
    return sol;
}

std::string rejection_reason(const Instance& inst, const model::Scenario& sc, const Solution& sol,
                             std::size_t project) {
    if (sol.chosen[project]) return {};

    for (std::size_t p : inst.prerequisites[project])
        if (!sol.chosen[p])
            return "prerequisite '" + sc.projects[p].id + "' is not funded";

    for (std::size_t x : inst.exclusions[project])
        if (sol.chosen[x])
            return "mutually exclusive with the funded '" + sc.projects[x].id + "'";

    const double budget_left = inst.budget - sol.cost;
    if (inst.cost[project] > budget_left + kEpsilon)
        return "costs " + rounded(inst.cost[project], 0) + " against " +
               rounded(budget_left, 0) + " of remaining budget";

    for (std::size_t r = 0; r < inst.capacity.size(); ++r) {
        const double left = inst.capacity[r] - sol.used[r];
        if (inst.usage[r][project] > left + kEpsilon)
            return "needs " + rounded(inst.usage[r][project], 1) + " of resource '" +
                   sc.resources[r].id + "', " + rounded(left, 1) + " left";
    }
    // Nothing forbids it on its own, so it lost on the objective rather than to
    // a constraint. Saying that plainly is more useful than inventing a
    // constraint that did not bind.
    return "no constraint blocks it on its own, it loses on value";
}

}  // namespace quorum::portfolio
