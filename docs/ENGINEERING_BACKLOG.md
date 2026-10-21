# Low-Latency Matching Engine — Engineering Backlog

## Completion rules

- No critical correctness issue may remain unresolved.
- Every implemented feature must have automated tests.
- Release builds must execute meaningful tests.
- Performance claims must be supported by reproducible benchmarks.
- Known limitations must be documented.

## Phase 4 — Limit Order Matching

- [x] P4-01: Guarantee failure-safe incoming order admission.
      Verified for tested insertion failure checkpoints.
      The guarantee covers logical trading state; allocated capacity
      may change. Actual allocator-failure testing remains pending.
- [x] P4-02: Prepare all required storage before irreversible executions.
      Implemented for incoming book storage and trade-vector capacity.
- [x] P4-03: Preserve accepted IDs after any completed execution.
      Verified.
- [x] P4-04: Safely rest partially filled incoming orders.
      Verified.
- [x] P4-05: Generate trade records with both order IDs,
      execution price and executed quantity.
      Verified.
- [ ] P4-06: Verify BUY and SELL price-time priority.
      Final verification tests pending.
- [x] P4-07: Test crossing, non-crossing, partial and full fills.
      Verified.
- [ ] P4-08: Verify book invariants after each operation.
      Comprehensive checks pending, including crossed-book detection
      and consistency between the active index and resting orders.
- [ ] P4-09: Complete Phase 4 regression tests and Git commit.
      Pending.

## Later phases

- [ ] Market orders and unfilled market-order behavior.
- [ ] Invalid input, overflow and quantity-boundary tests.
- [ ] Inject failures at actual allocator operations and verify recovery,
      beyond manually inserted failure checkpoints.
- [ ] Ensure Release builds execute meaningful test checks
      without relying on assert().
- [ ] Isolate MATCHING_ENGINE_TESTING and test-only failure hooks
      from production builds.
- [ ] Review duplicated session-ID state in MatchingEngine and OrderBook.
- [ ] Define how trade history is bounded or consumed in long-running sessions.
- [ ] Add stronger boundary tests and randomized operation-sequence tests.
- [ ] Benchmark preflight trade counting and incoming-order staging.
- [ ] Verify exception behavior of every operation performed after
      the first trade, including finalization.
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

- Failure-safe admission has been verified for tested insertion failure
  points; exhaustive allocation-failure coverage is not yet established.
- Test-only failure hooks are currently enabled in the main build.
- Current assert-based tests are Debug-dependent; Release-safe test
  checks are still required.
- Trade history has no enforced memory bound.
- Comprehensive book-invariant testing remains incomplete.
- Final BUY and SELL price-time-priority verification is pending.
- ID uniqueness currently has both book-level and engine-level state.
- Concurrency, market orders and benchmarking are not implemented.
- Trade-vector capacity is reserved before incoming-order staging.
  If staging fails, capacity may remain increased despite no trade
  occurring. The failure guarantee preserves logical trading state,
  not the previous memory allocation state.
