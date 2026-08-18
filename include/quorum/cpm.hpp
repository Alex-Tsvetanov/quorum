// Critical path method and the PERT three-point estimate.
//
// The same network type serves two levels. Inside a project the nodes are
// activities. Across the selected portfolio the nodes are whole projects, with
// each project's duration taken from its own critical path. One implementation,
// two uses, because the maths does not care what a node represents.
#pragma once

#include <stdexcept>
#include <string>
#include <vector>

namespace quorum::cpm {

// A node of the activity network. Durations are three-point PERT estimates:
// optimistic, most likely, pessimistic. A single-point estimate is the case
// where all three are equal, so there is no second code path for it.
struct Task {
    std::string id;
    double optimistic = 0.0;
    double likely = 0.0;
    double pessimistic = 0.0;
    std::vector<std::string> depends_on;

    // PERT expected value: the beta approximation with weight four on the mode.
    double expected() const { return (optimistic + 4.0 * likely + pessimistic) / 6.0; }
    // PERT variance: the range over six, squared.
    double variance() const {
        const double range = (pessimistic - optimistic) / 6.0;
        return range * range;
    }
};

struct Network {
    std::vector<Task> tasks;

    const Task* find(const std::string& id) const;
    std::size_t index_of(const std::string& id) const;  // throws if unknown
};

struct TaskSchedule {
    std::string id;
    double duration = 0.0;
    double early_start = 0.0;
    double early_finish = 0.0;
    double late_start = 0.0;
    double late_finish = 0.0;
    double total_float = 0.0;
    bool critical = false;
};

struct Schedule {
    std::vector<TaskSchedule> tasks;
    double makespan = 0.0;
    std::vector<std::string> critical_path;
    // Variance of the makespan under the PERT assumption: the sum of the
    // variances of the activities on the critical path.
    double variance = 0.0;

    double std_dev() const;
    // Probability of finishing by `t`, from the normal approximation of the
    // makespan. The approximation is the classical PERT one and it ignores the
    // chance that a near-critical path overtakes the critical one.
    double probability_by(double t) const;
    const TaskSchedule& at(const std::string& id) const;
};

// Raised when the dependency graph is not acyclic. `cycle()` names the nodes on
// the offending cycle in order, so the message points at the input rather than
// at the algorithm.
class CycleError : public std::runtime_error {
public:
    explicit CycleError(std::vector<std::string> cycle);
    const std::vector<std::string>& cycle() const { return cycle_; }

private:
    std::vector<std::string> cycle_;
};

class UnknownTaskError : public std::runtime_error {
public:
    explicit UnknownTaskError(const std::string& what) : std::runtime_error(what) {}
};

// Indices into `net.tasks`, dependencies first. Throws CycleError.
std::vector<std::size_t> topological_order(const Network& net);

// Forward pass, backward pass, float, critical path. Uses PERT expected
// durations.
Schedule schedule(const Network& net);

// Same, with durations supplied per task, which is what the Monte Carlo pass
// needs: it samples the durations and reuses the rest of the computation.
Schedule schedule_with(const Network& net, const std::vector<double>& durations);

// Peak concurrent demand for one resource under the earliest-start schedule.
// `demand[i]` is the per-period demand of task i while it runs.
double peak_demand(const Network& net, const Schedule& sched, const std::vector<double>& demand);

}  // namespace quorum::cpm
