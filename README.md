# C++ Low-Latency Order Matching Engine

A C++20 order matching engine built to explore the design and implementation of performance-sensitive trading infrastructure.

The engine supports limit and market orders, price-time priority, partial and full fills, cancellations, trade recording and concurrent order submission through a dedicated matching worker.

The project is developed using a **correctness-first, measurement-driven approach**, with particular attention to data structures, deterministic execution, memory management, exception safety, multithreading and automated verification.

**Current status:** Phases 1 through 8 are complete, including core matching,
correctness hardening, concurrent command processing, deterministic
and randomized verification, latency benchmarking and allocation
profiling.

Phase 9 optimization has demonstrated measurable improvements to
trade-history allocation behavior. Production Release build
verification is complete, with final documentation and release
verification in progress.

## Key Features

### Order Matching

- **Limit orders:** Match against eligible opposite-side liquidity. Any remaining quantity rests in the order book at the specified limit price.
- **Market orders:** Execute against available opposite-side liquidity without resting unfilled quantity.
- **Price-time priority:** Higher bid prices and lower ask prices receive priority, with FIFO ordering among orders at the same price.
- **Partial and full fills:** Track executed and remaining quantities while maintaining order lifecycle invariants.
- **Cancellations:** Remove active orders by ID, including partially filled resting orders.
- **Trade recording:** Record incoming order ID, resting order ID, execution price and executed quantity.
- **Order ID uniqueness:** Reject reuse of previously accepted order IDs within the engine session.
- **Integer price representation:** Use integer ticks instead of floating-point prices.

Trades execute at the resting order's price.

### Concurrent Command Processing

- Multiple producer threads can submit orders and cancellation requests.
- A dedicated worker thread processes matching commands.
- A bounded blocking queue provides FIFO ordering of accepted commands and backpressure.
- `std::future` and `std::promise` communicate results and exceptions to callers.
- Matching operations are serialized through the worker to avoid concurrent mutation of the order book.
- Shutdown closes the queue, rejects further commands and drains previously accepted commands.

Concurrent shutdown serialization, accepted-command draining, blocked-producer wake-up and exception recovery have been verified in Debug and Release builds. Deterministic and seeded stress testing confirmed FIFO ordering, price-time priority, quantity conservation and cancellation races across concurrent producers.

### Reliability and Correctness

- Order validation and lifecycle invariants.
- Active-order lookup and price-level cleanup.
- Preparation of incoming-order storage and trade-record capacity before limit-order execution.
- Failure-injection checkpoints for insertion and rollback paths.
- Exception propagation from the matching worker to callers.
- Deterministic reference-model differential testing.
- Seeded randomized matching tests.
- Debug and Release regression testing.

## Architecture

The engine follows a multiple-producer, single-matching-worker design.

```text
Producer 1 ──┐
Producer 2 ──┼──> Bounded FIFO Queue
Producer N ──┘             |
                           v
                    Matching Worker
                           |
                           v
                    Matching Engine
                           |
                           v
                       Order Book
```

Producers enqueue commands, while the dedicated worker executes them sequentially.

This architecture separates concurrent request submission from order-book mutation, preserving a defined FIFO command-processing order without requiring multiple threads to modify the matching state simultaneously.

For detailed thread-safety contracts, ordering guarantees,
backpressure behavior, shutdown semantics and limitations,
see [Concurrency Architecture](docs/CONCURRENCY_ARCHITECTURE.md).

### Core Components

| Component | Responsibility |
|---|---|
| `Order` | Order attributes, validation, remaining quantity and lifecycle |
| `OrderBook` | Bid/ask price levels, FIFO order storage, active-order lookup and cancellation |
| `MatchingEngine` | Order admission, matching, trade execution and ID management |
| `MatchingCommand` / `CancelCommand` | Order submission and cancellation requests with corresponding result promises |
| `BoundedBlockingQueue` | Thread-safe bounded FIFO queue with blocking producers and shutdown support |
| `MatchingWorker` | Dedicated worker thread, command dispatch and result delivery |

