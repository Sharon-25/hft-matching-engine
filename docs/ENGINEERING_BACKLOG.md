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
- [x] P6-10: Archive reproducible Debug and Release CTest
      verification for the Phase 6 implementation.
      Build and CTest logs recorded on a descendant of df85fa5.
      Both configurations passed all three CTest executables.
      Historical df85fa5 was not independently re-executed.
      Evidence: docs/VERIFICATION_LOG.md and archived logs.

### Phase 6.3 — Final correctness and robustness verification

- [x] P6-11: Complete deterministic quantity-boundary and arithmetic-overflow verification.
      Tests cover maximum OrderId, Quantity and Price values,
      rejected executions, full and partial fills, multi-trade
      quantity conservation, FIFO execution and MARKET remainder
      accounting. Debug and Release regression suites passed.
      Source audit confirmed bounded quantity subtraction and
      accumulation, guarded trade-vector capacity arithmetic,
      direct price comparisons and container-derived order counts.
      No arithmetic-overflow defect identified in audited paths.
- [x] P6-12: Add deterministic fault injection at actual
      allocation operations.
      Implemented allocator/container failure tests covering
      admission, ID reservation, insertion rollback and
      trade-history allocation behavior.
      Debug and Release regression tests passed.
      Commit: f19d45d.
- [x] P6-13: Audit operations after the first irreversible
      trade, including exception behavior and finalization.
      Completed the planned source-level exception-safety
      audit.
      Do not interpret this as proof of a universal strong
      exception guarantee for every possible failure.
- [x] P6-14: Extend differential verification to seeded
      randomized operation sequences.
      Implemented reproducible SplitMix64-based tests using
      five seeds and 1,000 operations per seed.
      Covered LIMIT and MARKET submissions, duplicate IDs
      and matching-state comparisons.
      Debug and Release regression tests passed.
      Commit: 6c49ddd.
      Randomized cancellation coverage remains a separate
      follow-up because cancellation was not included in
      the original completed randomized suite.
- [x] P6-15: Isolate MATCHING_ENGINE_TESTING and
      failure-injection hooks from production builds.
      Separated matching_core and matching_core_testing.
      Verified Debug and Release builds.
      Commit: db058b7.

## Phase 7 — Concurrent Order Processing
### Phase 7.1 — Concurrency architecture and contracts

- [x] P7-01: Define the concurrency architecture.
      Multiple producer threads submit commands into a bounded
      FIFO queue.
      A single dedicated matching worker owns MatchingEngine
      and performs all order-book mutations.
      Producers do not mutate the order book directly.
- [x] P7-02: Establish deterministic processing semantics.
      Commands execute in FIFO queue-insertion order.
      Concurrent submission call order is not guaranteed to
      equal queue-insertion order.
      Price-time priority remains enforced within the
      matching engine.
- [x] P7-03: Define asynchronous result delivery.
      Producers receive std::future results backed by
      std::promise objects.
      Successful operations return their normal results.
      Processing exceptions are propagated to the
      corresponding futures.

### Phase 7.2 — Bounded blocking FIFO queue

- [x] P7-04: Implement BoundedBlockingQueue<T>.
      Uses std::mutex, std::condition_variable and std::deque.
      Supports bounded capacity, blocking push(), blocking
      pop() and close().
      Supports move-only command payloads.
- [x] P7-05: Implement queue backpressure.
      Producers wait when the queue reaches capacity.
      Consumers wait when the queue is empty.
      Condition variables coordinate producer and
      consumer progress.
- [x] P7-06: Implement queue closure semantics.
      Closing the queue prevents new insertions.
      Blocked producers and consumers are notified.
      Previously accepted items remain available for draining.
- [x] P7-07: Add bounded queue correctness tests.
      Verify FIFO behavior, capacity enforcement,
      blocking and wake-up behavior, closure semantics,
      move-only payload handling and related invariants.
- [x] P7-08: Complete bounded queue verification and commit.
      Debug and Release regression tests passed.
      Bounded queue implementation committed.

### Phase 7.3 — Dedicated matching worker

