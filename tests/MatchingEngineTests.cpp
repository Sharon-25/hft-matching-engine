#include <cassert>
#include <iostream>
#include <new>
#include <type_traits>

#include "matching_engine/MatchingEngine.hpp"

using namespace matching_engine;

void test_engine_starts_with_empty_book() {
    MatchingEngine engine;

    const OrderBook& book = engine.order_book();

    assert(book.order_count() == 0);
    assert(!book.best_bid().has_value());
    assert(!book.best_ask().has_value());
}

void test_engine_is_not_copyable_or_movable() {
    static_assert(!std::is_copy_constructible_v<MatchingEngine>);
    static_assert(!std::is_copy_assignable_v<MatchingEngine>);
    static_assert(!std::is_move_constructible_v<MatchingEngine>);
    static_assert(!std::is_move_assignable_v<MatchingEngine>);
}

void test_accept_new_order_id() {
    MatchingEngine engine;

    Order order(100, Side::BUY, 11000, 100);

    assert(
        engine.submit_limit_order(order).status ==
        SubmissionStatus::ACCEPTED
    );

    assert(engine.was_order_id_accepted(100));
}

void test_reject_duplicate_order_id() {
    MatchingEngine engine;

    Order first(200, Side::BUY, 11000, 100);
    Order duplicate(200, Side::SELL, 11200, 200);

    assert(
        engine.submit_limit_order(first).status ==
        SubmissionStatus::ACCEPTED
    );

    assert(
        engine.submit_limit_order(duplicate).status ==
        SubmissionStatus::DUPLICATE_ID
    );
}

void test_invalid_order_does_not_consume_id() {
    MatchingEngine engine;

    Order invalid(
        300,
        static_cast<Side>(99),
        11000,
        100
    );

    assert(
        engine.submit_limit_order(invalid).status ==
        SubmissionStatus::INVALID_ORDER
    );

    assert(!engine.was_order_id_accepted(300));

    Order valid(300, Side::BUY, 11000, 100);

    assert(
        engine.submit_limit_order(valid).status ==
        SubmissionStatus::ACCEPTED
    );
}

void test_buy_limit_matching() {
    MatchingEngine engine;

    assert(engine.submit_limit_order(
        Order(1, Side::SELL, 11000, 100)
    ).status == SubmissionStatus::ACCEPTED);

    const SubmissionResult result =
        engine.submit_limit_order(
            Order(2, Side::BUY, 11500, 100)
        );

    assert(result.status == SubmissionStatus::ACCEPTED);
    assert(result.executed_quantity == 100);
    assert(result.remaining_quantity == 0);

    assert(engine.order_book().order_count() == 0);
    assert(!engine.order_book().best_ask().has_value());
    assert(!engine.order_book().best_bid().has_value());

    assert(engine.was_order_id_accepted(1));
    assert(engine.was_order_id_accepted(2));
}

void test_sell_limit_matching() {
    MatchingEngine engine;

    assert(engine.submit_limit_order(
        Order(10, Side::BUY, 11000, 200)
    ).status == SubmissionStatus::ACCEPTED);

    const SubmissionResult result =
        engine.submit_limit_order(
            Order(20, Side::SELL, 10900, 150)
        );

    assert(result.status == SubmissionStatus::ACCEPTED);
    assert(result.executed_quantity == 150);
    assert(result.remaining_quantity == 0);

    const Order* best =
        engine.order_book().best_order(Side::BUY);

    assert(best != nullptr);
    assert(best->id() == 10);
    assert(best->remaining_quantity() == 50);
    assert(best->status() == OrderStatus::PARTIALLY_FILLED);
}

void test_partial_incoming_order_rests() {
    MatchingEngine engine;

    assert(engine.submit_limit_order(
        Order(100, Side::SELL, 11000, 100)
    ).status == SubmissionStatus::ACCEPTED);

    const SubmissionResult result =
        engine.submit_limit_order(
            Order(200, Side::BUY, 11500, 250)
        );

    assert(result.status == SubmissionStatus::ACCEPTED);
    assert(result.executed_quantity == 100);
    assert(result.remaining_quantity == 150);

    const Order* best =
        engine.order_book().best_order(Side::BUY);

    assert(best != nullptr);
    assert(best->id() == 200);
    assert(best->remaining_quantity() == 150);
    assert(best->status() == OrderStatus::PARTIALLY_FILLED);

    assert(engine.order_book().order_count() == 1);
    assert(*engine.order_book().best_bid() == 11500);
}

