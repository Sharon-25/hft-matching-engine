#include <cstdio>
#include <cstdlib>

#define CHECK(condition)                                             \
    do {                                                             \
        if (!(condition)) {                                          \
            std::fprintf(stderr,                                    \
                         "CHECK failed: %s (%s:%d)\n",               \
                         #condition, __FILE__, __LINE__);           \
            std::abort();                                            \
        }                                                            \
    } while (false)
#include <iostream>
#include <new>
#include <stdexcept>
#include <type_traits>

#include "matching_engine/MatchingEngine.hpp"

using namespace matching_engine;

void test_engine_starts_with_empty_book() {
    MatchingEngine engine;

    const OrderBook& book = engine.order_book();

    CHECK(book.order_count() == 0);
    CHECK(!book.best_bid().has_value());
    CHECK(!book.best_ask().has_value());
}

void test_engine_is_not_copyable_or_movable() {
    static_assert(!std::is_copy_constructible_v<MatchingEngine>);
    static_assert(!std::is_copy_assignable_v<MatchingEngine>);
    static_assert(!std::is_move_constructible_v<MatchingEngine>);
    static_assert(!std::is_move_assignable_v<MatchingEngine>);

    static_assert(std::is_nothrow_move_constructible_v<Trade>);
    static_assert(std::is_nothrow_destructible_v<Trade>);
}

void test_accept_new_order_id() {
    MatchingEngine engine;

    Order order(100, Side::BUY, 11000, 100);

    CHECK(
        engine.submit_limit_order(order).status ==
        SubmissionStatus::ACCEPTED
    );

    CHECK(engine.was_order_id_accepted(100));
}

void test_reject_duplicate_order_id() {
    MatchingEngine engine;

    Order first(200, Side::BUY, 11000, 100);
    Order duplicate(200, Side::SELL, 11200, 200);

    CHECK(
        engine.submit_limit_order(first).status ==
        SubmissionStatus::ACCEPTED
    );

    CHECK(
        engine.submit_limit_order(duplicate).status ==
        SubmissionStatus::DUPLICATE_ID
    );
}

void test_invalid_order_does_not_consume_id() {
    MatchingEngine engine;

    Order cancelled_order(
        300,
        Side::BUY,
        11000,
        100
    );

    // The order is valid at construction.
    // Cancellation makes it invalid for submission.
    CHECK(cancelled_order.cancel());

    const SubmissionResult rejected =
        engine.submit_limit_order(cancelled_order);

    CHECK(
        rejected.status ==
        SubmissionStatus::INVALID_ORDER
    );

    CHECK(rejected.executed_quantity == 0);
    CHECK(engine.trades().empty());
    CHECK(engine.order_book().order_count() == 0);
    CHECK(!engine.was_order_id_accepted(300));
    CHECK(!engine.order_book().was_order_id_used(300));

    // A fresh order with the same ID must succeed.
    Order valid(300, Side::BUY, 11000, 100);

    const SubmissionResult accepted =
        engine.submit_limit_order(valid);

    CHECK(
        accepted.status ==
        SubmissionStatus::ACCEPTED
    );

    CHECK(engine.was_order_id_accepted(300));
    CHECK(engine.order_book().contains(300));
    CHECK(engine.order_book().validate_invariants());
}

void test_buy_limit_matching() {
    MatchingEngine engine;

    CHECK(engine.submit_limit_order(
        Order(1, Side::SELL, 11000, 100)
    ).status == SubmissionStatus::ACCEPTED);

    const SubmissionResult result =
        engine.submit_limit_order(
            Order(2, Side::BUY, 11500, 100)
        );

    CHECK(result.status == SubmissionStatus::ACCEPTED);
    CHECK(result.executed_quantity == 100);
    CHECK(result.remaining_quantity == 0);

    CHECK(engine.order_book().order_count() == 0);
    CHECK(!engine.order_book().best_ask().has_value());
    CHECK(!engine.order_book().best_bid().has_value());

    CHECK(engine.was_order_id_accepted(1));
    CHECK(engine.was_order_id_accepted(2));
}

void test_sell_limit_matching() {
    MatchingEngine engine;

    CHECK(engine.submit_limit_order(
        Order(10, Side::BUY, 11000, 200)
    ).status == SubmissionStatus::ACCEPTED);

    const SubmissionResult result =
        engine.submit_limit_order(
            Order(20, Side::SELL, 10900, 150)
        );

    CHECK(result.status == SubmissionStatus::ACCEPTED);
    CHECK(result.executed_quantity == 150);
    CHECK(result.remaining_quantity == 0);

    const Order* best =
        engine.order_book().best_order(Side::BUY);

    CHECK(best != nullptr);
    CHECK(best->id() == 10);
    CHECK(best->remaining_quantity() == 50);
    CHECK(best->status() == OrderStatus::PARTIALLY_FILLED);
}

void test_partial_incoming_order_rests() {
    MatchingEngine engine;

    CHECK(engine.submit_limit_order(
        Order(100, Side::SELL, 11000, 100)
    ).status == SubmissionStatus::ACCEPTED);

    const SubmissionResult result =
        engine.submit_limit_order(
            Order(200, Side::BUY, 11500, 250)
        );

    CHECK(result.status == SubmissionStatus::ACCEPTED);
    CHECK(result.executed_quantity == 100);
    CHECK(result.remaining_quantity == 150);

    const Order* best =
        engine.order_book().best_order(Side::BUY);

    CHECK(best != nullptr);
    CHECK(best->id() == 200);
    CHECK(best->remaining_quantity() == 150);
    CHECK(best->status() == OrderStatus::PARTIALLY_FILLED);

    CHECK(engine.order_book().order_count() == 1);
    CHECK(*engine.order_book().best_bid() == 11500);
}

void test_failed_submission_preserves_id_availability() {
    MatchingEngine engine;

    Order partially_filled(
        500,
        Side::SELL,
        11000,
        100
    );

    // Execute 40 shares before submission.
    CHECK(partially_filled.execute(40));

    CHECK(
        partially_filled.status() ==
        OrderStatus::PARTIALLY_FILLED
    );

    CHECK(partially_filled.remaining_quantity() == 60);

    const SubmissionResult rejected =
        engine.submit_limit_order(partially_filled);

    CHECK(
        rejected.status ==
        SubmissionStatus::INVALID_ORDER
    );

    CHECK(rejected.executed_quantity == 0);
    CHECK(rejected.remaining_quantity == 60);

    CHECK(engine.trades().empty());
    CHECK(engine.order_book().order_count() == 0);
    CHECK(!engine.was_order_id_accepted(500));
    CHECK(!engine.order_book().was_order_id_used(500));

    // ID 500 must still be available.
    Order valid(500, Side::SELL, 11000, 100);

    const SubmissionResult accepted =
        engine.submit_limit_order(valid);

    CHECK(
        accepted.status ==
        SubmissionStatus::ACCEPTED
    );

    CHECK(accepted.executed_quantity == 0);
    CHECK(accepted.remaining_quantity == 100);

    CHECK(engine.was_order_id_accepted(500));
    CHECK(engine.order_book().contains(500));
    CHECK(engine.order_book().validate_invariants());
}

void test_buy_stops_at_limit_price() {
    MatchingEngine engine;

    CHECK(engine.submit_limit_order(
        Order(1, Side::SELL, 11000, 100)
    ).status == SubmissionStatus::ACCEPTED);

    CHECK(engine.submit_limit_order(
        Order(2, Side::SELL, 11200, 100)
    ).status == SubmissionStatus::ACCEPTED);

    const SubmissionResult result =
        engine.submit_limit_order(
            Order(3, Side::BUY, 11100, 250)
        );

    CHECK(result.status == SubmissionStatus::ACCEPTED);
    CHECK(result.executed_quantity == 100);
    CHECK(result.remaining_quantity == 150);

    const OrderBook& book = engine.order_book();

    CHECK(book.order_count() == 2);

    // Order 1 was fully executed.
    CHECK(!book.contains(1));

    // Order 2 cannot match above the BUY limit.
    CHECK(book.contains(2));
    CHECK(*book.best_ask() == 11200);

    // The incoming remainder rests at its own limit.
    CHECK(book.contains(3));
    CHECK(*book.best_bid() == 11100);

    const Order* buy = book.best_order(Side::BUY);
    const Order* sell = book.best_order(Side::SELL);

    CHECK(buy != nullptr);
    CHECK(sell != nullptr);

    CHECK(buy->id() == 3);
    CHECK(buy->remaining_quantity() == 150);
    CHECK(buy->status() == OrderStatus::PARTIALLY_FILLED);

    CHECK(sell->id() == 2);
    CHECK(sell->remaining_quantity() == 100);
}

void test_fully_filled_incoming_id_is_preserved() {
    MatchingEngine engine;

    engine.submit_limit_order(
        Order(1, Side::SELL, 11000, 100)
    );

    const SubmissionResult result =
        engine.submit_limit_order(
            Order(2, Side::BUY, 11500, 100)
        );

    CHECK(result.status == SubmissionStatus::ACCEPTED);
    CHECK(result.executed_quantity == 100);
    CHECK(result.remaining_quantity == 0);

    CHECK(!engine.order_book().contains(2));
    CHECK(engine.was_order_id_accepted(2));
    CHECK(engine.order_book().was_order_id_used(2));

    const SubmissionResult duplicate =
        engine.submit_limit_order(
            Order(2, Side::BUY, 12000, 50)
        );

    CHECK(
        duplicate.status ==
        SubmissionStatus::DUPLICATE_ID
    );
}

void test_staged_incoming_partial_fill() {
    MatchingEngine engine;

    engine.submit_limit_order(
        Order(10, Side::SELL, 11000, 100)
    );

    const SubmissionResult result =
        engine.submit_limit_order(
            Order(20, Side::BUY, 11500, 250)
        );

    CHECK(result.status == SubmissionStatus::ACCEPTED);
    CHECK(result.executed_quantity == 100);
    CHECK(result.remaining_quantity == 150);

    const Order* incoming =
        engine.order_book().find_order(20);

    CHECK(incoming != nullptr);
    CHECK(incoming->remaining_quantity() == 150);
    CHECK(
        incoming->status() ==
        OrderStatus::PARTIALLY_FILLED
    );

    CHECK(engine.order_book().order_count() == 1);
}

void test_rejected_order_preserves_book() {
    MatchingEngine engine;

    const SubmissionResult first =
        engine.submit_limit_order(
            Order(10, Side::BUY, 11000, 100)
        );

    CHECK(first.status == SubmissionStatus::ACCEPTED);

    const std::size_t before_count =
        engine.order_book().order_count();

    const auto before_bid =
        engine.order_book().best_bid();

    const SubmissionResult duplicate =
        engine.submit_limit_order(
            Order(10, Side::SELL, 10500, 200)
        );

    CHECK(
        duplicate.status ==
        SubmissionStatus::DUPLICATE_ID
    );

    CHECK(
        engine.order_book().order_count() ==
        before_count
    );

    CHECK(engine.order_book().best_bid() == before_bid);
    CHECK(!engine.order_book().best_ask().has_value());

    const Order* original =
        engine.order_book().find_order(10);

    CHECK(original != nullptr);
    CHECK(original->remaining_quantity() == 100);
    CHECK(original->side() == Side::BUY);
}

void test_allocation_failure_preserves_book() {
    MatchingEngine engine;

    const SubmissionResult initial =
        engine.submit_limit_order(
            Order(1, Side::SELL, 11000, 100)
        );

    CHECK(initial.status == SubmissionStatus::ACCEPTED);

    engine.set_insertion_failure_for_test(
        InsertionFailurePoint::AFTER_LIST_INSERT
    );

    bool caught = false;

    try {
        engine.submit_limit_order(
            Order(2, Side::BUY, 11500, 200)
        );
    } catch (const std::bad_alloc&) {
        caught = true;
    }

    CHECK(caught);

    // The original resting order must remain unchanged.
    const Order* original =
        engine.order_book().find_order(1);

    CHECK(original != nullptr);
    CHECK(original->remaining_quantity() == 100);

    // The failed incoming order must not be stored.
    CHECK(!engine.order_book().contains(2));

    // Its session ID must be available for reuse.
    CHECK(!engine.was_order_id_accepted(2));
    CHECK(!engine.order_book().was_order_id_used(2));

    CHECK(engine.order_book().order_count() == 1);
    CHECK(*engine.order_book().best_ask() == 11000);

    // A subsequent valid submission must still work.
    const SubmissionResult retry =
        engine.submit_limit_order(
            Order(2, Side::BUY, 11500, 200)
        );

    CHECK(retry.status == SubmissionStatus::ACCEPTED);
    CHECK(retry.executed_quantity == 100);
    CHECK(retry.remaining_quantity == 100);
}

void test_insertion_rollback_at_all_checkpoints() {
    const InsertionFailurePoint points[] = {
        InsertionFailurePoint::AFTER_LIST_INSERT,
        InsertionFailurePoint::AFTER_INDEX_INSERT,
        InsertionFailurePoint::AFTER_USED_ID_INSERT
    };

    for (InsertionFailurePoint point : points) {
        MatchingEngine engine;

        const SubmissionResult initial =
            engine.submit_limit_order(
                Order(1, Side::SELL, 11000, 100)
            );

        CHECK(
            initial.status ==
            SubmissionStatus::ACCEPTED
        );

        engine.set_insertion_failure_for_test(point);

        bool caught = false;

        try {
            engine.submit_limit_order(
                Order(2, Side::BUY, 11500, 200)
            );
        } catch (const std::bad_alloc&) {
            caught = true;
        }

        CHECK(caught);

        const OrderBook& book = engine.order_book();

        CHECK(book.order_count() == 1);
        CHECK(book.contains(1));
        CHECK(!book.contains(2));

        const Order* original = book.find_order(1);

        CHECK(original != nullptr);
        CHECK(original->remaining_quantity() == 100);

        CHECK(!engine.was_order_id_accepted(2));
        CHECK(!book.was_order_id_used(2));

        // Retry with the same ID after rollback.
        const SubmissionResult retry =
            engine.submit_limit_order(
                Order(2, Side::BUY, 11500, 200)
            );

        CHECK(retry.status == SubmissionStatus::ACCEPTED);
        CHECK(retry.executed_quantity == 100);
        CHECK(retry.remaining_quantity == 100);
    }
}

static_assert(std::is_trivially_copyable_v<Trade>);

void test_trade_record_model() {
    Trade trade{
        3,
        1,
        11000,
        100
    };

    CHECK(trade.incoming_order_id == 3);
    CHECK(trade.resting_order_id == 1);
    CHECK(trade.execution_price_ticks == 11000);
    CHECK(trade.executed_quantity == 100);
}

void test_initial_trade_history_is_empty() {
    MatchingEngine engine;

    CHECK(engine.trades().empty());
}

void test_multiple_trade_records() {
    MatchingEngine engine;

    engine.submit_limit_order(
        Order(1, Side::SELL, 11000, 100)
    );

    engine.submit_limit_order(
        Order(2, Side::SELL, 11200, 100)
    );

    engine.submit_limit_order(
        Order(3, Side::SELL, 11400, 100)
    );

    const SubmissionResult result =
        engine.submit_limit_order(
            Order(4, Side::BUY, 11500, 250)
        );

    CHECK(result.status == SubmissionStatus::ACCEPTED);
    CHECK(result.executed_quantity == 250);
    CHECK(result.remaining_quantity == 0);

    const auto& trades = engine.trades();

    CHECK(trades.size() == 3);

    CHECK(trades[0].incoming_order_id == 4);
    CHECK(trades[0].resting_order_id == 1);
    CHECK(trades[0].execution_price_ticks == 11000);
    CHECK(trades[0].executed_quantity == 100);

    CHECK(trades[1].incoming_order_id == 4);
    CHECK(trades[1].resting_order_id == 2);
    CHECK(trades[1].execution_price_ticks == 11200);
    CHECK(trades[1].executed_quantity == 100);

    CHECK(trades[2].incoming_order_id == 4);
    CHECK(trades[2].resting_order_id == 3);
    CHECK(trades[2].execution_price_ticks == 11400);
    CHECK(trades[2].executed_quantity == 50);

    const Order* remaining =
        engine.order_book().find_order(3);

    CHECK(remaining != nullptr);
    CHECK(remaining->remaining_quantity() == 50);
}

void test_matchable_order_count() {
    OrderBook book;

    CHECK(book.add_order(
        Order(1, Side::SELL, 11000, 100)
    ));

    CHECK(book.add_order(
        Order(2, Side::SELL, 11200, 100)
    ));

    CHECK(book.add_order(
        Order(3, Side::SELL, 11400, 100)
    ));

    CHECK(
        book.count_matchable_orders(
            Side::BUY, 11500, 250
        ) == 3
    );

    CHECK(
        book.count_matchable_orders(
            Side::BUY, 11100, 250
        ) == 1
    );

    CHECK(
        book.count_matchable_orders(
            Side::BUY, 10900, 250
        ) == 0
    );
}

void test_paired_execution_partial_and_full_fill() {
    MatchingEngine engine;

    const SubmissionResult resting =
        engine.submit_limit_order(
            Order(10, Side::SELL, 11000, 100)
        );

    CHECK(resting.status == SubmissionStatus::ACCEPTED);

    const SubmissionResult incoming =
        engine.submit_limit_order(
            Order(20, Side::BUY, 11500, 150)
        );

    CHECK(incoming.status == SubmissionStatus::ACCEPTED);
    CHECK(incoming.executed_quantity == 100);
    CHECK(incoming.remaining_quantity == 50);

    const OrderBook& book = engine.order_book();

    CHECK(!book.contains(10));
    CHECK(book.contains(20));

    const Order* remainder = book.find_order(20);

    CHECK(remainder != nullptr);
    CHECK(remainder->remaining_quantity() == 50);
    CHECK(
        remainder->status() ==
        OrderStatus::PARTIALLY_FILLED
    );

    CHECK(engine.trades().size() == 1);

    const Trade& trade = engine.trades()[0];

    CHECK(trade.incoming_order_id == 20);
    CHECK(trade.resting_order_id == 10);
    CHECK(trade.execution_price_ticks == 11000);
    CHECK(trade.executed_quantity == 100);
}

void test_paired_execution_preserves_fifo() {
    MatchingEngine engine;

    engine.submit_limit_order(
        Order(1, Side::SELL, 11000, 100)
    );

    engine.submit_limit_order(
        Order(2, Side::SELL, 11000, 100)
    );

    const SubmissionResult result =
        engine.submit_limit_order(
            Order(3, Side::BUY, 11500, 150)
        );

    CHECK(result.executed_quantity == 150);
    CHECK(result.remaining_quantity == 0);

    const auto& trades = engine.trades();

    CHECK(trades.size() == 2);

    CHECK(trades[0].resting_order_id == 1);
    CHECK(trades[0].executed_quantity == 100);

    CHECK(trades[1].resting_order_id == 2);
    CHECK(trades[1].executed_quantity == 50);

    const Order* remaining =
        engine.order_book().find_order(2);

    CHECK(remaining != nullptr);
    CHECK(remaining->remaining_quantity() == 50);
}

void test_trade_history_consistency() {
    MatchingEngine engine;

    engine.submit_limit_order(
        Order(1, Side::SELL, 11000, 100)
    );

    engine.submit_limit_order(
        Order(2, Side::SELL, 11200, 150)
    );

    const SubmissionResult result =
        engine.submit_limit_order(
            Order(3, Side::BUY, 11500, 200)
        );

    CHECK(result.status == SubmissionStatus::ACCEPTED);

    Quantity total_executed = 0;

    for (const Trade& trade : engine.trades()) {
        CHECK(trade.incoming_order_id !=
               trade.resting_order_id);

        CHECK(trade.executed_quantity > 0);
        CHECK(trade.execution_price_ticks > 0);

        total_executed += trade.executed_quantity;
    }

    CHECK(total_executed == result.executed_quantity);
    CHECK(total_executed == 200);

    const Order* remaining =
        engine.order_book().find_order(2);

    CHECK(remaining != nullptr);
    CHECK(remaining->remaining_quantity() == 50);
}

void test_book_invariants_after_submission_sequence() {
    MatchingEngine engine;

    auto submit_and_validate =
        [&](const Order& order) {
            const SubmissionResult result =
                engine.submit_limit_order(order);

            CHECK(
                result.status ==
                SubmissionStatus::ACCEPTED
            );

            CHECK(
                engine.order_book().validate_invariants()
            );

            return result;
        };

    // Start with non-crossing orders.
    submit_and_validate(
        Order(1, Side::BUY, 10000, 200)
    );

    submit_and_validate(
        Order(2, Side::BUY, 9900, 100)
    );

    submit_and_validate(
        Order(3, Side::SELL, 10500, 150)
    );

    submit_and_validate(
        Order(4, Side::SELL, 10600, 200)
    );

    // Partially execute the best SELL order.
    submit_and_validate(
        Order(5, Side::BUY, 10500, 100)
    );

    // Fully execute the remaining best SELL quantity.
    submit_and_validate(
        Order(6, Side::BUY, 10500, 50)
    );

    // Cross multiple BUY price levels.
    submit_and_validate(
        Order(7, Side::SELL, 9800, 250)
    );

    // Add a new resting order after executions.
    submit_and_validate(
        Order(8, Side::BUY, 9700, 300)
    );
    CHECK(engine.order_book().validate_invariants());
}

void test_index_consistency_after_full_execution() {
    MatchingEngine engine;

    engine.submit_limit_order(
        Order(10, Side::SELL, 11000, 100)
    );

    engine.submit_limit_order(
        Order(20, Side::SELL, 11000, 100)
    );

    CHECK(engine.order_book().order_count() == 2);
    CHECK(engine.order_book().validate_invariants());

    const SubmissionResult result =
        engine.submit_limit_order(
            Order(30, Side::BUY, 11500, 200)
        );

    CHECK(result.executed_quantity == 200);
    CHECK(result.remaining_quantity == 0);

    CHECK(engine.order_book().order_count() == 0);

    CHECK(!engine.order_book().contains(10));
    CHECK(!engine.order_book().contains(20));
    CHECK(!engine.order_book().contains(30));

    CHECK(engine.order_book().was_order_id_used(10));
    CHECK(engine.order_book().was_order_id_used(20));
    CHECK(engine.order_book().was_order_id_used(30));

    CHECK(engine.order_book().validate_invariants());
}

void test_submission_trade_accounting() {
    MatchingEngine engine;

    engine.submit_limit_order(
        Order(1, Side::SELL, 11000, 100)
    );

    engine.submit_limit_order(
        Order(2, Side::SELL, 11200, 150)
    );

    const std::size_t trades_before =
        engine.trades().size();

    const SubmissionResult result =
        engine.submit_limit_order(
            Order(3, Side::BUY, 11500, 200)
        );

    CHECK(result.status == SubmissionStatus::ACCEPTED);

    Quantity recorded_quantity = 0;

    for (std::size_t i = trades_before;
         i < engine.trades().size(); ++i) {
        const Trade& trade = engine.trades()[i];

        CHECK(trade.incoming_order_id == 3);
        recorded_quantity += trade.executed_quantity;
    }

    CHECK(recorded_quantity == result.executed_quantity);
    CHECK(result.executed_quantity +
           result.remaining_quantity == 200);

    CHECK(engine.order_book().validate_invariants());
}

void test_limit_submission_rejects_market_order() {
    MatchingEngine engine;

    Order market(
        200,
        Side::SELL,
        OrderType::MARKET,
        0,
        300
    );

    const SubmissionResult result =
        engine.submit_limit_order(market);

    CHECK(
        result.status ==
        SubmissionStatus::INVALID_ORDER
    );

    CHECK(result.executed_quantity == 0);
    CHECK(result.remaining_quantity == 300);

    CHECK(engine.order_book().order_count() == 0);
    CHECK(engine.trades().empty());

    CHECK(!engine.was_order_id_accepted(200));
    CHECK(engine.order_book().validate_invariants());
}

void test_market_buy_multiple_levels() {
    MatchingEngine engine;

    engine.submit_limit_order(
        Order(1, Side::SELL, 11000, 100)
    );

    engine.submit_limit_order(
        Order(2, Side::SELL, 11200, 150)
    );

    const SubmissionResult result =
        engine.submit_market_order(
            Order(
                3,
                Side::BUY,
                OrderType::MARKET,
                0,
                300
            )
        );

    CHECK(result.status == SubmissionStatus::ACCEPTED);
    CHECK(result.executed_quantity == 250);
    CHECK(result.remaining_quantity == 50);

    CHECK(engine.trades().size() == 2);

    CHECK(engine.trades()[0].resting_order_id == 1);
    CHECK(engine.trades()[0].execution_price_ticks == 11000);
    CHECK(engine.trades()[0].executed_quantity == 100);

    CHECK(engine.trades()[1].resting_order_id == 2);
    CHECK(engine.trades()[1].execution_price_ticks == 11200);
    CHECK(engine.trades()[1].executed_quantity == 150);

    CHECK(engine.order_book().order_count() == 0);
    CHECK(!engine.order_book().contains(3));
    CHECK(engine.was_order_id_accepted(3));
    CHECK(engine.order_book().validate_invariants());
}

void test_market_sell_best_price_first() {
    MatchingEngine engine;

    engine.submit_limit_order(
        Order(10, Side::BUY, 10000, 100)
    );

    engine.submit_limit_order(
        Order(11, Side::BUY, 10200, 150)
    );

    const SubmissionResult result =
        engine.submit_market_order(
            Order(
                12,
                Side::SELL,
                OrderType::MARKET,
                0,
                200
            )
        );

    CHECK(result.status == SubmissionStatus::ACCEPTED);
    CHECK(result.executed_quantity == 200);
    CHECK(result.remaining_quantity == 0);

    CHECK(engine.trades().size() == 2);

    CHECK(engine.trades()[0].resting_order_id == 11);
    CHECK(engine.trades()[0].execution_price_ticks == 10200);
    CHECK(engine.trades()[0].executed_quantity == 150);

    CHECK(engine.trades()[1].resting_order_id == 10);
    CHECK(engine.trades()[1].execution_price_ticks == 10000);
    CHECK(engine.trades()[1].executed_quantity == 50);

    const Order* remaining =
        engine.order_book().find_order(10);

    CHECK(remaining != nullptr);
    CHECK(remaining->remaining_quantity() == 50);
    CHECK(engine.order_book().validate_invariants());
}

void test_market_order_empty_book() {
    MatchingEngine engine;

    const SubmissionResult result =
        engine.submit_market_order(
            Order(
                20,
                Side::BUY,
                OrderType::MARKET,
                0,
                100
            )
        );

    CHECK(result.status == SubmissionStatus::ACCEPTED);
    CHECK(result.executed_quantity == 0);
    CHECK(result.remaining_quantity == 100);

    CHECK(engine.trades().empty());
    CHECK(engine.order_book().order_count() == 0);
    CHECK(engine.was_order_id_accepted(20));

    // An accepted market-order ID cannot be reused.
    const SubmissionResult duplicate =
        engine.submit_limit_order(
            Order(20, Side::BUY, 11000, 100)
        );

    CHECK(
        duplicate.status ==
        SubmissionStatus::DUPLICATE_ID
    );

    CHECK(engine.order_book().validate_invariants());
}

void test_rejected_market_order_preserves_id() {
    MatchingEngine engine;

    Order invalid(
        100,
        Side::BUY,
        OrderType::MARKET,
        0,
        200
    );

    CHECK(invalid.cancel());

    const SubmissionResult rejected =
        engine.submit_market_order(invalid);

    CHECK(
        rejected.status ==
        SubmissionStatus::INVALID_ORDER
    );

    CHECK(rejected.executed_quantity == 0);
    CHECK(rejected.remaining_quantity == 200);

    CHECK(!engine.was_order_id_accepted(100));
    CHECK(engine.trades().empty());
    CHECK(engine.order_book().order_count() == 0);

    const SubmissionResult accepted =
        engine.submit_market_order(
            Order(
                100,
                Side::BUY,
                OrderType::MARKET,
                0,
                200
            )
        );

    CHECK(
        accepted.status ==
        SubmissionStatus::ACCEPTED
    );

    CHECK(accepted.executed_quantity == 0);
    CHECK(accepted.remaining_quantity == 200);
    CHECK(engine.was_order_id_accepted(100));
    CHECK(engine.order_book().validate_invariants());
}

void test_market_limit_shared_id_namespace() {
    MatchingEngine engine;

    const SubmissionResult market =
        engine.submit_market_order(
            Order(
                200,
                Side::BUY,
                OrderType::MARKET,
                0,
                100
            )
        );

    CHECK(
        market.status ==
        SubmissionStatus::ACCEPTED
    );

    const SubmissionResult duplicate_limit =
        engine.submit_limit_order(
            Order(200, Side::SELL, 11000, 100)
        );

    CHECK(
        duplicate_limit.status ==
        SubmissionStatus::DUPLICATE_ID
    );

    const SubmissionResult duplicate_market =
        engine.submit_market_order(
            Order(
                200,
                Side::SELL,
                OrderType::MARKET,
                0,
                100
            )
        );

    CHECK(
        duplicate_market.status ==
        SubmissionStatus::DUPLICATE_ID
    );

    CHECK(engine.order_book().order_count() == 0);
    CHECK(engine.trades().empty());
    CHECK(engine.order_book().validate_invariants());
}

void test_limit_id_rejected_by_market_submission() {
    MatchingEngine engine;

    const SubmissionResult limit =
        engine.submit_limit_order(
            Order(300, Side::SELL, 11000, 100)
        );

    CHECK(limit.status == SubmissionStatus::ACCEPTED);

    const SubmissionResult duplicate =
        engine.submit_market_order(
            Order(
                300,
                Side::BUY,
                OrderType::MARKET,
                0,
                100
            )
        );

    CHECK(
        duplicate.status ==
        SubmissionStatus::DUPLICATE_ID
    );

    CHECK(engine.order_book().contains(300));
    CHECK(engine.order_book().order_count() == 1);
    CHECK(engine.trades().empty());
    CHECK(engine.order_book().validate_invariants());
}

void test_market_partial_execution_accounting() {
    MatchingEngine engine;

    engine.submit_limit_order(
        Order(400, Side::SELL, 11000, 75)
    );

    const SubmissionResult result =
        engine.submit_market_order(
            Order(
                401,
                Side::BUY,
                OrderType::MARKET,
                0,
                200
            )
        );

    CHECK(result.status == SubmissionStatus::ACCEPTED);
    CHECK(result.executed_quantity == 75);
    CHECK(result.remaining_quantity == 125);

    CHECK(engine.trades().size() == 1);
    CHECK(engine.trades()[0].incoming_order_id == 401);
    CHECK(engine.trades()[0].resting_order_id == 400);
    CHECK(engine.trades()[0].executed_quantity == 75);

    CHECK(engine.was_order_id_accepted(401));
    CHECK(!engine.order_book().contains(401));
    CHECK(engine.order_book().order_count() == 0);
    CHECK(engine.order_book().validate_invariants());
}

void test_market_order_fully_executed_lifecycle() {
    MatchingEngine engine;

    engine.submit_limit_order(
        Order(1, Side::SELL, 11000, 100)
    );

    const SubmissionResult result =
        engine.submit_market_order(
            Order(
                2,
                Side::BUY,
                OrderType::MARKET,
                0,
                100
            )
        );

    CHECK(result.status == SubmissionStatus::ACCEPTED);

    const MarketOrderAccounting accounting =
        engine.market_order_accounting(result);

    CHECK(accounting.executed_quantity == 100);
    CHECK(accounting.cancelled_quantity == 0);

    CHECK(engine.was_order_id_accepted(2));
    CHECK(!engine.order_book().contains(2));
    CHECK(engine.order_book().order_count() == 0);
    CHECK(engine.order_book().validate_invariants());
}

void test_market_order_partially_executed_lifecycle() {
    MatchingEngine engine;

    engine.submit_limit_order(
        Order(10, Side::SELL, 11000, 60)
    );

    const SubmissionResult result =
        engine.submit_market_order(
            Order(
                11,
                Side::BUY,
                OrderType::MARKET,
                0,
                100
            )
        );

    CHECK(result.status == SubmissionStatus::ACCEPTED);

    const MarketOrderAccounting accounting =
        engine.market_order_accounting(result);

    CHECK(accounting.executed_quantity == 60);
    CHECK(accounting.cancelled_quantity == 40);

    CHECK(engine.was_order_id_accepted(11));
    CHECK(!engine.order_book().contains(11));
    CHECK(engine.order_book().order_count() == 0);
    CHECK(engine.order_book().validate_invariants());
}

void test_market_order_unfilled_lifecycle() {
    MatchingEngine engine;

    const SubmissionResult result =
        engine.submit_market_order(
            Order(
                20,
                Side::SELL,
                OrderType::MARKET,
                0,
                150
            )
        );

    CHECK(result.status == SubmissionStatus::ACCEPTED);

    const MarketOrderAccounting accounting =
        engine.market_order_accounting(result);

    CHECK(accounting.executed_quantity == 0);
    CHECK(accounting.cancelled_quantity == 150);

    CHECK(engine.was_order_id_accepted(20));
    CHECK(!engine.order_book().contains(20));
    CHECK(engine.trades().empty());
    CHECK(engine.order_book().validate_invariants());
}

int main() {
    test_engine_starts_with_empty_book();
    test_engine_is_not_copyable_or_movable();
    test_accept_new_order_id();
    test_reject_duplicate_order_id();
    test_invalid_order_does_not_consume_id();
    test_buy_limit_matching();
    test_sell_limit_matching();
    test_partial_incoming_order_rests();
    test_failed_submission_preserves_id_availability();
    test_buy_stops_at_limit_price();
    test_fully_filled_incoming_id_is_preserved();
    test_staged_incoming_partial_fill();
    test_rejected_order_preserves_book();
    test_allocation_failure_preserves_book();
    test_insertion_rollback_at_all_checkpoints();
    test_trade_record_model();
    test_initial_trade_history_is_empty();
    test_multiple_trade_records();
    test_matchable_order_count();
    test_paired_execution_partial_and_full_fill();
    test_paired_execution_preserves_fifo();
    test_trade_history_consistency();
    test_book_invariants_after_submission_sequence();
    test_index_consistency_after_full_execution();
    test_submission_trade_accounting();
    test_limit_submission_rejects_market_order();
    test_market_buy_multiple_levels();
    test_market_sell_best_price_first();
    test_market_order_empty_book();
    test_rejected_market_order_preserves_id();
    test_market_limit_shared_id_namespace();
    test_limit_id_rejected_by_market_submission();
    test_market_partial_execution_accounting();
    test_market_order_fully_executed_lifecycle();
    test_market_order_partially_executed_lifecycle();
    test_market_order_unfilled_lifecycle();

    std::cout << "MatchingEngine tests passed\n";
    return 0;
}
