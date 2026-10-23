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
#include <algorithm>
#include <iostream>
#include <limits>
#include <new>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

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

void test_limit_match_both_orders_fully_filled() {
    MatchingEngine engine;

    // Resting SELL: ID 10, Price 10500, Qty 100
    const SubmissionResult resting_res =
        engine.submit_limit_order(
            Order(10, Side::SELL, 10500, 100)
        );
    CHECK(resting_res.status == SubmissionStatus::ACCEPTED);
    CHECK(resting_res.executed_quantity == 0);
    CHECK(resting_res.remaining_quantity == 100);
    CHECK(engine.order_book().order_count() == 1);
    CHECK(engine.order_book().contains(10));
    CHECK(engine.order_book().validate_invariants());

    // Incoming BUY: ID 20, Price 11000, Qty 100 (crosses at 10500)
    const SubmissionResult incoming_res =
        engine.submit_limit_order(
            Order(20, Side::BUY, 11000, 100)
        );
    CHECK(incoming_res.status == SubmissionStatus::ACCEPTED);
    CHECK(incoming_res.executed_quantity == 100);
    CHECK(incoming_res.remaining_quantity == 0);

    // Trade record verification:
    // Both orders matched via execute_match() inside submit_limit_order()
    CHECK(engine.trades().size() == 1);
    const Trade& trade = engine.trades().back();
    CHECK(trade.incoming_order_id == 20);
    CHECK(trade.resting_order_id == 10);
    CHECK(trade.execution_price_ticks == 10500);
    CHECK(trade.executed_quantity == 100);

    // OrderBook contents: both fully filled, neither remains active
    const OrderBook& book = engine.order_book();
    CHECK(book.order_count() == 0);
    CHECK(!book.contains(10));
    CHECK(!book.contains(20));
    CHECK(!book.best_bid().has_value());
    CHECK(!book.best_ask().has_value());

    // Permanent ID tracking
    CHECK(engine.was_order_id_accepted(10));
    CHECK(engine.was_order_id_accepted(20));
    CHECK(book.was_order_id_used(10));
    CHECK(book.was_order_id_used(20));

    // ID reuse must be rejected
    CHECK(engine.submit_limit_order(Order(10, Side::BUY, 10000, 50)).status ==
          SubmissionStatus::DUPLICATE_ID);
    CHECK(engine.submit_limit_order(Order(20, Side::SELL, 12000, 50)).status ==
          SubmissionStatus::DUPLICATE_ID);

    CHECK(book.validate_invariants());
}

void test_limit_match_only_incoming_fully_filled() {
    MatchingEngine engine;

    // Resting SELL: ID 30, Price 10500, Qty 200
    const SubmissionResult resting_res =
        engine.submit_limit_order(
            Order(30, Side::SELL, 10500, 200)
        );
    CHECK(resting_res.status == SubmissionStatus::ACCEPTED);
    CHECK(resting_res.executed_quantity == 0);
    CHECK(resting_res.remaining_quantity == 200);

    // Incoming BUY: ID 40, Price 11000, Qty 100 (matches 100)
    const SubmissionResult incoming_res =
        engine.submit_limit_order(
            Order(40, Side::BUY, 11000, 100)
        );
    CHECK(incoming_res.status == SubmissionStatus::ACCEPTED);
    CHECK(incoming_res.executed_quantity == 100);
    CHECK(incoming_res.remaining_quantity == 0);

    // Trade verification
    CHECK(engine.trades().size() == 1);
    const Trade& trade = engine.trades().back();
    CHECK(trade.incoming_order_id == 40);
    CHECK(trade.resting_order_id == 30);
    CHECK(trade.execution_price_ticks == 10500);
    CHECK(trade.executed_quantity == 100);

    // OrderBook contents: incoming filled/removed; resting partially filled and remains
    const OrderBook& book = engine.order_book();
    CHECK(book.order_count() == 1);
    CHECK(!book.contains(40));
    CHECK(book.contains(30));
    const Order* resting_order = book.find_order(30);
    CHECK(resting_order != nullptr);
    CHECK(resting_order->remaining_quantity() == 100);
    CHECK(resting_order->executed_quantity() == 100);
    CHECK(resting_order->status() == OrderStatus::PARTIALLY_FILLED);
    CHECK(*book.best_ask() == 10500);
    CHECK(!book.best_bid().has_value());

    // Permanent ID tracking
    CHECK(engine.was_order_id_accepted(30));
    CHECK(engine.was_order_id_accepted(40));
    CHECK(book.was_order_id_used(30));
    CHECK(book.was_order_id_used(40));

    CHECK(book.validate_invariants());
}

void test_limit_match_only_resting_fully_filled() {
    MatchingEngine engine;

    // Resting BUY: ID 50, Price 11000, Qty 100
    const SubmissionResult resting_res =
        engine.submit_limit_order(
            Order(50, Side::BUY, 11000, 100)
        );
    CHECK(resting_res.status == SubmissionStatus::ACCEPTED);
    CHECK(resting_res.executed_quantity == 0);
    CHECK(resting_res.remaining_quantity == 100);

    // Incoming SELL: ID 60, Price 10500, Qty 250 (matches 100, remaining 150 rests)
    const SubmissionResult incoming_res =
        engine.submit_limit_order(
            Order(60, Side::SELL, 10500, 250)
        );
    CHECK(incoming_res.status == SubmissionStatus::ACCEPTED);
    CHECK(incoming_res.executed_quantity == 100);
    CHECK(incoming_res.remaining_quantity == 150);

    // Trade verification
    CHECK(engine.trades().size() == 1);
    const Trade& trade = engine.trades().back();
    CHECK(trade.incoming_order_id == 60);
    CHECK(trade.resting_order_id == 50);
    CHECK(trade.execution_price_ticks == 11000);
    CHECK(trade.executed_quantity == 100);

    // OrderBook contents: resting filled/removed; incoming partially filled and resting
    const OrderBook& book = engine.order_book();
    CHECK(book.order_count() == 1);
    CHECK(!book.contains(50));
    CHECK(book.contains(60));
    const Order* resting_remainder = book.find_order(60);
    CHECK(resting_remainder != nullptr);
    CHECK(resting_remainder->remaining_quantity() == 150);
    CHECK(resting_remainder->executed_quantity() == 100);
    CHECK(resting_remainder->status() == OrderStatus::PARTIALLY_FILLED);
    CHECK(*book.best_ask() == 10500);
    CHECK(!book.best_bid().has_value());

    // Permanent ID tracking
    CHECK(engine.was_order_id_accepted(50));
    CHECK(engine.was_order_id_accepted(60));
    CHECK(book.was_order_id_used(50));
    CHECK(book.was_order_id_used(60));

    CHECK(book.validate_invariants());
}

