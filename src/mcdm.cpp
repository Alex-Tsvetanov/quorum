#include "quorum/mcdm.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace quorum::mcdm {
namespace {

// A scenario writes 1/3 as 0.333, so the reciprocity check has to tolerate the
// rounding a human puts in the file. Two per cent admits three decimal places
// and still catches a genuinely wrong entry.
constexpr double kReciprocalTolerance = 0.02;
constexpr int kPowerIterations = 500;
constexpr double kConverged = 1e-14;

void check_square_positive(const Matrix& m) {
    if (m.empty()) throw McdmError("the comparison matrix is empty");
    for (const auto& row : m) {
        if (row.size() != m.size()) throw McdmError("the comparison matrix is not square");
        for (double x : row)
            if (!(x > 0.0)) throw McdmError("comparison entries must be positive");
    }
    for (std::size_t i = 0; i < m.size(); ++i) {
        if (std::fabs(m[i][i] - 1.0) > kReciprocalTolerance)
            throw McdmError("the comparison matrix must have ones on the diagonal");
        for (std::size_t j = i + 1; j < m.size(); ++j) {
            const double product = m[i][j] * m[j][i];
            if (std::fabs(product - 1.0) > kReciprocalTolerance)
                throw McdmError("the comparison matrix is not reciprocal at (" +
                                std::to_string(i) + "," + std::to_string(j) + ")");
        }
    }
}

void check_inputs(const Matrix& scores, const std::vector<double>& weights,
                  const Directions& maximise) {
    if (scores.empty()) throw McdmError("no alternatives to score");
    const std::size_t k = weights.size();
    if (k == 0) throw McdmError("no criteria weights");
    if (maximise.size() != k) throw McdmError("direction vector does not match the weights");
    for (const auto& row : scores)
        if (row.size() != k) throw McdmError("a score row does not match the number of criteria");
    double sum = 0.0;
    for (double w : weights) {
        if (w < 0.0) throw McdmError("negative criterion weight");
        sum += w;
    }
    if (sum <= 0.0) throw McdmError("the criteria weights sum to zero");
}

std::vector<double> normalised(std::vector<double> v) {
    const double sum = std::accumulate(v.begin(), v.end(), 0.0);
    if (sum > 0.0)
        for (double& x : v) x /= sum;
    return v;
}

}  // namespace

InconsistentJudgementError::InconsistentJudgementError(double ratio, double threshold)
    : McdmError("the pairwise judgements are inconsistent: consistency ratio " +
                std::to_string(ratio) + " exceeds the threshold " + std::to_string(threshold)),
      ratio_(ratio),
      threshold_(threshold) {}

double random_index(std::size_t n) {
    static const double table[] = {0.00, 0.00, 0.00, 0.58, 0.90, 1.12,
                                   1.24, 1.32, 1.41, 1.45, 1.49};
    const std::size_t last = sizeof(table) / sizeof(table[0]) - 1;
    return table[std::min(n, last)];
}

//LSTBEGINahp
AhpResult analyse_comparisons(const Matrix& m) {
    check_square_positive(m);
    const std::size_t n = m.size();

    // Principal eigenvector by power iteration. The matrix is positive, so
    // Perron-Frobenius guarantees a single dominant eigenvalue and convergence
    // from any positive start vector.
    std::vector<double> w(n, 1.0 / static_cast<double>(n));
    for (int iter = 0; iter < kPowerIterations; ++iter) {
        std::vector<double> next(n, 0.0);
        for (std::size_t i = 0; i < n; ++i)
            for (std::size_t j = 0; j < n; ++j) next[i] += m[i][j] * w[j];
        next = normalised(std::move(next));
        double delta = 0.0;
        for (std::size_t i = 0; i < n; ++i) delta = std::max(delta, std::fabs(next[i] - w[i]));
        w.swap(next);
        if (delta < kConverged) break;
    }

    AhpResult out;
    out.weights = w;

    // lambda_max as the mean of (A w)_i / w_i, the standard estimate.
    double lambda = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        double row = 0.0;
        for (std::size_t j = 0; j < n; ++j) row += m[i][j] * w[j];
        lambda += row / w[i];
    }
    out.lambda_max = lambda / static_cast<double>(n);

    if (n > 2) {
        out.consistency_index =
            (out.lambda_max - static_cast<double>(n)) / static_cast<double>(n - 1);
        const double ri = random_index(n);
        out.consistency_ratio = ri > 0.0 ? out.consistency_index / ri : 0.0;
    } else {
        // Orders one and two are consistent by construction: there is no triple
        // of judgements that could contradict itself.
        out.consistency_index = 0.0;
        out.consistency_ratio = 0.0;
    }
    out.consistent = out.consistency_ratio <= kConsistencyThreshold;
    return out;
}
//LSTENDahp

std::vector<double> ahp_weights(const Matrix& comparisons, double threshold) {
    AhpResult r = analyse_comparisons(comparisons);
    if (r.consistency_ratio > threshold)
        throw InconsistentJudgementError(r.consistency_ratio, threshold);
    return r.weights;
}

