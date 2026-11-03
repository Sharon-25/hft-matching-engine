# Low-Latency Matching Engine — Engineering Backlog

## Completion rules

- No critical correctness issue may remain unresolved.
- Every implemented feature must have automated tests.
- Release builds must execute meaningful tests.
- Performance claims must be supported by reproducible benchmarks.
- Known limitations must be documented.
- Portfolio completion does not imply production readiness.
- Deferred features must be documented and must not
  be represented as implemented functionality.

## Phase 4 — Limit Order Matching

- [x] P4-01: Guarantee failure-safe incoming order admission.
      Verified for tested insertion failure checkpoints.
      The guarantee covers logical trading state; allocated
      capacity may change.

      Additional allocator/container failure-injection
      verification was completed under P6-12
      (commit f19d45d).

      Exhaustive allocation-failure coverage is not claimed.
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
- [x] P7-46: Complete Phase 7.7 regression verification
      and commit.
      Debug and Release regression suites passed (6/6 each).
      MatchingWorker tests passed 20 repeated runs per build.
      Committed and pushed Phase 7.7 changes to GitHub.
      Commit: f9111b0.
      Verified clean working tree and synchronized master.

### Phase 7.8 — Thread-safety validation and documentation

- [x] P7-47: Audit shared-state ownership and access.
      Verified that MatchingEngine is privately owned by
      MatchingWorker and accessed only by its worker thread.
      Producers communicate through the bounded command queue.
      Results are returned through futures and promises.
      Object destruction requires external lifetime coordination.
      No shared-engine ownership violation identified.
- [x] P7-48: Audit queue synchronization and lock scope.
      Verified mutex protection of shared queue state,
      predicate-based condition-variable waits, FIFO
      ordering, bounded-capacity backpressure and closure.
      Confirmed blocked producers wake on closure and
      accepted commands drain before worker termination.
      No synchronization defect identified in the audit.
- [x] P7-49: Review worker lifetime and shutdown safety.
      Verified accepted-command draining, shutdown rejection,
      blocked-producer wakeup, repeated shutdown and
      concurrent shutdown serialization.
      Destructor drains accepted commands.
      Caller must coordinate producer lifetime before
      destruction; concurrent destruction is unsupported.
      No additional production-code changes required.
- [x] P7-50: Evaluate available concurrency diagnostics.
      Confirmed Windows/MSVC 14.43.34808 with CMake 3.30.5.
      Debug and Release configurations use MSVC.
      Clang and GCC were not found on PATH.
      ThreadSanitizer was not available in the current
      toolchain and was not executed.
      Existing concurrency tests and manual audits passed.
      AddressSanitizer is a possible memory-safety follow-up;
      Linux/WSL2 ThreadSanitizer remains future work.
      No sanitizer-based race-free claim is made.
- [x] P7-51: Document concurrency architecture,
      ordering guarantees, backpressure policy,
      shutdown behavior and known limitations.
      Added docs/CONCURRENCY_ARCHITECTURE.md.
      Updated README status and documentation link.
      Recorded sanitizer and lifetime limitations.
- [x] P7-52: Complete Phase 7 regression verification
      and commit.
      Debug and Release builds succeeded.
      Debug regression: 6/6 tests passed.
      Release regression: 6/6 tests passed.
      MatchingWorker repeated tests: 20/20 Debug
      and 20/20 Release.
      Phase 7.8 documentation and audits completed.
      Final regression and documentation verification completed.

## Phase 8 — Latency Benchmarking and Profiling

- [x] P8-01: Define benchmark methodology and workload.
      Separate single-thread matching latency from
      asynchronous end-to-end submission latency.
      Defined timing boundaries, warm-ups, fixed workloads,
      percentile reporting and measurement limitations.
- [x] P8-02: Implement reproducible benchmark harness.
      Implemented a standalone Release benchmark executable
      linked against production matching_core.
      Added timer baseline, controlled workloads,
      warm-ups and repeated concurrent measurements.
