// algorithms.h
// (1) Generic sort/search algorithms implemented by hand (no std::sort).
// (2) The deterministic mathematical model: occupancy, audience, revenue,
//     revenue-per-minute. These are pure functions of the model's inputs --
//     same inputs always produce the same output, which is what makes the
//     whole engine manually verifiable.
#pragma once
#include <vector>
#include <functional>
#include <algorithm>
#include "models.h"

// ======================= Sorting ==========================================

// Merge sort -- used for the candidate list, which is the largest
// collection in the program (movies x screens x candidate start times).
// Guaranteed O(n log n) in every case, unlike quicksort/insertion sort,
// which is why it is the one used for the list whose size actually matters.
template <typename T>
void mergeSort(std::vector<T>& v, const std::function<bool(const T&, const T&)>& less) {
    if (v.size() <= 1) return;
    size_t mid = v.size() / 2;
    std::vector<T> left(v.begin(), v.begin() + mid);
    std::vector<T> right(v.begin() + mid, v.end());
    mergeSort(left, less);
    mergeSort(right, less);

    size_t i = 0, j = 0, k = 0;
    while (i < left.size() && j < right.size())
        v[k++] = less(right[j], left[i]) ? right[j++] : left[i++];
    while (i < left.size())  v[k++] = left[i++];
    while (j < right.size()) v[k++] = right[j++];
}

// Insertion sort -- used for small, per-screen lists (e.g. displaying a
// single screen's shows in order, or sorting the handful of screens/movies
// for a report). Efficient here because these lists are small, and often
// already nearly sorted.
template <typename T>
void insertionSort(std::vector<T>& v, const std::function<bool(const T&, const T&)>& less) {
    for (size_t i = 1; i < v.size(); i++) {
        T key = v[i];
        size_t j = i;
        while (j > 0 && less(key, v[j - 1])) {
            v[j] = v[j - 1];
            j--;
        }
        v[j] = key;
    }
}

// ======================= Searching ========================================

// Linear search -- used for small unsorted collections (e.g. "does this
// screen ID exist in the theatre's screen list?"). Honest choice: these
// lists are tiny, so O(n) is simpler and no slower in practice than
// building an index.
template <typename T>
int linearSearch(const std::vector<T>& v, const std::function<bool(const T&)>& match) {
    for (size_t i = 0; i < v.size(); i++)
        if (match(v[i])) return static_cast<int>(i);
    return -1;
}

// Binary search -- requires v to already be sorted by the same key used in
// keyOf. Used for looking up a movie in a list that has been sorted by
// price (or any other numeric field) for reporting/filtering purposes.
template <typename T>
int binarySearch(const std::vector<T>& v, double target, const std::function<double(const T&)>& keyOf) {
    int lo = 0, hi = static_cast<int>(v.size()) - 1;
    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2;
        double k = keyOf(v[mid]);
        if (k == target) return mid;
        if (k < target) lo = mid + 1; else hi = mid - 1;
    }
    return -1;
}

// ======================= The mathematical model ===========================
//
// Time-of-day demand factor. Bucketed by the hour the show STARTS in.
// This is the "explainable, deterministic" demand-by-time-of-day model
// requested: four buckets, four fixed multipliers, no hidden curve-fitting.
//
//   Morning    (< 12:00) -> 0.60   quiet
//   Afternoon  (12:00-17:00) -> 0.85   moderate
//   Evening    (17:00-21:00) -> 1.15   prime time
//   Late night (>= 21:00) -> 0.75   drop-off after prime time
//
inline double timeOfDayFactor(int startMinutes) {
    int hour = (startMinutes / 60) % 24;
    if (hour < 12) return 0.60;
    if (hour < 17) return 0.85;
    if (hour < 21) return 1.15;
    return 0.75;
}

// Expected Occupancy = min(1.0, MovieDemandFraction x TimeOfDayFactor)
// The min(1.0, ...) clamp is the one deliberate departure from a naive
// product formula: without it, a popular movie in prime time could imply
// more than 100% of the seats are full, which is meaningless.
inline double expectedOccupancy(const Movie& movie, int startMinutes) {
    double demandFraction = movie.demandScore / 10.0;   // 0.1 .. 1.0
    double raw = demandFraction * timeOfDayFactor(startMinutes);
    return std::min(1.0, raw);
}

inline double expectedAudience(const Movie& movie, const Screen& screen, int startMinutes) {
    return screen.capacity * expectedOccupancy(movie, startMinutes);
}

inline double expectedRevenue(const Movie& movie, const Screen& screen, int startMinutes) {
    return expectedAudience(movie, screen, startMinutes) * movie.basePrice * screen.priceMultiplier;
}

// Revenue per occupied minute -- THE primary greedy ranking score.
//
// Why this instead of raw expected revenue: screen-minutes are the scarce
// resource, not shows. A 3-hour movie earning Rs 30,000 sounds better than
// a 90-minute movie earning Rs 18,000, but the short movie earns more
// PER MINUTE of screen time it occupies -- and that spare time can often
// be sold again later in the day. Ranking candidates by revenue-per-minute
// is a simple, well-known idea (the same intuition behind classic
// "weighted interval scheduling by rate" heuristics) and it is what makes
// the greedy scheduler naturally account for the runtime-vs-revenue
// trade-off without any lookahead or dynamic programming.
inline double revenuePerMinute(double revenue, int runtimeMinutes, int turnaroundMinutes) {
    int occupiedMinutes = runtimeMinutes + turnaroundMinutes;
    if (occupiedMinutes <= 0) return 0.0;
    return revenue / occupiedMinutes;
}