void test_limit_match_both_orders_partially_filled() {
    MatchingEngine engine;

    // Resting BUY: ID 70, Price 11000, Qty 300
    const SubmissionResult res70 =
        engine.submit_limit_order(
            Order(70, Side::BUY, 11000, 300)
        );
    CHECK(res70.status == SubmissionStatus::ACCEPTED);
    CHECK(res70.executed_quantity == 0);
    CHECK(res70.remaining_quantity == 300);

    // First incoming SELL: ID 80, Price 10500, Qty 120
    // Matches 120 against order 70.
    // Order 70 now partially filled with 180 remaining.
    const SubmissionResult res80 =
        engine.submit_limit_order(
            Order(80, Side::SELL, 10500, 120)
        );
    CHECK(res80.status == SubmissionStatus::ACCEPTED);
    CHECK(res80.executed_quantity == 120);
    CHECK(res80.remaining_quantity == 0);

    const OrderBook& book = engine.order_book();
    CHECK(book.contains(70));
    CHECK(book.find_order(70)->status() == OrderStatus::PARTIALLY_FILLED);
    CHECK(book.find_order(70)->remaining_quantity() == 180);
    CHECK(book.validate_invariants());

    // Second incoming SELL: ID 90, Price 10500, Qty 250
    // Matches 180 against resting order 70.
    // Order 70 is now fully filled (total 300 executed) and removed.
    // Order 90 is partially filled (180 executed, 70 remaining) and rests in book.
    const SubmissionResult res90 =
        engine.submit_limit_order(
            Order(90, Side::SELL, 10500, 250)
        );
    CHECK(res90.status == SubmissionStatus::ACCEPTED);
    CHECK(res90.executed_quantity == 180);
    CHECK(res90.remaining_quantity == 70);

    // Trade verification: exactly 2 trades
    CHECK(engine.trades().size() == 2);
    const Trade& trade1 = engine.trades()[0];
    CHECK(trade1.incoming_order_id == 80);
    CHECK(trade1.resting_order_id == 70);
    CHECK(trade1.execution_price_ticks == 11000);
    CHECK(trade1.executed_quantity == 120);

    const Trade& trade2 = engine.trades()[1];
    CHECK(trade2.incoming_order_id == 90);
    CHECK(trade2.resting_order_id == 70);
    CHECK(trade2.execution_price_ticks == 11000);
    CHECK(trade2.executed_quantity == 180);

    // Final order book state:
    // Order 70 filled & removed; Order 80 filled & removed; Order 90 resting partially filled
    CHECK(book.order_count() == 1);
    CHECK(!book.contains(70));
    CHECK(!book.contains(80));
    CHECK(book.contains(90));
    const Order* resting_90 = book.find_order(90);
    CHECK(resting_90 != nullptr);
    CHECK(resting_90->remaining_quantity() == 70);
    CHECK(resting_90->executed_quantity() == 180);
    CHECK(resting_90->status() == OrderStatus::PARTIALLY_FILLED);
    CHECK(*book.best_ask() == 10500);
    CHECK(!book.best_bid().has_value());

    // All order IDs permanently tracked
    CHECK(engine.was_order_id_accepted(70));
    CHECK(engine.was_order_id_accepted(80));
    CHECK(engine.was_order_id_accepted(90));
    CHECK(book.was_order_id_used(70));
    CHECK(book.was_order_id_used(80));
    CHECK(book.was_order_id_used(90));

    CHECK(book.validate_invariants());
}

void test_limit_order_id_reservation_hardening() {
    MatchingEngine engine;

    // 1. Non-crossing limit order: admission commits ID permanently.
    const SubmissionResult nc_res =
        engine.submit_limit_order(
            Order(1001, Side::BUY, 10000, 100)
        );
    CHECK(nc_res.status == SubmissionStatus::ACCEPTED);
    CHECK(nc_res.executed_quantity == 0);
    CHECK(nc_res.remaining_quantity == 100);
    CHECK(engine.was_order_id_accepted(1001));
    CHECK(engine.order_book().contains(1001));
    CHECK(engine.order_book().was_order_id_used(1001));

    // Duplicate submission with same ID must be rejected.
    CHECK(
        engine.submit_limit_order(
            Order(1001, Side::SELL, 11000, 50)
        ).status == SubmissionStatus::DUPLICATE_ID
    );

    // 2. Crossing limit order: admission commits ID even when fully filled
    //    and removed from the active book.
    const SubmissionResult resting_res =
        engine.submit_limit_order(
            Order(1002, Side::SELL, 10500, 50)
        );
    CHECK(resting_res.status == SubmissionStatus::ACCEPTED);
    CHECK(engine.was_order_id_accepted(1002));
    CHECK(engine.order_book().contains(1002));

    const SubmissionResult cross_fill_res =
        engine.submit_limit_order(
            Order(1003, Side::BUY, 10500, 50)
        );
    CHECK(cross_fill_res.status == SubmissionStatus::ACCEPTED);
    CHECK(cross_fill_res.executed_quantity == 50);
    CHECK(cross_fill_res.remaining_quantity == 0);

    // Both orders are fully filled and removed from the active book.
    CHECK(!engine.order_book().contains(1002));
    CHECK(!engine.order_book().contains(1003));

    // But both IDs are permanently recorded as accepted and used.
    CHECK(engine.was_order_id_accepted(1002));
    CHECK(engine.was_order_id_accepted(1003));
    CHECK(engine.order_book().was_order_id_used(1002));
    CHECK(engine.order_book().was_order_id_used(1003));

    // Reusing either fully filled ID must be rejected as duplicate.
    CHECK(
        engine.submit_limit_order(
            Order(1002, Side::BUY, 10000, 10)
        ).status == SubmissionStatus::DUPLICATE_ID
    );
    CHECK(
        engine.submit_limit_order(
            Order(1003, Side::SELL, 12000, 10)
        ).status == SubmissionStatus::DUPLICATE_ID
    );

    // 3. Insertion failures at all failure checkpoints:
    //    When add_active_order throws, reservation must NOT be committed,
    //    leaving the ID reusable and preserving the existing book.
    const InsertionFailurePoint failure_points[] = {
        InsertionFailurePoint::AFTER_LIST_INSERT,
        InsertionFailurePoint::AFTER_INDEX_INSERT,
        InsertionFailurePoint::AFTER_USED_ID_INSERT
    };

    OrderId fail_id = 2001;
    for (InsertionFailurePoint point : failure_points) {
        const std::size_t count_before =
            engine.order_book().order_count();
        CHECK(engine.order_book().contains(1001));

        engine.set_insertion_failure_for_test(point);

        bool exception_caught = false;
        try {
            engine.submit_limit_order(
                Order(fail_id, Side::BUY, 9500, 100)
            );
        } catch (const std::bad_alloc&) {
            exception_caught = true;
        }

        CHECK(exception_caught);

        // Incoming ID was not admitted; must NOT be accepted or used.
        CHECK(!engine.was_order_id_accepted(fail_id));
        CHECK(!engine.order_book().was_order_id_used(fail_id));
        CHECK(!engine.order_book().contains(fail_id));

        // Existing book is preserved.
        CHECK(engine.order_book().order_count() == count_before);
        CHECK(engine.order_book().contains(1001));
        CHECK(engine.order_book().validate_invariants());

        // ID remains available and can be successfully submitted now.
        const SubmissionResult retry_res =
            engine.submit_limit_order(
                Order(fail_id, Side::BUY, 9500, 100)
            );
        CHECK(retry_res.status == SubmissionStatus::ACCEPTED);
        CHECK(engine.was_order_id_accepted(fail_id));
        CHECK(engine.order_book().contains(fail_id));
        CHECK(engine.order_book().was_order_id_used(fail_id));

        ++fail_id;
    }

    // 4. Invalid order (e.g. cancelled order):
    //    add_active_order returns false, reservation must NOT be committed.
    Order cancelled(3001, Side::BUY, 9000, 100);
    CHECK(cancelled.cancel());
    const SubmissionResult inv_res =
        engine.submit_limit_order(cancelled);
    CHECK(inv_res.status == SubmissionStatus::INVALID_ORDER);
    CHECK(!engine.was_order_id_accepted(3001));
    CHECK(!engine.order_book().was_order_id_used(3001));

    // ID 3001 can now be submitted with a valid order.
    Order valid_3001(3001, Side::BUY, 9000, 100);
    CHECK(
        engine.submit_limit_order(valid_3001).status ==
        SubmissionStatus::ACCEPTED
    );
    CHECK(engine.was_order_id_accepted(3001));

    CHECK(engine.order_book().validate_invariants());
}

