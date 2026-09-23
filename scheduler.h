// scheduler.h
// The engine's public interface. Everything here operates on plain C++
// structs (models.h) -- no JSON, no file I/O, no console printing inside
// the algorithm itself, so a future web layer can call these functions
// directly after converting a request into Theatre/Screen/Movie objects.
#pragma once
#include <vector>
#include <string>
#include <map>
#include "models.h"

// ---------- Result types ---------------------------------------------

struct ScheduleResult {
    std::vector<Show> shows;                 // accepted shows, sorted by screen then start time
    int rejectedDueToOverlap = 0;
    int rejectedDueToMovieCap = 0;
    int rejectedDueToOtherReason = 0;
    std::vector<Rejection> rejectionSamples;  // a bounded sample, for the explanation report
};

struct MovieStat {
    int movieId;
    std::string movieName;
    int shows = 0;
    double expectedAudience = 0.0;
    double expectedRevenue = 0.0;
    double averageOccupancy = 0.0;
    int screensUsed = 0;
};

struct Metrics {
    double totalRevenue = 0.0;
    double totalAudience = 0.0;
    int totalShows = 0;
    double seatUtilizationPercent = 0.0;

    int totalScreenOperatingMinutes = 0;
    int scheduledScreenMinutes = 0;     // sum of movie runtimes only (turnaround excluded)
    int idleScreenMinutes = 0;
    double screenUtilizationPercent = 0.0;

    double primeTimeUtilizationPercent = 0.0; // 17:00-21:00 window

    std::vector<MovieStat> perMovie;
    std::vector<std::string> unmetMinimumShows; // movies whose minShows target wasn't reached
};

struct EngineResult {
    std::vector<std::string> validationErrors;
    ScheduleResult baseline;
    ScheduleResult optimized;
    Metrics baselineMetrics;
    Metrics optimizedMetrics;
    double revenueImprovementPercent = 0.0;
};

// ---------- Validation --------------------------------------------------
std::vector<std::string> validateInput(const Theatre& theatre,
                                        const std::vector<Screen>& screens,
                                        const std::vector<Movie>& movies);

// ---------- Candidate generation ----------------------------------------
// Generates every feasible (movie, screen, startTime) candidate, at
// `startIntervalMinutes` spacing, with occupancy/revenue/RPM pre-computed.
// A candidate is feasible purely on its own (fits in operating hours) --
// overlap with other shows is resolved later, during greedy selection,
// since that depends on what else has already been placed.
std::vector<Show> generateCandidates(const Theatre& theatre,
                                      const std::vector<Screen>& screens,
                                      const std::vector<Movie>& movies,
                                      int startIntervalMinutes);

// ---------- Schedulers ----------------------------------------------------
// Greedy Revenue Optimization Heuristic: rank all candidates by revenue per
// occupied minute (ties broken deterministically), then walk the ranked
// list accepting any candidate that doesn't conflict with an
// already-accepted show on the same screen and doesn't exceed that movie's
// show cap. See algorithms.h for the formula and README.md for the proof
// sketch / honest limitations.
ScheduleResult runGreedyOptimizer(const Theatre& theatre,
                                   const std::vector<Screen>& screens,
                                   const std::vector<Movie>& movies,
                                   const std::vector<Show>& candidates,
                                   bool captureAllRejections = false);

// Fair baseline: rotates through the movie list per screen (circular
// queue), packing shows back-to-back respecting runtime/turnaround/
// operating hours/show caps, but making no use of demand or revenue at
// all. Runs under the IDENTICAL constraints as the optimizer.
ScheduleResult runBaselineScheduler(const Theatre& theatre,
                                     const std::vector<Screen>& screens,
                                     const std::vector<Movie>& movies);

// ---------- Metrics & reporting -------------------------------------------
Metrics computeMetrics(const Theatre& theatre,
                        const std::vector<Screen>& screens,
                        const std::vector<Movie>& movies,
                        const ScheduleResult& result);

EngineResult runEngine(const Theatre& theatre,
                        std::vector<Screen> screens,
                        std::vector<Movie> movies,
                        int startIntervalMinutes = 30);

std::string toJson(const Theatre& theatre,
                    const std::vector<Screen>& screens,
                    const std::vector<Movie>& movies,
                    const EngineResult& result);

void printHumanReport(const Theatre& theatre,
                       const std::vector<Screen>& screens,
                       const std::vector<Movie>& movies,
                       const EngineResult& result,
                       bool verboseRejections = false);
