// scheduler.cpp
#include "scheduler.h"
#include "algorithms.h"
#include "data_structures.h"
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <iostream>

// ======================= Validation ========================================

std::vector<std::string> validateInput(const Theatre& theatre,
                                        const std::vector<Screen>& screens,
                                        const std::vector<Movie>& movies) {
    std::vector<std::string> errors;

    if (theatre.closingMinutes <= theatre.openingMinutes)
        errors.push_back("Theatre closing time must be after opening time.");
    if (theatre.turnaroundMinutes < 0)
        errors.push_back("Turnaround time cannot be negative.");

    if (screens.empty())
        errors.push_back("At least one screen is required.");
    {
        std::vector<int> ids;
        for (const auto& s : screens) {
            if (s.capacity <= 0)
                errors.push_back("Screen '" + s.name + "' must have positive capacity.");
            if (s.priceMultiplier < 1.0)
                errors.push_back("Screen '" + s.name + "' price multiplier must be >= 1.0.");
            if (linearSearch<int>(ids, [&](const int& id) { return id == s.id; }) != -1)
                errors.push_back("Duplicate screen ID: " + std::to_string(s.id));
            ids.push_back(s.id);
        }
        if (linearSearch<Screen>(screens, [](const Screen& s) { return s.available; }) == -1 && !screens.empty())
            errors.push_back("No available screens -- every screen is marked unavailable.");
    }

    if (movies.empty())
        errors.push_back("At least one movie is required.");
    {
        std::vector<int> ids;
        for (const auto& m : movies) {
            if (m.runtimeMinutes <= 0)
                errors.push_back("Movie '" + m.name + "' must have a positive runtime.");
            if (m.basePrice <= 0)
                errors.push_back("Movie '" + m.name + "' must have a positive base price.");
            if (m.demandScore < 1 || m.demandScore > 10)
                errors.push_back("Movie '" + m.name + "' demand score must be between 1 and 10.");
            if (linearSearch<int>(ids, [&](const int& id) { return id == m.id; }) != -1)
                errors.push_back("Duplicate movie ID: " + std::to_string(m.id));
            ids.push_back(m.id);
            if (m.runtimeMinutes + theatre.turnaroundMinutes > (theatre.closingMinutes - theatre.openingMinutes))
                errors.push_back("Movie '" + m.name + "' runtime exceeds the entire operating day -- it can never be scheduled.");
        }
    }
    return errors;
}

// ======================= Candidate generation ===============================

std::vector<Show> generateCandidates(const Theatre& theatre,
                                      const std::vector<Screen>& screens,
                                      const std::vector<Movie>& movies,
                                      int startIntervalMinutes) {
    std::vector<Show> candidates;
    for (const auto& screen : screens) {
        if (!screen.available) continue;
        for (const auto& movie : movies) {
            for (int start = theatre.openingMinutes; start <= theatre.closingMinutes; start += startIntervalMinutes) {
                int end = start + movie.runtimeMinutes;
                if (end > theatre.closingMinutes) break; // later starts only get worse -- stop this movie/screen pair
                Show s;
                s.movieId = movie.id;
                s.screenId = screen.id;
                s.startMinutes = start;
                s.endMinutes = end;
                s.expectedOccupancy = expectedOccupancy(movie, start);
                s.expectedAudience = expectedAudience(movie, screen, start);
                s.expectedRevenue = expectedRevenue(movie, screen, start);
                s.revenuePerMinute = revenuePerMinute(s.expectedRevenue, movie.runtimeMinutes, theatre.turnaroundMinutes);
                candidates.push_back(s);
            }
        }
    }
    return candidates;
}

// ======================= Shared helpers ======================================

// Two shows on the SAME screen conflict if they (including the turnaround
// buffer required between them) overlap in time.
static bool showsConflict(const Show& a, const Show& b, int turnaround) {
    return a.startMinutes < b.endMinutes + turnaround &&
           b.startMinutes < a.endMinutes + turnaround;
}

