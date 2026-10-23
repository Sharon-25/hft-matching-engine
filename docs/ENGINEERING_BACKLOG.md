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

## Phase 6 — Matching Correctness and Admission Hardening

### Phase 6.1 — Deterministic safety hardening (commit 0207d72, 2026-10-22)

- [x] P6-01: Strengthen pre-mutation validation for matching and resting-order index consistency. Matching validates the selected resting order, price-time eligibility, quantity bounds and index location before changing execution state.
- [x] P6-02: Harden OrderBook insertion rollback for injected failures after list insertion, index insertion and historical-ID insertion. Test checkpoint recovery preserves logical book state and index invariants.
- [x] P6-03: Harden LIMIT submission ID reservation lifetime. Commit the session-wide reservation immediately after successful book admission so subsequent exceptions do not make an admitted ID reusable.
- [x] P6-04: Expand targeted tests for match preflight, failure checkpoints, historical-ID behavior, quantity handling and book invariants. Targeted source-level regression coverage is present; current-head test verification is tracked separately under P6-10.
- [x] P6-05: Commit hardening changes as 0207d72.

### Phase 6.2A — Deterministic differential matching tests (commit df85fa5, 2026-10-23)

- [x] P6-06: Add a separate, deliberately simple reference matching model using a flat order vector and linear best-price search, independent of the production map/list/index implementation.
- [x] P6-07: Compare production and reference results after each submission: acceptance status, executed and remaining quantities, active orders and states, best bid/ask, trade history, historical IDs, invariant checks and FIFO order at price levels.
- [x] P6-08: Add seven fixed scenarios: FIFO at equal prices; multiple price levels; partial fills and continuation; BUY/SELL flows; MARKET orders with insufficient liquidity; duplicate IDs after execution; mixed LIMIT/MARKET sequence.
- [x] P6-09: Register all seven differential scenarios in MatchingEngineTests.cpp; commit the test-only change as df85fa5.
- [ ] P6-10: Archive reproducible Debug and Release CTest results for commit df85fa5 in the project verification log. Both configurations were previously reported passing, but the audit package does not contain independently archived test logs.

### Phase 6 remaining verification

- [ ] P6-11: Complete deterministic quantity-boundary and integer-overflow verification.
      Phase 6.2B added order-level and engine-level tests for maximum
      IDs, quantities and prices, quantity conservation, rejected
      executions and MARKET remainder accounting.
      Remaining: explicit arithmetic-overflow paths and additional
      multi-trade boundary scenarios.
      Additional multi-trade tests cover UINT64_MAX full fills,
      partial fills, FIFO execution and MARKET cancellation
      accounting. Debug and Release regression suites passed.
      Remaining: source-level audit of arithmetic overflow paths.
- [ ] P6-12: Add deterministic fault injection at actual allocation operations; verify exception and state guarantees beyond manual insertion checkpoints.
- [ ] P6-13: Audit all operations after the first irreversible trade for exceptions, including finalization. Do not claim a global strong exception guarantee until established.
- [ ] P6-14: Extend differential verification to seeded randomized operation sequences and cancellation operations once the appropriate engine API is designed.
- [ ] P6-15: Isolate MATCHING_ENGINE_TESTING and failure-injection hooks from production builds; it is currently a PUBLIC compile definition on matching_core.

## Later phases

- [ ] Invalid input, overflow and quantity-boundary tests (see P6-11).
- [ ] Add seeded randomized operation-sequence tests and reproduce failures from recorded seeds (see P6-14).
- [ ] Verify exception behavior of every operation performed after the first trade, including finalization (see P6-13).
- [ ] Inject failures at actual allocator operations and verify recovery, beyond manually inserted failure checkpoints.
- [ ] Isolate MATCHING_ENGINE_TESTING and test-only failure hooks from production builds.
- [ ] Review duplicated session-ID state in MatchingEngine and OrderBook.
- [ ] Define how trade history is bounded or consumed in long-running sessions.
- [ ] Add stronger boundary tests and randomized operation-sequence tests.
- [ ] Benchmark preflight trade counting and incoming-order staging.
- [ ] Review market SELL preflight crossing-bound asymmetry. Price{0} is correct under the current strictly positive limit-price model. Consider using std::numeric_limits<Price>::min() for symmetry and future price-model extensions.
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

- Failure-safe admission has been verified for tested insertion failure points; exhaustive allocation-failure coverage is not yet established.
- Test-only failure hooks are currently enabled in the main build (MATCHING_ENGINE_TESTING is PUBLIC on matching_core).
- Trade history has no enforced memory bound.
- Seven deterministic differential scenarios are implemented; seeded randomized book-invariant and differential testing remains incomplete.
- BUY and SELL price-time priority is covered by deterministic tests; broader randomized sequences remain pending.
- Debug and Release CTest runs for commit df85fa5 were previously reported passing, but the audit package does not contain independently archived test logs. Reproducible verification logs remain pending under P6-10.
- No comprehensive strong exception guarantee is claimed for operations after irreversible trade execution.
- ID uniqueness currently has both book-level and engine-level state.
- Concurrency and benchmarking are not implemented.
- Market SELL preflight uses Price{0}, which is correct for currently accepted positive limit prices but assumes that the price model will not admit zero or negative resting prices.
- Trade-vector capacity is reserved before incoming-order staging. If staging fails, capacity may remain increased despite no trade occurring. The failure guarantee preserves logical trading state, not the previous memory allocation state.