- [x] P7-09: Introduce MatchingCommand.
      Encapsulates an Order and
      std::promise<SubmissionResult>.
      Copy operations are disabled.
      Nothrow move construction is enforced.
- [x] P7-10: Implement MatchingWorker.
      Owns MatchingEngine, BoundedBlockingQueue and
      a dedicated std::jthread.
      MatchingEngine state is mutated exclusively
      by the matching worker.
- [x] P7-11: Implement asynchronous order submission.
      MatchingWorker::submit(Order) returns
      std::future<SubmissionResult>.
      LIMIT and MARKET orders are dispatched to their
      respective MatchingEngine methods.
- [x] P7-12: Implement worker exception propagation.
      Exceptions during command processing are captured
      using std::current_exception() and delivered through
      std::promise::set_exception().
      A failed command does not automatically terminate
      the worker's processing loop.
- [x] P7-13: Implement initial worker lifecycle management.
      MatchingWorker::shutdown() closes the queue and
      joins the worker thread.
      The destructor invokes shutdown().
      Already accepted commands are drained under
      the implemented shutdown policy.
- [x] P7-14: Add test-only worker failure injection.
      submit_with_failure_for_test() associates an
      insertion failure checkpoint with an individual
      command.
      Failure injection is reset for subsequent commands.
- [x] P7-15: Verify dedicated worker behavior.
      Tests cover LIMIT/MARKET submissions, duplicate IDs,
      FIFO processing, shutdown rejection, accepted-command
      draining, destructor behavior, exception propagation
      and recovery after injected failures.
      Debug and Release tests passed.
- [x] P7-16: Commit dedicated matching worker implementation.
      Commit: e90a6c0
      Message: Implement dedicated matching worker with
      asynchronous results.

### Phase 7.4 — Concurrent submission API verification

- [x] P7-17: Verify concurrent submissions from multiple producers.
      Four producer threads submit 250 non-crossing
      LIMIT orders each.
      Verify all accepted submissions and expected
      execution accounting.
- [x] P7-18: Verify concurrent submissions under backpressure.
      Four producers submit 250 orders each before
      retrieving their futures.
      Bounded queue capacity: 16.
      Verify successful completion without lost results.
- [x] P7-19: Verify duplicate OrderId handling under concurrency.
      Four producers concurrently submit the same OrderId.
      Verify exactly one accepted submission and three
      duplicate-ID rejections.
- [x] P7-20: Verify observable FIFO command processing.
      Submit a resting SELL order, a MARKET BUY and
      a subsequent LIMIT BUY.
      Verify that the MARKET order consumes the resting
      liquidity before the later LIMIT order executes.
- [x] P7-21: Complete concurrent submission regression verification.
      All six CTest suites passed in Debug and Release.
      Matching worker tests were also repeated successfully
      across multiple direct executions.
- [x] P7-22: Commit concurrent submission tests.
      Commit: 9f19183
      Message: Verify concurrent matching submissions
      and FIFO processing.

### Phase 7.5 — Cancellation through the matching worker

- [x] P7-23: Define cancellation semantics.
      Cancellation operates on active resting orders.
      A successful cancellation removes the remaining
      quantity from the active order book.
      Unknown, already cancelled and fully filled
      orders cannot be cancelled.
      Previously accepted OrderIds remain reserved.
- [x] P7-24: Expose cancellation through MatchingEngine.
      Add MatchingEngine::cancel_order(OrderId).
      Delegate to OrderBook::cancel_order(OrderId).
      Preserve existing book-level cancellation behavior.
- [x] P7-25: Extend the worker command representation.
      Introduce CancelCommand containing an OrderId
      and std::promise<bool>.
      Use std::variant<MatchingCommand, CancelCommand>
      as WorkerCommand.
      Enforce nothrow move construction.
- [x] P7-26: Add asynchronous cancellation API.
      MatchingWorker::cancel(OrderId) returns
      std::future<bool>.
      Cancellation commands enter the same bounded
      FIFO queue as order submissions.