## Data Structures and Design Decisions

The order book combines several standard C++ containers to balance price discovery, FIFO execution and cancellation efficiency.

| Data structure | Usage | Purpose |
|---|---|---|
| `std::map` | Bid and ask price levels | Maintain ordered prices |
| `std::list` | Orders within each price level | Preserve FIFO priority and stable iterators |
| `std::unordered_map` | Active order ID index | Support average constant-time lookup |
| `std::deque` | Bounded command queue | Store pending commands in FIFO order |
| `std::mutex` and `std::condition_variable` | Queue synchronization | Coordinate concurrent producers and the worker |

Bid prices are maintained in descending priority, while ask prices are maintained in ascending priority.

Each price level stores orders in arrival order. An index from order IDs to list iterators allows the engine to locate active orders without scanning the entire book.

### Complexity Characteristics

| Operation | Expected complexity |
|---|---|
| Active order ID lookup | Average O(1) |
| Best bid/ask access | O(1) when the corresponding side is nonempty |
| Insert into an existing price level | O(1) for list insertion, excluding auxiliary index operations |
| Create or locate a price level | O(log(P + 1)) |
| Cancel an active order | Average O(1) lookup, with up to O(log P) price-level removal |
| Match an incoming order | Depends on the number of executions and price levels traversed |

`P` represents the number of price levels on the relevant side of the book.

These are data-structure complexity characteristics, not measured latency guarantees. Hash-table behavior, memory allocation and other implementation costs can affect actual performance.

## Order Execution Example

Consider the following resting sell orders:

| Order ID | Side | Price (ticks) | Quantity |
|---|---|---|---|
| 101 | SELL | 10500 | 5 |
| 102 | SELL | 10500 | 10 |
| 103 | SELL | 10600 | 8 |

An incoming limit BUY order arrives:

```text
Order ID: 201
Side: BUY
Type: LIMIT
Price: 10600
Quantity: 12
```

The engine executes:

```text
Trade 1
Incoming Order: 201
Resting Order: 101
Price: 10500
Quantity: 5

Trade 2
Incoming Order: 201
Resting Order: 102
Price: 10500
Quantity: 7
```

The incoming BUY order is fully filled.

The remaining resting liquidity is:

```text
SELL 102 | Price 10500 | Quantity 3
SELL 103 | Price 10600 | Quantity 8
```

This illustrates price priority, FIFO ordering within a price level, partial fills and execution at resting-order prices.

## Testing and Verification

The project includes automated tests for both matching correctness and concurrent command processing.

### Core Matching Tests

- Order construction, validation and lifecycle transitions
- Limit and market order execution
- Crossing and non-crossing orders
- Partial and full fills
- Price-time priority
- Cancellation and price-level cleanup
- Duplicate and historical order IDs
- Order-book invariants
- Quantity boundaries and overflow-related behavior
- Insertion-failure checkpoints and rollback paths

### Differential Testing

The engine is compared against a simpler reference implementation to detect behavioral discrepancies.

Verification includes:

- Fixed deterministic matching scenarios
- Seeded randomized operation sequences
- Comparison of matching results and relevant engine state
- Reproducible random seeds for investigating failures

The randomized matching suite includes five seeds with 1,000 operations per seed.

Additional randomized cancellation edge cases remain tracked
follow-up work. Concurrent correctness, ordering and seeded
stress verification were completed in Phase 7.7.

### Concurrent Worker Tests

Automated tests cover:

- Limit and market submissions through the worker
- Multiple concurrent producers
- Multiple outstanding futures
- Concurrent duplicate order IDs
- FIFO command processing
- Cancellation through the worker
- Exception propagation and worker recovery
- Rejection of new commands after shutdown
- Draining accepted submissions and cancellations
- Backpressure and shutdown interaction
- Completion of accepted-command futures during shutdown
- Blocked-producer wake-up on shutdown
- Concurrent shutdown calls

