# C++ Low-Latency Order Matching Engine — Final Engineering Report

**Language:** C++20
**Platform:** Windows / MSVC
**Build system:** CMake
**Project status:** Phase 10 completed — final portfolio release verified

## 1. Executive Summary

This project implements a C++20 order matching engine designed to investigate correctness, data-structure selection, concurrency, exception safety and performance optimization in latency-sensitive trading infrastructure.

The engine supports LIMIT and MARKET orders, price-time priority, partial and full fills, cancellations, historical order ID validation and trade recording.

A dedicated matching worker processes commands submitted by multiple producer threads through a bounded FIFO queue. Matching-state mutations are serialized to preserve a defined command-processing order.

The engineering process progressed from deterministic matching correctness to differential verification, concurrent command processing, benchmarking, allocation profiling and measurement-driven optimization.

A Phase 9 optimization replaced exact-capacity trade-history vector growth with geometric capacity growth. In the recorded 10,000-trade workload, median elapsed time decreased from **259.361 ms to 10.6309 ms**, an approximately **24.4× improvement**.

These measurements describe a specific benchmark workload. They do not establish production trading latency, universal matching performance or deterministic tail-latency guarantees.

## 2. System Architecture

### 2.1 Core Components

| Component | Responsibility |
|---|---|
| `Order` | Order validation, attributes, quantities and lifecycle |
| `OrderBook` | Bid/ask price levels, resting orders, FIFO priority and cancellation |
| `MatchingEngine` | Admission, matching, execution, trade recording and order ID management |
| `MatchingCommand` / `CancelCommand` | Typed asynchronous commands and result delivery |
| `BoundedBlockingQueue` | Thread-safe bounded FIFO command storage and backpressure |
| `MatchingWorker` | Dedicated command-processing thread and shutdown lifecycle |

### 2.2 Data Structures

The order book uses ordered price levels backed by `std::map`, with `std::list` maintaining FIFO order within each price level.

An `std::unordered_map` provides average constant-time lookup of active orders by ID.

The concurrent command queue uses `std::deque`, `std::mutex` and `std::condition_variable`.

This design favors explicit ordering, stable order iterators and predictable matching semantics. It does not claim lock-free execution or zero dynamic allocation.

### 2.3 Matching Rules

- Higher bid prices take priority over lower bid prices.
- Lower ask prices take priority over higher ask prices.
- Orders at the same price execute in FIFO order.
- Trades execute at the resting order's price.
- Partially filled LIMIT orders may rest with their remaining quantity.
- MARKET orders execute against available opposite-side liquidity without resting their unfilled quantity.
- Accepted order IDs cannot be reused within the same engine instance, including after execution or cancellation.
- Prices are represented using integer ticks.

## 3. Correctness Verification

### 3.1 Matching Behavior

Automated tests cover order validation, lifecycle transitions, LIMIT and MARKET execution, crossing and non-crossing scenarios, partial and full fills, cancellations, duplicate IDs and order-book invariants.

Boundary and failure-injection tests exercise selected invalid-input, overflow-related and rollback scenarios.

### 3.2 Differential and Randomized Verification

The project includes deterministic comparison against a simpler reference implementation and seeded randomized matching tests.

The recorded randomized matching workload includes five seeds with 1,000 operations per seed.

These tests increase confidence in behavioral consistency but do not constitute exhaustive state-space verification.

Phase 10 added deterministic cancellation verification and seeded randomized differential testing covering LIMIT orders, MARKET orders and cancellations.

The cancellation-focused randomized verification executed five fixed seeds with 1,000 operations per seed.

Selected allocator-operation and container-failure injection scenarios were verified under P6-12. Exhaustive allocation-failure coverage and universal exception-safety guarantees are not claimed.

### 3.3 Exception Safety

The engine includes admission preparation, capacity management and failure-injection checkpoints intended to reduce failures after matching begins.

Test-only failure-injection functionality is separated from the production matching-core target.

However, exhaustive real allocator-failure coverage and a global strong exception guarantee after irreversible trade execution have not been established.

