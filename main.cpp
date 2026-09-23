// main.cpp
// Builds each demo dataset directly as C++ structs (see the note in
// test_data/README.md about why there's no JSON *parser* yet), runs the
// engine, prints a human-readable report, and writes the JSON *output*
// (the actual future frontend contract) to test_data/ for inspection.
#include <iostream>
#include <fstream>
#include <vector>
#include "models.h"
#include "scheduler.h"
#include "algorithms.h"

using namespace std;

static void writeJsonFile(const string& path, const string& content) {
    ofstream out(path);
    out << content;
    cout << "  (JSON output written to " << path << ")\n";
}

// A tiny demonstration of the search/sort utilities operating outside the
// scheduler itself -- e.g. "the owner wants movies sorted by price, and to
// binary-search for one at an exact price point" -- a realistic small
// reporting feature that a dashboard might want.
static void demoSearchAndSort(vector<Movie> movies) {
    cout << "\n--- DSA demo: sort movies by price (merge sort), then binary search ---\n";
    mergeSort<Movie>(movies, [](const Movie& a, const Movie& b) { return a.basePrice < b.basePrice; });
    for (const auto& m : movies) cout << "  Rs " << m.basePrice << "  " << m.name << "\n";

    double target = movies.empty() ? 0 : movies[movies.size() / 2].basePrice;
    int idx = binarySearch<Movie>(movies, target, [](const Movie& m) { return m.basePrice; });
    if (idx != -1) cout << "  binarySearch(Rs " << target << ") -> found \"" << movies[idx].name << "\"\n";
    else cout << "  binarySearch(Rs " << target << ") -> not found\n";
}

// ===========================================================================
// MANUAL EXAMPLE -- Section 21: small enough to trace by hand.
// 4 screens, 4 movies, a 4-hour window, 60-minute candidate spacing.
// ===========================================================================
static void runManualExample() {
    Theatre theatre{"Manual Example Cinema", 9 * 60, 13 * 60, 15}; // 09:00-13:00, 15 min turnaround

    vector<Screen> screens = {
        {1, "Screen 1 (Premium)", 200, "Premium", 1.2, true},
        {2, "Screen 2", 150, "Standard", 1.0, true},
        {3, "Screen 3", 120, "Standard", 1.0, true},
        {4, "Screen 4", 100, "Standard", 1.0, true},
    };
    vector<Movie> movies = {
        {1, "Blockbuster", 150, 250.0, 9, "Action", 1, 2},
        {2, "Comedy Hour", 100, 180.0, 6, "Comedy", 1, 2},
        {3, "Drama Nights", 120, 150.0, 4, "Drama", 0, 2},
        {4, "Indie Spark",   90, 120.0, 3, "Indie", 0, 2},
    };

    cout << "\n########## MANUAL EXAMPLE (pen-and-paper) ##########\n";
    EngineResult result = runEngine(theatre, screens, movies, /*startInterval=*/60);
    printHumanReport(theatre, screens, movies, result, /*verboseRejections=*/true);
    writeJsonFile("test_data/manual_example_output.json", toJson(theatre, screens, movies, result));
}

// ===========================================================================
// TEST 1 -- Normal 4-screen cinema, a full realistic day.
// ===========================================================================
static void runTest1() {
    Theatre theatre{"Test 1: City Multiplex", 10 * 60, 24 * 60, 20}; // 10:00-24:00, 20 min turnaround

    vector<Screen> screens = {
        {1, "Screen 1 (IMAX)", 280, "IMAX", 1.4, true},
        {2, "Screen 2 (Premium)", 200, "Premium", 1.15, true},
        {3, "Screen 3", 160, "Standard", 1.0, true},
        {4, "Screen 4", 140, "Standard", 1.0, true},
    };
    vector<Movie> movies = {
        {1, "Galactic Siege",     165, 280.0, 9, "Sci-Fi",   1, 3},
        {2, "Summer Romance",     110, 200.0, 7, "Romance",  1, 3},
        {3, "Laugh Riot",          95, 180.0, 6, "Comedy",   1, 3},
        {4, "The Long Wait",      150, 190.0, 5, "Drama",    0, 2},
        {5, "Midnight Chase",     128, 210.0, 6, "Thriller", 0, 2},
        {6, "Quiet Places",        98, 150.0, 4, "Indie",    0, 2},
        {7, "Kids' Adventure",    100, 160.0, 6, "Family",   1, 2},
    };

    cout << "\n########## TEST 1: Normal 4-screen cinema ##########\n";
    EngineResult result = runEngine(theatre, screens, movies, /*startInterval=*/30);
    printHumanReport(theatre, screens, movies, result);
    writeJsonFile("test_data/test1_output.json", toJson(theatre, screens, movies, result));
}

