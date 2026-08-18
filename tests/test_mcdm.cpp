#include <cmath>

#include "check.hpp"
#include "quorum/mcdm.hpp"

using namespace quorum;

TEST(mcdm, "a perfectly consistent matrix has ratio zero and the weights it was built from") {
    // Built as a_ij = w_i / w_j from w = (0.5, 0.3, 0.2), so the judgements are
    // consistent by construction and the eigenvector must recover w exactly.
    const std::vector<double> w{0.5, 0.3, 0.2};
    mcdm::Matrix m(3, std::vector<double>(3, 0.0));
    for (std::size_t i = 0; i < 3; ++i)
        for (std::size_t j = 0; j < 3; ++j) m[i][j] = w[i] / w[j];

    const auto r = mcdm::analyse_comparisons(m);
    CHECK_NEAR(r.lambda_max, 3.0, 1e-9);
    CHECK_NEAR(r.consistency_index, 0.0, 1e-9);
    CHECK_NEAR(r.consistency_ratio, 0.0, 1e-9);
    CHECK_TRUE(r.consistent);
    for (std::size_t i = 0; i < 3; ++i) CHECK_NEAR(r.weights[i], w[i], 1e-9);
}

TEST(mcdm, "an inconsistent judgement matrix is rejected instead of scored") {
    // A beats B, B beats C, and C beats A, each by the strongest margin on the
    // scale. There is no weight vector behind judgements like these.
    const mcdm::Matrix m{{1.0, 9.0, 1.0 / 9.0}, {1.0 / 9.0, 1.0, 9.0}, {9.0, 1.0 / 9.0, 1.0}};
    const auto r = mcdm::analyse_comparisons(m);
    CHECK_TRUE(r.consistency_ratio > mcdm::kConsistencyThreshold);
    CHECK_FALSE(r.consistent);
    CHECK_THROWS(mcdm::ahp_weights(m), mcdm::InconsistentJudgementError);

    double reported = 0.0;
    try {
        mcdm::ahp_weights(m);
    } catch (const mcdm::InconsistentJudgementError& e) {
        reported = e.consistency_ratio();
    }
    CHECK_NEAR(reported, r.consistency_ratio, 1e-12);
}

TEST(mcdm, "a mildly inconsistent matrix is still accepted") {
    // The bundled scenario's judgements: not built from a ratio vector, but
    // close enough to one that the ratio stays under the threshold.
    const mcdm::Matrix m{{1, 0.5, 0.5, 2}, {2, 1, 1, 3}, {2, 1, 1, 3}, {0.5, 0.333, 0.333, 1}};
    const auto r = mcdm::analyse_comparisons(m);
    CHECK_TRUE(r.consistency_ratio < mcdm::kConsistencyThreshold);
    CHECK_TRUE(r.consistent);
    double sum = 0.0;
    for (double x : r.weights) sum += x;
    CHECK_NEAR(sum, 1.0, 1e-9);
    // The two criteria judged equal throughout must come out equal.
    CHECK_NEAR(r.weights[1], r.weights[2], 1e-9);
}

TEST(mcdm, "a matrix that is not reciprocal or not square is an input error, not inconsistency") {
    CHECK_THROWS(mcdm::analyse_comparisons({{1.0, 3.0}, {3.0, 1.0}}), mcdm::McdmError);
    CHECK_THROWS(mcdm::analyse_comparisons({{1.0, 3.0}}), mcdm::McdmError);
    CHECK_THROWS(mcdm::analyse_comparisons({{1.0, 0.0}, {0.0, 1.0}}), mcdm::McdmError);
    CHECK_THROWS(mcdm::analyse_comparisons({}), mcdm::McdmError);
}

TEST(mcdm, "orders one and two are consistent by construction") {
    const auto one = mcdm::analyse_comparisons({{1.0}});
    CHECK_NEAR(one.weights[0], 1.0, 1e-12);
    CHECK_NEAR(one.consistency_ratio, 0.0, 1e-12);
    const auto two = mcdm::analyse_comparisons({{1.0, 4.0}, {0.25, 1.0}});
    CHECK_NEAR(two.consistency_ratio, 0.0, 1e-12);
    CHECK_NEAR(two.weights[0] / two.weights[1], 4.0, 1e-6);
    CHECK_NEAR(mcdm::random_index(3), 0.58, 1e-12);
    CHECK_NEAR(mcdm::random_index(50), mcdm::random_index(10), 1e-12);
}