static const Movie* findMovie(const std::vector<Movie>& movies, int id) {
    int idx = linearSearch<Movie>(movies, [&](const Movie& m) { return m.id == id; });
    return idx == -1 ? nullptr : &movies[idx];
}

static const Screen* findScreen(const std::vector<Screen>& screens, int id) {
    int idx = linearSearch<Screen>(screens, [&](const Screen& s) { return s.id == id; });
    return idx == -1 ? nullptr : &screens[idx];
}

static std::string timeBucketName(int startMinutes) {
    int hour = (startMinutes / 60) % 24;
    if (hour < 12) return "morning";
    if (hour < 17) return "afternoon";
    if (hour < 21) return "evening (prime time)";
    return "late night";
}

// ======================= Greedy Revenue Optimization Heuristic ==============

ScheduleResult runGreedyOptimizer(const Theatre& theatre,
                                   const std::vector<Screen>& screens,
                                   const std::vector<Movie>& movies,
                                   const std::vector<Show>& candidatesIn,
                                   bool captureAllRejections) {
    ScheduleResult result;

    // Build a BST index of movies by ID -- used repeatedly below to fetch a
    // movie's name/cap while walking the ranked candidate list.
    MovieBST<Movie> movieIndex;
    for (const auto& m : movies) movieIndex.insert(m.id, m);

    // Rank candidates: revenue-per-minute first (the opportunity-cost-aware
    // score), then absolute revenue, then occupancy, then earliest start,
    // then lowest movie/screen ID -- a fully deterministic total order.
    std::vector<Show> candidates = candidatesIn;
    mergeSort<Show>(candidates, [](const Show& a, const Show& b) {
        if (a.revenuePerMinute != b.revenuePerMinute) return a.revenuePerMinute > b.revenuePerMinute;
        if (a.expectedRevenue != b.expectedRevenue) return a.expectedRevenue > b.expectedRevenue;
        if (a.expectedOccupancy != b.expectedOccupancy) return a.expectedOccupancy > b.expectedOccupancy;
        if (a.startMinutes != b.startMinutes) return a.startMinutes < b.startMinutes;
        if (a.movieId != b.movieId) return a.movieId < b.movieId;
        return a.screenId < b.screenId;
    });

    // Feed the ranked list into a processing queue -- the greedy walk below
    // simply dequeues candidates in ranked order.
    Queue<Show> processing;
    for (const auto& c : candidates) processing.enqueue(c);

    std::map<int, DoublyLinkedList<Show>> screenSchedules; // per-screen chronological timeline
    std::map<int, int> movieShowCount;
    Stack<Show> decisionHistory; // undo/history log of accepted decisions

    const int SAMPLE_CAP = 25; // don't flood the report with thousands of rejections

    while (!processing.empty()) {
        Show cand = processing.dequeue();
        const Movie* movie = movieIndex.find(cand.movieId);
        if (!movie) continue; // defensive; shouldn't happen with validated input

        if (movie->maxShows > 0 && movieShowCount[cand.movieId] >= movie->maxShows) {
            result.rejectedDueToMovieCap++;
            if (captureAllRejections || result.rejectionSamples.size() < SAMPLE_CAP)
                result.rejectionSamples.push_back({cand.movieId, cand.screenId, cand.startMinutes,
                    "Maximum shows for '" + movie->name + "' already reached."});
            continue;
        }

        bool conflict = false;
        for (const auto& existing : screenSchedules[cand.screenId].toVector()) {
            if (showsConflict(cand, existing, theatre.turnaroundMinutes)) { conflict = true; break; }
        }
        if (conflict) {
            result.rejectedDueToOverlap++;
            if (captureAllRejections || result.rejectionSamples.size() < SAMPLE_CAP)
                result.rejectionSamples.push_back({cand.movieId, cand.screenId, cand.startMinutes,
                    "Screen already occupied at this time (including turnaround buffer)."});
            continue;
        }

        std::ostringstream reason;
        reason << "Highest revenue efficiency (Rs " << std::fixed << std::setprecision(1)
               << cand.revenuePerMinute << "/min) among remaining feasible candidates; "
               << std::setprecision(0) << (cand.expectedOccupancy * 100) << "% projected occupancy in the "
               << timeBucketName(cand.startMinutes) << " slot.";
        cand.reason = reason.str();

        screenSchedules[cand.screenId].insertSorted(cand, [](const Show& s) { return s.startMinutes; });
        movieShowCount[cand.movieId]++;
        decisionHistory.push(cand);
    }

    // Flatten per-screen timelines into one screen-ordered, time-ordered list.
    std::vector<int> screenIds;
    for (const auto& screen : screens) screenIds.push_back(screen.id);
    insertionSort<int>(screenIds, [](const int& a, const int& b) { return a < b; });
    for (int id : screenIds) {
        auto it = screenSchedules.find(id);
        if (it == screenSchedules.end()) continue;
        for (const auto& show : it->second.toVector()) result.shows.push_back(show);
    }
    return result;
}