- [x] P7-27: Implement worker-side cancellation dispatch.
      Use std::get_if to distinguish submission and
      cancellation commands.
      Execute both operations on the dedicated
      matching worker thread.
      Deliver results and exceptions through the
      corresponding command promises.
- [x] P7-28: Verify basic cancellation behavior.
      Tests cover:
      - Successful cancellation of an active order.
      - Repeated cancellation returning false.
      - Cancellation of an unknown OrderId.
      - Cancellation after a complete fill.
      - Cancellation before a crossing submission.
- [x] P7-29: Verify partially filled order cancellation.
      Resting SELL quantity 10 receives a partial fill
      of quantity 4.
      Cancellation removes the remaining quantity 6.
      A later crossing BUY executes zero quantity
      against the cancelled order.
- [x] P7-30: Verify historical OrderId preservation.
      Cancel an accepted order.
      Submit another order using the same OrderId.
      Verify SubmissionStatus::DUPLICATE_ID.
- [x] P7-31: Verify cancellation during shutdown.
      Submit an order and enqueue its cancellation.
      Invoke shutdown before retrieving the results.
      Verify both accepted commands complete.
      Verify cancellation requests made after shutdown
      are rejected.
- [x] P7-32: Complete cancellation regression verification.
      Debug build successful.
      Debug CTest: 6/6 passed.
      Debug total test time: 7.01 seconds.
      Release build successful.
      Release CTest: 6/6 passed.
      Release total test time: 2.33 seconds.
      git diff --check reported no whitespace errors.
      LF/CRLF conversion warnings were observed on Windows.
- [x] P7-33: Commit Phase 7.5 cancellation implementation.
      Stage MatchingCommand.hpp, MatchingEngine.hpp,
      MatchingWorker.hpp, MatchingEngine.cpp,
      MatchingWorker.cpp, MatchingWorkerTests.cpp
      and the updated engineering backlog.
      Commit message: Add FIFO-ordered cancellation through matching worker
      Commit hash: 2e4c906

### Phase 7.6 — Shutdown, backpressure and failure handling

- [x] P7-34: Review shutdown and backpressure contracts.
      Inspect the current queue and worker implementations.
      Identify guarantees already covered by queue-level
      tests and those requiring worker-level verification.
- [x] P7-35: Verify blocked producers during shutdown.
      Ensure producers waiting for queue capacity wake
      when the queue closes.
      Verify rejected commands cannot remain blocked
      indefinitely.
- [x] P7-36: Verify draining of all accepted commands.
      Accepted submissions and cancellations must
      complete before shutdown returns.
      Verify their futures become ready with either
      a result or an exception.
- [x] P7-37: Verify concurrent shutdown boundaries.
      Test submissions and cancellations racing with
      shutdown.
      Verify that accepted commands are processed
      and rejected commands report failure correctly.
- [x] P7-38: Verify repeated shutdown behavior.
      Confirm shutdown is safe when invoked repeatedly
      in supported usage scenarios.
      Review concurrent shutdown calls separately
      before claiming thread-safe shutdown invocation.
- [x] P7-39: Verify exception recovery.
      Test failure propagation through futures.
      Confirm subsequent commands can continue
      processing after recoverable failures.
- [x] P7-40: Add deterministic lifecycle and backpressure tests.
      Prefer explicit synchronization over arbitrary
      sleep-based timing assumptions.
      Avoid introducing flaky scheduling-dependent tests.
- [x] P7-41: Complete Debug/Release verification,
      document shutdown guarantees and commit Phase 7.6.
      Debug and Release each passed all six CTest suites.
      Shutdown guarantees and thread-safety lifetime contract
      documented in MatchingWorker.hpp.
      Commit hash: 9d4ee9b

### Phase 7.7 — Concurrent correctness and ordering

- [x] P7-42: Expand multi-producer correctness tests.
      Include mixed LIMIT, MARKET and CANCEL commands.
      Added four-producer mixed-command test with independent
      workloads and deterministic result expectations.
      Debug: 6/6 passed.
      Release: 6/6 passed.
      Repeated Debug worker verification: 20/20 passed.