TEST(mcdm, "TOPSIS puts the dominant alternative at one and the dominated at zero") {
    // Three alternatives, two benefit criteria. The first dominates on both, the
    // last is dominated on both, so relative closeness has to hit the endpoints.
    const mcdm::Matrix scores{{9, 9}, {5, 5}, {1, 1}};
    const auto c = mcdm::topsis_scores(scores, {0.5, 0.5}, {true, true});
    CHECK_NEAR(c[0], 1.0, 1e-9);
    CHECK_NEAR(c[2], 0.0, 1e-9);
    CHECK_TRUE(c[1] > 0.0 && c[1] < 1.0);
}

TEST(mcdm, "TOPSIS inverts the preference on a criterion where less is better") {
    // One criterion only, and it is a cost. The cheapest alternative must win.
    const mcdm::Matrix scores{{8}, {3}, {5}};
    const auto c = mcdm::topsis_scores(scores, {1.0}, {false});
    const auto rank = mcdm::rank_of(c);
    CHECK_EQ(rank[1], std::size_t{1});
    CHECK_EQ(rank[0], std::size_t{3});
}

TEST(mcdm, "AHP synthesis weights the normalised columns and handles a cost criterion") {
    // Two alternatives, one benefit criterion. Column normalisation makes the
    // scores 6/8 and 2/8, and the single weight passes them through unchanged.
    const auto benefit = mcdm::ahp_scores({{6}, {2}}, {1.0}, {true});
    CHECK_NEAR(benefit[0], 0.75, 1e-9);
    CHECK_NEAR(benefit[1], 0.25, 1e-9);

    // The same numbers read as a cost: inverted to 1/6 and 1/2, normalised to
    // 0.25 and 0.75, so the ranking flips.
    const auto cost = mcdm::ahp_scores({{6}, {2}}, {1.0}, {false});
    CHECK_NEAR(cost[0], 0.25, 1e-9);
    CHECK_NEAR(cost[1], 0.75, 1e-9);
}

TEST(mcdm, "AHP scores sum to one across the alternatives") {
    const mcdm::Matrix scores{{8, 3, 5}, {5, 6, 2}, {2, 9, 7}, {6, 1, 4}};
    const auto s = mcdm::ahp_scores(scores, {0.5, 0.2, 0.3}, {true, false, true});
    double sum = 0.0;
    for (double x : s) sum += x;
    CHECK_NEAR(sum, 1.0, 1e-9);
}

TEST(mcdm, "malformed scoring input is rejected") {
    CHECK_THROWS(mcdm::topsis_scores({{1, 2}}, {1.0}, {true}), mcdm::McdmError);
    CHECK_THROWS(mcdm::topsis_scores({{1, 2}}, {0.5, 0.5}, {true}), mcdm::McdmError);
    CHECK_THROWS(mcdm::topsis_scores({}, {1.0}, {true}), mcdm::McdmError);
    CHECK_THROWS(mcdm::ahp_scores({{1}}, {0.0}, {true}), mcdm::McdmError);
}

TEST(mcdm, "ranking is one based, ties share a rank, and Spearman spans its range") {
    const auto rank = mcdm::rank_of({0.4, 0.9, 0.4, 0.1});
    CHECK_EQ(rank[1], std::size_t{1});
    CHECK_EQ(rank[0], std::size_t{2});
    CHECK_EQ(rank[2], std::size_t{2});
    CHECK_EQ(rank[3], std::size_t{4});

    const std::vector<std::size_t> a{1, 2, 3, 4};
    const std::vector<std::size_t> reversed{4, 3, 2, 1};
    CHECK_NEAR(mcdm::spearman(a, a), 1.0, 1e-12);
    CHECK_NEAR(mcdm::spearman(a, reversed), -1.0, 1e-12);
    CHECK_THROWS(mcdm::spearman(a, {1, 2}), mcdm::McdmError);
}

TEST(mcdm, "the two methods can rank the same input differently") {
    // Why both are implemented. On a criterion where less is better, the AHP
    // synthesis inverts the column while TOPSIS keeps it and moves the ideal
    // point instead. The two transforms are not monotone images of each other,
    // so the middle of the ranking can come out in a different order.
    const mcdm::Matrix scores{{9, 2}, {6, 3}, {3, 1}, {5, 8}};
    const auto ahp = mcdm::ahp_scores(scores, {0.5, 0.5}, {true, false});
    const auto topsis = mcdm::topsis_scores(scores, {0.5, 0.5}, {true, false});
    const auto ra = mcdm::rank_of(ahp);
    const auto rt = mcdm::rank_of(topsis);
    bool differ = false;
    for (std::size_t i = 0; i < ra.size(); ++i)
        if (ra[i] != rt[i]) differ = true;
    CHECK_TRUE(differ);
}
