// Console rendering of the results. Plain ASCII on purpose: the demo has to be
// readable on a Windows console with a legacy code page as well as on a UTF-8
// terminal, and a box-drawing character that renders as a question mark is
// worse than a dash.
#pragma once

#include <iosfwd>
#include <string>
#include <vector>

#include "quorum/mcdm.hpp"
#include "quorum/portfolio.hpp"
#include "quorum/risk.hpp"
#include "quorum/sensitivity.hpp"

namespace quorum::report {

// Fixed-width text table. Columns are sized to their widest cell.
class Table {
public:
    explicit Table(std::vector<std::string> headers);
    void row(std::vector<std::string> cells);
    void write(std::ostream& os, const std::string& indent = "  ") const;

private:
    std::vector<std::string> headers_;
    std::vector<std::vector<std::string>> rows_;
    std::vector<std::size_t> width_;
};

std::string number(double x, int decimals = 2);

void heading(std::ostream& os, const std::string& text);

void scenario_summary(std::ostream& os, const model::Scenario& sc);

void weights(std::ostream& os, const model::Scenario& sc, const mcdm::AhpResult& ahp);

void rankings(std::ostream& os, const model::Scenario& sc, const std::vector<double>& ahp,
              const std::vector<double>& topsis);

void portfolio(std::ostream& os, const model::Scenario& sc, const portfolio::Instance& inst,
               const portfolio::Solution& exact, const portfolio::Solution& greedy);

void programme(std::ostream& os, const model::Scenario& sc,
               const std::vector<std::size_t>& selected);

void risk(std::ostream& os, const model::Scenario& sc, const quorum::risk::Result& r);

void sensitivity(std::ostream& os, const std::string& title, const sensitivity::Result& r);

// A grid of which projects were funded under which run of a sweep.
void membership(std::ostream& os, const model::Scenario& sc, const sensitivity::Result& r);

}  // namespace quorum::report
