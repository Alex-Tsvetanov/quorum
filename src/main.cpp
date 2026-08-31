// Command line front end.
//
//   quorum demo        [--scenario FILE]   everything, on the bundled scenario
//   quorum solve       FILE [--method M]   rank, select, schedule
//   quorum sensitivity FILE [--method M]   weight and budget sweeps
//   quorum risk        FILE [--samples N]  Monte Carlo on the chosen portfolio
//   quorum serve       [--port N]          the decision-support page
//   quorum bench       [--out FILE]        the measurements reported in the text
#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "quorum/analyse.hpp"
#include "quorum/report.hpp"
#include "quorum/web.hpp"

int run_bench(const std::vector<std::string>& args, std::ostream& os);

namespace {

using namespace quorum;

#ifndef QUORUM_WEB_ROOT
#define QUORUM_WEB_ROOT "web"
#endif
#ifndef QUORUM_SCENARIO_DIR
#define QUORUM_SCENARIO_DIR "scenarios"
#endif

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

void report_selection(std::ostream& os, const analyse::Result& a) {
    report::heading(os, std::string("Selected portfolio, scored by ") +
                            sensitivity::name_of(a.method));
    report::portfolio(os, a.scenario, a.instance, a.exact, a.greedy);

    report::heading(os, "Programme schedule of the funded portfolio");
    report::programme(os, a.scenario, a.exact.selected);
}

void report_common(std::ostream& os, const analyse::Result& a) {
    report::heading(os, "Scenario");
    report::scenario_summary(os, a.scenario);
    report::heading(os, "Criteria weights from the pairwise judgements");
    report::weights(os, a.scenario, a.ahp);
    report::heading(os, "Rankings, AHP against TOPSIS");
    report::rankings(os, a.scenario, a.ahp_scores, a.topsis_scores);
}

int cmd_solve(const std::vector<std::string>& args, std::ostream& os) {
    analyse::Options opt;
    opt.method = analyse::method_of(option(args, "--method", "topsis"));
    const analyse::Result a = analyse::run_file(positional(args, kDefaultScenario), opt);
    report_common(os, a);
    report_selection(os, a);
    return 0;
}

int cmd_sensitivity(const std::vector<std::string>& args, std::ostream& os) {
    analyse::Options opt;
    opt.method = analyse::method_of(option(args, "--method", "topsis"));
    const analyse::Result a = analyse::run_file(positional(args, kDefaultScenario), opt);

    report::heading(os, "Sensitivity to the criteria weights");
    const auto w = sensitivity::sweep_weights(a.scenario, a.weights, {0.5, 0.75, 1.25, 1.5}, a.method);
    report::sensitivity(os, "one criterion weight varied at a time", w);

    report::heading(os, "Sensitivity to the budget");
    const auto b = sensitivity::sweep_budget(
        a.scenario, a.weights, {0.6, 0.7, 0.8, 0.9, 1.0, 1.1, 1.2, 1.3, 1.4}, a.method);
    report::sensitivity(os, "budget varied from 60 to 140 per cent", b);
    os << "\n  Funded under each budget:\n\n";
    report::membership(os, a.scenario, b);
    return 0;
}

int cmd_risk(const std::vector<std::string>& args, std::ostream& os) {
    analyse::Options opt;
    opt.method = analyse::method_of(option(args, "--method", "topsis"));
    opt.risk_samples = static_cast<std::size_t>(std::stoul(option(args, "--samples", "20000")));
    const analyse::Result a = analyse::run_file(positional(args, kDefaultScenario), opt);

    report::heading(os, "Monte Carlo over the funded portfolio");
    report::risk(os, a.scenario, a.monte_carlo);
    return 0;
}

int cmd_demo(const std::vector<std::string>& args, std::ostream& os) {
    const std::string path = option(args, "--scenario", positional(args, kDefaultScenario));
    analyse::Options opt;
    opt.method = sensitivity::Method::Topsis;
    opt.risk_samples = 20000;
    const analyse::Result a = analyse::run_file(path, opt);

    os << "Quorum: portfolio selection under budget and capacity limits\n";
    os << "Reading " << path << "\n";

    report_common(os, a);
    report_selection(os, a);

    report::heading(os, "Monte Carlo over the funded portfolio");
    report::risk(os, a.scenario, a.monte_carlo);

    report::heading(os, "Sensitivity to the criteria weights");
    report::sensitivity(os, "one criterion weight varied at a time",
                        sensitivity::sweep_weights(a.scenario, a.weights, {0.5, 0.75, 1.25, 1.5},
                                                   a.method));

    report::heading(os, "Sensitivity to the budget");
    const auto budget_sweep = sensitivity::sweep_budget(
        a.scenario, a.weights, {0.6, 0.7, 0.8, 0.9, 1.0, 1.1, 1.2, 1.3, 1.4}, a.method);
    report::sensitivity(os, "budget varied from 60 to 140 per cent", budget_sweep);
    os << "\n  Funded under each budget:\n\n";
    report::membership(os, a.scenario, budget_sweep);

    os << "\nDone.\n";
    return 0;
}

int cmd_serve(const std::vector<std::string>& args) {
    web::ServeOptions opt;
    opt.port = static_cast<std::uint16_t>(std::stoul(option(args, "--port", "8080")));
    opt.config.web_root = option(args, "--web", QUORUM_WEB_ROOT);
    const std::string bundled = std::string(QUORUM_SCENARIO_DIR) + "/example.json";
    opt.config.example_path = option(args, "--scenario", bundled);
    return web::serve(opt);
}

void usage(std::ostream& os) {
    os << "usage: quorum <command> [options]\n\n"
          "  demo        [--scenario FILE]      the whole pipeline on one scenario\n"
          "  solve       FILE [--method M]      rank, select and schedule\n"
          "  sensitivity FILE [--method M]      weight and budget sweeps\n"
          "  risk        FILE [--samples N]     Monte Carlo on the chosen portfolio\n"
          "  serve       [--port N] [--web DIR] the decision-support page\n"
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
        if (command == "serve") return cmd_serve(rest);
        if (command == "bench") return run_bench(rest, std::cout);
        std::cerr << "unknown command '" << command << "'\n\n";
        usage(std::cerr);
        return 2;
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
}
