// Multi-criteria decision analysis: the analytic hierarchy process and TOPSIS.
//
// Both methods take the same projects-by-criteria matrix and return one score
// per project, so they can be run on identical input and compared. That
// comparison is the point: the two do not always agree, and the disagreement is
// a result rather than a defect.
#pragma once

#include <stdexcept>
#include <string>
#include <vector>

namespace quorum::mcdm {

using Matrix = std::vector<std::vector<double>>;
// One flag per criterion: true when a larger raw score is better.
using Directions = std::vector<bool>;

class McdmError : public std::runtime_error {
public:
    explicit McdmError(const std::string& what) : std::runtime_error(what) {}
};

// Raised when the pairwise judgements are too inconsistent to be used. The
// weights derived from such a matrix are arithmetic without meaning, so the
// method refuses rather than returning them.
class InconsistentJudgementError : public McdmError {
public:
    InconsistentJudgementError(double ratio, double threshold);
    double consistency_ratio() const { return ratio_; }
    double threshold() const { return threshold_; }

private:
    double ratio_;
    double threshold_;
};

struct AhpResult {
    std::vector<double> weights;      // principal eigenvector, normalised to sum one
    double lambda_max = 0.0;          // principal eigenvalue
    double consistency_index = 0.0;   // (lambda_max - n) / (n - 1)
    double consistency_ratio = 0.0;   // consistency index over the random index
    bool consistent = false;          // ratio at or below the threshold
};

// Saaty's average consistency index of a randomly filled reciprocal matrix of
// order n. Values are the published table for n up to ten; larger orders reuse
// the value for ten, and an order that large is a sign the criteria should be
// grouped rather than compared flat.
double random_index(std::size_t n);

// The default rejection threshold. Saaty's rule of thumb is ten per cent.
inline constexpr double kConsistencyThreshold = 0.10;

// Derives the weights and the consistency measures. Does not reject: the caller
// decides, and the report prints the ratio either way.
AhpResult analyse_comparisons(const Matrix& comparisons);

// Same, but refuses an inconsistent matrix.
std::vector<double> ahp_weights(const Matrix& comparisons,
                                double threshold = kConsistencyThreshold);

// AHP synthesis in distributive mode: each criterion column is normalised by its
// own sum, then combined with the criteria weights. Columns where a smaller
// value is better are inverted before normalising, which is the standard
// treatment of a cost criterion when no separate pairwise comparison of the
// alternatives is available.
std::vector<double> ahp_scores(const Matrix& scores, const std::vector<double>& weights,
                               const Directions& maximise);

// TOPSIS: vector normalisation, weighting, then relative closeness to the ideal
// point and distance from the anti-ideal point.
std::vector<double> topsis_scores(const Matrix& scores, const std::vector<double>& weights,
                                  const Directions& maximise);

// Rank one, best first. Equal scores take the same rank.
std::vector<std::size_t> rank_of(const std::vector<double>& scores);

// Indices sorted by score, best first.
std::vector<std::size_t> order_of(const std::vector<double>& scores);

// Spearman rank correlation between two rankings of the same items.
double spearman(const std::vector<std::size_t>& a, const std::vector<std::size_t>& b);

}  // namespace quorum::mcdm