// ======================= Fair baseline (demand-blind) ========================

ScheduleResult runBaselineScheduler(const Theatre& theatre,
                                     const std::vector<Screen>& screens,
                                     const std::vector<Movie>& movies) {
    ScheduleResult result;
    if (movies.empty()) return result;

    std::map<int, int> movieShowCount;

    std::vector<Screen> sortedScreens = screens;
    insertionSort<Screen>(sortedScreens, [](const Screen& a, const Screen& b) { return a.id < b.id; });

    for (const auto& screen : sortedScreens) {
        if (!screen.available) continue;

        // Fresh round-robin rotation over ALL movies, per screen -- a
        // genuinely circular structure: we keep pulling "the next movie in
        // line" and putting it back at the end, exactly like a manager
        // working down a fixed list over and over through the day.
        CircularQueue<int> rotation(static_cast<int>(movies.size()));
        for (size_t i = 0; i < movies.size(); i++) rotation.enqueue(static_cast<int>(i));

        int t = theatre.openingMinutes;
        int consecutiveFails = 0;
        DoublyLinkedList<Show> timeline;

        while (consecutiveFails < static_cast<int>(movies.size())) {
            int movieIdx = rotation.rotate();
            const Movie& movie = movies[movieIdx];

            if (movie.maxShows > 0 && movieShowCount[movie.id] >= movie.maxShows) {
                consecutiveFails++;
                continue;
            }
            int start = t, end = t + movie.runtimeMinutes;
            if (end > theatre.closingMinutes) {
                consecutiveFails++;
                continue;
            }

            Show show;
            show.movieId = movie.id;
            show.screenId = screen.id;
            show.startMinutes = start;
            show.endMinutes = end;
            show.expectedOccupancy = expectedOccupancy(movie, start);
            show.expectedAudience = expectedAudience(movie, screen, start);
            show.expectedRevenue = expectedRevenue(movie, screen, start);
            show.revenuePerMinute = revenuePerMinute(show.expectedRevenue, movie.runtimeMinutes, theatre.turnaroundMinutes);
            show.reason = "Baseline: next movie in the fixed rotation that fits the remaining screen time.";

            timeline.insertSorted(show, [](const Show& s) { return s.startMinutes; });
            movieShowCount[movie.id]++;
            t = end + theatre.turnaroundMinutes;
            consecutiveFails = 0;
        }
        for (const auto& show : timeline.toVector()) result.shows.push_back(show);
    }
    return result;
}

// ======================= Metrics ============================================