namespace {

struct ReferenceOrder {
    OrderId id;
    Side side;
    Price price;
    Quantity original_quantity;
    Quantity remaining_quantity;
    Quantity executed_quantity;

    OrderStatus status() const noexcept {
        if (remaining_quantity == 0) {
            return OrderStatus::FILLED;
        }
        if (executed_quantity > 0) {
            return OrderStatus::PARTIALLY_FILLED;
        }
        return OrderStatus::NEW;
    }
};

class ReferenceMatchingEngine {
private:
    std::vector<ReferenceOrder> active_orders_;
    std::vector<OrderId> accepted_order_ids_;
    std::vector<OrderId> used_limit_ids_;
    std::vector<Trade> trades_;

public:
    SubmissionResult submit_limit_order(const Order& order) {
        if (order.type() != OrderType::LIMIT ||
            order.status() != OrderStatus::NEW ||
            (order.side() != Side::BUY && order.side() != Side::SELL)) {
            return {
                SubmissionStatus::INVALID_ORDER,
                0,
                order.remaining_quantity()
            };
        }

        if (was_order_id_accepted(order.id()) || was_order_id_used(order.id())) {
            return {
                SubmissionStatus::DUPLICATE_ID,
                0,
                order.remaining_quantity()
            };
        }

        accepted_order_ids_.push_back(order.id());
        used_limit_ids_.push_back(order.id());

        Quantity executed_quantity = 0;
        Quantity remaining_quantity = order.remaining_quantity();

        while (remaining_quantity > 0) {
            int best_idx = -1;

            if (order.side() == Side::BUY) {
                for (int i = 0; i < static_cast<int>(active_orders_.size()); ++i) {
                    if (active_orders_[i].side == Side::SELL) {
                        if (best_idx == -1 || active_orders_[i].price < active_orders_[best_idx].price) {
                            best_idx = i;
                        }
                    }
                }

                if (best_idx == -1 || order.price_ticks() < active_orders_[best_idx].price) {
                    break;
                }
            } else {
                for (int i = 0; i < static_cast<int>(active_orders_.size()); ++i) {
                    if (active_orders_[i].side == Side::BUY) {
                        if (best_idx == -1 || active_orders_[i].price > active_orders_[best_idx].price) {
                            best_idx = i;
                        }
                    }
                }

                if (best_idx == -1 || order.price_ticks() > active_orders_[best_idx].price) {
                    break;
                }
            }

            ReferenceOrder& resting = active_orders_[best_idx];
            const Quantity executable = std::min(remaining_quantity, resting.remaining_quantity);
            const Price exec_price = resting.price;

            trades_.push_back(Trade{
                order.id(),
                resting.id,
                exec_price,
                executable
            });

            executed_quantity += executable;
            remaining_quantity -= executable;
            resting.remaining_quantity -= executable;
            resting.executed_quantity += executable;

            if (resting.remaining_quantity == 0) {
                active_orders_.erase(active_orders_.begin() + best_idx);
            }
        }

        if (remaining_quantity > 0) {
            active_orders_.push_back(ReferenceOrder{
                order.id(),
                order.side(),
                order.price_ticks(),
                order.original_quantity(),
                remaining_quantity,
                executed_quantity
            });
        }

        return {
            SubmissionStatus::ACCEPTED,
            executed_quantity,
            remaining_quantity
        };
    }

    SubmissionResult submit_market_order(const Order& order) {
        if (order.type() != OrderType::MARKET ||
            order.status() != OrderStatus::NEW ||
            (order.side() != Side::BUY && order.side() != Side::SELL)) {
            return {
                SubmissionStatus::INVALID_ORDER,
                0,
                order.remaining_quantity()
            };
        }

        if (was_order_id_accepted(order.id()) || was_order_id_used(order.id())) {
            return {
                SubmissionStatus::DUPLICATE_ID,
                0,
                order.remaining_quantity()
            };
        }

        accepted_order_ids_.push_back(order.id());

        Quantity executed_quantity = 0;
        Quantity remaining_quantity = order.remaining_quantity();

        while (remaining_quantity > 0) {
            int best_idx = -1;

            if (order.side() == Side::BUY) {
                for (int i = 0; i < static_cast<int>(active_orders_.size()); ++i) {
                    if (active_orders_[i].side == Side::SELL) {
                        if (best_idx == -1 || active_orders_[i].price < active_orders_[best_idx].price) {
                            best_idx = i;
                        }
                    }
                }

                if (best_idx == -1) {
                    break;
                }
            } else {
                for (int i = 0; i < static_cast<int>(active_orders_.size()); ++i) {
                    if (active_orders_[i].side == Side::BUY) {
                        if (best_idx == -1 || active_orders_[i].price > active_orders_[best_idx].price) {
                            best_idx = i;
                        }
                    }
                }

                if (best_idx == -1) {
                    break;
                }
            }

            ReferenceOrder& resting = active_orders_[best_idx];
            const Quantity executable = std::min(remaining_quantity, resting.remaining_quantity);
            const Price exec_price = resting.price;

            trades_.push_back(Trade{
                order.id(),
                resting.id,
                exec_price,
                executable
            });

            executed_quantity += executable;
            remaining_quantity -= executable;
            resting.remaining_quantity -= executable;
            resting.executed_quantity += executable;

            if (resting.remaining_quantity == 0) {
                active_orders_.erase(active_orders_.begin() + best_idx);
            }
        }

        return {
            SubmissionStatus::ACCEPTED,
            executed_quantity,
            remaining_quantity
        };
    }

