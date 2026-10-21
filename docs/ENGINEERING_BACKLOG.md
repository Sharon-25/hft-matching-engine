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
- [x] P4-06: Verify BUY and SELL price-time priority.
      Verified through best-price selection, FIFO matching,
      limit-price enforcement and trade-order regression tests.
- [x] P4-07: Test crossing, non-crossing, partial and full fills.
      Verified.
- [x] P4-08: Verify book invariants after each operation.
      Verified after each accepted submission in the
      end-to-end sequence test, including active-index
      consistency, order counts, price-level correctness,
      active-order validity and crossed-book detection.
      Broader randomized and fault-injection testing
      remains tracked separately.
- [x] P4-09: Complete Phase 4 regression tests and Git commit.
      Debug regression suite passed with all three CTest
      executables successful. Phase 4 changes committed.
      Release-safe tests and production build hardening
      remain tracked separately.

## Phase 5 — Market Orders and Execution Hardening

- [x] P5-01: Add OrderType enum (LIMIT, MARKET) and five-argument Order constructor.
      Existing four-argument constructor delegates to the new one with LIMIT type.
      Accessor Order::type() added.
- [x] P5-02: Implement MatchingEngine::submit_market_order().
      Validates type, status, side; rejects duplicate IDs; uses OrderIdReservation RAII.
      Pre-flight counts matchable orders, reserves trade-vector capacity before any mutation.
- [x] P5-03: Add MarketOrderAccounting helper struct and accessor.
- [x] P5-04: Add market-order preflight guard:
      expected_trades > book_.order_count() raises std::logic_error before any mutation.
- [x] P5-05: Add post-execution quantity-accounting invariant in submit_market_order():
      executed_quantity <= original_quantity and remaining_quantity == original – executed.
- [x] P5-06: Harden OrderBook::execute_best_order() (Phase 5.5).
      Pre-mutation order_index_ lookup; OrderLocation consistency validation
      (side, price, iterator == begin()); iterator-based index erasure (erase(index_it));
      no post-erasure dereferences of order, list, or map iterators;
      used_order_ids_ left unchanged so filled IDs cannot be reused.
- [x] P5-07: Add targeted execute_best_order() regression tests
      (full fill, partial fill, FIFO ordering, invalid quantity rejection,
       subsequent matching after full fill, invariant checks after each step).
- [x] P5-08: Add static_assert(std::is_nothrow_move_constructible_v<Trade>) and
      static_assert(std::is_nothrow_destructible_v<Trade>) in OrderBookTests and
      MatchingEngineTests.
- [x] P5-09: Replace assert() with CHECK() macro in all three test files
      (OrderTests.cpp, OrderBookTests.cpp, MatchingEngineTests.cpp).
      CHECK() evaluates in both Debug and Release configurations.
      All static_assert() calls left intact.
- [x] P5-10: Complete Phase 5 regression verification.
      All three CTest executables passed in the existing build
      and in an independently configured Release build.
      Runtime CHECK() assertions remain active under NDEBUG.
      Broader randomized, allocator-failure and concurrency
      testing remain pending.
- [x] P5-11: Complete Phase 5 Git commit.
      Verify staged changes, exclude generated build artifacts,
      and record the final commit.

## Later phases

- [ ] Invalid input, overflow and quantity-boundary tests.
- [ ] Inject failures at actual allocator operations and verify recovery,
      beyond manually inserted failure checkpoints.
- [ ] Isolate MATCHING_ENGINE_TESTING and test-only failure hooks
      from production builds.
- [ ] Review duplicated session-ID state in MatchingEngine and OrderBook.
- [ ] Define how trade history is bounded or consumed in long-running sessions.
- [ ] Add stronger boundary tests and randomized operation-sequence tests.
- [ ] Benchmark preflight trade counting and incoming-order staging.
- [ ] Verify exception behavior of every operation performed after
      the first trade, including finalization.
- [ ] Review market SELL preflight crossing-bound asymmetry.
      Price{0} is correct under the current strictly positive
      limit-price model. Consider using
      std::numeric_limits<Price>::min() for symmetry and
      future price-model extensions.
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
- Test-only failure hooks are currently enabled in the main build
  (MATCHING_ENGINE_TESTING is PUBLIC on matching_core).
- Trade history has no enforced memory bound.
- Comprehensive randomized book-invariant testing remains incomplete.
- Final BUY and SELL price-time-priority verification under randomized
  sequences is pending.
- ID uniqueness currently has both book-level and engine-level state.
- Concurrency and benchmarking are not implemented.
- Market SELL preflight uses Price{0}, which is correct for
  currently accepted positive limit prices but assumes that
  the price model will not admit zero or negative resting prices.
- Trade-vector capacity is reserved before incoming-order staging.
  If staging fails, capacity may remain increased despite no trade
  occurring. The failure guarantee preserves logical trading state,
  not the previous memory allocation state.
