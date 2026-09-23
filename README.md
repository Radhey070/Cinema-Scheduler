# Cinema Show Scheduling & Revenue Optimization Engine

A C++17 backend/core engine for a DSA final-year project: given a theatre's
screens, operating hours and a slate of movies, it builds a realistic
**timeline-based** show schedule (movies of different runtimes, back-to-back
with a turnaround buffer, inside real operating hours) two ways -- a fair
**baseline** and a **greedy revenue-optimization heuristic** -- and reports
the difference in projected revenue, honestly.

This document is the redesigned Phase 1 concept plus everything from Phase 2
onward, in the order requested: algorithm, why it was chosen, files, how to
compile/run, example input/output, complexity, and what's left for the web
frontend.

---

## 1. Final Algorithm

**Model:** the cinema day is a continuous timeline per screen, not fixed
slots. A show is `(movie, screen, startTime)`; its end time is
`startTime + runtime`, and no two shows on the same screen may be closer
together than `turnaround` minutes.

**Demand model:**
```
DemandFraction     = movie.demandScore / 10                    (0.1 .. 1.0)
TimeOfDayFactor(t)  = 0.60 morning (<12:00) | 0.85 afternoon (12:00-17:00)
                     | 1.15 evening/prime (17:00-21:00) | 0.75 late night (>=21:00)
ExpectedOccupancy   = min(1.0, DemandFraction x TimeOfDayFactor(startTime))
ExpectedAudience    = screen.capacity x ExpectedOccupancy
ExpectedRevenue     = ExpectedAudience x movie.basePrice x screen.priceMultiplier
```

**Greedy ranking score -- Revenue per Occupied Minute:**
```
RevenuePerMinute = ExpectedRevenue / (movie.runtime + turnaround)
```

**Greedy Revenue Optimization Heuristic:**
1. Generate every feasible `(movie, screen, startTime)` candidate on a fixed
   time grid (default: every 30 minutes) across operating hours.
2. Sort all candidates by `RevenuePerMinute` descending (ties broken by raw
   revenue, then occupancy, then earliest start, then lowest movie/screen ID
   -- fully deterministic).
3. Walk the ranked list, accepting a candidate only if its screen is free in
   that window (respecting turnaround) and its movie hasn't hit
   `maxShows`; reject and log otherwise.
4. Stop when the list is exhausted.

**Fair baseline:** for each screen independently, rotate through the movie
list (a circular queue) packing shows back-to-back from opening time,
skipping a movie only if it doesn't fit or has hit its cap -- same runtimes,
same turnaround, same operating hours, same screens, same caps. It simply
never looks at demand or revenue when deciding order.

---

## 2. Why This Design

**Timeline instead of fixed slots.** Real showtimes aren't 200-minute boxes
-- movies range from 90 to 180+ minutes, and a slot system either wastes
time (long slot, short movie) or is infeasible (short slot, long movie).
Modelling `end = start + runtime` and checking real overlaps is barely more
code and is what an actual scheduling engine needs to look like.

**Revenue-per-minute as the primary score, not raw revenue.** This is the
direct, simple answer to the opportunity-cost question in the brief: a
3-hour show worth ₹30,000 occupies screen time that a 90-minute show worth
₹18,000 would use twice, potentially earning ₹36,000+ from the same window.
Ranking by ₹/minute makes the greedy walk naturally prefer the more
time-efficient movie first -- without dynamic programming, without
lookahead, and it is easy to compute by hand for any one candidate.

**Why greedy, not DP/backtracking:** the brief is explicit that this should
stay provable and explainable in a viva. Ranking by a single deterministic
score and walking it once is O(N log N) and traceable row-by-row on paper.
It is a heuristic, not a proof of a globally optimal schedule -- see
Section 8 ("Honest Limitations") for exactly where it can fall short, and
the Manual Example below is a real, reproduced case where it does.

**Fixed-interval candidate grid (not literally "any minute").** Generating
a candidate at every single minute would blow up the candidate count for no
real benefit -- ticket sales, staffing and trailers don't operate on
minute-level granularity anyway. A 30-minute grid (configurable) is coarse
enough to keep the candidate list small and coarse-grained enough to match
how a cinema actually publishes showtimes, while still being fine enough
that it doesn't materially constrain the schedule (see Test 1-3 results).

---

## 3. Files Created