// ===========================================================================
// TEST 2 -- Deliberately varied runtimes: 90 / 120 / 150 / 180 minutes.
// Demonstrates the runtime-vs-revenue (opportunity cost) trade-off.
// ===========================================================================
static void runTest2() {
    Theatre theatre{"Test 2: Runtime Variety Cinema", 9 * 60, 23 * 60 + 30, 15}; // 09:00-23:30

    vector<Screen> screens = {
        {1, "Screen 1", 220, "Standard", 1.1, true},
        {2, "Screen 2", 200, "Standard", 1.0, true},
        {3, "Screen 3", 180, "Standard", 1.0, true},
        {4, "Screen 4", 150, "Standard", 1.0, true},
    };
    vector<Movie> movies = {
        {1, "Ninety Minutes Flat", 90,  170.0, 7, "Comedy",  0, 3},
        {2, "Two-Hour Thriller",  120, 200.0, 7, "Thriller", 0, 3},
        {3, "Two-and-a-Half Epic", 150, 230.0, 7, "Drama",   0, 3},
        {4, "The Three Hour Saga", 180, 260.0, 7, "Fantasy", 0, 2},
    };
    // Demand scores are deliberately EQUAL across all four movies so that
    // runtime and price are the only things distinguishing the candidates
    // -- this isolates the revenue-per-minute effect for the report.

    cout << "\n########## TEST 2: Varied movie runtimes ##########\n";
    EngineResult result = runEngine(theatre, screens, movies, /*startInterval=*/30);
    printHumanReport(theatre, screens, movies, result);
    writeJsonFile("test_data/test2_output.json", toJson(theatre, screens, movies, result));
}

// ===========================================================================
// TEST 3 -- High-demand blockbuster: shows the algorithm favouring prime time.
// ===========================================================================
static void runTest3() {
    Theatre theatre{"Test 3: Blockbuster Weekend", 9 * 60, 24 * 60, 15}; // 09:00-24:00

    vector<Screen> screens = {
        {1, "Screen 1 (IMAX)", 300, "IMAX", 1.4, true},
        {2, "Screen 2", 200, "Standard", 1.0, true},
        {3, "Screen 3", 180, "Standard", 1.0, true},
        {4, "Screen 4", 150, "Standard", 1.0, true},
    };
    vector<Movie> movies = {
        {1, "Mega Blockbuster", 150, 300.0, 10, "Action", 1, 4}, // the headline release
        {2, "Steady Seller",    110, 190.0, 6,  "Drama",  0, 3},
        {3, "Family Pick",       98, 160.0, 5,  "Family", 0, 3},
        {4, "Niche Indie",      105, 140.0, 3,  "Indie",  0, 2},
    };

    cout << "\n########## TEST 3: High-demand blockbuster ##########\n";
    EngineResult result = runEngine(theatre, screens, movies, /*startInterval=*/30);
    printHumanReport(theatre, screens, movies, result);
    writeJsonFile("test_data/test3_output.json", toJson(theatre, screens, movies, result));

    cout << "\nMega Blockbuster shows and their start times:\n";
    for (const auto& s : result.optimized.shows)
        if (s.movieId == 1) cout << "  Screen " << s.screenId << " @ " << formatTime(s.startMinutes) << "\n";
}

int main() {
    runManualExample();
    runTest1();
    runTest2();
    runTest3();

    vector<Movie> sampleForDemo = {
        {1, "Alpha", 100, 220.0, 8, "Action", 0, 2},
        {2, "Beta",   90, 150.0, 5, "Comedy", 0, 2},
        {3, "Gamma", 130, 300.0, 6, "Drama",  0, 2},
    };
    demoSearchAndSort(sampleForDemo);

    cout << "\nAll runs complete.\n";
    return 0;
}
