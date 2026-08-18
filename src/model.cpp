#include "quorum/model.hpp"

#include <algorithm>
#include <set>

namespace quorum::model {
namespace {

void require(bool condition, const std::string& message) {
    if (!condition) throw ModelError(message);
}

// Reads a three-point estimate. A scenario may give a single value, in which
// case all three points collapse onto it and the variance is zero. That keeps
// one code path for both deterministic and uncertain input.
void read_three_point(const json::Value& v, const std::string& where, const std::string& single,
                      const std::string& lo, const std::string& mid, const std::string& hi,
                      double& out_lo, double& out_mid, double& out_hi) {
    const double point = v.number_or(single, -1.0, where);
    out_mid = v.number_or(mid, point, where);
    require(out_mid >= 0.0, where + ": missing or negative '" + single + "'");
    out_lo = v.number_or(lo, out_mid, where);
    out_hi = v.number_or(hi, out_mid, where);
    require(out_lo <= out_mid && out_mid <= out_hi,
            where + ": the three-point estimate must satisfy " + lo + " <= " + mid + " <= " + hi);
    require(out_lo >= 0.0, where + ": negative estimate");
}

std::vector<std::string> read_string_list(const json::Value& parent, const std::string& key,
                                          const std::string& where) {
    std::vector<std::string> out;
    const json::Value* v = parent.find(key);
    if (!v || v->is_null()) return out;
    for (const auto& item : v->as_array(where + "." + key))
        out.push_back(item.as_string(where + "." + key + " item"));
    return out;
}

}  // namespace

cpm::Network Project::network() const {
    cpm::Network net;
    net.tasks.reserve(activities.size());
    for (const auto& a : activities)
        net.tasks.push_back(cpm::Task{a.id, a.optimistic, a.likely, a.pessimistic, a.depends_on});
    return net;
}

std::size_t Scenario::project_index(const std::string& id) const {
    for (std::size_t i = 0; i < projects.size(); ++i)
        if (projects[i].id == id) return i;
    throw ModelError("unknown project '" + id + "'");
}

std::size_t Scenario::criterion_index(const std::string& id) const {
    for (std::size_t i = 0; i < criteria.size(); ++i)
        if (criteria[i].id == id) return i;
    throw ModelError("unknown criterion '" + id + "'");
}

std::vector<std::size_t> Scenario::funding_order() const {
    cpm::Network net;
    net.tasks.reserve(projects.size());
    for (const auto& p : projects)
        net.tasks.push_back(cpm::Task{p.id, 0.0, 0.0, 0.0, p.requires_projects});
    return cpm::topological_order(net);
}

std::vector<std::vector<double>> Scenario::score_matrix() const {
    std::vector<std::vector<double>> m;
    m.reserve(projects.size());
    for (const auto& p : projects) m.push_back(p.scores);
    return m;
}

Scenario load(const json::Value& doc) {
    require(doc.is_object(), "the scenario document must be an object");
    Scenario sc;
    sc.name = doc.string_or("name", "unnamed scenario", "scenario");

    // --- criteria -----------------------------------------------------------
    const auto& criteria = doc.at("criteria", "scenario").as_array("scenario.criteria");
    require(!criteria.empty(), "scenario.criteria must not be empty");
    for (const auto& c : criteria) {
        Criterion cr;
        cr.id = c.at("id", "criterion").as_string("criterion.id");
        cr.label = c.string_or("label", cr.id, "criterion");
        const std::string dir = c.string_or("direction", "max", "criterion");
        require(dir == "max" || dir == "min",
                "criterion '" + cr.id + "': direction must be 'max' or 'min'");
        cr.maximise = (dir == "max");
        cr.weight = c.number_or("weight", 0.0, "criterion");
        require(cr.weight >= 0.0, "criterion '" + cr.id + "': negative weight");
        for (const auto& seen : sc.criteria)
            require(seen.id != cr.id, "duplicate criterion identifier '" + cr.id + "'");
        sc.criteria.push_back(cr);
    }

    double weight_sum = 0.0;
    for (const auto& c : sc.criteria) weight_sum += c.weight;
    if (weight_sum > 0.0)
        for (auto& c : sc.criteria) c.weight /= weight_sum;

    // --- pairwise comparisons, optional ------------------------------------
    if (const json::Value* cmp = doc.find("comparisons"); cmp && !cmp->is_null()) {
        const auto& rows = cmp->as_array("scenario.comparisons");
        require(rows.size() == sc.criteria.size(),
                "scenario.comparisons must be square over the criteria");
        for (const auto& row : rows) {
            const auto& cells = row.as_array("scenario.comparisons row");
            require(cells.size() == sc.criteria.size(),
                    "scenario.comparisons must be square over the criteria");
            std::vector<double> values;
            for (const auto& cell : cells) {
                const double x = cell.as_number("scenario.comparisons cell");
                require(x > 0.0, "scenario.comparisons: entries must be positive");
                values.push_back(x);
            }
            sc.comparisons.push_back(std::move(values));
        }
    }
    require(!sc.comparisons.empty() || weight_sum > 0.0,
            "the scenario gives neither criterion weights nor a comparison matrix");

    // --- resources ----------------------------------------------------------
    if (const json::Value* res = doc.find("resources"); res && !res->is_null()) {
        for (const auto& r : res->as_array("scenario.resources")) {
            Resource rr;
            rr.id = r.at("id", "resource").as_string("resource.id");
            rr.label = r.string_or("label", rr.id, "resource");
            rr.capacity = r.number_or("capacity", -1.0, "resource");
            require(rr.capacity >= 0.0, "resource '" + rr.id + "': missing or negative capacity");
            sc.resources.push_back(rr);
        }
    }

    sc.budget = doc.number_or("budget", -1.0, "scenario");
    require(sc.budget >= 0.0, "scenario.budget is missing or negative");

    // --- projects -----------------------------------------------------------
    const auto& projects = doc.at("projects", "scenario").as_array("scenario.projects");
    require(!projects.empty(), "scenario.projects must not be empty");
    for (const auto& p : projects) {
        Project pr;
        pr.id = p.at("id", "project").as_string("project.id");
        pr.name = p.string_or("name", pr.id, "project");
        const std::string where = "project '" + pr.id + "'";
        for (const auto& seen : sc.projects)
            require(seen.id != pr.id, "duplicate project identifier '" + pr.id + "'");

        const auto& scores = p.at("scores", where).as_object(where + ".scores");
        pr.scores.assign(sc.criteria.size(), 0.0);
        std::vector<char> given(sc.criteria.size(), 0);
        for (const auto& entry : scores) {
            const std::size_t k = sc.criterion_index(entry.first);
            pr.scores[k] = entry.second.as_number(where + ".scores." + entry.first);
            require(pr.scores[k] >= 0.0, where + ": negative score for '" + entry.first + "'");
            given[k] = 1;
        }
        for (std::size_t k = 0; k < sc.criteria.size(); ++k)
            require(given[k], where + ": no score for criterion '" + sc.criteria[k].id + "'");

        pr.requires_projects = read_string_list(p, "requires", where);
        pr.excludes_projects = read_string_list(p, "excludes", where);

        const auto& activities = p.at("activities", where).as_array(where + ".activities");
        require(!activities.empty(), where + ": must have at least one activity");
        for (const auto& a : activities) {
            Activity act;
            act.id = a.at("id", where + ".activity").as_string("activity.id");
            act.name = a.string_or("name", act.id, "activity");
            const std::string awhere = where + ", activity '" + act.id + "'";
            for (const auto& seen : pr.activities)
                require(seen.id != act.id, awhere + ": duplicate activity identifier");

            read_three_point(a, awhere, "duration", "optimistic", "likely", "pessimistic",
                             act.optimistic, act.likely, act.pessimistic);
            read_three_point(a, awhere, "cost", "cost_optimistic", "cost_likely",
                             "cost_pessimistic", act.cost_optimistic, act.cost_likely,
                             act.cost_pessimistic);

            act.depends_on = read_string_list(a, "depends_on", awhere);
            act.demand.assign(sc.resources.size(), 0.0);
            if (const json::Value* d = a.find("demand"); d && !d->is_null()) {
                for (const auto& entry : d->as_object(awhere + ".demand")) {
                    bool found = false;
                    for (std::size_t r = 0; r < sc.resources.size(); ++r) {
                        if (sc.resources[r].id != entry.first) continue;
                        act.demand[r] = entry.second.as_number(awhere + ".demand." + entry.first);
                        require(act.demand[r] >= 0.0, awhere + ": negative demand");
                        found = true;
                        break;
                    }
                    require(found, awhere + ": demand for unknown resource '" + entry.first + "'");
                }
            }
            pr.activities.push_back(std::move(act));
        }
        sc.projects.push_back(std::move(pr));
    }

    // --- cross references ---------------------------------------------------
    for (const auto& p : sc.projects) {
        for (const auto& dep : p.requires_projects) {
            if (dep == p.id) throw ModelError("project '" + p.id + "' requires itself");
            sc.project_index(dep);  // throws when unknown
        }
        for (const auto& ex : p.excludes_projects) {
            if (ex == p.id) throw ModelError("project '" + p.id + "' excludes itself");
            sc.project_index(ex);
        }
    }
    // Acyclicity of the funding graph. A cycle makes every project in it
    // unfundable, which is a modelling mistake rather than a hard constraint,
    // so it is rejected at load time with the cycle named.
    sc.funding_order();

    // A required project that is also mutually exclusive with the requiring one
    // is contradictory: the pair can never both hold, so the dependant can never
    // be funded. Catching it here beats returning an empty portfolio later.
    for (const auto& p : sc.projects)
        for (const auto& dep : p.requires_projects)
            for (const auto& ex : p.excludes_projects)
                require(dep != ex, "project '" + p.id + "' both requires and excludes '" + dep + "'");

    // --- derived quantities -------------------------------------------------
    for (auto& p : sc.projects) {
        const cpm::Network net = p.network();
        p.schedule = cpm::schedule(net);  // throws CycleError on a cyclic activity graph
        p.expected_duration = p.schedule.makespan;
        p.expected_cost = 0.0;
        for (const auto& a : p.activities) p.expected_cost += a.expected_cost();
        p.peak_demand.assign(sc.resources.size(), 0.0);
        for (std::size_t r = 0; r < sc.resources.size(); ++r) {
            std::vector<double> demand;
            demand.reserve(p.activities.size());
            for (const auto& a : p.activities) demand.push_back(a.demand[r]);
            p.peak_demand[r] = cpm::peak_demand(net, p.schedule, demand);
        }
    }

    return sc;
}

Scenario load_file(const std::string& path) { return load(json::parse_file(path)); }

cpm::Network programme_network(const Scenario& sc, const std::vector<std::size_t>& selected) {
    std::set<std::string> included;
    for (std::size_t i : selected) included.insert(sc.projects[i].id);

    cpm::Network net;
    net.tasks.reserve(selected.size());
    for (std::size_t i : selected) {
        const Project& p = sc.projects[i];
        cpm::Task t;
        t.id = p.id;
        // The programme node inherits the project's own critical path length as
        // its duration, and the spread of that path as its three points, so the
        // PERT variance survives the level change instead of being reset.
        const double spread = 3.0 * p.schedule.std_dev();
        t.likely = p.expected_duration;
        t.optimistic = std::max(0.0, p.expected_duration - spread);
        t.pessimistic = p.expected_duration + spread;
        for (const auto& dep : p.requires_projects)
            if (included.count(dep)) t.depends_on.push_back(dep);
        net.tasks.push_back(std::move(t));
    }
    return net;
}

}  // namespace quorum::model