std::vector<double> ahp_scores(const Matrix& scores, const std::vector<double>& weights,
                               const Directions& maximise) {
    check_inputs(scores, weights, maximise);
    const std::size_t m = scores.size(), k = weights.size();
    const std::vector<double> w = normalised(weights);

    Matrix contribution(m, std::vector<double>(k, 0.0));
    for (std::size_t j = 0; j < k; ++j) {
        std::vector<double> column(m);
        for (std::size_t i = 0; i < m; ++i) {
            double x = scores[i][j];
            if (!maximise[j]) {
                // A cost criterion is inverted before normalising. A zero cost
                // would dominate every alternative, so it is floored at the
                // smallest positive value in the column rather than at an
                // arbitrary constant.
                double smallest = 0.0;
                for (std::size_t r = 0; r < m; ++r)
                    if (scores[r][j] > 0.0 && (smallest == 0.0 || scores[r][j] < smallest))
                        smallest = scores[r][j];
                if (smallest == 0.0) smallest = 1.0;
                x = 1.0 / std::max(x, smallest);
            }
            column[i] = x;
        }
        const double sum = std::accumulate(column.begin(), column.end(), 0.0);
        for (std::size_t i = 0; i < m; ++i)
            contribution[i][j] = sum > 0.0 ? column[i] / sum : 1.0 / static_cast<double>(m);
    }

    std::vector<double> out(m, 0.0);
    for (std::size_t i = 0; i < m; ++i)
        for (std::size_t j = 0; j < k; ++j) out[i] += w[j] * contribution[i][j];
    return out;
}

std::vector<double> topsis_scores(const Matrix& scores, const std::vector<double>& weights,
                                  const Directions& maximise) {
    check_inputs(scores, weights, maximise);
    const std::size_t m = scores.size(), k = weights.size();
    const std::vector<double> w = normalised(weights);

    // Vector normalisation, then weighting.
    Matrix v(m, std::vector<double>(k, 0.0));
    for (std::size_t j = 0; j < k; ++j) {
        double norm = 0.0;
        for (std::size_t i = 0; i < m; ++i) norm += scores[i][j] * scores[i][j];
        norm = std::sqrt(norm);
        for (std::size_t i = 0; i < m; ++i)
            v[i][j] = norm > 0.0 ? w[j] * scores[i][j] / norm : 0.0;
    }

    std::vector<double> best(k), worst(k);
    for (std::size_t j = 0; j < k; ++j) {
        double hi = v[0][j], lo = v[0][j];
        for (std::size_t i = 1; i < m; ++i) {
            hi = std::max(hi, v[i][j]);
            lo = std::min(lo, v[i][j]);
        }
        best[j] = maximise[j] ? hi : lo;
        worst[j] = maximise[j] ? lo : hi;
    }

    std::vector<double> out(m, 0.0);
    for (std::size_t i = 0; i < m; ++i) {
        double to_best = 0.0, to_worst = 0.0;
        for (std::size_t j = 0; j < k; ++j) {
            const double db = v[i][j] - best[j];
            const double dw = v[i][j] - worst[j];
            to_best += db * db;
            to_worst += dw * dw;
        }
        to_best = std::sqrt(to_best);
        to_worst = std::sqrt(to_worst);
        const double total = to_best + to_worst;
        // Every alternative identical means every distance is zero. Calling that
        // a tie at one is the only answer that does not divide by zero.
        out[i] = total > 0.0 ? to_worst / total : 1.0;
    }
    return out;
}

std::vector<std::size_t> order_of(const std::vector<double>& scores) {
    std::vector<std::size_t> idx(scores.size());
    std::iota(idx.begin(), idx.end(), std::size_t{0});
    std::stable_sort(idx.begin(), idx.end(),
                     [&](std::size_t a, std::size_t b) { return scores[a] > scores[b]; });
    return idx;
}

std::vector<std::size_t> rank_of(const std::vector<double>& scores) {
    const auto idx = order_of(scores);
    std::vector<std::size_t> rank(scores.size(), 0);
    std::size_t position = 0;
    while (position < idx.size()) {
        std::size_t end = position + 1;
        while (end < idx.size() && scores[idx[end]] == scores[idx[position]]) ++end;
        for (std::size_t k = position; k < end; ++k) rank[idx[k]] = position + 1;
        position = end;
    }
    return rank;
}

double spearman(const std::vector<std::size_t>& a, const std::vector<std::size_t>& b) {
    if (a.size() != b.size()) throw McdmError("rankings of different length");
    const double n = static_cast<double>(a.size());
    if (n < 2.0) return 1.0;
    double sum = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const double d = static_cast<double>(a[i]) - static_cast<double>(b[i]);
        sum += d * d;
    }
    return 1.0 - 6.0 * sum / (n * (n * n - 1.0));
}

}  // namespace quorum::mcdm
