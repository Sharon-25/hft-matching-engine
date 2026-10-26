# Phase 6 - Post-Trade Exception-Safety Audit (P6-13)

## Scope

This audit covers the LIMIT and MARKET submission paths in `MatchingEngine.cpp`, their execution operations in `OrderBook.cpp`, trade-history recording, order-ID commitment and finalization.

## Preparation

Before trade execution, the engine:

- Validates the submitted order and checks duplicate IDs.
- Reserves the incoming order ID with rollback protection.
- Counts the expected matching operations.
- Reserves trade-history capacity.
- For LIMIT orders, inserts the incoming order into the book before matching.

Existing rollback mechanisms and failure-injection tests cover the tested preparation failure paths. This audit does not establish rollback guarantees for every conceivable exception or internal invariant violation.

## First Irreversible Mutation

For LIMIT submissions, the first irreversible trade mutation occurs inside `OrderBook::execute_match()`.

For MARKET submissions, it occurs inside `OrderBook::execute_best_order()`.

Once execution changes stored order quantities or removes filled orders, the submission cannot be rolled back by the order-ID reservation mechanism alone.

## Post-Mutation Operations

The current implementation uses numeric order fields, non-throwing trade value types, pre-reserved trade-history capacity and iterator-based filled-order cleanup.

Under valid inputs and preserved internal invariants, these operations are designed not to throw after execution begins.

However, defensive `std::logic_error` branches remain in execution and finalization. If an internal invariant is violated, an exception may occur after the book has already changed.

## Exception Guarantees

**Preparation:** Existing rollback and allocation-failure tests support preservation of the logical engine state for the tested preparation failures.

**Execution:** The post-mutation path is conditionally non-throwing when the engine begins in a consistent state, its invariants remain valid and no concurrent mutation occurs.

**Global guarantee:** An unconditional strong exception guarantee for every possible exception is not claimed.

## Verification

- Compile-time assertions verify the required `Trade`, `SubmissionResult` and destruction properties.
- Existing tests cover multi-trade LIMIT and MARKET submissions, partial fills, price-level removal, trade-history consistency and maximum-quantity accounting.
- Debug: 3/3 tests passed.
- Release: 3/3 tests passed.

## Deferred Work

- P6-14: Seeded randomized differential testing and cancellation coverage after the engine cancellation API is designed.
- P6-15: Isolate `MATCHING_ENGINE_TESTING` and failure-injection facilities from production builds.

## Limitations

This audit does not establish thread safety, resilience to arbitrary internal corruption or an unconditional global strong exception guarantee.