## 4. Concurrency Verification

The system uses a multiple-producer, single-matching-worker architecture.

Producer threads enqueue commands, and one worker processes accepted commands sequentially.

The implementation uses futures and promises to communicate results and exceptions to callers.

Verification covers:

- Concurrent submissions and duplicate order IDs.
- FIFO processing of accepted commands.
- Asynchronous cancellation.
- Worker exception propagation and recovery.
- Bounded queue backpressure.
- Rejection of new commands after shutdown.
- Draining of previously accepted commands.
- Blocked-producer wake-up.
- Concurrent shutdown calls.
- Seeded concurrent stress scenarios.

FIFO ordering is determined by successful queue insertion, not by the wall-clock order in which concurrent API calls begin.

The design does not establish that unsynchronized external reads of matching state are safe while the worker is active.

## 5. Performance Measurement

### 5.1 Phase 8 Baseline

Phase 8 introduced benchmark workloads for matching latency, throughput, concurrent command processing and allocation behavior.

The detailed methodology and results are retained in:

- `docs/PHASE8_PERFORMANCE_REPORT.md`
- `docs/P8-09-ALLOCATION-PROFILING.md`

This report does not reproduce Phase 8 numerical measurements that have not been independently rechecked here.

### 5.2 Identified Bottleneck

Phase 9 profiling identified repeated trade-history vector capacity growth as a significant cost in a workload that generated many trades.

The baseline implementation expanded capacity exactly as required, producing repeated reallocations and copies or moves of stored trade records.

The optimized implementation uses geometric capacity growth, reducing the frequency of reallocations.

### 5.3 Recorded Benchmark Comparison

**Environment:** Windows, MSVC, Release build
**Workload:** 10,000 MARKET orders generating 10,000 trades
**Measurements:** Seven runs per configuration

| Configuration | Median elapsed time | Capacity growth events |
|---|---:|---:|
| Exact-capacity growth | 259.361 ms | 10,000 |
| Geometric growth | 10.6309 ms | 15 |
| Preallocated capacity | 10.9291 ms | 0 |

The measured comparison indicates:

- Approximately **24.4× lower elapsed-time ratio** for geometric growth versus the exact-capacity baseline.
- Approximately **95.9% reduction** in median elapsed time.
- Approximately **99.85% reduction** in capacity growth events.

The geometric-growth configuration reached a final capacity of 16,384 trade records for the measured 10,000-trade workload.

The exact-capacity baseline exhibited substantial run-to-run variability. Results should therefore be interpreted alongside the full benchmark report rather than as universal performance guarantees.

The detailed comparison is recorded in `docs/P9_04_BENCHMARK_REPORT.md`.

### 5.4 Optimization Validation

Following the optimization, the project underwent Debug and Release correctness and concurrency regression testing.

Final Phase 10 verification completed all six CTest suites successfully in both Debug and Release configurations.

- Debug: 6/6 tests passed in 5.52 seconds.
- Release: 6/6 tests passed in 5.51 seconds.

These results verify the tested behavior under both configurations.
They do not establish production latency guarantees.

### 5.5 AddressSanitizer Verification

AddressSanitizer verification was performed on Windows using
Visual Studio 2022, MSVC 19.43 and the x64 target.

A separate Debug build was compiled with `/fsanitize=address /Zi /EHsc`.

All six CTest suites passed under AddressSanitizer in 16.16 seconds,
with no sanitizer violations reported during the executed tests.

The MSVC AddressSanitizer runtime directory was added to PATH
to resolve an initial runtime DLL loading failure.

ThreadSanitizer was not executed because the native Windows/MSVC
toolchain does not provide the required support.

Consequently, AddressSanitizer results do not establish freedom
from data races or guarantee memory safety on untested execution paths.

## 6. Engineering Decisions and Tradeoffs

### 6.1 Trade-History Capacity

Geometric capacity growth was retained because the measured workload demonstrated a substantial reduction in repeated reallocations.

This approach may retain unused vector capacity in exchange for lower allocation frequency.

### 6.2 Trade-History Retention

