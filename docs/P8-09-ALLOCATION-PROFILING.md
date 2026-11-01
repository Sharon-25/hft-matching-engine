# P8-09 — Allocation and Memory Profiling

## Objective

Identify allocation-related performance bottlenecks in the C++20 order matching engine and evaluate trade-history capacity management.

## Environment

- Platform: Windows 11
- Compiler: MSVC
- Build: Release
- Workload: 10,000 BUY MARKET orders
- Setup: 10,000 preloaded SELL LIMIT orders
- Measurement: `std::chrono::steady_clock`
- Repetitions: 5 per scenario
- Aggregation: Median elapsed time

## Results

| Strategy | Median (ms) | Min (ms) | Max (ms) | Capacity Growth Events |
|---|---:|---:|---:|---:|
| Exact-capacity baseline | 245.732 | 89.3003 | 264.066 | 10,000 |
| Full preallocation | 5.2084 | 5.0503 | 6.0086 | 0 |
| Geometric growth | 5.6377 | 5.3289 | 6.3953 | 15 |

## Findings

1. Exact-capacity reservation causes repeated vector reallocations as trade history grows.
2. Preallocation eliminates measured capacity growth during execution.
3. Geometric growth reduces capacity growth events from 10,000 to 15.
4. Geometric growth achieves approximately 43.6× lower median execution time than the baseline.
5. Geometric growth ends with capacity for 16,384 trades, compared with 10,000 stored trades.

## Limitations

- Results measure complete workload execution, not individual order latency.
- Runs were not randomized or interleaved across strategies.
- A substantial baseline timing variation was observed.
- Process private-memory deltas do not represent exact allocation counts.
- CPU cache misses and allocation call stacks were not measured.
- The experiment does not establish production performance under concurrent workloads.

## Engineering Decision

Geometric trade-history growth is a promising optimization candidate.

The optimization remains isolated behind `MATCHING_ENGINE_PROFILING`. Production behavior is unchanged.

Production adoption requires exception-safety verification, fault-injection regression tests and representative benchmark comparisons.

## Artifacts

- `benchmarks/AllocationProfile.cpp`
- `docs/P8-09-allocation-profile.csv`
- `include/matching_engine/MatchingEngine.hpp`
- `src/MatchingEngine.cpp`

## Status

P8-09 completed. Production optimization deferred pending validation.
