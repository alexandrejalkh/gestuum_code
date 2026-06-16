/**
 * GESTUUM — Dynamic Time Warping Header
 * Bloco: 1.3 — Biblioteca shared/ - DTW
 * Responsibility: Declare DTW class for comparing 3D trajectories
 *                 against a gesture database using Dynamic Time Warping.
 */

#ifndef GESTUUM_DTW_H
#define GESTUUM_DTW_H

#include "matrix3d.h"
#include <vector>
#include <utility>

/**
 * Resultado de uma operacao de match DTW contra o banco de gestos.
 */
struct DTWResult {
    float score;       // Score DTW (menor = mais similar)
    bool matched;      // True se score esta abaixo do threshold do gesto
    int matchIndex;    // Indice do melhor match no banco de gestos
    float confidence;  // 1.0 - (score/threshold), maior = mais confiante
    bool ambiguous;    // FIX M06: Flag de ambiguidade incluso no resultado
                       // (antes so disponivel via hasAmbiguity() com estado stale)
};

/**
 * Dynamic Time Warping engine for gesture recognition.
 *
 * Compares real-time 3D trajectories (from Matrix3D) against pre-recorded
 * gesture templates. Supports dual-sensor matching with weighted score
 * combination (60% sensor A, 40% sensor B).
 *
 * Usage:
 *   DTW dtw;
 *   DTWResult result = dtw.match(trajA, trajB, gesturesA, gesturesB, thresholds);
 *   if (result.matched && !dtw.hasAmbiguity()) { ... }
 */
class DTW {
public:
    DTW();

    /**
     * Calculate the DTW distance between two 3D trajectories.
     * Returns the path cost normalized by (N + M).
     * If either trajectory is empty, returns FLT_MAX.
     * If either trajectory exceeds DTW_MAX_COST_MATRIX_SIZE, returns FLT_MAX.
     */
    float calculate(const std::vector<Point3D>& t1,
                    const std::vector<Point3D>& t2);

    /**
     * Match captured trajectories against the gesture database.
     * Combines scores from sensor A (weight 0.6) and sensor B (weight 0.4).
     *
     * @param trajectoryA  Captured trajectory from sensor A
     * @param trajectoryB  Captured trajectory from sensor B
     * @param gesturesA    Reference trajectories for sensor A (one per gesture)
     * @param gesturesB    Reference trajectories for sensor B (one per gesture)
     * @param thresholds   DTW threshold per gesture (score must be below to match)
     * @return DTWResult with best match info
     */
    DTWResult match(const std::vector<Point3D>& trajectoryA,
                    const std::vector<Point3D>& trajectoryB,
                    const std::vector<std::vector<Point3D>>& gesturesA,
                    const std::vector<std::vector<Point3D>>& gesturesB,
                    const std::vector<float>& thresholds);

    /**
     * Match with configurable sensor weights.
     * @param weightA  Weight for sensor A score (e.g., 0.6)
     * @param weightB  Weight for sensor B score (e.g., 0.4)
     */
    DTWResult match(const std::vector<Point3D>& trajectoryA,
                    const std::vector<Point3D>& trajectoryB,
                    const std::vector<std::vector<Point3D>>& gesturesA,
                    const std::vector<std::vector<Point3D>>& gesturesB,
                    const std::vector<float>& thresholds,
                    float weightA, float weightB);

    /**
     * Check if the last match result is ambiguous.
     * Returns true if the difference between the best and second-best
     * scores is less than the given margin.
     */
    bool hasAmbiguity(float margin = 0.5f);

    /**
     * Get the top N matches from the last match() call,
     * sorted by score ascending (best first).
     *
     * @param n  Number of matches to return (default 3)
     * @return Vector of (gestureIndex, score) pairs
     */
    std::vector<std::pair<int, float>> getTopMatches(int n = 3);

private:
    std::vector<std::pair<int, float>> lastScores;  // (index, combinedScore) sorted ascending
    float lastBestScore;                             // Best score from last match

    /**
     * Euclidean distance between two 3D points.
     */
    float pointDistance(const Point3D& p1, const Point3D& p2);
};

#endif // GESTUUM_DTW_H