- [x] P8-03: Benchmark LIMIT order submissions.
      B1: Non-crossing LIMIT insertion.
      B2: Crossing LIMIT execution.
      Verified results outside timed regions.
- [x] P8-04: Benchmark MARKET order submissions.
      B3: MARKET execution against resting liquidity.
      Verified execution quantities and trade details.
- [x] P8-05: Benchmark cancellation operations.
      B4: Cancellation of a resting LIMIT order.
      Verified successful removal and no generated trades.
- [x] P8-06: Benchmark concurrent producer throughput
      and bounded-queue backpressure.
      Verified 1, 2 and 4 producers with queue capacities
      8, 64 and 1024.
      Completed 2 warm-up and 5 measured runs per scenario.
      Added isolated compile-time queue instrumentation.
      Measured blocked-submission frequency, cumulative
      queue-wait time and wait-duration percentiles.
      Preserved the uninstrumented production benchmark.
- [x] P8-07: Report latency distribution.
      Implemented minimum, p50, p95, p99 and maximum
      for direct-engine workloads.
      Added submission API duration and
      submission-to-observation latency reporting.
      Observation latency is not exact completion latency.
- [x] P8-08: Measure throughput under different workloads
      and queue capacities.
      Completed B5 non-crossing LIMIT, B6 MARKET execution,
      B7 cancellation and B8 deterministic mixed workload.
      Verified 1/2/4 producers with queue capacities 8/64/1024.
      Release and instrumented benchmark runs completed.
- [x] P8-09: Profile memory allocation and cache behavior.
      Implemented Windows-specific LIMIT, MARKET and CANCEL
      profiling workloads.
      Identified repeated trade-history vector reallocations.
      Compared exact-capacity, preallocated and geometric growth.
      Completed 5 measured runs per strategy.
      Geometric growth reduced capacity expansion events from
      10,000 to 15 and achieved a 43.6x median speedup.
      Isolated experimental changes from production.
- [x] P8-10: Measure preflight and staging costs.
      Instrumented preflight counting, trade-history reservation
      and matching execution in the profiling build.
      Compared shallow and deep order books with 1 and 100 fills.
      Completed 30 measured runs per scenario.
      Recorded median preflight, execution and submission times.
      Found execution count had greater observed latency impact
      than resting order count in single-price-level workloads.
- [x] P8-11: Document benchmark methodology and findings.
      Consolidated allocation, preflight and execution results.
      Recorded experimental methodology, measured performance,
      profiling overhead and measurement limitations.
      Created PHASE8_PERFORMANCE_REPORT.md.
      Preserved P8-09 and P8-10 measurement datasets.
      Documented future optimization opportunities.
- [x] P8-12: Final verification and Git commit.
      Successfully built all targets in Release configuration.
      Passed all 6 regression tests with no failures.
      Verified benchmark and profiling build configurations.
      Reviewed production and instrumented build separation.
      Validated staged changes and resolved whitespace issues.
      Converted benchmark result files to UTF-8 for GitHub.
      Committed and pushed Phase 8 changes to GitHub.
      Commit: 7bfa820.
      Verified clean working tree and synchronized master.

## Phase 9 — Optimization, Documentation and Release

- [x] P9-01: Identify measured performance bottlenecks.
      Reviewed Phase 8 allocation and latency measurements.
      Identified repeated exact-capacity trade-history
      reservation as the primary measured bottleneck.
      Baseline median: 245.732 ms for 10,000 MARKET orders.
      Geometric-growth median: 5.6377 ms.
      Identified preflight counting and matching execution
      as secondary profiling areas.
      Prioritized exception-safe trade capacity optimization.
      Deferred bounded trade-history policy to P9-06.
      No production changes made during this review.
