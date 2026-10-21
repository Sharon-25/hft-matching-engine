# Low-Latency Matching Engine — Engineering Backlog

## Completion rules

- No critical correctness issue may remain unresolved.
- Every implemented feature must have automated tests.
- Release builds must execute meaningful tests.
- Performance claims must be supported by reproducible benchmarks.
- Known limitations must be documented.

## Phase 4 — Limit Order Matching

- [ ] P4-01: Guarantee failure-safe incoming order admission.
- [ ] P4-02: Prepare all required storage before irreversible executions.
- [ ] P4-03: Preserve accepted IDs after any completed execution.
- [ ] P4-04: Safely rest partially filled incoming orders.
- [ ] P4-05: Generate trade records with both order IDs,
      execution price and executed quantity.
- [ ] P4-06: Verify BUY and SELL price-time priority.
- [ ] P4-07: Test crossing, non-crossing, partial and full fills.
- [ ] P4-08: Verify book invariants after each operation.
- [ ] P4-09: Complete Phase 4 regression tests and Git commit.

## Later phases

- [ ] Market orders and unfilled market-order behavior.
- [ ] Invalid input, overflow and quantity-boundary tests.
- [ ] Allocation-failure injection and recovery tests.
- [ ] Release-safe test assertions.
- [ ] Session-wide identity ownership across multiple books.
- [ ] Concurrent submission architecture and thread-safety tests.
- [ ] Deterministic order processing and execution sequencing.
- [ ] Latency and throughput benchmark harness.
- [ ] p50, p95, p99 and maximum latency reporting.
- [ ] Allocation and cache profiling.
- [ ] Measured performance optimizations.
- [ ] Reproducible Release builds.
- [ ] Application usage and deployment documentation.
- [ ] Final correctness, performance and limitations report.

## Architecture decisions

- C++20.
- Integer price ticks.
- Price-time priority.
- FIFO within each price level.
- Trades execute at resting-order prices.
- Accepted order IDs are unique within a session.
- OrderBook owns resting orders.
- MatchingEngine controls incoming order processing.
- No direct external mutation of resting orders.
- Correctness precedes optimization.

## Known limitations

- Current matching submission is not transactionally safe.
- Trade records are not yet implemented.
- ID uniqueness currently has both book-level and engine-level state.
- Current assert-based tests are Debug-dependent.
- Concurrency, market orders and benchmarking are not implemented.