    std::optional<Price> best_bid() const {
        std::optional<Price> best;
        for (const auto& ord : active_orders_) {
            if (ord.side == Side::BUY) {
                if (!best.has_value() || ord.price > *best) {
                    best = ord.price;
                }
            }
        }
        return best;
    }

    std::optional<Price> best_ask() const {
        std::optional<Price> best;
        for (const auto& ord : active_orders_) {
            if (ord.side == Side::SELL) {
                if (!best.has_value() || ord.price < *best) {
                    best = ord.price;
                }
            }
        }
        return best;
    }

    std::size_t order_count() const noexcept {
        return active_orders_.size();
    }

    bool contains(OrderId id) const noexcept {
        for (const auto& ord : active_orders_) {
            if (ord.id == id) {
                return true;
            }
        }
        return false;
    }

    const ReferenceOrder* find_order(OrderId id) const noexcept {
        for (const auto& ord : active_orders_) {
            if (ord.id == id) {
                return &ord;
            }
        }
        return nullptr;
    }

    bool was_order_id_accepted(OrderId id) const noexcept {
        for (OrderId aid : accepted_order_ids_) {
            if (aid == id) {
                return true;
            }
        }
        return false;
    }

    bool was_order_id_used(OrderId id) const noexcept {
        for (OrderId uid : used_limit_ids_) {
            if (uid == id) {
                return true;
            }
        }
        return false;
    }

    std::vector<OrderId> order_ids_at_price(Side side, Price price) const {
        std::vector<OrderId> result;
        for (const auto& ord : active_orders_) {
            if (ord.side == side && ord.price == price) {
                result.push_back(ord.id);
            }
        }
        return result;
    }

    const std::vector<Trade>& trades() const noexcept {
        return trades_;
    }
};

class DifferentialHarness {
private:
    MatchingEngine engine_;
    ReferenceMatchingEngine ref_;
    std::vector<OrderId> tracked_ids_;
    std::vector<std::pair<Side, Price>> tracked_levels_;

    void track_order(const Order& order) {
        if (std::find(tracked_ids_.begin(), tracked_ids_.end(), order.id()) == tracked_ids_.end()) {
            tracked_ids_.push_back(order.id());
        }
        if (order.type() == OrderType::LIMIT) {
            auto level = std::make_pair(order.side(), order.price_ticks());
            if (std::find(tracked_levels_.begin(), tracked_levels_.end(), level) == tracked_levels_.end()) {
                tracked_levels_.push_back(level);
            }
        }
    }

    void verify_state(const SubmissionResult& prod_res, const SubmissionResult& ref_res) {
        CHECK(engine_.order_book().validate_invariants());

        CHECK(prod_res.status == ref_res.status);
        CHECK(prod_res.executed_quantity == ref_res.executed_quantity);
        CHECK(prod_res.remaining_quantity == ref_res.remaining_quantity);

        CHECK(engine_.order_book().best_bid() == ref_.best_bid());
        CHECK(engine_.order_book().best_ask() == ref_.best_ask());

        CHECK(engine_.order_book().order_count() == ref_.order_count());

        const auto& prod_trades = engine_.trades();
        const auto& ref_trades = ref_.trades();
        CHECK(prod_trades.size() == ref_trades.size());
        for (std::size_t i = 0; i < prod_trades.size(); ++i) {
            CHECK(prod_trades[i].incoming_order_id == ref_trades[i].incoming_order_id);
            CHECK(prod_trades[i].resting_order_id == ref_trades[i].resting_order_id);
            CHECK(prod_trades[i].execution_price_ticks == ref_trades[i].execution_price_ticks);
            CHECK(prod_trades[i].executed_quantity == ref_trades[i].executed_quantity);
        }

        for (OrderId id : tracked_ids_) {
            CHECK(engine_.was_order_id_accepted(id) == ref_.was_order_id_accepted(id));
            CHECK(engine_.order_book().was_order_id_used(id) == ref_.was_order_id_used(id));
            CHECK(engine_.order_book().contains(id) == ref_.contains(id));

            if (ref_.contains(id)) {
                const Order* prod_ord = engine_.order_book().find_order(id);
                const ReferenceOrder* ref_ord = ref_.find_order(id);
                CHECK(prod_ord != nullptr);
                CHECK(ref_ord != nullptr);
                CHECK(prod_ord->remaining_quantity() == ref_ord->remaining_quantity);
                CHECK(prod_ord->executed_quantity() == ref_ord->executed_quantity);
                CHECK(prod_ord->price_ticks() == ref_ord->price);
                CHECK(prod_ord->side() == ref_ord->side);
                CHECK(prod_ord->status() == ref_ord->status());
            }
        }

        for (const auto& [side, price] : tracked_levels_) {
            CHECK(engine_.order_book().order_ids_at_price(side, price) ==
                  ref_.order_ids_at_price(side, price));
        }
    }

public:
	const MatchingEngine& engine() const noexcept {
        return engine_;
	}
    SubmissionResult submit_limit_order(const Order& order) {
        track_order(order);
        const SubmissionResult ref_res = ref_.submit_limit_order(order);
        const SubmissionResult prod_res = engine_.submit_limit_order(order);
        verify_state(prod_res, ref_res);
        return prod_res;
    }

    SubmissionResult submit_market_order(const Order& order) {
        track_order(order);
        const SubmissionResult ref_res = ref_.submit_market_order(order);
        const SubmissionResult prod_res = engine_.submit_market_order(order);
        verify_state(prod_res, ref_res);
        return prod_res;
    }
};

} // unnamed namespace