Metrics computeMetrics(const Theatre& theatre,
                        const std::vector<Screen>& screens,
                        const std::vector<Movie>& movies,
                        const ScheduleResult& result) {
    Metrics m;
    m.totalShows = static_cast<int>(result.shows.size());

    double totalSeatsOffered = 0.0;
    int primeStart = 17 * 60, primeEnd = 21 * 60;
    long long filledPrimeMinutes = 0;

    std::map<int, MovieStat> stats;
    std::map<int, std::vector<int>> screensPerMovie;

    for (const auto& show : result.shows) {
        m.totalRevenue += show.expectedRevenue;
        m.totalAudience += show.expectedAudience;
        m.scheduledScreenMinutes += (show.endMinutes - show.startMinutes);

        const Screen* screen = findScreen(screens, show.screenId);
        if (screen) totalSeatsOffered += screen->capacity;

        int overlapStart = std::max(show.startMinutes, primeStart);
        int overlapEnd = std::min(show.endMinutes, primeEnd);
        if (overlapEnd > overlapStart) filledPrimeMinutes += (overlapEnd - overlapStart);

        auto& stat = stats[show.movieId];
        if (stat.shows == 0) {
            const Movie* mv = findMovie(movies, show.movieId);
            stat.movieId = show.movieId;
            stat.movieName = mv ? mv->name : ("Movie #" + std::to_string(show.movieId));
        }
        stat.shows++;
        stat.expectedAudience += show.expectedAudience;
        stat.expectedRevenue += show.expectedRevenue;
        stat.averageOccupancy += show.expectedOccupancy; // divided by count below
        auto& usedScreens = screensPerMovie[show.movieId];
        if (linearSearch<int>(usedScreens, [&](const int& id) { return id == show.screenId; }) == -1)
            usedScreens.push_back(show.screenId);
    }

    for (auto& [id, stat] : stats) {
        if (stat.shows > 0) stat.averageOccupancy /= stat.shows;
        stat.screensUsed = static_cast<int>(screensPerMovie[id].size());
        m.perMovie.push_back(stat);
    }
    insertionSort<MovieStat>(m.perMovie, [](const MovieStat& a, const MovieStat& b) {
        return a.expectedRevenue > b.expectedRevenue;
    });

    for (const auto& screen : screens)
        if (screen.available) m.totalScreenOperatingMinutes += (theatre.closingMinutes - theatre.openingMinutes);

    m.idleScreenMinutes = std::max(0, m.totalScreenOperatingMinutes - m.scheduledScreenMinutes);
    m.screenUtilizationPercent = m.totalScreenOperatingMinutes > 0
        ? (100.0 * m.scheduledScreenMinutes / m.totalScreenOperatingMinutes) : 0.0;
    m.seatUtilizationPercent = totalSeatsOffered > 0 ? (100.0 * m.totalAudience / totalSeatsOffered) : 0.0;

    int availableScreenCount = 0;
    for (const auto& s : screens) if (s.available) availableScreenCount++;
    int primeWindowLen = std::max(0, std::min(theatre.closingMinutes, primeEnd) - std::max(theatre.openingMinutes, primeStart));
    long long totalPrimeMinutes = static_cast<long long>(primeWindowLen) * availableScreenCount;
    m.primeTimeUtilizationPercent = totalPrimeMinutes > 0 ? (100.0 * filledPrimeMinutes / totalPrimeMinutes) : 0.0;

    for (const auto& movie : movies) {
        int shownCount = stats.count(movie.id) ? stats[movie.id].shows : 0;
        if (shownCount < movie.minShows)
            m.unmetMinimumShows.push_back(movie.name + " (" + std::to_string(shownCount) +
                                           "/" + std::to_string(movie.minShows) + " shows)");
    }
    return m;
}

// ======================= Top-level orchestration =============================

EngineResult runEngine(const Theatre& theatre,
                        std::vector<Screen> screens,
                        std::vector<Movie> movies,
                        int startIntervalMinutes) {
    EngineResult result;
    result.validationErrors = validateInput(theatre, screens, movies);
    if (!result.validationErrors.empty()) return result;

    auto candidates = generateCandidates(theatre, screens, movies, startIntervalMinutes);

    result.baseline = runBaselineScheduler(theatre, screens, movies);
    result.optimized = runGreedyOptimizer(theatre, screens, movies, candidates, false);

    result.baselineMetrics = computeMetrics(theatre, screens, movies, result.baseline);
    result.optimizedMetrics = computeMetrics(theatre, screens, movies, result.optimized);

    double base = result.baselineMetrics.totalRevenue;
    result.revenueImprovementPercent = base > 0
        ? (100.0 * (result.optimizedMetrics.totalRevenue - base) / base) : 0.0;
    return result;
}

// ======================= Reporting ===========================================

