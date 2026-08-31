#include "quorum/analyse.hpp"

#include <stdexcept>

namespace quorum::analyse {
namespace {

json::Value number(double x) { return json::Value(x); }

json::Value summary_json(const risk::Summary& s) {
    return json::Value(json::Object{
        {"mean", number(s.mean)},
        {"std_dev", number(s.std_dev)},
        {"p05", number(s.p05)},
        {"p50", number(s.p50)},
        {"p95", number(s.p95)},
    });
}

const std::vector<double>& scores_for(const Result& r, sensitivity::Method m) {
    return m == sensitivity::Method::Ahp ? r.ahp_scores : r.topsis_scores;
}

}  // namespace

sensitivity::Method method_of(const std::string& name) {
    if (name == "ahp" || name == "AHP") return sensitivity::Method::Ahp;
    if (name == "topsis" || name == "TOPSIS") return sensitivity::Method::Topsis;
    throw model::ModelError("unknown method '" + name + "', expected 'ahp' or 'topsis'");
}

Result run(model::Scenario scenario, const Options& options) {
    Result r;
    r.method = options.method;
    r.scenario = std::move(scenario);

    if (!r.scenario.comparisons.empty()) {
        r.ahp = mcdm::analyse_comparisons(r.scenario.comparisons);
        if (!r.ahp.consistent)
            throw mcdm::InconsistentJudgementError(r.ahp.consistency_ratio,
                                                   mcdm::kConsistencyThreshold);
        r.weights = r.ahp.weights;
    } else {
        for (const auto& c : r.scenario.criteria) r.weights.push_back(c.weight);
        r.ahp.weights = r.weights;
        r.ahp.lambda_max = static_cast<double>(r.weights.size());
        r.ahp.consistent = true;
    }

    r.ahp_scores = sensitivity::score(r.scenario, r.weights, sensitivity::Method::Ahp);
    r.topsis_scores = sensitivity::score(r.scenario, r.weights, sensitivity::Method::Topsis);

    r.instance = portfolio::make_instance(r.scenario, scores_for(r, options.method));
    r.exact = portfolio::solve_exact(r.instance);
    r.greedy = portfolio::solve_greedy(r.instance);

    if (r.greedy.value > r.exact.value + 1e-9)
        throw model::ModelError("the exact solver returned less than the greedy heuristic");

    r.programme = cpm::schedule(model::programme_network(r.scenario, r.exact.selected));

    if (options.risk_samples > 0)
        r.monte_carlo = risk::simulate(r.scenario, r.exact.selected, options.risk_samples);

    return r;
}

Result run_file(const std::string& path, const Options& options) {
    return run(model::load_file(path), options);
}

Result run_text(const std::string& text, const Options& options) {
    return run(model::load(json::parse(text)), options);
}

json::Value to_json(const Result& r) {
    double asked = 0.0;
    for (const auto& p : r.scenario.projects) asked += p.expected_cost;

    json::Array resources;
    for (const auto& res : r.scenario.resources)
        resources.push_back(json::Value(json::Object{
            {"id", json::Value(res.id)},
            {"label", json::Value(res.label)},
            {"capacity", number(res.capacity)},
        }));

    json::Array criteria;
    for (std::size_t i = 0; i < r.scenario.criteria.size(); ++i)
        criteria.push_back(json::Value(json::Object{
            {"id", json::Value(r.scenario.criteria[i].id)},
            {"label", json::Value(r.scenario.criteria[i].label)},
            {"direction", json::Value(r.scenario.criteria[i].maximise ? "max" : "min")},
            {"weight", number(r.ahp.weights.empty() ? 0.0 : r.ahp.weights[i])},
        }));

    const auto rank_a = mcdm::rank_of(r.ahp_scores);
    const auto rank_t = mcdm::rank_of(r.topsis_scores);
    json::Array ranking_rows;
    std::size_t disagreements = 0;
    for (std::size_t i = 0; i < r.scenario.projects.size(); ++i) {
        if (rank_a[i] != rank_t[i]) ++disagreements;
        ranking_rows.push_back(json::Value(json::Object{
            {"id", json::Value(r.scenario.projects[i].id)},
            {"name", json::Value(r.scenario.projects[i].name)},
            {"ahp", number(r.ahp_scores[i])},
            {"ahp_rank", number(static_cast<double>(rank_a[i]))},
            {"topsis", number(r.topsis_scores[i])},
            {"topsis_rank", number(static_cast<double>(rank_t[i]))},
        }));
    }

    json::Array funded;
    for (std::size_t i : r.exact.selected) {
        json::Array requires_list;
        for (const auto& dep : r.scenario.projects[i].requires_projects)
            requires_list.push_back(json::Value(dep));
        funded.push_back(json::Value(json::Object{
            {"id", json::Value(r.scenario.projects[i].id)},
            {"name", json::Value(r.scenario.projects[i].name)},
            {"cost", number(r.scenario.projects[i].expected_cost)},
            {"duration", number(r.scenario.projects[i].expected_duration)},
            {"value", number(r.instance.value[i])},
            {"requires", json::Value(std::move(requires_list))},
        }));
    }

    json::Array rejected;
    for (std::size_t i = 0; i < r.scenario.projects.size(); ++i) {
        if (r.exact.chosen[i]) continue;
        rejected.push_back(json::Value(json::Object{
            {"id", json::Value(r.scenario.projects[i].id)},
            {"name", json::Value(r.scenario.projects[i].name)},
            {"cost", number(r.scenario.projects[i].expected_cost)},
            {"value", number(r.instance.value[i])},
            {"reason", json::Value(portfolio::rejection_reason(r.instance, r.scenario, r.exact, i))},
        }));
    }

    json::Array used;
    for (std::size_t k = 0; k < r.scenario.resources.size(); ++k)
        used.push_back(json::Value(json::Object{
            {"id", json::Value(r.scenario.resources[k].id)},
            {"used", number(r.exact.used[k])},
            {"capacity", number(r.instance.capacity[k])},
        }));

    json::Array tasks;
    json::Array critical_path;
    for (const auto& id : r.programme.critical_path) critical_path.push_back(json::Value(id));
    for (const auto& task : r.programme.tasks)
        tasks.push_back(json::Value(json::Object{
            {"id", json::Value(task.id)},
            {"duration", number(task.duration)},
            {"early_start", number(task.early_start)},
            {"early_finish", number(task.early_finish)},
            {"late_start", number(task.late_start)},
            {"float", number(task.total_float)},
            {"critical", json::Value(task.critical)},
        }));

    json::Object root;
    root.emplace_back("scenario", json::Value(json::Object{
        {"name", json::Value(r.scenario.name)},
        {"budget", number(r.scenario.budget)},
        {"asked_for", number(asked)},
        {"candidates", number(static_cast<double>(r.scenario.projects.size()))},
        {"resources", json::Value(std::move(resources))},
    }));
    root.emplace_back("method", json::Value(sensitivity::name_of(r.method)));
    root.emplace_back("weights", json::Value(json::Object{
        {"lambda_max", number(r.ahp.lambda_max)},
        {"consistency_index", number(r.ahp.consistency_index)},
        {"consistency_ratio", number(r.ahp.consistency_ratio)},
        {"consistent", json::Value(r.ahp.consistent)},
        {"criteria", json::Value(std::move(criteria))},
    }));
    root.emplace_back("rankings", json::Value(json::Object{
        {"spearman", number(mcdm::spearman(rank_a, rank_t))},
        {"disagreements", number(static_cast<double>(disagreements))},
        {"projects", json::Value(std::move(ranking_rows))},
    }));
    root.emplace_back("portfolio", json::Value(json::Object{
        {"value", number(r.exact.value)},
        {"cost", number(r.exact.cost)},
        {"budget", number(r.instance.budget)},
        {"optimal", json::Value(r.exact.optimal)},
        {"nodes", number(static_cast<double>(r.exact.nodes))},
        {"greedy_value", number(r.greedy.value)},
        {"used", json::Value(std::move(used))},
        {"funded", json::Value(std::move(funded))},
        {"rejected", json::Value(std::move(rejected))},
    }));
    root.emplace_back("programme", json::Value(json::Object{
        {"duration", number(r.programme.makespan)},
        {"std_dev", number(r.programme.std_dev())},
        {"critical_path", json::Value(std::move(critical_path))},
        {"tasks", json::Value(std::move(tasks))},
    }));
    if (r.monte_carlo.samples > 0) {
        root.emplace_back("risk", json::Value(json::Object{
            {"samples", number(static_cast<double>(r.monte_carlo.samples))},
            {"cost", summary_json(r.monte_carlo.cost)},
            {"completion", summary_json(r.monte_carlo.completion)},
            {"probability_within_budget", number(r.monte_carlo.cost_within(r.scenario.budget))},
        }));
    }
    return json::Value(std::move(root));
}

std::string to_json_text(const Result& result) { return json::dump(to_json(result)); }

}  // namespace quorum::analyse
