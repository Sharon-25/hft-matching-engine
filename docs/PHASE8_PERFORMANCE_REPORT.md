# Phase 8 — Performance Profiling Report

## 1. Objective

Evaluate the performance characteristics of the C++20 order matching engine, identify allocation-related bottlenecks and measure the cost of preflight preparation and order execution.

The experiments focus on:

- Trade-history vector capacity management.
- Allocation-related execution overhead.
- Preflight match counting.
- Trade-history reservation.
- Matching execution across different order-book depths.

## 2. Environment and Methodology

**Environment**

- Operating system: Windows 11
- Compiler: Microsoft Visual C++ (MSVC)
- Language: C++20
- Build configuration: Release
- Timing source: `std::chrono::steady_clock`
- Memory observation: Windows `GetProcessMemoryInfo`
- Profiling executable: `matching_allocation_profile`

**Methodology**

Profiling-specific changes were isolated behind `MATCHING_ENGINE_PROFILING` and compiled into a separate profiling library.

The production matching engine retained its existing capacity-management behavior.

All benchmark workloads verified their expected execution quantities and order-book state.

Measurements were performed using repeated executions and median aggregation.

The results characterize the tested workloads and should not be interpreted as production trading-system latency.

## 3. Allocation Profiling — P8-09

### 3.1 Workload

- 10,000 preloaded SELL LIMIT orders.
- 10,000 incoming BUY MARKET orders.
- Each incoming MARKET order generated one trade.
- Three trade-history capacity strategies were compared.

### 3.2 Results

| Strategy | Median Execution Time | Capacity Growth Events | Final Capacity |
|---|---:|---:|---:|
| Exact-capacity baseline | 245.732 ms | 10,000 | 10,000 |
| Full preallocation | 5.2084 ms | 0 | 10,000 |
| Geometric growth | 5.6377 ms | 15 | 16,384 |

Five independent workload executions were recorded for each strategy.

### 3.3 Findings

The baseline repeatedly reserved the exact required trade-history capacity.

Because capacity increased with each submitted order, the vector experienced 10,000 growth events during the workload.

Full preallocation eliminated growth events during execution.

Geometric growth reduced growth events to 15, with a final capacity of 16,384 trade records.

Compared with the exact-capacity baseline, geometric growth achieved approximately **43.6× lower median execution time**.

Full preallocation achieved approximately **47.2× lower median execution time** than the baseline.

These measurements demonstrate that trade-history capacity management is a significant performance factor in the tested workload.

### 3.4 Engineering Implications

Geometric growth is a promising candidate for reducing repeated reallocations while avoiding the need to know the complete future trade count.

However, production adoption requires additional validation of:

- Exception-safety guarantees.
- Allocation failure handling.
- Capacity overflow boundaries.
- Regression behavior.
- Memory consumption under sustained workloads.

No production optimization was introduced during this profiling phase.

## 4. Preflight and Execution Profiling — P8-10

### 4.1 Objective

Measure the relative costs of:

1. Preflight match counting.
2. Trade-history reservation.
3. Matching execution.

The profiling build accumulated timing measurements for these stages.

### 4.2 Aggregate MARKET Workload

For 10,000 MARKET orders using geometric trade-history growth:

| Component | Total Time |
|---|---:|
| Preflight counting | 0.8473 ms |
| Trade-history reservation | 1.0832 ms |
| Matching execution | 3.2664 ms |
| Complete measured workload | 10.1063 ms |

The component measurements cover selected sections of the submission path.

The remaining elapsed time includes operations outside those timed sections and instrumentation overhead.

### 4.3 Order-Book Depth Experiment

Three workloads were evaluated:

| Scenario | Resting Orders | Executed Resting Orders |
|---|---:|---:|
| Shallow book | 100 | 1 |
| Deep book, single execution | 10,000 | 1 |
| Deep book, multiple executions | 10,000 | 100 |

Each scenario used a fresh engine with preloaded resting orders.

Thirty measurements were collected per scenario, with randomized scenario execution order.

### 4.4 Median Results

All times are in nanoseconds.

| Scenario | Preflight | Execution | Total Submission |
|---|---:|---:|---:|
| Shallow, 1 execution | 200 | 1,100 | 2,700 |
| Deep, 1 execution | 200 | 1,600 | 3,700 |
| Deep, 100 executions | 2,800 | 27,350 | 32,750 |

### 4.5 Findings

**Book depth versus execution count**

Increasing resting order count from 100 to 10,000 while executing one order did not increase the median measured preflight duration.

This is consistent with preflight counting stopping once sufficient executable quantity is identified.

**Multiple executions**

Increasing the execution count from one to 100 in the deep-book scenario increased median matching execution time from 1,600 ns to 27,350 ns.

This corresponds to approximately 17.1× greater measured execution time.

**Preflight overhead**

In the 100-execution scenario, median preflight time was 2,800 ns compared with 27,350 ns for matching execution.

Preflight counting was therefore not the dominant measured component in that workload.

## 5. Measurement Limitations

The following limitations apply:

- Measurements were collected on Windows using MSVC Release builds.
- CPU model, frequency behavior and system load were not recorded.
- The allocation comparison used complete workload timings, not per-order latency distributions.
- The depth experiment measured individual submissions against freshly constructed books.
- Internal instrumentation introduces timing overhead.
- Nanosecond-scale results are sensitive to clock resolution, cache state and operating-system scheduling.
- All resting orders in the depth experiment occupied one price level.
- Multiple price-level traversal was not independently evaluated.
- The allocation experiments were not interleaved or randomized across strategies.
- Process private-memory changes are not equivalent to allocation counts.
- CPU cache misses and hardware performance counters were not collected.
- These experiments do not establish production latency or concurrent matching performance.

## 6. Optimization Opportunities

The experiments identify several opportunities for future investigation.

**Trade-history storage**

Evaluate geometric capacity growth, bounded trade-history retention or external trade consumption.

**Preflight traversal**

Evaluate preflight cost under different numbers of price levels, match counts and order-book distributions.

**Order-book storage**

Investigate cache locality, node allocations and alternative data structures while preserving price-time priority.

**Latency benchmarking**

Collect warm-state per-order latency distributions, including median, p95, p99 and worst observed latency.

**Concurrency**

Measure queueing delay, contention and end-to-end processing latency under concurrent submission workloads.

These are future engineering investigations, not completed optimizations.

## 7. Verification

The Release regression suite completed successfully:

- OrderTests
- OrderBookTests
- MatchingEngineTests
- BoundedBlockingQueueTests
- MatchingCommandTests
- MatchingWorkerTests

Result: **6/6 tests passed**.

The profiling experiments additionally verified expected execution quantities and remaining resting order counts.

## 8. Supporting Artifacts

- `benchmarks/AllocationProfile.cpp`
- `docs/P8-09-allocation-profile.csv`
- `docs/P8-10-depth-profile.csv`
- `include/matching_engine/MatchingEngine.hpp`
- `src/MatchingEngine.cpp`

## 9. Conclusion

Phase 8 profiling identified repeated exact-capacity trade-history reservation as a substantial performance bottleneck in the tested MARKET workload.

Profiling-only geometric growth reduced capacity expansion events from 10,000 to 15 and achieved approximately 43.6× lower median workload execution time.

Preflight measurements indicated that execution count was a more significant factor than total resting order count for the tested single-price-level scenarios.

The findings provide evidence for future optimization work while preserving the existing production implementation and exception-safety design.