static std::string jsonEscape(const std::string& s) {
    std::string out;
    for (char c : s) { if (c == '"' || c == '\\') out += '\\'; out += c; }
    return out;
}

static std::string showToJson(const Show& s, const std::vector<Movie>& movies) {
    const Movie* mv = findMovie(movies, s.movieId);
    std::ostringstream o;
    o << std::fixed << std::setprecision(2);
    o << "{\"movieId\":" << s.movieId
      << ",\"movieName\":\"" << jsonEscape(mv ? mv->name : "") << "\""
      << ",\"screenId\":" << s.screenId
      << ",\"startTime\":\"" << formatTime(s.startMinutes) << "\""
      << ",\"endTime\":\"" << formatTime(s.endMinutes) << "\""
      << ",\"runtimeMinutes\":" << (s.endMinutes - s.startMinutes)
      << ",\"expectedOccupancy\":" << s.expectedOccupancy
      << ",\"expectedAudience\":" << s.expectedAudience
      << ",\"expectedRevenue\":" << s.expectedRevenue
      << ",\"revenuePerMinute\":" << s.revenuePerMinute
      << ",\"reason\":\"" << jsonEscape(s.reason) << "\"}";
    return o.str();
}

static std::string metricsToJson(const Metrics& m) {
    std::ostringstream o;
    o << std::fixed << std::setprecision(2);
    o << "{\"totalRevenue\":" << m.totalRevenue
      << ",\"totalAudience\":" << m.totalAudience
      << ",\"totalShows\":" << m.totalShows
      << ",\"seatUtilizationPercent\":" << m.seatUtilizationPercent
      << ",\"totalScreenOperatingMinutes\":" << m.totalScreenOperatingMinutes
      << ",\"scheduledScreenMinutes\":" << m.scheduledScreenMinutes
      << ",\"idleScreenMinutes\":" << m.idleScreenMinutes
      << ",\"screenUtilizationPercent\":" << m.screenUtilizationPercent
      << ",\"primeTimeUtilizationPercent\":" << m.primeTimeUtilizationPercent
      << ",\"perMovie\":[";
    for (size_t i = 0; i < m.perMovie.size(); i++) {
        const auto& s = m.perMovie[i];
        if (i) o << ",";
        o << "{\"movieId\":" << s.movieId << ",\"movieName\":\"" << jsonEscape(s.movieName) << "\""
          << ",\"shows\":" << s.shows << ",\"expectedAudience\":" << s.expectedAudience
          << ",\"expectedRevenue\":" << s.expectedRevenue << ",\"averageOccupancy\":" << s.averageOccupancy
          << ",\"screensUsed\":" << s.screensUsed << "}";
    }
    o << "],\"unmetMinimumShows\":[";
    for (size_t i = 0; i < m.unmetMinimumShows.size(); i++) {
        if (i) o << ",";
        o << "\"" << jsonEscape(m.unmetMinimumShows[i]) << "\"";
    }
    o << "]}";
    return o.str();
}

std::string toJson(const Theatre& theatre,
                    const std::vector<Screen>& screens,
                    const std::vector<Movie>& movies,
                    const EngineResult& result) {
    std::ostringstream o;
    o << std::fixed << std::setprecision(2);
    o << "{\n";
    if (!result.validationErrors.empty()) {
        o << "  \"valid\": false,\n  \"errors\": [";
        for (size_t i = 0; i < result.validationErrors.size(); i++) {
            if (i) o << ",";
            o << "\"" << jsonEscape(result.validationErrors[i]) << "\"";
        }
        o << "]\n}\n";
        return o.str();
    }
    o << "  \"valid\": true,\n";
    o << "  \"theatre\": \"" << jsonEscape(theatre.name) << "\",\n";
    o << "  \"revenueImprovementPercent\": " << result.revenueImprovementPercent << ",\n";

    o << "  \"baselineMetrics\": " << metricsToJson(result.baselineMetrics) << ",\n";
    o << "  \"optimizedMetrics\": " << metricsToJson(result.optimizedMetrics) << ",\n";

    o << "  \"baselineSchedule\": [";
    for (size_t i = 0; i < result.baseline.shows.size(); i++) {
        if (i) o << ",";
        o << "\n    " << showToJson(result.baseline.shows[i], movies);
    }
    o << "\n  ],\n";

    o << "  \"optimizedSchedule\": [";
    for (size_t i = 0; i < result.optimized.shows.size(); i++) {
        if (i) o << ",";
        o << "\n    " << showToJson(result.optimized.shows[i], movies);
    }
    o << "\n  ],\n";

    o << "  \"rejectionSummary\": {\"dueToOverlap\": " << result.optimized.rejectedDueToOverlap
      << ", \"dueToMovieCap\": " << result.optimized.rejectedDueToMovieCap << "}\n";
    o << "}\n";
    return o.str();
}