- [x] P7-43: Verify matching invariants under concurrent input.
      Added worker-level tests for quantity conservation,
      same-price FIFO, different-price priority,
      active-order consistency and historical ID uniqueness.
      Reused existing partial-fill and engine invariant tests.
      Debug and Release: 6/6 tests passed.
- [x] P7-44: Verify ordering-sensitive cancellation races.
      Covered submit-before-cancel, cancel-before-submit
      and crossing-order interactions using explicitly
      established FIFO queue insertion order.
      Debug and Release: 6/6 tests passed.
- [x] P7-45: Add reproducible concurrent stress tests.
      Added five fixed seeds with four concurrent producers
      and 400 commands per seed. Recorded seed, producer,
      operation and queue configuration for diagnostics.
      Debug and Release: 6/6 tests passed.
      Repeated worker tests: 20/20 per configuration.
- [ ] P7-46: Complete Phase 7.7 regression verification
      and commit.

### Phase 7.8 — Thread-safety validation and documentation

- [ ] P7-47: Audit shared-state ownership and access.
      Ensure producers cannot access mutable engine
      state without appropriate synchronization.
- [ ] P7-48: Audit queue synchronization and lock scope.
      Review mutex ownership, condition-variable
      predicates, closure behavior and deadlock risks.
- [ ] P7-49: Review worker lifetime and shutdown safety.
      Verify object lifetime assumptions and supported
      concurrent API usage.
- [ ] P7-50: Evaluate available concurrency diagnostics.
      Use suitable race-detection or sanitizer tools
      where supported by the build environment.
      Document unavailable diagnostics honestly.
- [ ] P7-51: Document concurrency architecture,
      ordering guarantees, backpressure policy,
      shutdown behavior and known limitations.
- [ ] P7-52: Complete Phase 7 regression verification
      and commit.

## Phase 8 — Latency Benchmarking and Profiling

- [ ] P8-01: Define benchmark methodology and workload.
      Separate single-thread matching latency from
      asynchronous end-to-end submission latency.
- [ ] P8-02: Implement reproducible benchmark harness.
      Use Release builds and controlled workloads.
- [ ] P8-03: Benchmark LIMIT order submissions.
- [ ] P8-04: Benchmark MARKET order submissions.
- [ ] P8-05: Benchmark cancellation operations.
- [ ] P8-06: Benchmark concurrent producer throughput
      and bounded-queue backpressure.
- [ ] P8-07: Report latency distribution.
      Include p50, p95, p99 and maximum latency.
- [ ] P8-08: Measure throughput under different workloads
      and queue capacities.
- [ ] P8-09: Profile allocation behavior, memory usage
      and cache-related performance where tooling permits.
- [ ] P8-10: Measure preflight trade-counting and
      incoming-order staging costs.
- [ ] P8-11: Record hardware, compiler, optimization flags,
      benchmark inputs and measurement limitations.
- [ ] P8-12: Complete baseline performance report
      and commit Phase 8.

## Phase 9 — Optimization, Documentation and Release

- [ ] P9-01: Identify measured performance bottlenecks.
- [ ] P9-02: Apply targeted optimizations supported
      by benchmark evidence.
- [ ] P9-03: Re-run correctness and concurrency tests
      after each optimization.
- [ ] P9-04: Compare optimized performance against
      the recorded baseline.
- [ ] P9-05: Review duplicated OrderId ownership between
      MatchingEngine and OrderBook.
- [ ] P9-06: Define trade-history consumption or
      bounded-storage policy.
- [ ] P9-07: Review session-wide identity ownership
      across multiple order books.
- [ ] P9-08: Review MARKET SELL preflight crossing-bound
      assumptions and future price-model extensibility.
- [ ] P9-09: Prepare reproducible production Release builds.
- [ ] P9-10: Document application architecture,
      build instructions, usage, testing and benchmarks.
- [ ] P9-11: Prepare final correctness, performance
      and known-limitations report.
- [ ] P9-12: Complete final regression verification
      and release documentation.

## Later-phase and cross-phase verification