void test_differential_fifo_at_same_price() {
    DifferentialHarness harness;

    // BUY side FIFO: orders 1, 2, 3 at price 10000
    const auto r1 = harness.submit_limit_order(Order(1, Side::BUY, 10000, 100));
    CHECK(r1.status == SubmissionStatus::ACCEPTED);
    CHECK(r1.executed_quantity == 0);
    CHECK(r1.remaining_quantity == 100);

    const auto r2 = harness.submit_limit_order(Order(2, Side::BUY, 10000, 150));
    CHECK(r2.status == SubmissionStatus::ACCEPTED);
    CHECK(r2.executed_quantity == 0);
    CHECK(r2.remaining_quantity == 150);

    const auto r3 = harness.submit_limit_order(Order(3, Side::BUY, 10000, 200));
    CHECK(r3.status == SubmissionStatus::ACCEPTED);
    CHECK(r3.executed_quantity == 0);
    CHECK(r3.remaining_quantity == 200);

    // Initial FIFO queue verification: 1, 2, 3 in arrival order
    CHECK((harness.engine().order_book().order_ids_at_price(Side::BUY, 10000) ==
           std::vector<OrderId>{1, 2, 3}));

    // Partially fill queue: order 4 SELL 150 at 10000
    // Manually verified expectation: fills #1 (100) first, then takes 50 of #2
    const auto r4 = harness.submit_limit_order(Order(4, Side::SELL, 10000, 150));
    CHECK(r4.status == SubmissionStatus::ACCEPTED);
    CHECK(r4.executed_quantity == 150);
    CHECK(r4.remaining_quantity == 0);
    CHECK(harness.engine().trades().size() == 2);
    CHECK(harness.engine().trades()[0].incoming_order_id == 4);
    CHECK(harness.engine().trades()[0].resting_order_id == 1);
    CHECK(harness.engine().trades()[0].execution_price_ticks == 10000);
    CHECK(harness.engine().trades()[0].executed_quantity == 100);
    CHECK(harness.engine().trades()[1].incoming_order_id == 4);
    CHECK(harness.engine().trades()[1].resting_order_id == 2);
    CHECK(harness.engine().trades()[1].execution_price_ticks == 10000);
    CHECK(harness.engine().trades()[1].executed_quantity == 50);
    CHECK(!harness.engine().order_book().contains(1));
    CHECK(harness.engine().order_book().contains(2));
    CHECK(harness.engine().order_book().find_order(2)->remaining_quantity() == 100);
    CHECK((harness.engine().order_book().order_ids_at_price(Side::BUY, 10000) ==
           std::vector<OrderId>{2, 3}));

    // Fill remainder of #2 and part of #3: order 5 SELL 150 at 9900
    // Manually verified expectation: takes remaining 100 of #2, then takes 50 of #3
    const auto r5 = harness.submit_limit_order(Order(5, Side::SELL, 9900, 150));
    CHECK(r5.status == SubmissionStatus::ACCEPTED);
    CHECK(r5.executed_quantity == 150);
    CHECK(r5.remaining_quantity == 0);
    CHECK(harness.engine().trades().size() == 4);
    CHECK(harness.engine().trades()[2].incoming_order_id == 5);
    CHECK(harness.engine().trades()[2].resting_order_id == 2);
    CHECK(harness.engine().trades()[2].execution_price_ticks == 10000);
    CHECK(harness.engine().trades()[2].executed_quantity == 100);
    CHECK(harness.engine().trades()[3].incoming_order_id == 5);
    CHECK(harness.engine().trades()[3].resting_order_id == 3);
    CHECK(harness.engine().trades()[3].execution_price_ticks == 10000);
    CHECK(harness.engine().trades()[3].executed_quantity == 50);
    CHECK(!harness.engine().order_book().contains(2));
    CHECK(harness.engine().order_book().contains(3));
    CHECK(harness.engine().order_book().find_order(3)->remaining_quantity() == 150);
    CHECK((harness.engine().order_book().order_ids_at_price(Side::BUY, 10000) ==
           std::vector<OrderId>{3}));

    // SELL side FIFO: orders 6, 7, 8 at price 10500
    harness.submit_limit_order(Order(6, Side::SELL, 10500, 80));
    harness.submit_limit_order(Order(7, Side::SELL, 10500, 120));
    harness.submit_limit_order(Order(8, Side::SELL, 10500, 200));
    CHECK((harness.engine().order_book().order_ids_at_price(Side::SELL, 10500) ==
           std::vector<OrderId>{6, 7, 8}));

    // Partially fill ask queue: order 9 BUY 150 at 10500
    // Manually verified expectation: fills #6 (80) first, then takes 70 of #7 (rem 50)
    const auto r9 = harness.submit_limit_order(Order(9, Side::BUY, 10500, 150));
    CHECK(r9.status == SubmissionStatus::ACCEPTED);
    CHECK(r9.executed_quantity == 150);
    CHECK(r9.remaining_quantity == 0);
    CHECK(harness.engine().trades().size() == 6);
    CHECK(harness.engine().trades()[4].incoming_order_id == 9);
    CHECK(harness.engine().trades()[4].resting_order_id == 6);
    CHECK(harness.engine().trades()[4].execution_price_ticks == 10500);
    CHECK(harness.engine().trades()[4].executed_quantity == 80);
    CHECK(harness.engine().trades()[5].incoming_order_id == 9);
    CHECK(harness.engine().trades()[5].resting_order_id == 7);
    CHECK(harness.engine().trades()[5].execution_price_ticks == 10500);
    CHECK(harness.engine().trades()[5].executed_quantity == 70);
    CHECK(!harness.engine().order_book().contains(6));
    CHECK(harness.engine().order_book().contains(7));
    CHECK(harness.engine().order_book().find_order(7)->remaining_quantity() == 50);
    CHECK((harness.engine().order_book().order_ids_at_price(Side::SELL, 10500) ==
           std::vector<OrderId>{7, 8}));

    // Finish ask queue at 10500: order 10 BUY 250 at 10600
    // Manually verified expectation: fills #7 (50), fills #8 (200)
    const auto r10 = harness.submit_limit_order(Order(10, Side::BUY, 10600, 250));
    CHECK(r10.status == SubmissionStatus::ACCEPTED);
    CHECK(r10.executed_quantity == 250);
    CHECK(r10.remaining_quantity == 0);
    CHECK(harness.engine().trades().size() == 8);
    CHECK(harness.engine().trades()[6].incoming_order_id == 10);
    CHECK(harness.engine().trades()[6].resting_order_id == 7);
    CHECK(harness.engine().trades()[6].execution_price_ticks == 10500);
    CHECK(harness.engine().trades()[6].executed_quantity == 50);
    CHECK(harness.engine().trades()[7].incoming_order_id == 10);
    CHECK(harness.engine().trades()[7].resting_order_id == 8);
    CHECK(harness.engine().trades()[7].execution_price_ticks == 10500);
    CHECK(harness.engine().trades()[7].executed_quantity == 200);
    CHECK(!harness.engine().order_book().contains(7));
    CHECK(!harness.engine().order_book().contains(8));
    CHECK(harness.engine().order_book().order_ids_at_price(Side::SELL, 10500).empty());
}