- [x] P9-02: Apply targeted optimizations supported
      by benchmark evidence.

      Implemented geometric trade-history vector
      capacity growth in both LIMIT and MARKET
      matching paths.

      Preserved pre-mutation capacity reservation
      and existing logical exception-safety guarantees.

      Added overflow-aware capacity growth and
      regression tests for allocation behavior.

      Retained a profiling-only exact-growth mode
      for reproducible baseline comparison.

      Implementation committed as part of 7070012.
- [x] P9-03: Re-run correctness and concurrency tests
      after optimization.

      Verified geometric growth and capacity reuse
      through targeted regression tests.

      Final Debug CTest: 6/6 passed.
      Final Release CTest: 6/6 passed.

      Confirmed Release tests remain meaningful
      without relying on disabled assert() checks.
- [x] P9-04: Compare optimized performance against
      the recorded baseline.

      Compared exact-capacity growth, geometric
      growth and preallocated trade-history storage
      using the documented 10,000-trade workload.

      Final measured median elapsed times:
      - Exact-capacity baseline: 259.361 ms.
      - Geometric growth: 10.6309 ms.
      - Preallocated storage: 10.9291 ms.

      Geometric growth reduced observed capacity
      expansion events from 10,000 to 15.

      The final comparison showed approximately
      24.4x lower median elapsed time than the
      exact-capacity baseline.

      Earlier Phase 8 profiling reported a separate
      43.6x comparison under its recorded conditions.
      These results represent different measurement
      rounds and should not be combined.

      Evidence: docs/P9_04_BENCHMARK_REPORT.md.

      Results are workload-specific and do not
      establish production latency guarantees.

- [x] P9-05: Review duplicated OrderId ownership
      between MatchingEngine and OrderBook.

      Decision:
      Retain both historical OrderId sets.

      MatchingEngine tracks accepted incoming IDs,
      including immediately executed orders that
      never rest in the book.

      OrderBook tracks IDs inserted into the book
      and independently protects its insertion API
      against historical ID reuse.

      The sets overlap for resting orders but serve
      different ownership boundaries.

      Trade-off:
      Additional hash-set memory and lookup overhead
      are retained to preserve existing component
      contracts and exception-safety behavior.

      Future consolidation remains optional.
      No production code changes were required.
- [x] P9-06: Define trade-history consumption or
      bounded-storage policy.
      P9-06 Decision — Trade-History Policy

      Current implementation:
      - Trade records accumulate in std::vector<Trade>.
      - Geometric capacity growth reduces allocation overhead.
      - Historical storage remains unbounded.

      Chosen direction:
      - Retain the existing trade-history API for this release.
      - Define future batch consumption with explicit acknowledgment.
      - Do not discard unconsumed trades.
      - Require a bounded pending-trade policy and backpressure
        before claiming production-safe memory behavior.

      Known limitation:
      - A long-running engine can experience unbounded memory growth
        when trades are not consumed.

      Implementation deferred:
      - Consumer acknowledgment protocol.
      - Maximum pending-trade capacity.
      - Backpressure or overflow handling.
      - Consumption and recovery tests.
- [x] P9-07: Review session-wide identity ownership across multiple order books.
      P9-07 Decision — OrderId Uniqueness Scope

      Current contract:
      - OrderId uniqueness is enforced within one MatchingEngine instance.
      - Accepted IDs cannot be reused during that instance's lifetime.
      - Filled and cancelled orders retain their historical IDs.
      - Failed admission must not permanently consume an ID.

      Current architecture:
      - One MatchingEngine owns one OrderBook.
      - No multi-instrument session manager is implemented.

      Future extension:
      - Define whether IDs are session-global or instrument-scoped.
      - Introduce shared identity ownership only if multiple books
        are managed within one session.
      - Preserve atomic reservation, rollback and commitment semantics.
      - Test cross-book duplicates and concurrent admission.

      Decision:
      Retain the current per-engine identity model.
      No production code changes are required for P9-07.
