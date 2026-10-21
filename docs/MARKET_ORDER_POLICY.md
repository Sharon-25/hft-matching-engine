# Market Order Policy

## Execution behavior

Market orders execute immediately against available
opposite-side liquidity in price-time priority.

Execution prices are determined by resting orders.

Market orders do not have a limit price and are never
inserted into the resting order book.

## Unfilled quantity

Any quantity not executed immediately is cancelled.

A market order can be accepted even when no liquidity
is available.

## Submission result

For accepted market orders:

- executed_quantity is the total quantity traded.
- remaining_quantity is the unexecuted quantity
  cancelled at the end of submission.

For accepted limit orders, remaining_quantity instead
represents the quantity remaining after execution,
which may be resting in the book.

## Order IDs

Accepted market-order IDs cannot be reused during
the lifetime of the matching engine.

Rejected submissions do not consume an unused ID.

## Current limitations

- The engine does not retain a historical market-order
  object or expose a queryable final market-order status.
- Market-order execution is single-threaded.
- There are no slippage limits or market protection bands.
- Trade history currently grows without a configured bound.
- Unexpected failures after irreversible execution are
  not transactionally recoverable.
- Persistence and crash recovery are not implemented.