```
backend/
├── models.h            Theatre, Screen, Movie, Show, Rejection structs + time formatting
├── data_structures.h   SinglyLinkedList, DoublyLinkedList, Queue, CircularQueue, Stack, MovieBST
├── algorithms.h         mergeSort, insertionSort, linearSearch, binarySearch,
│                        + the demand/occupancy/revenue formulas
├── scheduler.h          Public engine interface (Theatre/Screen/Movie in -> EngineResult out)
├── scheduler.cpp         Validation, candidate generation, baseline scheduler,
│                        greedy optimizer, metrics, JSON + human-readable reporting
├── main.cpp             Builds the 4 demo datasets, runs the engine, prints reports
└── test_data/           JSON *output* of each run, written here after every execution
    ├── manual_example_output.json
    ├── test1_output.json
    ├── test2_output.json
    └── test3_output.json
```

No JSON *parser* is included yet (see Section 9) -- all four demo datasets
are built directly as C++ structs in `main.cpp`, which is also exactly the
shape a future web layer would hand the engine after parsing a request.
Nothing in `scheduler.cpp`/`.h` does file or console I/O except the two
explicitly-named reporting functions (`toJson`, `printHumanReport`), so the
algorithm itself has zero I/O coupling.

---

## 4. How to Compile

```bash
g++ -std=c++17 -O2 -Wall -Wextra -o cinema_engine main.cpp scheduler.cpp
```

No external libraries, no build system needed -- two translation units and
the standard library only.

## 5. How to Run

```bash
./cinema_engine
```

This runs, in order: the Manual Example, Test 1, Test 2, and Test 3 (see
Section 7 for what each demonstrates), printing a human-readable report for
each to stdout and writing the full JSON result of each to `test_data/`.

---

## 6. Example Input (the Manual Example dataset, from `main.cpp`)

```cpp
Theatre theatre{"Manual Example Cinema", 9*60, 13*60, 15}; // 09:00-13:00, 15 min turnaround

Screens: S1 cap=200 Premium x1.2 | S2 cap=150 x1.0 | S3 cap=120 x1.0 | S4 cap=100 x1.0

Movies:
  Blockbuster   runtime=150  price=250  demand=9  maxShows=2
  Comedy Hour   runtime=100  price=180  demand=6  maxShows=2
  Drama Nights  runtime=120  price=150  demand=4  maxShows=2
  Indie Spark   runtime=90   price=120  demand=3  maxShows=2

startIntervalMinutes = 60   // coarse grid, chosen so the candidate list stays hand-sized
```

## 7. Example Output & Pen-and-Paper Verification

Every number below is exactly what `./cinema_engine` printed -- nothing is
hand-massaged.

**Hand-verifying two cells** (anyone can redo this with a calculator):

`Blockbuster` at Screen 1 (cap 200, x1.2), 09:00 start:
```
DemandFraction = 9/10 = 0.9
TimeOfDayFactor(09:00) = 0.60          (morning, hour 9 < 12)
Occupancy = min(1, 0.9 x 0.60) = 0.54
Audience  = 200 x 0.54 = 108
Revenue   = 108 x 250 x 1.2 = Rs 32,400   <- matches program output exactly
```
`Comedy Hour` at Screen 3 (cap 120, x1.0), 09:00 start:
```
DemandFraction = 6/10 = 0.6
TimeOfDayFactor(09:00) = 0.60
Occupancy = min(1, 0.36) = 0.36
Audience  = 120 x 0.36 = 43.2
Revenue   = 43.2 x 180 x 1.0 = Rs 7,776   <- matches program output exactly
```

**Baseline schedule (round-robin, ignores demand):**

| Screen | Show | Time | Audience | Revenue |
|---|---|---|---|---|
| 1 | Blockbuster | 09:00-11:30 | 108 | 32,400 |
| 2 | Blockbuster | 09:00-11:30 | 81 | 20,250 |
| 3 | Comedy Hour | 09:00-10:40 | 43 | 7,776 |
| 3 | Drama Nights | 10:55-12:55 | 29 | 4,320 |
| 4 | Comedy Hour | 09:00-10:40 | 36 | 6,480 |
| 4 | Drama Nights | 10:55-12:55 | 24 | 3,600 |

**Total Baseline Revenue = Rs 74,826** (6 shows, 321 expected viewers)

**Optimized schedule (greedy, ranked by revenue-per-minute):** Blockbuster
(Rs 216/min on Screen 1: 32,400/150) and Comedy Hour (Rs 77.8/min: 7,776/100)
consistently out-rank Drama Nights (Rs 32/min: 3,600/120) and Indie Spark,
so the ranked candidate list is dominated by Blockbuster and Comedy Hour
picks first; both hit their `maxShows=2` cap quickly, and every screen-slot
that only Drama Nights/Indie Spark could have filled at 60-minute grid
resolution is rejected either for a movie-cap or screen-overlap reason
(31 overlap rejections, 8 cap rejections -- both logged verbatim by the
program). The final accepted schedule:

| Screen | Show | Time | Audience | Revenue |
|---|---|---|---|---|
| 1 | Blockbuster | 09:00-11:30 | 108 | 32,400 |
| 2 | Blockbuster | 09:00-11:30 | 81 | 20,250 |
| 3 | Comedy Hour | 09:00-10:40 | 43 | 7,776 |
| 3 | Comedy Hour | 11:00-12:40 | 43 | 7,776 |
| 4 | Drama Nights | 09:00-11:00 | 24 | 3,600 |

**Total Optimized Revenue = Rs 71,802** (5 shows, 299 expected viewers)

**Revenue Improvement = -4.04%** -- the optimized schedule earns *less*
here. This is a deliberate, honest result (see Section 8) rather than a
manipulated one: Screen 4's 11:15-13:00 gap could fit a 90-minute Indie
Spark show in continuous time, but the 60-minute candidate grid used for
this hand-traceable example only offers starts at 09:00/10:00/11:00/12:00,
none of which fit inside that gap without overlapping or running past
13:00 -- so the greedy walk correctly reports no feasible candidate there,
while the baseline's continuous back-to-back packing finds a 6th show
anyway. Tests 1-3 (finer 30-minute grid, longer operating days) don't show
this effect and are consistently positive -- see Section 8.

---

## 8. Complexity Analysis

Let `M` = movies, `S` = screens, `T` = candidate start-times per
movie/screen pair, `N = M x S x T` = total candidates.

| Step | Time | Space |
|---|---|---|
| Candidate generation | O(N) | O(N) |
| Merge sort of candidates by score | O(N log N) | O(N) |
| Greedy walk (accept/reject) | O(N) amortized (O(k) overlap check per candidate against the k shows already on that screen, k is small and bounded by day-length/min-runtime) | O(S x shows-per-screen) |
| Doubly linked list insert (schedule) | O(shows-per-screen) to find sorted position, tiny in practice | O(1) per node |
| Baseline round-robin | O(S x M) worst case (bounded consecutive-fail counter) | O(S x shows-per-screen) |
| BST movie lookup | O(log M) average, O(M) worst case (no self-balancing -- stated plainly, fine for the tens-of-movies scale here) | O(M) |
| Merge/binary search demo (price) | O(M log M) sort, O(log M) search | O(M) |

**Overall pipeline: O(N log N), dominated entirely by the one merge sort of
the candidate list** -- the same headline as before, now over a genuinely
timeline-based candidate space instead of fixed slots.

---

## 9. Data Structures -- what's used and why

| Structure | Used for | Why it's the right fit |
|---|---|---|
| Singly Linked List | Master movie roster as loaded | Only ever appended once and walked forward during candidate generation |
| Doubly Linked List | Each screen's chronological show timeline | Must stay sorted by start time (the visual timeline); inserting/removing a show and checking the previous show's end+turnaround needs backward links |
| Queue | Feeds the ranked candidate list into the greedy walk | "Process candidates in ranked order" is literally a FIFO processing queue |
| Circular Queue | Baseline's per-screen movie rotation | The ONLY genuinely round-robin operation in the engine -- repeatedly cycling through the movie list until the day is full |
| Stack | Accepted-decision history (undo / most-recent-first log) | LIFO is exactly what "undo the last decision" needs |
| Binary Search Tree | Movie lookup by ID | O(log n) average lookup for the explanation/report layer without a hash map |
| Merge Sort | Sorting the (large) candidate list by score | Guaranteed O(n log n), no worst-case blow-up, and it's the one sort whose performance matters |
| Insertion Sort | Sorting small lists (screen IDs, per-movie stats) | Small n, simple, no dependency on being "the big sort" |
| Linear Search | Small unsorted lookups (find screen by ID, is a screen available) | Lists are tiny; honest choice over building an index nobody needs |
| Binary Search | Look up a movie by exact price in a price-sorted list | Demonstrated in `main.cpp::demoSearchAndSort` -- a realistic small dashboard feature |

Nothing here is forced in -- every structure above is called from a real
code path in `scheduler.cpp`/`main.cpp`, not just declared.

---

## 10. Honest Limitations (stated up front, not hidden)

- **The greedy heuristic is not proven globally optimal.** It is a strong,
  simple, explainable rule (rank by revenue/minute, take what fits), and the
  Manual Example above is an honest case where it underperforms the
  baseline by -4.04%, specifically because of candidate-grid granularity
  interacting with a very short operating window. Tests 1-3, with a more
  realistic 30-minute grid over a full operating day, are consistently
  positive (+9.6% to +22.3%).
