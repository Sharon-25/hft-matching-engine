#include <cassert>
#include <iostream>
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

    std::cout << "MatchingEngine tests passed\n";
    return 0;
}