void test_failed_submission_preserves_id_availability() {
    MatchingEngine engine;

    Order invalid(
        500,
        static_cast<Side>(99),
        11000,
        100
    );

    const SubmissionResult rejected =
        engine.submit_limit_order(invalid);

    assert(
        rejected.status ==
        SubmissionStatus::INVALID_ORDER
    );

    assert(!engine.was_order_id_accepted(500));

    const SubmissionResult accepted =
        engine.submit_limit_order(
            Order(500, Side::BUY, 11000, 100)
        );

    assert(
        accepted.status ==
        SubmissionStatus::ACCEPTED
    );

    assert(engine.was_order_id_accepted(500));
    assert(engine.order_book().contains(500));
}

void test_buy_stops_at_limit_price() {
    MatchingEngine engine;

    assert(engine.submit_limit_order(
        Order(1, Side::SELL, 11000, 100)
    ).status == SubmissionStatus::ACCEPTED);

    assert(engine.submit_limit_order(
        Order(2, Side::SELL, 11200, 100)
    ).status == SubmissionStatus::ACCEPTED);

    const SubmissionResult result =
        engine.submit_limit_order(
            Order(3, Side::BUY, 11100, 250)
        );

    assert(result.status == SubmissionStatus::ACCEPTED);
    assert(result.executed_quantity == 100);
    assert(result.remaining_quantity == 150);

    const OrderBook& book = engine.order_book();

    assert(book.order_count() == 2);

    // Order 1 was fully executed.
    assert(!book.contains(1));

    // Order 2 cannot match above the BUY limit.
    assert(book.contains(2));
    assert(*book.best_ask() == 11200);

    // The incoming remainder rests at its own limit.
    assert(book.contains(3));
    assert(*book.best_bid() == 11100);

    const Order* buy = book.best_order(Side::BUY);
    const Order* sell = book.best_order(Side::SELL);

    assert(buy != nullptr);
    assert(sell != nullptr);

    assert(buy->id() == 3);
    assert(buy->remaining_quantity() == 150);
    assert(buy->status() == OrderStatus::PARTIALLY_FILLED);

    assert(sell->id() == 2);
    assert(sell->remaining_quantity() == 100);
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

    assert(result.status == SubmissionStatus::ACCEPTED);
    assert(result.executed_quantity == 100);
    assert(result.remaining_quantity == 0);

    assert(!engine.order_book().contains(2));
    assert(engine.was_order_id_accepted(2));
    assert(engine.order_book().was_order_id_used(2));

    const SubmissionResult duplicate =
        engine.submit_limit_order(
            Order(2, Side::BUY, 12000, 50)
        );

    assert(
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

    assert(result.status == SubmissionStatus::ACCEPTED);
    assert(result.executed_quantity == 100);
    assert(result.remaining_quantity == 150);

    const Order* incoming =
        engine.order_book().find_order(20);

    assert(incoming != nullptr);
    assert(incoming->remaining_quantity() == 150);
    assert(
        incoming->status() ==
        OrderStatus::PARTIALLY_FILLED
    );

    assert(engine.order_book().order_count() == 1);
}

void test_rejected_order_preserves_book() {
    MatchingEngine engine;

    const SubmissionResult first =
        engine.submit_limit_order(
            Order(10, Side::BUY, 11000, 100)
        );

    assert(first.status == SubmissionStatus::ACCEPTED);

    const std::size_t before_count =
        engine.order_book().order_count();

    const auto before_bid =
        engine.order_book().best_bid();

    const SubmissionResult duplicate =
        engine.submit_limit_order(
            Order(10, Side::SELL, 10500, 200)
        );

    assert(
        duplicate.status ==
        SubmissionStatus::DUPLICATE_ID
    );

    assert(
        engine.order_book().order_count() ==
        before_count
    );

    assert(engine.order_book().best_bid() == before_bid);
    assert(!engine.order_book().best_ask().has_value());

    const Order* original =
        engine.order_book().find_order(10);

    assert(original != nullptr);
    assert(original->remaining_quantity() == 100);
    assert(original->side() == Side::BUY);
}

void test_allocation_failure_preserves_book() {
    MatchingEngine engine;

    const SubmissionResult initial =
        engine.submit_limit_order(
            Order(1, Side::SELL, 11000, 100)
        );

    assert(initial.status == SubmissionStatus::ACCEPTED);

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

    assert(caught);

    // The original resting order must remain unchanged.
    const Order* original =
        engine.order_book().find_order(1);

    assert(original != nullptr);
    assert(original->remaining_quantity() == 100);

    // The failed incoming order must not be stored.
    assert(!engine.order_book().contains(2));

    // Its session ID must be available for reuse.
    assert(!engine.was_order_id_accepted(2));
    assert(!engine.order_book().was_order_id_used(2));

    assert(engine.order_book().order_count() == 1);
    assert(*engine.order_book().best_ask() == 11000);

    // A subsequent valid submission must still work.
    const SubmissionResult retry =
        engine.submit_limit_order(
            Order(2, Side::BUY, 11500, 200)
        );

    assert(retry.status == SubmissionStatus::ACCEPTED);
    assert(retry.executed_quantity == 100);
    assert(retry.remaining_quantity == 100);
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

        assert(
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

        assert(caught);

        const OrderBook& book = engine.order_book();

        assert(book.order_count() == 1);
        assert(book.contains(1));
        assert(!book.contains(2));

        const Order* original = book.find_order(1);

        assert(original != nullptr);
        assert(original->remaining_quantity() == 100);

        assert(!engine.was_order_id_accepted(2));
        assert(!book.was_order_id_used(2));

        // Retry with the same ID after rollback.
        const SubmissionResult retry =
            engine.submit_limit_order(
                Order(2, Side::BUY, 11500, 200)
            );

        assert(retry.status == SubmissionStatus::ACCEPTED);
        assert(retry.executed_quantity == 100);
        assert(retry.remaining_quantity == 100);
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

    assert(trade.incoming_order_id == 3);
    assert(trade.resting_order_id == 1);
    assert(trade.execution_price_ticks == 11000);
    assert(trade.executed_quantity == 100);
}

void test_initial_trade_history_is_empty() {
    MatchingEngine engine;

    assert(engine.trades().empty());
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

    assert(result.status == SubmissionStatus::ACCEPTED);
    assert(result.executed_quantity == 250);
    assert(result.remaining_quantity == 0);

    const auto& trades = engine.trades();

    assert(trades.size() == 3);

    assert(trades[0].incoming_order_id == 4);
    assert(trades[0].resting_order_id == 1);
    assert(trades[0].execution_price_ticks == 11000);
    assert(trades[0].executed_quantity == 100);

    assert(trades[1].incoming_order_id == 4);
    assert(trades[1].resting_order_id == 2);
    assert(trades[1].execution_price_ticks == 11200);
    assert(trades[1].executed_quantity == 100);

    assert(trades[2].incoming_order_id == 4);
    assert(trades[2].resting_order_id == 3);
    assert(trades[2].execution_price_ticks == 11400);
    assert(trades[2].executed_quantity == 50);

    const Order* remaining =
        engine.order_book().find_order(3);

    assert(remaining != nullptr);
    assert(remaining->remaining_quantity() == 50);
}

void test_matchable_order_count() {
    OrderBook book;

    assert(book.add_order(
        Order(1, Side::SELL, 11000, 100)
    ));

    assert(book.add_order(
        Order(2, Side::SELL, 11200, 100)
    ));

    assert(book.add_order(
        Order(3, Side::SELL, 11400, 100)
    ));

    assert(
        book.count_matchable_orders(
            Side::BUY, 11500, 250
        ) == 3
    );

    assert(
        book.count_matchable_orders(
            Side::BUY, 11100, 250
        ) == 1
    );

    assert(
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

    assert(resting.status == SubmissionStatus::ACCEPTED);

    const SubmissionResult incoming =
        engine.submit_limit_order(
            Order(20, Side::BUY, 11500, 150)
        );

    assert(incoming.status == SubmissionStatus::ACCEPTED);
    assert(incoming.executed_quantity == 100);
    assert(incoming.remaining_quantity == 50);

    const OrderBook& book = engine.order_book();

    assert(!book.contains(10));
    assert(book.contains(20));

    const Order* remainder = book.find_order(20);

    assert(remainder != nullptr);
    assert(remainder->remaining_quantity() == 50);
    assert(
        remainder->status() ==
        OrderStatus::PARTIALLY_FILLED
    );

    assert(engine.trades().size() == 1);

    const Trade& trade = engine.trades()[0];

    assert(trade.incoming_order_id == 20);
    assert(trade.resting_order_id == 10);
    assert(trade.execution_price_ticks == 11000);
    assert(trade.executed_quantity == 100);
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

    assert(result.executed_quantity == 150);
    assert(result.remaining_quantity == 0);

    const auto& trades = engine.trades();

    assert(trades.size() == 2);

    assert(trades[0].resting_order_id == 1);
    assert(trades[0].executed_quantity == 100);

    assert(trades[1].resting_order_id == 2);
    assert(trades[1].executed_quantity == 50);

    const Order* remaining =
        engine.order_book().find_order(2);

    assert(remaining != nullptr);
    assert(remaining->remaining_quantity() == 50);
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

    assert(result.status == SubmissionStatus::ACCEPTED);

    Quantity total_executed = 0;

    for (const Trade& trade : engine.trades()) {
        assert(trade.incoming_order_id !=
               trade.resting_order_id);

        assert(trade.executed_quantity > 0);
        assert(trade.execution_price_ticks > 0);

        total_executed += trade.executed_quantity;
    }

    assert(total_executed == result.executed_quantity);
    assert(total_executed == 200);

    const Order* remaining =
        engine.order_book().find_order(2);

    assert(remaining != nullptr);
    assert(remaining->remaining_quantity() == 50);
}

void test_book_invariants_after_submission_sequence() {
    MatchingEngine engine;

    auto submit_and_validate =
        [&](const Order& order) {
            const SubmissionResult result =
                engine.submit_limit_order(order);

            assert(
                result.status ==
                SubmissionStatus::ACCEPTED
            );

            assert(
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
    assert(engine.order_book().validate_invariants());
}

void test_index_consistency_after_full_execution() {
    MatchingEngine engine;

    engine.submit_limit_order(
        Order(10, Side::SELL, 11000, 100)
    );

    engine.submit_limit_order(
        Order(20, Side::SELL, 11000, 100)
    );

    assert(engine.order_book().order_count() == 2);
    assert(engine.order_book().validate_invariants());

    const SubmissionResult result =
        engine.submit_limit_order(
            Order(30, Side::BUY, 11500, 200)
        );

    assert(result.executed_quantity == 200);
    assert(result.remaining_quantity == 0);

    assert(engine.order_book().order_count() == 0);

    assert(!engine.order_book().contains(10));
    assert(!engine.order_book().contains(20));
    assert(!engine.order_book().contains(30));

    assert(engine.order_book().was_order_id_used(10));
    assert(engine.order_book().was_order_id_used(20));
    assert(engine.order_book().was_order_id_used(30));

    assert(engine.order_book().validate_invariants());
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

    assert(result.status == SubmissionStatus::ACCEPTED);

    Quantity recorded_quantity = 0;

    for (std::size_t i = trades_before;
         i < engine.trades().size(); ++i) {
        const Trade& trade = engine.trades()[i];

        assert(trade.incoming_order_id == 3);
        recorded_quantity += trade.executed_quantity;
    }

    assert(recorded_quantity == result.executed_quantity);
    assert(result.executed_quantity +
           result.remaining_quantity == 200);

    assert(engine.order_book().validate_invariants());
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

    std::cout << "MatchingEngine tests passed\n";
    return 0;
}