void test_differential_multiple_price_levels() {
    DifferentialHarness harness;

    // Build ask ladder across multiple price levels
    harness.submit_limit_order(Order(1, Side::SELL, 10100, 50));
    harness.submit_limit_order(Order(2, Side::SELL, 10200, 100));
    harness.submit_limit_order(Order(3, Side::SELL, 10200, 50));
    harness.submit_limit_order(Order(4, Side::SELL, 10300, 200));
    harness.submit_limit_order(Order(5, Side::SELL, 10400, 300));

    // Build bid ladder across multiple price levels
    harness.submit_limit_order(Order(6, Side::BUY, 9900, 100));
    harness.submit_limit_order(Order(7, Side::BUY, 9800, 200));
    harness.submit_limit_order(Order(8, Side::BUY, 9700, 150));

    // Aggressive BUY sweeps multiple levels up to limit price 10350:
    // Manually verified expectation:
    // executes 400 units, leaves 20 resting, and matches resting IDs 1, 2, 3, 4 in sequence.
    const auto r9 = harness.submit_limit_order(Order(9, Side::BUY, 10350, 420));
    CHECK(r9.status == SubmissionStatus::ACCEPTED);
    CHECK(r9.executed_quantity == 400);
    CHECK(r9.remaining_quantity == 20);

    CHECK(harness.engine().trades().size() == 4);
    CHECK(harness.engine().trades()[0].incoming_order_id == 9);
    CHECK(harness.engine().trades()[0].resting_order_id == 1);
    CHECK(harness.engine().trades()[0].execution_price_ticks == 10100);
    CHECK(harness.engine().trades()[0].executed_quantity == 50);

    CHECK(harness.engine().trades()[1].incoming_order_id == 9);
    CHECK(harness.engine().trades()[1].resting_order_id == 2);
    CHECK(harness.engine().trades()[1].execution_price_ticks == 10200);
    CHECK(harness.engine().trades()[1].executed_quantity == 100);

    CHECK(harness.engine().trades()[2].incoming_order_id == 9);
    CHECK(harness.engine().trades()[2].resting_order_id == 3);
    CHECK(harness.engine().trades()[2].execution_price_ticks == 10200);
    CHECK(harness.engine().trades()[2].executed_quantity == 50);

    CHECK(harness.engine().trades()[3].incoming_order_id == 9);
    CHECK(harness.engine().trades()[3].resting_order_id == 4);
    CHECK(harness.engine().trades()[3].execution_price_ticks == 10300);
    CHECK(harness.engine().trades()[3].executed_quantity == 200);

    CHECK(!harness.engine().order_book().contains(1));
    CHECK(!harness.engine().order_book().contains(2));
    CHECK(!harness.engine().order_book().contains(3));
    CHECK(!harness.engine().order_book().contains(4));
    CHECK(harness.engine().order_book().contains(5));
    CHECK(harness.engine().order_book().contains(9));
    CHECK(harness.engine().order_book().find_order(9)->remaining_quantity() == 20);
    CHECK(harness.engine().order_book().find_order(9)->price_ticks() == 10350);
    CHECK(*harness.engine().order_book().best_bid() == 10350);
    CHECK(*harness.engine().order_book().best_ask() == 10400);

    // Aggressive SELL sweeps through 10350, 9900, 9800 down to limit price 9750:
    // Manually verified expectation:
    // executes 320 units (20 of #9, 100 of #6, 200 of #7), leaves 30 resting at 9750
    const auto r10 = harness.submit_limit_order(Order(10, Side::SELL, 9750, 350));
    CHECK(r10.status == SubmissionStatus::ACCEPTED);
    CHECK(r10.executed_quantity == 320);
    CHECK(r10.remaining_quantity == 30);

    CHECK(harness.engine().trades().size() == 7);
    CHECK(harness.engine().trades()[4].incoming_order_id == 10);
    CHECK(harness.engine().trades()[4].resting_order_id == 9);
    CHECK(harness.engine().trades()[4].execution_price_ticks == 10350);
    CHECK(harness.engine().trades()[4].executed_quantity == 20);

    CHECK(harness.engine().trades()[5].incoming_order_id == 10);
    CHECK(harness.engine().trades()[5].resting_order_id == 6);
    CHECK(harness.engine().trades()[5].execution_price_ticks == 9900);
    CHECK(harness.engine().trades()[5].executed_quantity == 100);

    CHECK(harness.engine().trades()[6].incoming_order_id == 10);
    CHECK(harness.engine().trades()[6].resting_order_id == 7);
    CHECK(harness.engine().trades()[6].execution_price_ticks == 9800);
    CHECK(harness.engine().trades()[6].executed_quantity == 200);

    CHECK(!harness.engine().order_book().contains(9));
    CHECK(!harness.engine().order_book().contains(6));
    CHECK(!harness.engine().order_book().contains(7));
    CHECK(harness.engine().order_book().contains(10));
    CHECK(harness.engine().order_book().find_order(10)->remaining_quantity() == 30);
    CHECK(harness.engine().order_book().find_order(10)->price_ticks() == 9750);
    CHECK(harness.engine().order_book().contains(8));
    CHECK(harness.engine().order_book().find_order(8)->remaining_quantity() == 150);
    CHECK(*harness.engine().order_book().best_bid() == 9700);
    CHECK(*harness.engine().order_book().best_ask() == 9750);
}

void test_differential_partial_fills_and_continuation() {
    DifferentialHarness harness;

    // Resting order experiences repeated partial fills
    const auto r1 = harness.submit_limit_order(Order(1, Side::BUY, 10000, 500));
    CHECK(r1.status == SubmissionStatus::ACCEPTED);
    CHECK(r1.executed_quantity == 0);
    CHECK(r1.remaining_quantity == 500);

    // Partial fill 1: 120 units
    const auto r2 = harness.submit_limit_order(Order(2, Side::SELL, 10000, 120));
    CHECK(r2.status == SubmissionStatus::ACCEPTED);
    CHECK(r2.executed_quantity == 120);
    CHECK(r2.remaining_quantity == 0);
    CHECK(harness.engine().order_book().find_order(1)->remaining_quantity() == 380);

    // Partial fill 2: 80 units
    const auto r3 = harness.submit_limit_order(Order(3, Side::SELL, 10000, 80));
    CHECK(r3.status == SubmissionStatus::ACCEPTED);
    CHECK(r3.executed_quantity == 80);
    CHECK(r3.remaining_quantity == 0);
    CHECK(harness.engine().order_book().find_order(1)->remaining_quantity() == 300);

    // Add another order behind #1 at same price, and another at lower price
    harness.submit_limit_order(Order(4, Side::BUY, 10000, 200));
    harness.submit_limit_order(Order(5, Side::BUY, 9900, 300));

    // Fill remaining 300 of #1 and partially fill #4 (100 units)
    const auto r6 = harness.submit_limit_order(Order(6, Side::SELL, 9950, 400));
    CHECK(r6.status == SubmissionStatus::ACCEPTED);
    CHECK(r6.executed_quantity == 400);
    CHECK(r6.remaining_quantity == 0);
    CHECK(!harness.engine().order_book().contains(1));
    CHECK(harness.engine().order_book().contains(4));
    CHECK(harness.engine().order_book().find_order(4)->remaining_quantity() == 100);

    // Fill remaining 100 of #4 and partially fill #5 (150 units)
    const auto r7 = harness.submit_limit_order(Order(7, Side::SELL, 9900, 250));
    CHECK(r7.status == SubmissionStatus::ACCEPTED);
    CHECK(r7.executed_quantity == 250);
    CHECK(r7.remaining_quantity == 0);
    CHECK(!harness.engine().order_book().contains(4));
    CHECK(harness.engine().order_book().contains(5));
    CHECK(harness.engine().order_book().find_order(5)->remaining_quantity() == 150);

    // Join level 9900 behind #5
    harness.submit_limit_order(Order(8, Side::BUY, 9900, 100));

    // Fill remaining 150 of #5 and partially fill #8 (50 units)
    const auto r9 = harness.submit_limit_order(Order(9, Side::SELL, 9900, 200));
    CHECK(r9.status == SubmissionStatus::ACCEPTED);
    CHECK(r9.executed_quantity == 200);
    CHECK(r9.remaining_quantity == 0);
    CHECK(!harness.engine().order_book().contains(5));
    CHECK(harness.engine().order_book().contains(8));
    CHECK(harness.engine().order_book().find_order(8)->remaining_quantity() == 50);
}