Trade history currently remains unbounded.

A future production-oriented design would require an explicit consumption, acknowledgment, retention or backpressure policy. Silent trade loss is not an acceptable substitute for such a policy.

### 6.3 Historical Order IDs

`MatchingEngine` and `OrderBook` retain historical order ID state for different responsibilities.

This preserves their respective admission and book-level uniqueness contracts but introduces overlapping memory and hash-table overhead.

The current design retains these structures rather than introducing an unverified ownership refactor during release preparation.

### 6.4 Session Identity Scope

Order ID uniqueness is enforced within a matching engine instance.

A future multi-instrument architecture would require an explicit policy for instrument-scoped or session-wide identifiers.

### 6.5 MARKET SELL Preflight

MARKET SELL preflight uses `Price{0}` as a crossing bound.

This is consistent with the current model, which requires strictly positive LIMIT prices.

Supporting zero or negative resting prices would require reconsideration of this assumption.

## 7. Production Build Configuration

A separate CMake configuration builds the production matching core and demonstration executable without test or benchmark targets.

```powershell
cmake -S . -B build-production-x64 -G "Visual Studio 17 2022" -A x64 `
    -DBUILD_TESTING=OFF `
    -DBUILD_BENCHMARKS=OFF

cmake --build build-production-x64 --config Release

.\build-production-x64\Release\matching_engine.exe
```

Phase 10 verified a clean x64 production Release configuration using MSVC 19.43. The build completed successfully with testing and benchmarks disabled. The only generated application executable was `matching_engine.exe`.

The production configuration does not include the dedicated testing and benchmarking targets.

The executable demonstrates matching functionality; it is not an exchange gateway or production trading service.

## 8. Known Limitations

The following limitations remain explicitly acknowledged:

1. No production exchange connectivity or real-world trading deployment.
2. No persistent trade storage or crash-recovery mechanism.
3. No bounded trade-history retention or consumer acknowledgment.
4. Historical order ID storage grows throughout an engine instance's lifetime.
5. Overlapping historical ID ownership between the engine and order book.
6. No multi-instrument session-wide identity manager.
7. No lock-free matching architecture or parallel matching within one engine.
8. No guaranteed deterministic latency or zero-allocation matching.
9. No exhaustive allocator-failure or post-first-trade exception-safety proof.
10. No established safety guarantee for unsynchronized external reads during worker operation.
11. MARKET SELL preflight depends on the current positive-price model.
12. No independently validated production latency or tail-latency guarantees.
13. No exhaustive formal verification of concurrent behavior.

These limitations define the scope of the current implementation and provide concrete directions for future engineering work.


## 9. Verification and Release Status

| Area | Status |
|---|---|
| Core LIMIT and MARKET matching | Implemented and tested |
| Price-time priority and cancellations | Implemented and tested |
| Deterministic and randomized differential verification | Completed |
| Concurrent worker and shutdown behavior | Implemented and tested |
| Phase 8 performance measurement | Completed |
| Phase 9 allocation optimization | Completed and measured |
| Final Debug verification | 6/6 passed |
| Final Release verification | 6/6 passed |
| AddressSanitizer verification | 6/6 passed |
| ThreadSanitizer verification | Not performed; toolchain limitation |
| Production x64 Release build | Verified |
| Final portfolio documentation | Completed |

## 10. Conclusion

The project demonstrates a correctness-first approach to developing performance-sensitive C++ systems.

Its principal engineering outcomes are a price-time-priority matching implementation, automated correctness verification, serialized concurrent command processing, documented architectural tradeoffs and a benchmark-supported optimization of trade-history allocation behavior.

The measured optimization provides evidence that memory-management decisions can materially affect performance under specific workloads.

The implementation remains an educational and portfolio-oriented matching engine rather than a production exchange system.

Final Phase 10 regression verification, AddressSanitizer testing, standalone production Release compilation and documentation review have completed successfully.

The project is finalized as an educational and portfolio-oriented C++ low-latency order matching engine, with remaining engineering limitations explicitly documented.
