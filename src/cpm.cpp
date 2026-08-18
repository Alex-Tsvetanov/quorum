#include "quorum/cpm.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <unordered_map>

namespace quorum::cpm {
namespace {

constexpr double kFloatTolerance = 1e-9;

std::string join(const std::vector<std::string>& parts, const char* sep) {
    std::string out;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i) out += sep;
        out += parts[i];
    }
    return out;
}

std::unordered_map<std::string, std::size_t> index_map(const Network& net) {
    std::unordered_map<std::string, std::size_t> map;
    for (std::size_t i = 0; i < net.tasks.size(); ++i) map.emplace(net.tasks[i].id, i);
    return map;
}

// Depth-first search restricted to `alive` nodes, returning the first cycle it
// meets as an ordered list of identifiers. Only called once a cycle is known to
// exist, so the recursion always terminates on one.
std::vector<std::string> extract_cycle(const Network& net,
                                       const std::vector<std::vector<std::size_t>>& succ,
                                       const std::vector<char>& alive) {
    enum State { kUnseen, kOnStack, kDone };
    std::vector<State> state(net.tasks.size(), kUnseen);
    std::vector<std::size_t> stack;

    auto walk = [&](auto&& self, std::size_t v) -> std::vector<std::string> {
        state[v] = kOnStack;
        stack.push_back(v);
        for (std::size_t w : succ[v]) {
            if (!alive[w]) continue;
            if (state[w] == kOnStack) {
                auto begin = std::find(stack.begin(), stack.end(), w);
                std::vector<std::string> cycle;
                for (auto it = begin; it != stack.end(); ++it) cycle.push_back(net.tasks[*it].id);
                cycle.push_back(net.tasks[w].id);
                return cycle;
            }
            if (state[w] == kUnseen) {
                auto found = self(self, w);
                if (!found.empty()) return found;
            }
        }
        stack.pop_back();
        state[v] = kDone;
        return {};
    };

    for (std::size_t v = 0; v < net.tasks.size(); ++v) {
        if (!alive[v] || state[v] != kUnseen) continue;
        auto found = walk(walk, v);
        if (!found.empty()) return found;
    }
    return {};
}

}  // namespace

const Task* Network::find(const std::string& id) const {
    for (const auto& t : tasks)
        if (t.id == id) return &t;
    return nullptr;
}

std::size_t Network::index_of(const std::string& id) const {
    for (std::size_t i = 0; i < tasks.size(); ++i)
        if (tasks[i].id == id) return i;
    throw UnknownTaskError("unknown activity '" + id + "'");
}

CycleError::CycleError(std::vector<std::string> cycle)
    : std::runtime_error("dependency cycle: " + join(cycle, " -> ")), cycle_(std::move(cycle)) {}

double Schedule::std_dev() const { return std::sqrt(variance); }

double Schedule::probability_by(double t) const {
    const double sigma = std_dev();
    if (sigma <= 0.0) return t >= makespan ? 1.0 : 0.0;
    return 0.5 * (1.0 + std::erf((t - makespan) / (sigma * std::sqrt(2.0))));
}

const TaskSchedule& Schedule::at(const std::string& id) const {
    for (const auto& t : tasks)
        if (t.id == id) return t;
    throw UnknownTaskError("no schedule entry for '" + id + "'");
}

std::vector<std::size_t> topological_order(const Network& net) {
    const auto map = index_map(net);
    const std::size_t n = net.tasks.size();
    std::vector<std::vector<std::size_t>> succ(n);
    std::vector<std::size_t> indegree(n, 0);

    for (std::size_t i = 0; i < n; ++i) {
        for (const auto& dep : net.tasks[i].depends_on) {
            auto it = map.find(dep);
            if (it == map.end())
                throw UnknownTaskError("activity '" + net.tasks[i].id + "' depends on unknown '" +
                                       dep + "'");
            succ[it->second].push_back(i);
            ++indegree[i];
        }
    }

    std::vector<std::size_t> ready;
    for (std::size_t i = 0; i < n; ++i)
        if (indegree[i] == 0) ready.push_back(i);

    std::vector<std::size_t> order;
    order.reserve(n);
    while (!ready.empty()) {
        std::size_t v = ready.back();
        ready.pop_back();
        order.push_back(v);
        for (std::size_t w : succ[v])
            if (--indegree[w] == 0) ready.push_back(w);
    }

    if (order.size() != n) {
        std::vector<char> alive(n, 0);
        for (std::size_t i = 0; i < n; ++i) alive[i] = indegree[i] > 0 ? 1 : 0;
        throw CycleError(extract_cycle(net, succ, alive));
    }
    return order;
}

