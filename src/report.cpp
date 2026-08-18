#include "quorum/report.hpp"

#include <algorithm>
#include <cstdio>
#include <ostream>

namespace quorum::report {
namespace {

std::string pad(const std::string& s, std::size_t width) {
    return s.size() >= width ? s : s + std::string(width - s.size(), ' ');
}

}  // namespace

Table::Table(std::vector<std::string> headers) : headers_(std::move(headers)) {
    width_.reserve(headers_.size());
    for (const auto& h : headers_) width_.push_back(h.size());
}

void Table::row(std::vector<std::string> cells) {
    cells.resize(headers_.size());
    for (std::size_t i = 0; i < cells.size(); ++i) width_[i] = std::max(width_[i], cells[i].size());
    rows_.push_back(std::move(cells));
}

void Table::write(std::ostream& os, const std::string& indent) const {
    std::string rule;
    for (std::size_t i = 0; i < width_.size(); ++i) {
        rule += std::string(width_[i], '-');
        if (i + 1 < width_.size()) rule += "  ";
    }
    os << indent;
    for (std::size_t i = 0; i < headers_.size(); ++i) {
        os << pad(headers_[i], width_[i]);
        if (i + 1 < headers_.size()) os << "  ";
    }
    os << "\n" << indent << rule << "\n";
    for (const auto& r : rows_) {
        os << indent;
        for (std::size_t i = 0; i < r.size(); ++i) {
            os << pad(r[i], width_[i]);
            if (i + 1 < r.size()) os << "  ";
        }
        os << "\n";
    }
}

std::string number(double x, int decimals) {
    char buf[48];
    std::snprintf(buf, sizeof buf, "%.*f", decimals, x);
    return buf;
}

void heading(std::ostream& os, const std::string& text) {
    os << "\n" << text << "\n" << std::string(text.size(), '=') << "\n\n";
}

void scenario_summary(std::ostream& os, const model::Scenario& sc) {
    os << "Scenario   : " << sc.name << "\n";
    os << "Candidates : " << sc.projects.size() << " projects, " << sc.criteria.size()
       << " criteria\n";
    os << "Budget     : " << number(sc.budget, 0) << "\n";
    double total = 0.0;
    for (const auto& p : sc.projects) total += p.expected_cost;
    os << "Asked for  : " << number(total, 0) << " (" << number(100.0 * sc.budget / total, 1)
       << " per cent of the ask is fundable)\n";
    for (const auto& r : sc.resources)
        os << "Capacity   : " << r.id << " = " << number(r.capacity, 1) << " (" << r.label << ")\n";
}

void weights(std::ostream& os, const model::Scenario& sc, const mcdm::AhpResult& ahp) {
    Table t({"criterion", "direction", "weight", "label"});
    for (std::size_t i = 0; i < sc.criteria.size(); ++i)
        t.row({sc.criteria[i].id, sc.criteria[i].maximise ? "max" : "min",
               number(ahp.weights[i], 4), sc.criteria[i].label});
    t.write(os);
    os << "\n  principal eigenvalue " << number(ahp.lambda_max, 4) << ", consistency index "
       << number(ahp.consistency_index, 4) << ", consistency ratio "
       << number(ahp.consistency_ratio, 4) << "\n";
    os << "  judgement matrix " << (ahp.consistent ? "accepted" : "REJECTED")
       << " against the 0.10 threshold\n";
}

void rankings(std::ostream& os, const model::Scenario& sc, const std::vector<double>& ahp,
              const std::vector<double>& topsis) {
    const auto rank_a = mcdm::rank_of(ahp);
    const auto rank_t = mcdm::rank_of(topsis);
    const auto order = mcdm::order_of(ahp);

    Table t({"project", "AHP score", "rank", "TOPSIS score", "rank", "shift"});
    for (std::size_t i : order) {
        const long shift = static_cast<long>(rank_a[i]) - static_cast<long>(rank_t[i]);
        std::string mark = shift == 0 ? "." : (shift > 0 ? "+" : "-");
        if (shift != 0) mark += std::to_string(std::abs(shift));
        t.row({sc.projects[i].id, number(ahp[i], 4), std::to_string(rank_a[i]),
               number(topsis[i], 4), std::to_string(rank_t[i]), mark});
    }
    t.write(os);
    os << "\n  Spearman rank correlation between the two rankings: "
       << number(mcdm::spearman(rank_a, rank_t), 4) << "\n";
    std::size_t disagreements = 0;
    for (std::size_t i = 0; i < rank_a.size(); ++i)
        if (rank_a[i] != rank_t[i]) ++disagreements;
    os << "  projects placed differently by the two methods: " << disagreements << " of "
       << rank_a.size() << "\n";
}

void portfolio(std::ostream& os, const model::Scenario& sc, const portfolio::Instance& inst,
               const portfolio::Solution& exact, const portfolio::Solution& greedy) {
    Table t({"funded", "cost", "duration", "value", "requires"});
    for (std::size_t i : exact.selected) {
        std::string requires_list;
        for (const auto& d : sc.projects[i].requires_projects) {
            if (!requires_list.empty()) requires_list += ", ";
            requires_list += d;
        }
        t.row({sc.projects[i].id, number(sc.projects[i].expected_cost, 0),
               number(sc.projects[i].expected_duration, 1), number(inst.value[i], 4),
               requires_list.empty() ? "-" : requires_list});
    }
    t.write(os);

    os << "\n  total value " << number(exact.value, 4) << ", cost " << number(exact.cost, 0)
       << " of " << number(inst.budget, 0) << " budget";
    for (std::size_t r = 0; r < inst.capacity.size(); ++r)
        os << ", " << sc.resources[r].id << " " << number(exact.used[r], 1) << " of "
           << number(inst.capacity[r], 1);
    os << "\n";
    os << "  branch and bound: " << (exact.optimal ? "proved optimal" : "stopped early") << ", "
       << exact.nodes << " nodes, " << number(exact.seconds * 1000.0, 3) << " ms\n";
    os << "  greedy heuristic: value " << number(greedy.value, 4) << " ("
       << number(exact.value > 0.0 ? 100.0 * greedy.value / exact.value : 100.0, 2)
       << " per cent of the optimum), " << number(greedy.seconds * 1000.0, 3) << " ms\n";

    os << "\n  Not funded, and why:\n";
    Table r({"project", "cost", "value", "reason"});
    for (std::size_t i = 0; i < sc.projects.size(); ++i) {
        if (exact.chosen[i]) continue;
        r.row({sc.projects[i].id, number(sc.projects[i].expected_cost, 0), number(inst.value[i], 4),
               portfolio::rejection_reason(inst, sc, exact, i)});
    }
    r.write(os, "    ");
}

void programme(std::ostream& os, const model::Scenario& sc,
               const std::vector<std::size_t>& selected) {
    const cpm::Network net = model::programme_network(sc, selected);
    const cpm::Schedule sched = cpm::schedule(net);

    Table t({"project", "duration", "early start", "early finish", "late start", "float",
             "critical"});
    for (const auto& task : sched.tasks)
        t.row({task.id, number(task.duration, 1), number(task.early_start, 1),
               number(task.early_finish, 1), number(task.late_start, 1),
               number(task.total_float, 1), task.critical ? "yes" : ""});
    t.write(os);

    os << "\n  programme duration " << number(sched.makespan, 1) << " periods\n";
    os << "  critical path     : ";
    for (std::size_t i = 0; i < sched.critical_path.size(); ++i)
        os << (i ? " -> " : "") << sched.critical_path[i];
    os << "\n";
    os << "  PERT deviation    : " << number(sched.std_dev(), 2) << " periods, so a "
       << number(100.0 * sched.probability_by(sched.makespan + sched.std_dev()), 1)
       << " per cent chance of finishing within " << number(sched.makespan + sched.std_dev(), 1)
       << "\n";
}

void risk(std::ostream& os, const model::Scenario& sc, const quorum::risk::Result& r) {
    Table t({"quantity", "mean", "std dev", "p05", "p50", "p95"});
    t.row({"total cost", number(r.cost.mean, 0), number(r.cost.std_dev, 1), number(r.cost.p05, 0),
           number(r.cost.p50, 0), number(r.cost.p95, 0)});
    t.row({"completion", number(r.completion.mean, 2), number(r.completion.std_dev, 2),
           number(r.completion.p05, 2), number(r.completion.p50, 2), number(r.completion.p95, 2)});
    t.write(os);
    os << "\n  " << r.samples << " samples in " << number(r.seconds * 1000.0, 1) << " ms\n";
    os << "  90 per cent central interval on cost      : [" << number(r.cost.p05, 0) << ", "
       << number(r.cost.p95, 0) << "]\n";
    os << "  90 per cent central interval on completion: [" << number(r.completion.p05, 2) << ", "
       << number(r.completion.p95, 2) << "] periods\n";
    os << "  probability the portfolio stays within the budget of " << number(sc.budget, 0) << ": "
       << number(100.0 * r.cost_within(sc.budget), 1) << " per cent\n";
}

void sensitivity(std::ostream& os, const std::string& title, const sensitivity::Result& r) {
    os << "  " << title << ", " << r.points.size() << " runs in "
       << number(r.seconds * 1000.0, 1) << " ms\n\n";
    Table t({"project", "selected in", "share", "verdict"});
    for (const auto& row : r.rows)
        t.row({row.project_id,
               std::to_string(row.times_selected) + " of " + std::to_string(r.points.size()),
               number(100.0 * row.frequency, 1) + " %", sensitivity::name_of(row.verdict)});
    t.write(os);
}

void membership(std::ostream& os, const model::Scenario& sc, const sensitivity::Result& r) {
    // One column per run, one row per project. The share alone says how often a
    // project was funded; this says under which assumptions, which is what makes
    // an interval of budgets readable rather than a single percentage.
    std::vector<std::string> headers{"project"};
    for (const auto& p : r.points) headers.push_back(p.label);
    Table t(std::move(headers));
    for (std::size_t i = 0; i < sc.projects.size(); ++i) {
        std::vector<std::string> cells{sc.projects[i].id};
        for (const auto& p : r.points) {
            const bool in = std::find(p.selected.begin(), p.selected.end(), i) != p.selected.end();
            cells.push_back(in ? "x" : ".");
        }
        t.row(std::move(cells));
    }
    t.write(os);
}

}  // namespace quorum::report