Shutdown and backpressure tests include deterministic synchronization, concurrent shutdown calls, quantity conservation, price-time priority, ordering races and repeated seeded stress execution. Phase 7 concurrency implementation, correctness verification,
lifecycle audits and documentation are complete.

### Build Configurations

Both Debug and Release configurations are used for verification.

Tests use checks that remain active in Release builds rather than relying exclusively on standard `assert()` behavior.

Test-only failure-injection functionality is separated from the production matching-core target.

For recorded verification results and revisions, see [Verification Log](docs/VERIFICATION_LOG.md).

## Build Requirements

- CMake 3.20 or newer
- C++20-compatible compiler
- A build tool supported by CMake
- Windows with MSVC and an appropriate Visual Studio developer terminal for the commands below

The project has been developed and tested on Windows using MSVC, CMake, NMake and the Visual Studio/MSBuild generator.

Other platforms and toolchains have not yet been established as verified configurations.

## Build and Run

Run the following commands from the repository root.

### Debug Build

```powershell
cmake -S . -B build -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
.\build\matching_engine.exe
```

### Release Build

```powershell
cmake -S . -B build-release -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build-release
ctest --test-dir build-release --output-on-failure
.\build-release\matching_engine.exe
```

These commands use the single-configuration NMake generator. An existing build directory must use the generator with which it was originally configured.

The `matching_engine` executable runs the demonstration in `src/main.cpp`. Automated regression tests execute separately through CTest.

### Production Release Build (Visual Studio / MSVC)

A separate production configuration excludes test and benchmark
targets.

```powershell
cmake -S . -B build-production `
    -DBUILD_TESTING=OFF `
    -DBUILD_BENCHMARKS=OFF

cmake --build build-production --config Release

.\build-production\Release\matching_engine.exe
```

## Project Structure

