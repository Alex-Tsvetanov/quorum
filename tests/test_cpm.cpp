#include <cmath>

#include "check.hpp"
#include "quorum/cpm.hpp"

using namespace quorum;

namespace {

// A network with a known answer, worked out by hand.
//
//   start(2) -> build(5) -> test(3) -> ship(1)
//   start(2) -> docs(2)  ------------> ship(1)
//
// The build chain is 2 + 5 + 3 = 10 before ship, the docs chain is 2 + 2 = 4,
// so docs carries six periods of float and the critical path runs through build
// and test. Total duration is 11.
cpm::Network textbook() {
    cpm::Network net;
    net.tasks.push_back({"start", 2, 2, 2, {}});
    net.tasks.push_back({"build", 5, 5, 5, {"start"}});
    net.tasks.push_back({"test", 3, 3, 3, {"build"}});
    net.tasks.push_back({"docs", 2, 2, 2, {"start"}});
    net.tasks.push_back({"ship", 1, 1, 1, {"test", "docs"}});
    return net;
}

}  // namespace

TEST(cpm, "forward pass gives the hand computed earliest times") {
    const auto s = cpm::schedule(textbook());
    CHECK_NEAR(s.at("start").early_start, 0.0, 1e-9);
    CHECK_NEAR(s.at("build").early_start, 2.0, 1e-9);
    CHECK_NEAR(s.at("test").early_finish, 10.0, 1e-9);
    CHECK_NEAR(s.at("docs").early_finish, 4.0, 1e-9);
    CHECK_NEAR(s.at("ship").early_start, 10.0, 1e-9);
    CHECK_NEAR(s.makespan, 11.0, 1e-9);
}

TEST(cpm, "backward pass gives the latest times and the float") {
    const auto s = cpm::schedule(textbook());
    CHECK_NEAR(s.at("ship").late_start, 10.0, 1e-9);
    CHECK_NEAR(s.at("docs").late_finish, 10.0, 1e-9);
    CHECK_NEAR(s.at("docs").total_float, 6.0, 1e-9);
    CHECK_NEAR(s.at("build").total_float, 0.0, 1e-9);
    CHECK_NEAR(s.at("test").total_float, 0.0, 1e-9);
}

TEST(cpm, "the critical path is the zero float chain from zero to the makespan") {
    const auto s = cpm::schedule(textbook());
    const std::vector<std::string> expected{"start", "build", "test", "ship"};
    CHECK_EQ(s.critical_path.size(), expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i) CHECK_EQ(s.critical_path[i], expected[i]);
    CHECK_FALSE(s.at("docs").critical);
}

TEST(cpm, "a cycle is rejected and the offending nodes are named") {
    cpm::Network net;
    net.tasks.push_back({"a", 1, 1, 1, {"c"}});
    net.tasks.push_back({"b", 1, 1, 1, {"a"}});
    net.tasks.push_back({"c", 1, 1, 1, {"b"}});
    bool named = false;
    try {
        cpm::schedule(net);
    } catch (const cpm::CycleError& e) {
        // The cycle is reported as a closed walk, so the first identifier
        // repeats at the end.
        named = e.cycle().size() == 4 && e.cycle().front() == e.cycle().back();
    }
    CHECK_TRUE(named);
}

TEST(cpm, "a dependency on an activity that does not exist is rejected") {
    cpm::Network net;
    net.tasks.push_back({"a", 1, 1, 1, {"ghost"}});
    CHECK_THROWS(cpm::schedule(net), cpm::UnknownTaskError);
}

TEST(cpm, "PERT expected value and variance follow the beta approximation") {
    const cpm::Task t{"x", 2, 5, 14, {}};
    CHECK_NEAR(t.expected(), (2.0 + 20.0 + 14.0) / 6.0, 1e-12);
    CHECK_NEAR(t.expected(), 6.0, 1e-12);
    CHECK_NEAR(t.variance(), 4.0, 1e-12);
    const cpm::Task certain{"y", 4, 4, 4, {}};
    CHECK_NEAR(certain.expected(), 4.0, 1e-12);
    CHECK_NEAR(certain.variance(), 0.0, 1e-12);
}

TEST(cpm, "makespan variance sums only the critical path and drives the probability") {
    cpm::Network net;
    net.tasks.push_back({"a", 1, 4, 7, {}});          // variance 1
    net.tasks.push_back({"b", 2, 5, 8, {"a"}});       // variance 1
    net.tasks.push_back({"slack", 0, 1, 2, {}});      // off the critical path
    const auto s = cpm::schedule(net);
    CHECK_NEAR(s.makespan, 9.0, 1e-9);
    CHECK_NEAR(s.variance, 2.0, 1e-9);
    CHECK_NEAR(s.std_dev(), std::sqrt(2.0), 1e-9);
    // The expected makespan sits at the median of the normal approximation.
    CHECK_NEAR(s.probability_by(s.makespan), 0.5, 1e-9);
    CHECK_TRUE(s.probability_by(s.makespan + 3.0 * s.std_dev()) > 0.99);
    CHECK_TRUE(s.probability_by(s.makespan - 3.0 * s.std_dev()) < 0.01);
}

TEST(cpm, "a certain network reports a step probability rather than dividing by zero") {
    const auto s = cpm::schedule(textbook());
    CHECK_NEAR(s.std_dev(), 0.0, 1e-12);
    CHECK_NEAR(s.probability_by(11.0), 1.0, 1e-12);
    CHECK_NEAR(s.probability_by(10.99), 0.0, 1e-12);
}

TEST(cpm, "peak demand counts only the activities that actually overlap") {
    const auto net = textbook();
    const auto s = cpm::schedule(net);
    // start, build, test, docs, ship. Build and docs overlap in the window from
    // period two to period four, so the peak is their sum and not the total.
    const std::vector<double> demand{1.0, 4.0, 2.0, 3.0, 1.0};
    CHECK_NEAR(cpm::peak_demand(net, s, demand), 7.0, 1e-9);

    // Activities in a chain never overlap, so the peak is the largest single
    // demand rather than the sum.
    const std::vector<double> chain{1.0, 4.0, 2.0, 0.0, 1.0};
    CHECK_NEAR(cpm::peak_demand(net, s, chain), 4.0, 1e-9);
}

TEST(cpm, "topological order puts every dependency before its dependant") {
    const auto net = textbook();
    const auto order = cpm::topological_order(net);
    CHECK_EQ(order.size(), net.tasks.size());
    std::vector<std::size_t> position(net.tasks.size(), 0);
    for (std::size_t p = 0; p < order.size(); ++p) position[order[p]] = p;
    for (std::size_t i = 0; i < net.tasks.size(); ++i)
        for (const auto& dep : net.tasks[i].depends_on)
            CHECK_TRUE(position[net.index_of(dep)] < position[i]);
}

TEST(cpm, "sampled durations reuse the same schedule computation") {
    const auto net = textbook();
    const std::vector<double> doubled{4, 10, 6, 4, 2};
    const auto s = cpm::schedule_with(net, doubled);
    CHECK_NEAR(s.makespan, 22.0, 1e-9);
    CHECK_THROWS(cpm::schedule_with(net, {1.0}), cpm::UnknownTaskError);
}