- [x] P9-08: Review MARKET SELL preflight crossing-bound assumptions and future price-model extensibility.
      P9-08 Decision — MARKET Preflight Price Boundaries

      Current behavior:
      - MARKET BUY uses numeric_limits<Price>::max().
      - MARKET SELL uses Price{0}.
      - LIMIT order prices must be strictly positive.
      - MARKET orders have price_ticks == 0.

      Verification:
      - MARKET SELL preflight includes all valid resting BUY levels.
      - MARKET BUY preflight includes all valid resting SELL levels.
      - Preflight bounds are not execution prices.

      Decision:
      Retain the existing implementation.

      Known assumption:
      All valid resting LIMIT prices are strictly positive.

      Future extension:
      If zero or negative LIMIT prices become supported, revisit
      the preflight API and remove its dependency on synthetic
      MARKET price boundaries.
- [x] P9-09: Prepare reproducible production Release builds.
P9-09 — Production Release Build Verification

Environment:
- Windows 11
- MSVC / Visual Studio generator
- CMake
- C++20

Configuration:
- BUILD_TESTING=OFF
- BUILD_BENCHMARKS=OFF
- Release configuration

Results:
- matching_core.lib built successfully.
- matching_engine.exe built successfully.
- Executable launched successfully.
- Bid/ask demonstration output verified.
- Test and benchmark options confirmed disabled.

Production build command:
cmake -S . -B build-production -DBUILD_TESTING=OFF -DBUILD_BENCHMARKS=OFF
cmake --build build-production --config Release
- [x] P9-10: Document application architecture,
      build instructions, usage, testing and benchmarks.

      Updated README.md with the current architecture,
      matching behavior, concurrency design, build configurations, test coverage, performance measurements and limitations.

      Verified UTF-8 encoding, Unicode symbols, Markdown
      formatting and local documentation links.
- [x] P9-11: Prepare final correctness, performance
      and known-limitations report.

      Created docs/FINAL_ENGINEERING_REPORT.md.

      Documented architecture, correctness verification,
      concurrency behavior, engineering tradeoffs,
      production build configuration and known limitations.

      Recorded the Phase 9 benchmark comparison:
      259.361 ms baseline versus 10.6309 ms optimized
      median elapsed time, approximately 24.4x faster
      for the measured 10,000-trade workload.

      Verified UTF-8 encoding and corrected trailing whitespace.
- [x] P9-12: Complete final regression verification
      and release documentation.

      Debug CTest: 6/6 passed.
      Release CTest: 6/6 passed.

      Production Release build verified with
      BUILD_TESTING=OFF and BUILD_BENCHMARKS=OFF.

      Production executable launched successfully.
      No test or benchmark targets generated.

      Final documentation and code audits passed.
      No staged whitespace errors identified.

      Phase 9 implementation, benchmark report
      and final engineering report committed
      and pushed to origin/master.

      Implementation commit: 7070012.

      Final backlog corrections are tracked
      in a separate documentation commit.


## Phase 10 — Final Hardening and Portfolio Closure

### Phase 10.1 — Backlog reconciliation

- [x] P10-01: Reconcile completed milestones and documentation.
      Correct outdated backlog entries and verify
      Phase 9 optimization and benchmark evidence.

      Ensure completed reviews are distinguished
      from deferred implementation work.

      Verify README, engineering backlog and
      final reports describe the same release scope.

### Phase 10.2 — Randomized cancellation verification

- [x] P10-02: Extend the reference matching model
      to support order cancellation.

      Implement cancellation independently of the
      production OrderBook data structures.

      Preserve historical OrderId uniqueness.

      Support cancellation of active resting orders,
      unknown IDs, filled orders and previously
      cancelled orders.
- [x] P10-03: Extend seeded randomized differential
      tests to include cancellation operations.

      Generate reproducible mixed LIMIT, MARKET
      and CANCEL operation sequences.

      Compare production and reference cancellation
      results after each operation.

      Verify active orders, best bid/ask, trade history,
      historical IDs and matching invariants.

      Record failing seeds and operation indices
      for reproducible debugging.