Schedule schedule_with(const Network& net, const std::vector<double>& durations) {
    const std::size_t n = net.tasks.size();
    if (durations.size() != n)
        throw UnknownTaskError("duration vector does not match the network size");

    const auto order = topological_order(net);
    const auto map = index_map(net);

    Schedule out;
    out.tasks.resize(n);
    for (std::size_t i = 0; i < n; ++i) {
        out.tasks[i].id = net.tasks[i].id;
        out.tasks[i].duration = durations[i];
    }

    // Forward pass: earliest start is the latest earliest finish of the
    // predecessors. The topological order guarantees they are already done.
    for (std::size_t v : order) {
        double es = 0.0;
        for (const auto& dep : net.tasks[v].depends_on)
            es = std::max(es, out.tasks[map.at(dep)].early_finish);
        out.tasks[v].early_start = es;
        out.tasks[v].early_finish = es + durations[v];
        out.makespan = std::max(out.makespan, out.tasks[v].early_finish);
    }

    // Backward pass over the reverse order. Latest finish is the earliest late
    // start among the successors, or the makespan for a terminal activity.
    std::vector<std::vector<std::size_t>> succ(n);
    for (std::size_t i = 0; i < n; ++i)
        for (const auto& dep : net.tasks[i].depends_on) succ[map.at(dep)].push_back(i);

    for (auto it = order.rbegin(); it != order.rend(); ++it) {
        const std::size_t v = *it;
        double lf = succ[v].empty() ? out.makespan : std::numeric_limits<double>::infinity();
        for (std::size_t w : succ[v]) lf = std::min(lf, out.tasks[w].late_start);
        out.tasks[v].late_finish = lf;
        out.tasks[v].late_start = lf - durations[v];
        out.tasks[v].total_float = out.tasks[v].late_start - out.tasks[v].early_start;
        out.tasks[v].critical = std::fabs(out.tasks[v].total_float) <= kFloatTolerance;
    }

    // The critical path is the chain of zero-float activities that runs without
    // a gap from time zero to the makespan. Zero float alone is not enough:
    // parallel zero-float chains exist, and the path is one of them.
    std::vector<std::size_t> critical;
    for (std::size_t i = 0; i < n; ++i)
        if (out.tasks[i].critical) critical.push_back(i);
    std::sort(critical.begin(), critical.end(), [&](std::size_t a, std::size_t b) {
        return out.tasks[a].early_start < out.tasks[b].early_start;
    });

    std::size_t current = n;
    for (std::size_t v : critical) {
        if (out.tasks[v].early_start <= kFloatTolerance) {
            current = v;
            break;
        }
    }
    while (current < n) {
        out.critical_path.push_back(net.tasks[current].id);
        out.variance += net.tasks[current].variance();
        const double finish = out.tasks[current].early_finish;
        if (finish >= out.makespan - kFloatTolerance) break;
        std::size_t next = n;
        for (std::size_t w : succ[current]) {
            if (out.tasks[w].critical && std::fabs(out.tasks[w].early_start - finish) <= kFloatTolerance) {
                next = w;
                break;
            }
        }
        current = next;
    }

    return out;
}

Schedule schedule(const Network& net) {
    std::vector<double> durations;
    durations.reserve(net.tasks.size());
    for (const auto& t : net.tasks) durations.push_back(t.expected());
    return schedule_with(net, durations);
}

double peak_demand(const Network& net, const Schedule& sched, const std::vector<double>& demand) {
    if (demand.size() != net.tasks.size()) throw UnknownTaskError("demand vector size mismatch");
    // Sweep line over the start and finish instants. Concurrency can only change
    // at one of them, so sampling those points is exact and needs no period grid.
    std::vector<std::pair<double, double>> events;  // time, delta
    events.reserve(demand.size() * 2);
    for (std::size_t i = 0; i < demand.size(); ++i) {
        if (demand[i] == 0.0 || sched.tasks[i].duration <= 0.0) continue;
        events.emplace_back(sched.tasks[i].early_start, demand[i]);
        events.emplace_back(sched.tasks[i].early_finish, -demand[i]);
    }
    std::sort(events.begin(), events.end());
    double running = 0.0, peak = 0.0;
    for (const auto& e : events) {
        running += e.second;
        peak = std::max(peak, running);
    }
    return peak;
}

}  // namespace quorum::cpm