```text
hft-matching-engine/
|
|-- include/
|   `-- matching_engine/
|       |-- Order.hpp
|       |-- OrderBook.hpp
|       |-- MatchingEngine.hpp
|       |-- MatchingCommand.hpp
|       |-- MatchingWorker.hpp
|       `-- BoundedBlockingQueue.hpp
|
|-- src/
|   |-- Order.cpp
|   |-- OrderBook.cpp
|   |-- MatchingEngine.cpp
|   |-- MatchingWorker.cpp
|   `-- main.cpp
|
|-- tests/
|   |-- OrderTests.cpp
|   |-- OrderBookTests.cpp
|   |-- MatchingEngineTests.cpp
|   |-- MatchingCommandTests.cpp
|   |-- MatchingWorkerTests.cpp
|   `-- BoundedBlockingQueueTests.cpp
|
|-- benchmarks/
|   |-- BenchmarkMain.cpp
|   `-- AllocationProfile.cpp
|
|-- docs/
|   |-- CONCURRENCY_ARCHITECTURE.md
|   |-- ENGINEERING_BACKLOG.md
|   |-- MARKET_ORDER_POLICY.md
|   |-- PHASE8_PERFORMANCE_REPORT.md
|   |-- P8-09-ALLOCATION-PROFILING.md
|   |-- P9_04_BENCHMARK_REPORT.md
|   `-- VERIFICATION_LOG.md
|
|-- CMakeLists.txt
`-- README.md
```

## Development Roadmap

| Phase | Focus | Status |
|---|---|---|
| 1–3 | Project setup, order model and order book | Completed |
| 4 | Limit order matching and price-time priority | Completed |
| 5 | Market order execution | Completed |
| 6 | Correctness hardening, failure testing and differential verification | Completed |
| 7 | Concurrent command processing and worker lifecycle | Completed |
| 8 | Latency benchmarking, throughput analysis and allocation profiling | Completed |
| 9 | Measurement-driven optimization, architecture review and release preparation | In progress |

### Current Engineering Priorities

1. Complete architecture and usage documentation.
2. Consolidate correctness, performance and known-limitations reports.
3. Run final Debug, Release and production build verification.
4. Prepare the final repository release.

Additional allocator-failure coverage and exception-safety verification remain tracked follow-up items.

Detailed engineering tasks and outstanding limitations are maintained in the [Engineering Backlog](docs/ENGINEERING_BACKLOG.md).

## Performance and Benchmarking

Phase 8 established reproducible benchmark workloads for matching,
latency, throughput, concurrency and allocation behavior.

Phase 9 identified excessive trade-history vector reallocations
as a measurable performance bottleneck.

### Trade-History Allocation Optimization

The original implementation expanded trade-history capacity
exactly as required. The optimized implementation uses geometric
capacity growth.

The following results were recorded using a Release build on
Windows with MSVC.

**Workload:** 10,000 MARKET orders generating 10,000 trades.
Seven measured runs were performed for each configuration.

| Configuration | Median elapsed time | Capacity growth events |
|---|---:|---:|
| Exact-capacity growth (baseline) | 259.361 ms | 10,000 |
| Geometric growth (optimized) | 10.6309 ms | 15 |
| Preallocated capacity | 10.9291 ms | 0 |

### Observed Improvement

- Approximately **24.4x faster** than the exact-capacity baseline.
- Approximately **95.9% lower median elapsed time**.
- Approximately **99.85% fewer capacity growth events**.
- Geometric growth retained additional capacity to reduce
  repeated reallocations.

These results apply to the measured workload and environment.
They are not universal matching-engine speedup claims or
guarantees of production tail latency.

The exact-capacity baseline also exhibited substantial
run-to-run variability.

### Benchmark Documentation

- [Phase 8 Performance Report](docs/PHASE8_PERFORMANCE_REPORT.md)
- [Phase 8 Allocation Profiling](docs/P8-09-ALLOCATION-PROFILING.md)
- [Phase 9 Optimization Comparison](docs/P9_04_BENCHMARK_REPORT.md)

Performance optimization follows a measurement-driven process:
establish a baseline, identify a bottleneck, modify the
implementation, rerun correctness tests and compare results.

## Scope and Limitations

This project is an educational and portfolio-oriented matching engine, not a production exchange system.

It currently does not claim:

- Production-ready exchange connectivity
- Real-world trading deployment
- Lock-free command processing
- Guaranteed deterministic latency
- Zero-allocation matching
- Fully verified concurrent object destruction
- Exhaustive exception-safety guarantees
- Independently validated production performance results

The goal is to build a well-tested foundation, make engineering tradeoffs explicit and improve performance using reproducible evidence.

### Known Engineering Limitations

- Trade-history storage remains unbounded; no consumer acknowledgment
  or bounded pending-trade policy is implemented.
- Historical order IDs are retained throughout the engine lifetime.
- MatchingEngine and OrderBook maintain overlapping historical ID sets
  for different ownership responsibilities.
- Order ID uniqueness is scoped to a MatchingEngine instance.
- Multi-instrument session-wide identity management is not implemented.
- MARKET preflight assumes strictly positive LIMIT prices.
- Exchange connectivity, persistent storage and crash recovery
  are not implemented.
- Benchmark results are workload-specific and do not establish
  guaranteed production tail latency.
- The current executable is a demonstration, not a trading service.

## Documentation

- [Concurrency Architecture](docs/CONCURRENCY_ARCHITECTURE.md)
- [Market Order Policy](docs/MARKET_ORDER_POLICY.md)
- [Engineering Backlog](docs/ENGINEERING_BACKLOG.md)
- [Verification Log](docs/VERIFICATION_LOG.md)
- [Phase 8 Performance Report](docs/PHASE8_PERFORMANCE_REPORT.md)
- [Phase 8 Allocation Profiling](docs/P8-09-ALLOCATION-PROFILING.md)
- [Phase 9 Benchmark Comparison](docs/P9_04_BENCHMARK_REPORT.md)

## Development Philosophy

**Correctness -> Deterministic Verification -> Concurrency Safety -> Benchmarking -> Profiling -> Optimization**

The project emphasizes understanding the underlying C++ and data-structure decisions, validating behavior through automated tests and making performance claims only when supported by measurements.