- **Expected Occupancy/Revenue are forecasts, not guarantees** -- the UI/API
  terminology throughout uses "Expected"/"Projected", never "guaranteed".
- **No self-balancing BST.** Fine at the scale of a single cinema's movie
  roster (tens of movies); would matter at a chain-wide scale.
- **The time-of-day demand buckets (four fixed multipliers) are a modelling
  simplification**, not a fitted curve -- chosen specifically so it's
  simple, explainable and deterministic, per the brief's requirements.

---

## 11. Testing

Three scenarios are implemented (the constraint-heavy and pure edge-case
suites were intentionally dropped from this pass per the latest scope):

| Test | What it demonstrates |
|---|---|
| **Manual Example** | Full pen-and-paper traceability; also the honest "small/negative improvement" case (Section 20 of the brief) |
| **Test 1 -- Normal 4-screen cinema** | 7 movies, a realistic 14-hour day, 4 screens with mixed capacity/premium -- +21.11% improvement |
| **Test 2 -- Varied runtimes** | 4 movies at 90/120/150/180 minutes with **equal demand scores**, isolating the runtime-vs-revenue (opportunity cost) effect -- +9.64% improvement |
| **Test 3 -- High-demand blockbuster** | One demand=10 movie against three moderate ones -- shows the optimizer concentrating the blockbuster into every prime-time slot up to its cap (Screen 1, 09:00/12:00/17:00/20:00) while the baseline spreads it out mechanically -- +22.25% improvement, prime-time utilization rises from 43.75% to 84.69% |

Run `./cinema_engine` to reproduce all four; full JSON output for each is
written to `test_data/`.

---

## 11.5 Full-Stack Mode — the frontend calling the REAL C++ engine

A second executable, `server.cpp`, wraps the engine in a small hand-written
HTTP server (raw POSIX sockets, no library) and a hand-written JSON
*parser* (`json.h` — the writer side already existed in `scheduler.cpp`).
It serves the frontend (`public/index.html`) and one API route:

```
POST /api/schedule   body: {theatre, screens, movies}  ->  full EngineResult as JSON
GET  /api/health      ->  {"status":"ok"}
```

**Build and run it** (separate from `main.cpp` — both define `main()`,
so never compile them together):
```bash
g++ -std=c++17 -O2 -pthread -o server server.cpp scheduler.cpp
./server
```
Then open `http://localhost:3000` — the page loads with **"Use live C++
backend"** checked by default, so every "Generate schedule" click sends
your on-page inputs to `/api/schedule`, the real C++ engine computes the
answer, and the page renders whatever comes back. If the server is
unreachable, the page automatically falls back to its in-browser
JavaScript port and says so in the status line next to the button — it
never just silently breaks.

The **API base URL** field lets the same page call a backend hosted
somewhere else (e.g. a Replit URL) instead of the server that served the
page — leave it blank to call the same origin.

---

## 12. What Remains for Frontend Integration

The engine already returns a structured JSON document (see
`test_data/*_output.json` for real examples) shaped as:

```json
{
  "valid": true,
  "revenueImprovementPercent": 21.11,
  "baselineMetrics": { "totalRevenue": ..., "perMovie": [...], ... },
  "optimizedMetrics": { ... },
  "baselineSchedule": [ { "movieId", "movieName", "screenId", "startTime",
                          "endTime", "expectedAudience", "expectedRevenue",
                          "reason", ... } ],
  "optimizedSchedule": [ ... ],
  "rejectionSummary": { "dueToOverlap": ..., "dueToMovieCap": ... }
}
```

Still needed before a web frontend can drive this live (deliberately not
built yet, per the brief):

1. **A JSON *parser*** to turn an incoming request body into
   `Theatre`/`vector<Screen>`/`vector<Movie>` -- the writer side already
   exists (`toJson`); only the reader side is missing. A small hand-rolled
   parser or a single-header library (e.g. nlohmann/json) both fit the
   "no huge framework" constraint.
2. **A thin HTTP layer** (or a CLI reading stdin) that calls
   `runEngine(...)` and returns `toJson(...)` -- `main.cpp` already shows
   the exact call sequence to wrap.
3. **Full rejection detail on demand** -- `runGreedyOptimizer(...,
   captureAllRejections=true)` already supports returning every rejected
   candidate (not just the first 25) when the frontend explicitly asks for
   the full explanation trail, e.g. for a "why wasn't this movie scheduled
   more?" view.

No changes to `scheduler.h`/`scheduler.cpp`/`algorithms.h`/`data_structures.h`
should be needed for that integration -- that boundary was the point of
keeping I/O out of the algorithm layer from the start.