- [x] Complete actual allocator-failure injection
      and recovery verification (P6-12).
      Commit: f19d45d.
      Exhaustive failure coverage is not claimed.
- [x] Complete post-trade exception-safety audit (P6-13).
      The planned audit is complete.
      A universal strong exception guarantee is not claimed.
- [x] Add seeded randomized differential tests (P6-14).
      Five seeds with 1,000 operations per seed.
      Commit: 6c49ddd.
- [x] Isolate test-only failure-injection hooks from
      production builds (P6-15).
      Commit: db058b7.
- [ ] Extend the reference matching model and seeded
      randomized differential tests to include cancellation.
      Track separately from P7-45 concurrent stress testing.
- [ ] Establish trade-history memory management policy
      for long-running sessions (P9-06).
- [ ] Benchmark preflight counting and incoming-order
      staging overhead (P8-10).
- [ ] Review duplicated engine-level and book-level
      historical OrderId state (P9-05).
- [ ] Validate all supported shutdown and concurrent
      API interactions (P7-34 through P7-52).
- [ ] Complete reproducible latency and throughput
      benchmarking before making performance claims.

## Architecture decisions

- C++20 is the implementation language.
- Prices use integer ticks.
- Orders follow price-time priority.
- Orders at the same price level follow FIFO.
- Trades execute at resting-order prices.
- Accepted OrderIds remain unique within a session.
- OrderBook owns active resting orders.
- MatchingEngine controls order processing and matching.
- External producers do not directly mutate resting orders.
- Concurrent producers submit through a bounded FIFO queue.
- A single dedicated matching worker owns MatchingEngine.
- Matching operations are serialized on the worker thread.
- Command processing follows actual queue-insertion order.
- Submission and cancellation use the same command queue.
- Asynchronous results use std::promise and std::future.
- Command processing exceptions are propagated through futures.
- Queue backpressure blocks producers when capacity is exhausted.
- Queue closure rejects new commands and allows accepted
  commands to drain.
- Cancelled OrderIds remain reserved.
- Correctness and reproducibility precede optimization.
- Performance claims require measured benchmark evidence.

## Known limitations

- Actual allocator-failure injection and recovery tests
  were implemented under P6-12 (commit f19d45d).
  Exhaustive coverage of every possible allocation
  failure and exception path is not claimed.
- No comprehensive strong exception guarantee is claimed
  for all operations after irreversible trade execution.
- Seeded randomized differential verification was
  completed under P6-14 (commit 6c49ddd).
  Five seeds with 1,000 operations per seed were tested.
  Randomized cancellation coverage remains pending.
- Trade history currently has no enforced memory bound.
- Historical OrderId uniqueness is represented at both
  MatchingEngine and OrderBook levels.
  The duplicated ownership remains under review.
- The matching engine uses a single worker thread.
  Concurrent producers are supported, but matching
  operations themselves are serialized.
  This is a deliberate correctness and ordering decision.
- FIFO processing is defined by queue insertion order,
  not by the wall-clock order in which concurrent
  producers begin their API calls.
- Bounded queue backpressure can block producers.
  Comprehensive shutdown-race and blocked-producer
  verification remains pending under Phase 7.6.
- Concurrent correctness stress testing with mixed
  submission and cancellation workloads remains pending.
- A comprehensive thread-safety and lifecycle audit
  remains pending under Phase 7.8.
- No production-grade latency or throughput benchmark
  results have been established yet.
- No performance superiority or HFT production-readiness
  claim is made without benchmark and validation evidence.
- MARKET SELL preflight currently uses Price{0}.
  This is consistent with the existing strictly positive
  resting limit-price model, but assumes the model will
  not admit zero or negative resting prices.
- Trade-vector capacity is reserved before incoming-order
  staging. If staging fails, capacity may remain increased
  despite no trade occurring.
  The current guarantee preserves logical trading state,
  not necessarily the previous memory allocation state.
- The latest Phase 7.5 verification passed all six CTest
  suites in both Debug and Release.
  These results establish tested functional behavior,
  not exhaustive concurrency or exception safety.