- [x] P10-04: Complete cancellation regression verification.

      Run deterministic and randomized cancellation
      tests in Debug and Release configurations.

      Confirm existing matching and concurrent
      worker regression suites remain successful.

### Phase 10.3 — Sanitizer verification

- [x] P10-05: Evaluate available sanitizer toolchains.

      Verified Windows 11, Visual Studio 2022,
      MSVC 19.43 and CMake 3.30.5.

      Confirmed MSVC x64 AddressSanitizer support
      using /fsanitize=address.

      Confirmed ThreadSanitizer is unavailable
      with the current native Windows/MSVC toolchain.

      Preserved existing Debug, Release and
      production build configurations.

      Used a separate build-asan directory.

- [x] P10-06: Run supported sanitizer diagnostics.

      Configured the x64 AddressSanitizer build:

      cmake -S . -B build-asan -G "Visual Studio 17 2022" -A x64 `
        -DBUILD_TESTING=ON `
        -DBUILD_BENCHMARKS=OFF `
        -DCMAKE_CXX_FLAGS="/fsanitize=address /Zi /EHsc"

      Built using:

      cmake --build build-asan --config Debug

      Added the MSVC Hostx64/x64 runtime directory
      to PATH to resolve missing ASan runtime DLLs.

      Executed:

      ctest --test-dir build-asan -C Debug --output-on-failure

      Result: 6/6 CTest suites passed (16.16 seconds).

      Verified order, order-book, matching-engine,
      bounded-queue, command and worker test suites.

      Included deterministic cancellation and
      seeded randomized differential coverage
      within MatchingEngineTests.

      No AddressSanitizer violations were reported
      during the executed test suites.

      ThreadSanitizer was not executed because
      it is unsupported by the current toolchain.

      Limitations:
      - ASan covers only exercised execution paths.
      - ASan does not establish absence of data races.
      - Concurrent worker code is not claimed race-free.
      - TSan verification remains unavailable
        in the tested Windows/MSVC environment.

### Phase 10.4 — Final release verification

- [ ] P10-07: Complete final Debug and Release verification.

      Build all supported test targets.

      Run all six CTest suites in both configurations.

      Verify tests remain meaningful in Release.
- [ ] P10-08: Verify production Release build.

      Configure with BUILD_TESTING=OFF
      and BUILD_BENCHMARKS=OFF.

      Build and launch the production executable.

      Confirm test-only and profiling targets
      are excluded from the production build.
- [ ] P10-09: Finalize portfolio documentation.

      Update README and engineering reports
      only where final verification changes
      the documented evidence.

      Record any sanitizer results and
      randomized cancellation test coverage.

      Preserve accurate benchmark methodology,
      limitations and reproducibility instructions.
- [ ] P10-10: Complete final Git commit and push.

      Verify source changes and documentation.

      Run git diff --check.

      Commit and push completed Phase 10 changes.

      Confirm clean working tree and synchronized
      local master and origin/master.

      Record the final commit hash.

      Declare the defined portfolio scope complete.

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
      Tracked under P10-02 through P10-04.
- [x] Review trade-history memory management policy (P9-06).
      Retained the existing trade-history API for this release.
      Defined future batch consumption with acknowledgment.
      Current trade-history storage remains unbounded.
- [ ] Implement bounded trade-history consumption
      with acknowledgment, retention limits and backpressure.
      Deferred beyond the defined portfolio release scope.
      The policy was reviewed under P9-06.
- [x] Benchmark preflight counting and incoming-order
      staging overhead (P8-10).
      Measured preflight, reservation and execution costs
      across shallow and deep order-book workloads.
      Results documented in the Phase 8 performance report.
- [x] Review duplicated engine-level and book-level
      historical OrderId state (P9-05).
      Decision: retain both sets for this release.
      The sets serve different ownership boundaries.
      Future consolidation remains optional.
- [x] Validate planned shutdown and concurrent
      API interactions (P7-34 through P7-52).
      Completed deterministic lifecycle, backpressure,
      shutdown and concurrent correctness tests.
      Completed thread-safety and ownership source audits.
      Exhaustive concurrency verification is not claimed.
- [x] Complete reproducible latency and throughput
      benchmarking (Phase 8).
      Measured direct-engine latency, concurrent throughput
      and bounded-queue backpressure.
      Completed Phase 9 optimization comparison.
      Results are workload-specific, not production guarantees.
- [ ] Evaluate and execute supported sanitizer diagnostics.
      Tracked under P10-05 and P10-06.
      Unsupported diagnostics must be documented.

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
  The planned post-trade exception-safety audit was
  completed under P6-13.
- Seeded randomized differential verification was
  completed under P6-14 (commit 6c49ddd).
  Five seeds with 1,000 operations per seed were tested.
  Randomized cancellation coverage remains pending.
- Trade history currently has no enforced memory bound.
  Trade records accumulate in std::vector<Trade>.
  Geometric capacity growth reduces allocation overhead
  but does not limit total memory consumption.
  Bounded consumption, acknowledgment and backpressure
  remain future implementation work.
- Historical OrderId uniqueness is represented at both
  MatchingEngine and OrderBook levels.
  The duplicated ownership was reviewed under P9-05.
  Both sets are intentionally retained for this release
  because they protect different ownership boundaries.
  Future consolidation remains optional.
- OrderId uniqueness is enforced within a single
  MatchingEngine instance.
  No multi-instrument session manager or cross-book
  identity coordination has been implemented.
- The matching engine uses a single worker thread.
  Concurrent producers are supported, but matching
  operations themselves are serialized.
  This is a deliberate correctness and ordering decision.
- FIFO processing is defined by queue insertion order,
  not by the wall-clock order in which concurrent
  producers begin their API calls.
- Bounded queue backpressure can block producers.
  Phase 7 shutdown, backpressure and concurrent
  correctness tests have been completed.
  These tests do not establish exhaustive race freedom
  or formal concurrency correctness.
- Thread-safety, shared-state ownership and worker
  lifecycle audits were completed under Phase 7.8.
  Object destruction still requires external producer
  lifetime coordination.
  Concurrent destruction is unsupported.
- ThreadSanitizer was unavailable in the verified
  Windows/MSVC environment.
  No sanitizer-based race-free guarantee is claimed.
  Sanitizer verification using a supported toolchain
  remains future work.
- Phase 8 and Phase 9 performance measurements are
  available. Results apply only to the documented
  benchmark environments, configurations and workloads.
- The Phase 9 trade-history optimization reduced
  median elapsed time from 259.361 ms to 10.6309 ms
  in the recorded 10,000-trade workload.
  This represents approximately a 24.4x speedup
  for that specific benchmark comparison.
- No production-grade tail-latency guarantee,
  exchange connectivity or live trading deployment
  has been established.
  Benchmark results do not establish HFT
  production-readiness or performance superiority
  across other systems and workloads.
- MARKET SELL preflight currently uses Price{0}.
  This is consistent with the existing strictly positive
  resting LIMIT price model, but assumes the model
  will not admit zero or negative resting prices.
  Future price-model extensions require revisiting
  these synthetic preflight boundaries.
- Trade-vector capacity is reserved before incoming-order
  staging. If staging fails, capacity may remain increased
  despite no trade occurring.
  The current guarantee preserves logical trading state,
  not necessarily the previous memory allocation state.
- Final Phase 9 Debug and Release regression tests
  passed all six CTest suites in each configuration.
  These results establish tested functional behavior,
  not exhaustive concurrency or exception safety.
- The production Release build succeeded with
  BUILD_TESTING=OFF and BUILD_BENCHMARKS=OFF.
  The executable launched successfully.
  No test or benchmark targets were generated.
