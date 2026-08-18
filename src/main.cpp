// Command line front end.
//
//   quorum demo        [--scenario FILE]   everything, on the bundled scenario
//   quorum solve       FILE [--method M]   rank, select, schedule
//   quorum sensitivity FILE [--method M]   weight and budget sweeps
//   quorum risk        FILE [--samples N]  Monte Carlo on the chosen portfolio
//   quorum bench       [--out FILE]        the measurements reported in the text
#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "quorum/report.hpp"

int run_bench(const std::vector<std::string>& args, std::ostream& os);

namespace {

using namespace quorum;

const char* kDefaultScenario = "scenarios/example.json";

std::string option(const std::vector<std::string>& args, const std::string& name,
                   const std::string& fallback) {
    for (std::size_t i = 0; i + 1 < args.size(); ++i)
        if (args[i] == name) return args[i + 1];
    return fallback;
}

std::string positional(const std::vector<std::string>& args, const std::string& fallback) {
    for (const auto& a : args)
        if (!a.empty() && a[0] != '-') return a;
    return fallback;
}

sensitivity::Method method_of(const std::string& name) {
    if (name == "ahp" || name == "AHP") return sensitivity::Method::Ahp;
    if (name == "topsis" || name == "TOPSIS") return sensitivity::Method::Topsis;
    throw model::ModelError("unknown method '" + name + "', expected 'ahp' or 'topsis'");
}

// Everything downstream of loading needs the same three things, so they are
// derived once and passed around rather than recomputed per command.
struct Analysis {
    model::Scenario scenario;
    mcdm::AhpResult ahp;
    std::vector<double> weights;
    std::vector<double> ahp_scores;
    std::vector<double> topsis_scores;
};

Analysis analyse(const std::string& path) {
    Analysis a;
    a.scenario = model::load_file(path);

    if (!a.scenario.comparisons.empty()) {
        a.ahp = mcdm::analyse_comparisons(a.scenario.comparisons);
        if (!a.ahp.consistent)
            throw mcdm::InconsistentJudgementError(a.ahp.consistency_ratio,
                                                   mcdm::kConsistencyThreshold);
        a.weights = a.ahp.weights;
    } else {
        for (const auto& c : a.scenario.criteria) a.weights.push_back(c.weight);
        a.ahp.weights = a.weights;
        a.ahp.lambda_max = static_cast<double>(a.weights.size());
        a.ahp.consistent = true;
    }
    a.ahp_scores = sensitivity::score(a.scenario, a.weights, sensitivity::Method::Ahp);
    a.topsis_scores = sensitivity::score(a.scenario, a.weights, sensitivity::Method::Topsis);
    return a;
}

const std::vector<double>& scores_for(const Analysis& a, sensitivity::Method m) {
    return m == sensitivity::Method::Ahp ? a.ahp_scores : a.topsis_scores;
}

void report_selection(std::ostream& os, const Analysis& a, sensitivity::Method m) {
    const auto instance = portfolio::make_instance(a.scenario, scores_for(a, m));
    const auto exact = portfolio::solve_exact(instance);
    const auto greedy = portfolio::solve_greedy(instance);

    // The heuristic is a lower bound on the optimum. A solver below it is not
    // slow, it is wrong, and the run stops rather than reporting the number.
    if (greedy.value > exact.value + 1e-9)
        throw model::ModelError("the exact solver returned less than the greedy heuristic");

    report::heading(os, std::string("Selected portfolio, scored by ") + sensitivity::name_of(m));
    report::portfolio(os, a.scenario, instance, exact, greedy);

    report::heading(os, "Programme schedule of the funded portfolio");
    report::programme(os, a.scenario, exact.selected);
}

int cmd_solve(const std::vector<std::string>& args, std::ostream& os) {
    const Analysis a = analyse(positional(args, kDefaultScenario));
    const auto m = method_of(option(args, "--method", "topsis"));

    report::heading(os, "Scenario");
    report::scenario_summary(os, a.scenario);
    report::heading(os, "Criteria weights from the pairwise judgements");
    report::weights(os, a.scenario, a.ahp);
    report::heading(os, "Rankings, AHP against TOPSIS");
    report::rankings(os, a.scenario, a.ahp_scores, a.topsis_scores);
    report_selection(os, a, m);
    return 0;
}

int cmd_sensitivity(const std::vector<std::string>& args, std::ostream& os) {
    const Analysis a = analyse(positional(args, kDefaultScenario));
    const auto m = method_of(option(args, "--method", "topsis"));

    report::heading(os, "Sensitivity to the criteria weights");
    const auto w = sensitivity::sweep_weights(a.scenario, a.weights, {0.5, 0.75, 1.25, 1.5}, m);
    report::sensitivity(os, "one criterion weight varied at a time", w);

    report::heading(os, "Sensitivity to the budget");
    const auto b = sensitivity::sweep_budget(
        a.scenario, a.weights, {0.6, 0.7, 0.8, 0.9, 1.0, 1.1, 1.2, 1.3, 1.4}, m);
    report::sensitivity(os, "budget varied from 60 to 140 per cent", b);
    os << "\n  Funded under each budget:\n\n";
    report::membership(os, a.scenario, b);
    return 0;
}

int cmd_risk(const std::vector<std::string>& args, std::ostream& os) {
    const Analysis a = analyse(positional(args, kDefaultScenario));
    const auto m = method_of(option(args, "--method", "topsis"));
    const std::size_t samples =
        static_cast<std::size_t>(std::stoul(option(args, "--samples", "20000")));

    const auto instance = portfolio::make_instance(a.scenario, scores_for(a, m));
    const auto exact = portfolio::solve_exact(instance);

    report::heading(os, "Monte Carlo over the funded portfolio");
    report::risk(os, a.scenario, risk::simulate(a.scenario, exact.selected, samples));
    return 0;
}

int cmd_demo(const std::vector<std::string>& args, std::ostream& os) {
    const std::string path = option(args, "--scenario", positional(args, kDefaultScenario));
    const Analysis a = analyse(path);
    const auto m = sensitivity::Method::Topsis;

    os << "Quorum: portfolio selection under budget and capacity limits\n";
    os << "Reading " << path << "\n";

    report::heading(os, "Scenario");
    report::scenario_summary(os, a.scenario);

    report::heading(os, "Criteria weights from the pairwise judgements");
    report::weights(os, a.scenario, a.ahp);

    report::heading(os, "Rankings, AHP against TOPSIS");
    report::rankings(os, a.scenario, a.ahp_scores, a.topsis_scores);

    report_selection(os, a, m);

    const auto instance = portfolio::make_instance(a.scenario, scores_for(a, m));
    const auto exact = portfolio::solve_exact(instance);

    report::heading(os, "Monte Carlo over the funded portfolio");
    report::risk(os, a.scenario, risk::simulate(a.scenario, exact.selected, 20000));

    report::heading(os, "Sensitivity to the criteria weights");
    report::sensitivity(os, "one criterion weight varied at a time",
                        sensitivity::sweep_weights(a.scenario, a.weights, {0.5, 0.75, 1.25, 1.5}, m));

    report::heading(os, "Sensitivity to the budget");
    const auto budget_sweep = sensitivity::sweep_budget(
        a.scenario, a.weights, {0.6, 0.7, 0.8, 0.9, 1.0, 1.1, 1.2, 1.3, 1.4}, m);
    report::sensitivity(os, "budget varied from 60 to 140 per cent", budget_sweep);
    os << "\n  Funded under each budget:\n\n";
    report::membership(os, a.scenario, budget_sweep);

    os << "\nDone.\n";
    return 0;
}

void usage(std::ostream& os) {
    os << "usage: quorum <command> [options]\n\n"
          "  demo        [--scenario FILE]      the whole pipeline on one scenario\n"
          "  solve       FILE [--method M]      rank, select and schedule\n"
          "  sensitivity FILE [--method M]      weight and budget sweeps\n"
          "  risk        FILE [--samples N]     Monte Carlo on the chosen portfolio\n"
          "  bench       [--out FILE]           the measurements reported in the text\n\n"
          "  M is 'ahp' or 'topsis', default topsis.\n";
}

}  // namespace

int main(int argc, char** argv) {
    const std::vector<std::string> args(argv + 1, argv + argc);
    if (args.empty() || args[0] == "-h" || args[0] == "--help") {
        usage(std::cout);
        return args.empty() ? 2 : 0;
    }
    const std::string command = args[0];
    const std::vector<std::string> rest(args.begin() + 1, args.end());

    try {
        if (command == "demo") return cmd_demo(rest, std::cout);
        if (command == "solve") return cmd_solve(rest, std::cout);
        if (command == "sensitivity") return cmd_sensitivity(rest, std::cout);
        if (command == "risk") return cmd_risk(rest, std::cout);
        if (command == "bench") return run_bench(rest, std::cout);
        std::cerr << "unknown command '" << command << "'\n\n";
        usage(std::cerr);
        return 2;
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
}