void printHumanReport(const Theatre& theatre,
                       const std::vector<Screen>& screens,
                       const std::vector<Movie>& movies,
                       const EngineResult& result,
                       bool verboseRejections) {
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "=== " << theatre.name << " ===\n";
    std::cout << "Operating hours: " << formatTime(theatre.openingMinutes)
               << " - " << formatTime(theatre.closingMinutes)
               << " | Turnaround: " << theatre.turnaroundMinutes << " min\n\n";

    if (!result.validationErrors.empty()) {
        std::cout << "INPUT INVALID:\n";
        for (const auto& e : result.validationErrors) std::cout << "  - " << e << "\n";
        return;
    }

    auto printSchedule = [&](const std::string& label, const ScheduleResult& sr) {
        std::cout << "----- " << label << " -----\n";
        int lastScreen = -1;
        for (const auto& s : sr.shows) {
            if (s.screenId != lastScreen) {
                const Screen* sc = findScreen(screens, s.screenId);
                std::cout << "\nSCREEN " << s.screenId << (sc ? " (" + sc->name + ")" : "") << "\n";
                lastScreen = s.screenId;
            }
            const Movie* mv = findMovie(movies, s.movieId);
            std::cout << "  " << formatTime(s.startMinutes) << "-" << formatTime(s.endMinutes)
                       << "  " << std::setw(18) << std::left << (mv ? mv->name : "?")
                       << " aud~" << std::setw(6) << std::right << (int)std::round(s.expectedAudience)
                       << "  Rs " << (int)std::round(s.expectedRevenue) << "\n";
        }
        std::cout << "\n";
    };
    printSchedule("BASELINE SCHEDULE", result.baseline);
    printSchedule("OPTIMIZED SCHEDULE", result.optimized);

    auto printMetrics = [&](const std::string& label, const Metrics& m) {
        std::cout << label << ": Revenue Rs " << m.totalRevenue
                   << " | Viewers " << (int)std::round(m.totalAudience)
                   << " | Shows " << m.totalShows
                   << " | Seat Util " << m.seatUtilizationPercent << "%"
                   << " | Screen Util " << m.screenUtilizationPercent << "%"
                   << " | Prime-time Util " << m.primeTimeUtilizationPercent << "%\n";
    };
    printMetrics("Baseline ", result.baselineMetrics);
    printMetrics("Optimized", result.optimizedMetrics);
    std::cout << "\nRevenue Improvement: " << result.revenueImprovementPercent << "%\n";

    if (!result.optimizedMetrics.unmetMinimumShows.empty()) {
        std::cout << "\nMovies below their minimum-shows target:\n";
        for (const auto& s : result.optimizedMetrics.unmetMinimumShows) std::cout << "  - " << s << "\n";
    }

    std::cout << "\nOptimizer rejections -- overlap: " << result.optimized.rejectedDueToOverlap
               << ", movie cap reached: " << result.optimized.rejectedDueToMovieCap << "\n";
    if (verboseRejections) {
        std::cout << "Sample rejections:\n";
        for (const auto& r : result.optimized.rejectionSamples) {
            const Movie* mv = findMovie(movies, r.movieId);
            std::cout << "  - " << (mv ? mv->name : "?") << " @ Screen " << r.screenId
                       << " " << formatTime(r.startMinutes) << " -> " << r.reason << "\n";
        }
    }
}