void test_differential_buy_and_sell_order_flows() {
    DifferentialHarness harness;

    // Interleaved market-making flow
    harness.submit_limit_order(Order(1, Side::BUY, 10000, 100));
    harness.submit_limit_order(Order(2, Side::SELL, 10200, 100));
    harness.submit_limit_order(Order(3, Side::BUY, 10050, 50));
    harness.submit_limit_order(Order(4, Side::SELL, 10150, 75));
    harness.submit_limit_order(Order(5, Side::BUY, 10100, 60));
    harness.submit_limit_order(Order(6, Side::SELL, 10120, 80));

    // Order inside the spread
    harness.submit_limit_order(Order(7, Side::BUY, 10110, 40));

    // Crossing BUY: takes 80 at 10120 (#6) and 30 at 10150 (#4)
    const auto r8 = harness.submit_limit_order(Order(8, Side::BUY, 10150, 110));
    CHECK(r8.status == SubmissionStatus::ACCEPTED);
    CHECK(r8.executed_quantity == 110);
    CHECK(r8.remaining_quantity == 0);
    CHECK(!harness.engine().order_book().contains(6));
    CHECK(harness.engine().order_book().find_order(4)->remaining_quantity() == 45);

    // Crossing SELL: takes 40 at 10110 (#7), 60 at 10100 (#5), 50 at 10050 (#3)
    const auto r9 = harness.submit_limit_order(Order(9, Side::SELL, 10020, 150));
    CHECK(r9.status == SubmissionStatus::ACCEPTED);
    CHECK(r9.executed_quantity == 150);
    CHECK(r9.remaining_quantity == 0);
    CHECK(!harness.engine().order_book().contains(7));
    CHECK(!harness.engine().order_book().contains(5));
    CHECK(!harness.engine().order_book().contains(3));

    // Additional crossing orders establishing new levels
    harness.submit_limit_order(Order(10, Side::BUY, 10080, 90));
    const auto r11 = harness.submit_limit_order(Order(11, Side::SELL, 10070, 120));
    CHECK(r11.status == SubmissionStatus::ACCEPTED);
    CHECK(r11.executed_quantity == 90);
    CHECK(r11.remaining_quantity == 30);
    CHECK(*harness.engine().order_book().best_bid() == 10000);
    CHECK(*harness.engine().order_book().best_ask() == 10070);
}

void test_differential_market_insufficient_liquidity() {
    DifferentialHarness harness;

    // Market orders against empty book: 0 executed, remainder cancelled
    const auto r1 = harness.submit_market_order(Order(1, Side::BUY, OrderType::MARKET, 0, 100));
    CHECK(r1.status == SubmissionStatus::ACCEPTED);
    CHECK(r1.executed_quantity == 0);
    CHECK(r1.remaining_quantity == 100);
    const auto acc1 = harness.engine().market_order_accounting(r1);
    CHECK(acc1.executed_quantity == 0);
    CHECK(acc1.cancelled_quantity == 100);

    const auto r2 = harness.submit_market_order(Order(2, Side::SELL, OrderType::MARKET, 0, 50));
    CHECK(r2.status == SubmissionStatus::ACCEPTED);
    CHECK(r2.executed_quantity == 0);
    CHECK(r2.remaining_quantity == 50);
    const auto acc2 = harness.engine().market_order_accounting(r2);
    CHECK(acc2.executed_quantity == 0);
    CHECK(acc2.cancelled_quantity == 50);

    // Provide partial ask liquidity: 40 + 60 = 100 units
    harness.submit_limit_order(Order(3, Side::SELL, 10500, 40));
    harness.submit_limit_order(Order(4, Side::SELL, 10600, 60));

    // Market BUY for 250: consumes all 100 available asks, cancels remaining 150
    const auto r5 = harness.submit_market_order(Order(5, Side::BUY, OrderType::MARKET, 0, 250));
    CHECK(r5.status == SubmissionStatus::ACCEPTED);
    CHECK(r5.executed_quantity == 100);
    CHECK(r5.remaining_quantity == 150);
    const auto acc5 = harness.engine().market_order_accounting(r5);
    CHECK(acc5.executed_quantity == 100);
    CHECK(acc5.cancelled_quantity == 150);
    CHECK(!harness.engine().order_book().best_ask().has_value());
    CHECK(!harness.engine().order_book().contains(3));
    CHECK(!harness.engine().order_book().contains(4));
    CHECK(!harness.engine().order_book().contains(5));

    // Provide partial bid liquidity: 30 + 70 = 100 units
    harness.submit_limit_order(Order(6, Side::BUY, 10000, 30));
    harness.submit_limit_order(Order(7, Side::BUY, 9900, 70));

    // Market SELL for 160: consumes all 100 available bids, cancels remaining 60
    const auto r8 = harness.submit_market_order(Order(8, Side::SELL, OrderType::MARKET, 0, 160));
    CHECK(r8.status == SubmissionStatus::ACCEPTED);
    CHECK(r8.executed_quantity == 100);
    CHECK(r8.remaining_quantity == 60);
    const auto acc8 = harness.engine().market_order_accounting(r8);
    CHECK(acc8.executed_quantity == 100);
    CHECK(acc8.cancelled_quantity == 60);
    CHECK(!harness.engine().order_book().best_bid().has_value());
    CHECK(!harness.engine().order_book().contains(6));
    CHECK(!harness.engine().order_book().contains(7));
    CHECK(!harness.engine().order_book().contains(8));
    CHECK(harness.engine().order_book().order_count() == 0);
}

