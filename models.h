// models.h
// Plain data structures shared across the engine. Deliberately dumb structs
// (no behaviour) so the JSON boundary layer and the algorithm layer both
// operate on the exact same types. Time is stored as "minutes since 00:00"
// (an int) everywhere -- this is what makes runtime/turnaround arithmetic
// trivial and exact (no floating point clock math).
#pragma once
#include <string>
#include <vector>
#include <sstream>
#include <iomanip>

// ---------- Time helpers -----------------------------------------------
// Format minutes-since-midnight as "HH:MM". Values >= 1440 are shown as
// hours past 24 (e.g. 1450 -> "24:10") which is the simplest, least
// error-prone way to represent a theatre that runs past midnight.
inline std::string formatTime(int minutes) {
    int hh = minutes / 60;
    int mm = minutes % 60;
    std::ostringstream oss;
    oss << std::setw(2) << std::setfill('0') << hh << ":"
        << std::setw(2) << std::setfill('0') << mm;
    return oss.str();
}

// ---------- Core entities ------------------------------------------------

struct Theatre {
    std::string name;
    int openingMinutes;      // e.g. 09:00 -> 540
    int closingMinutes;      // e.g. 24:00 -> 1440
    int turnaroundMinutes;   // cleaning/trailer buffer required between shows
};

struct Screen {
    int id;
    std::string name;
    int capacity;
    std::string type;          // "Standard", "Premium", "IMAX" ... display only
    double priceMultiplier;    // >= 1.0
    bool available;            // owner can mark a screen out of service
};

struct Movie {
    int id;
    std::string name;
    int runtimeMinutes;
    double basePrice;
    int demandScore;   // 1 (low) .. 10 (high)
    std::string genre;
    int minShows;       // soft target, reported if unmet (not force-satisfied)
    int maxShows;       // hard cap enforced by the scheduler (0/negative = unlimited)
};

// A single placed (or candidate) showing.
struct Show {
    int movieId;
    int screenId;
    int startMinutes;
    int endMinutes;            // startMinutes + runtime (does NOT include turnaround)
    double expectedOccupancy;  // 0..1
    double expectedAudience;   // capacity * occupancy
    double expectedRevenue;
    double revenuePerMinute;   // expectedRevenue / (runtime + turnaround) -- the greedy score
    std::string reason;        // human-readable explanation, filled in when accepted
};

// Why a candidate was NOT scheduled -- used for the explanation layer.
struct Rejection {
    int movieId;
    int screenId;
    int startMinutes;
    std::string reason;
};