void test_differential_duplicate_ids_after_execution() {
    DifferentialHarness harness;

    // Active resting limit order: duplicate submissions rejected
    const auto r1 = harness.submit_limit_order(Order(1, Side::BUY, 10000, 100));
    CHECK(r1.status == SubmissionStatus::ACCEPTED);

    const auto dup1_a = harness.submit_limit_order(Order(1, Side::BUY, 10000, 100));
    CHECK(dup1_a.status == SubmissionStatus::DUPLICATE_ID);
    CHECK(dup1_a.executed_quantity == 0);
    CHECK(dup1_a.remaining_quantity == 100);

    const auto dup1_b = harness.submit_limit_order(Order(1, Side::SELL, 11000, 50));
    CHECK(dup1_b.status == SubmissionStatus::DUPLICATE_ID);
    CHECK(dup1_b.executed_quantity == 0);
    CHECK(dup1_b.remaining_quantity == 50);

    const auto dup1_c = harness.submit_market_order(Order(1, Side::BUY, OrderType::MARKET, 0, 50));
    CHECK(dup1_c.status == SubmissionStatus::DUPLICATE_ID);
    CHECK(dup1_c.executed_quantity == 0);
    CHECK(dup1_c.remaining_quantity == 50);

    // Fully fill order 1 with order 2
    const auto r2 = harness.submit_limit_order(Order(2, Side::SELL, 10000, 100));
    CHECK(r2.status == SubmissionStatus::ACCEPTED);
    CHECK(r2.executed_quantity == 100);
    CHECK(!harness.engine().order_book().contains(1));
    CHECK(!harness.engine().order_book().contains(2));

    // After full fill and removal from book: duplicate submissions must still be rejected
    const auto dup1_d = harness.submit_limit_order(Order(1, Side::BUY, 10000, 100));
    CHECK(dup1_d.status == SubmissionStatus::DUPLICATE_ID);

    const auto dup1_e = harness.submit_market_order(Order(1, Side::BUY, OrderType::MARKET, 0, 50));
    CHECK(dup1_e.status == SubmissionStatus::DUPLICATE_ID);

    const auto dup2_a = harness.submit_limit_order(Order(2, Side::SELL, 10000, 100));
    CHECK(dup2_a.status == SubmissionStatus::DUPLICATE_ID);

    const auto dup2_b = harness.submit_market_order(Order(2, Side::SELL, OrderType::MARKET, 0, 50));
    CHECK(dup2_b.status == SubmissionStatus::DUPLICATE_ID);

    // Unfilled market order: duplicate rejected
    const auto r3 = harness.submit_market_order(Order(3, Side::BUY, OrderType::MARKET, 0, 50));
    CHECK(r3.status == SubmissionStatus::ACCEPTED);
    CHECK(r3.executed_quantity == 0);
    CHECK(r3.remaining_quantity == 50);

    const auto dup3_a = harness.submit_limit_order(Order(3, Side::BUY, 10000, 50));
    CHECK(dup3_a.status == SubmissionStatus::DUPLICATE_ID);

    const auto dup3_b = harness.submit_market_order(Order(3, Side::BUY, OrderType::MARKET, 0, 50));
    CHECK(dup3_b.status == SubmissionStatus::DUPLICATE_ID);

    // Partially filled market order: duplicate rejected
    const auto r4 = harness.submit_limit_order(Order(4, Side::SELL, 10500, 40));
    CHECK(r4.status == SubmissionStatus::ACCEPTED);

    const auto r5 = harness.submit_market_order(Order(5, Side::BUY, OrderType::MARKET, 0, 100));
    CHECK(r5.status == SubmissionStatus::ACCEPTED);
    CHECK(r5.executed_quantity == 40);
    CHECK(r5.remaining_quantity == 60);

    const auto dup4 = harness.submit_limit_order(Order(4, Side::SELL, 10500, 40));
    CHECK(dup4.status == SubmissionStatus::DUPLICATE_ID);

    const auto dup5_a = harness.submit_limit_order(Order(5, Side::BUY, 10000, 100));
    CHECK(dup5_a.status == SubmissionStatus::DUPLICATE_ID);

    const auto dup5_b = harness.submit_market_order(Order(5, Side::BUY, OrderType::MARKET, 0, 100));
    CHECK(dup5_b.status == SubmissionStatus::DUPLICATE_ID);
}

void test_differential_mixed_limit_and_market_sequence() {
    DifferentialHarness harness;

    // Initial limit orders on both sides
    harness.submit_limit_order(Order(1, Side::BUY, 10000, 100));
    harness.submit_limit_order(Order(2, Side::BUY, 9900, 150));
    harness.submit_limit_order(Order(3, Side::BUY, 9800, 200));

    harness.submit_limit_order(Order(4, Side::SELL, 10200, 80));
    harness.submit_limit_order(Order(5, Side::SELL, 10300, 120));
    harness.submit_limit_order(Order(6, Side::SELL, 10400, 160));

    // Market BUY sweeps all 80 of #4 and 70 of #5
    const auto r7 = harness.submit_market_order(Order(7, Side::BUY, OrderType::MARKET, 0, 150));
    CHECK(r7.status == SubmissionStatus::ACCEPTED);
    CHECK(r7.executed_quantity == 150);
    CHECK(r7.remaining_quantity == 0);
    CHECK(!harness.engine().order_book().contains(4));
    CHECK(harness.engine().order_book().find_order(5)->remaining_quantity() == 50);

    // Limit order establishing a tighter ask
    harness.submit_limit_order(Order(8, Side::SELL, 10250, 90));
    CHECK(*harness.engine().order_book().best_ask() == 10250);

    // Market SELL sweeps all 100 of #1 and 80 of #2
    const auto r9 = harness.submit_market_order(Order(9, Side::SELL, OrderType::MARKET, 0, 180));
    CHECK(r9.status == SubmissionStatus::ACCEPTED);
    CHECK(r9.executed_quantity == 180);
    CHECK(r9.remaining_quantity == 0);
    CHECK(!harness.engine().order_book().contains(1));
    CHECK(harness.engine().order_book().find_order(2)->remaining_quantity() == 70);

    // Aggressive limit BUY crossing at 10300: takes 90 of #8 and 30 of #5
    const auto r10 = harness.submit_limit_order(Order(10, Side::BUY, 10300, 120));
    CHECK(r10.status == SubmissionStatus::ACCEPTED);
    CHECK(r10.executed_quantity == 120);
    CHECK(r10.remaining_quantity == 0);
    CHECK(!harness.engine().order_book().contains(8));
    CHECK(harness.engine().order_book().find_order(5)->remaining_quantity() == 20);

    // Market BUY takes remaining 20 of #5 and 80 of #6
    const auto r11 = harness.submit_market_order(Order(11, Side::BUY, OrderType::MARKET, 0, 100));
    CHECK(r11.status == SubmissionStatus::ACCEPTED);
    CHECK(r11.executed_quantity == 100);
    CHECK(r11.remaining_quantity == 0);
    CHECK(!harness.engine().order_book().contains(5));
    CHECK(harness.engine().order_book().find_order(6)->remaining_quantity() == 80);
    CHECK(*harness.engine().order_book().best_bid() == 9900);
    CHECK(*harness.engine().order_book().best_ask() == 10400);
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
    test_limit_match_both_orders_fully_filled();
    test_limit_match_only_incoming_fully_filled();
    test_limit_match_only_resting_fully_filled();
    test_limit_match_both_orders_partially_filled();
    test_limit_order_id_reservation_hardening();
    test_differential_fifo_at_same_price();
    test_differential_multiple_price_levels();
    test_differential_partial_fills_and_continuation();
    test_differential_buy_and_sell_order_flows();
    test_differential_market_insufficient_liquidity();
    test_differential_duplicate_ids_after_execution();
    test_differential_mixed_limit_and_market_sequence();

    std::cout << "MatchingEngine tests passed\n";
    return 0;
}
