#include <cassert>
#include <iostream>
#include <type_traits>
#include <vector>

#include "matching_engine/OrderBook.hpp"

using namespace matching_engine;

static_assert(!std::is_copy_constructible_v<OrderBook>);
static_assert(!std::is_copy_assignable_v<OrderBook>);
static_assert(!std::is_move_constructible_v<OrderBook>);
static_assert(!std::is_move_assignable_v<OrderBook>);

void test_empty_book() {
    OrderBook book;

    assert(!book.best_bid().has_value());
    assert(!book.best_ask().has_value());
}

void test_best_prices() {
    OrderBook book;

    assert(book.add_order(Order(1, Side::BUY, 11000, 100)));
    assert(book.add_order(Order(2, Side::BUY, 10800, 200)));
    assert(book.add_order(Order(3, Side::SELL, 11500, 100)));
    assert(book.add_order(Order(4, Side::SELL, 11200, 200)));

    assert(book.best_bid().has_value());
    assert(book.best_ask().has_value());

    assert(*book.best_bid() == 11000);
    assert(*book.best_ask() == 11200);
}

void test_order_index() {
    OrderBook book;

    assert(book.add_order(
        Order(101, Side::BUY, 10000, 100)
    ));

    assert(book.add_order(
        Order(102, Side::SELL, 10500, 200)
    ));

    assert(book.contains(101));
    assert(book.contains(102));
    assert(!book.contains(999));

    assert(book.order_count() == 2);
}

void test_duplicate_order_rejection() {
    OrderBook book;

    assert(book.add_order(
        Order(201, Side::BUY, 10000, 100)
    ));

    // Same ID, different price and side.
    assert(!book.add_order(
        Order(201, Side::SELL, 11000, 200)
    ));

    assert(book.order_count() == 1);

    assert(*book.best_bid() == 10000);
    assert(!book.best_ask().has_value());
}

void test_same_price_insertion() {
    OrderBook book;

    assert(book.add_order(
        Order(301, Side::BUY, 11000, 100)
    ));

    assert(book.add_order(
        Order(302, Side::BUY, 11000, 200)
    ));

    assert(book.add_order(
        Order(303, Side::BUY, 11000, 300)
    ));

    assert(book.order_count() == 3);
    assert(*book.best_bid() == 11000);
}

void test_cancel_existing_order() {
    OrderBook book;

    assert(book.add_order(
        Order(10, Side::BUY, 11000, 100)
    ));

    assert(book.add_order(
        Order(14, Side::BUY, 11000, 200)
    ));

    assert(book.add_order(
        Order(21, Side::BUY, 11000, 300)
    ));

    assert(book.cancel_order(14));

    assert(!book.contains(14));
    assert(book.contains(10));
    assert(book.contains(21));

    assert(book.order_count() == 2);
    assert(*book.best_bid() == 11000);
}

void test_cancel_best_price_level() {
    OrderBook book;

    assert(book.add_order(
        Order(10, Side::BUY, 11000, 100)
    ));

    assert(book.add_order(
        Order(20, Side::BUY, 10800, 100)
    ));

    assert(book.cancel_order(10));

    assert(book.order_count() == 1);
    assert(*book.best_bid() == 10800);
}

void test_cancel_nonexistent_order() {
    OrderBook book;

    assert(!book.cancel_order(999));
    assert(book.order_count() == 0);
}

void test_cancel_last_ask() {
    OrderBook book;

    assert(book.add_order(
        Order(50, Side::SELL, 11200, 100)
    ));

    assert(book.cancel_order(50));

    assert(!book.best_ask().has_value());
    assert(book.order_count() == 0);
}

void test_reject_partially_filled_order() {
    OrderBook book;

    Order order(401, Side::BUY, 10000, 100);

    assert(order.execute(40));

    assert(order.status() ==
           OrderStatus::PARTIALLY_FILLED);

    assert(!book.add_order(order));

    assert(book.order_count() == 0);
    assert(!book.best_bid().has_value());
}

void test_reject_cancelled_order() {
    OrderBook book;

    Order order(402, Side::SELL, 11000, 100);

    assert(order.cancel());

    assert(!book.add_order(order));

    assert(book.order_count() == 0);
    assert(!book.best_ask().has_value());
}

void test_reject_invalid_side() {
    OrderBook book;

    Side invalid_side = static_cast<Side>(99);

    Order order(
        403,
        invalid_side,
        10000,
        100
    );

    assert(!book.add_order(order));

    assert(book.order_count() == 0);
    assert(!book.best_bid().has_value());
    assert(!book.best_ask().has_value());
}

void test_fifo_ordering() {
    OrderBook book;

    assert(book.add_order(
        Order(301, Side::BUY, 11000, 100)
    ));

    assert(book.add_order(
        Order(302, Side::BUY, 11000, 200)
    ));

    assert(book.add_order(
        Order(303, Side::BUY, 11000, 300)
    ));

    std::vector<OrderId> expected = {
        301, 302, 303
    };

    auto actual = book.order_ids_at_price(
        Side::BUY,
        11000
    );

    assert(actual == expected);
}

void test_fifo_after_cancellation() {
    OrderBook book;

    assert(book.add_order(
        Order(301, Side::BUY, 11000, 100)
    ));

    assert(book.add_order(
        Order(302, Side::BUY, 11000, 200)
    ));

    assert(book.add_order(
        Order(303, Side::BUY, 11000, 300)
    ));

    assert(book.cancel_order(302));

    std::vector<OrderId> expected = {
        301, 303
    };

    assert(
        book.order_ids_at_price(Side::BUY, 11000)
        == expected
    );

    assert(!book.contains(302));
    assert(book.order_count() == 2);
}

void test_empty_price_level_removal() {
    OrderBook book;

    assert(book.add_order(
        Order(401, Side::BUY, 11000, 100)
    ));

    assert(book.add_order(
        Order(402, Side::BUY, 10800, 100)
    ));

    assert(book.cancel_order(401));

    assert(*book.best_bid() == 10800);

    assert(
        book.order_ids_at_price(Side::BUY, 11000)
        .empty()
    );

    assert(book.cancel_order(402));

    assert(!book.best_bid().has_value());
    assert(book.order_count() == 0);
}

void test_fifo_after_cancel_and_insert() {
    OrderBook book;

    assert(book.add_order(
        Order(10, Side::BUY, 11000, 100)
    ));

    assert(book.add_order(
        Order(20, Side::BUY, 11000, 100)
    ));

    assert(book.add_order(
        Order(30, Side::BUY, 11000, 100)
    ));

    assert(book.add_order(
        Order(40, Side::BUY, 10800, 100)
    ));

    assert(book.cancel_order(20));

    assert(book.add_order(
        Order(50, Side::BUY, 11000, 100)
    ));

    assert((book.order_ids_at_price(
        Side::BUY, 11000
    ) == std::vector<OrderId>{10, 30, 50}));

    assert(book.cancel_order(10));
    assert(book.cancel_order(30));
    assert(book.cancel_order(50));

    assert(*book.best_bid() == 10800);
    assert(book.contains(40));
    assert(book.order_count() == 1);
}

void test_reject_reused_id_after_cancellation() {
    OrderBook book;

    assert(book.add_order(
        Order(100, Side::BUY, 11000, 100)
    ));

    assert(book.contains(100));
    assert(book.was_order_id_used(100));

    assert(book.cancel_order(100));

    assert(!book.contains(100));
    assert(book.was_order_id_used(100));

    // Reusing the cancelled ID must fail.
    assert(!book.add_order(
        Order(100, Side::SELL, 11200, 200)
    ));

    assert(book.order_count() == 0);
    assert(!book.best_bid().has_value());
    assert(!book.best_ask().has_value());
}

void test_rejected_order_does_not_consume_id() {
    OrderBook book;

    Order invalid(
        200,
        static_cast<Side>(99),
        10000,
        100
    );

    assert(!book.add_order(invalid));

    assert(!book.was_order_id_used(200));

    // The same ID can be used for a valid order.
    assert(book.add_order(
        Order(200, Side::BUY, 10000, 100)
    ));

    assert(book.contains(200));
    assert(book.was_order_id_used(200));
    assert(book.order_count() == 1);
}

void test_independent_order_id_namespaces() {
    OrderBook session_one;
    OrderBook session_two;

    assert(session_one.add_order(
        Order(300, Side::BUY, 10000, 100)
    ));

    assert(session_two.add_order(
        Order(300, Side::SELL, 11000, 200)
    ));

    assert(session_one.was_order_id_used(300));
    assert(session_two.was_order_id_used(300));

    assert(session_one.order_count() == 1);
    assert(session_two.order_count() == 1);
}

void test_order_book_invariants() {
    OrderBook book;

    // Insert three BUY orders.
    assert(book.add_order(
        Order(601, Side::BUY, 11000, 100)
    ));

    assert(book.add_order(
        Order(602, Side::BUY, 11000, 200)
    ));

    assert(book.add_order(
        Order(603, Side::BUY, 10800, 150)
    ));

    // Verify active index and session ID tracking.
    assert(book.order_count() == 3);

    assert(book.contains(601));
    assert(book.contains(602));
    assert(book.contains(603));

    assert(book.was_order_id_used(601));
    assert(book.was_order_id_used(602));
    assert(book.was_order_id_used(603));

    // Verify FIFO at the best price.
    assert((book.order_ids_at_price(
        Side::BUY, 11000
    ) == std::vector<OrderId>{601, 602}));

    assert(*book.best_bid() == 11000);

    // Cancel the first order.
    assert(book.cancel_order(601));

    assert(!book.contains(601));
    assert(book.was_order_id_used(601));

    assert((book.order_ids_at_price(
        Side::BUY, 11000
    ) == std::vector<OrderId>{602}));

    // Cancel the final order at price 11000.
    assert(book.cancel_order(602));

    // The next price level becomes the best bid.
    assert(*book.best_bid() == 10800);

    assert(book.order_count() == 1);

    // Reusing a previously accepted ID must fail.
    assert(!book.add_order(
        Order(601, Side::SELL, 11200, 100)
    ));

    assert(!book.best_ask().has_value());

    // Cancel the final active order.
    assert(book.cancel_order(603));

    assert(book.order_count() == 0);
    assert(!book.best_bid().has_value());
    assert(!book.best_ask().has_value());

    // Historical IDs remain reserved.
    assert(book.was_order_id_used(601));
    assert(book.was_order_id_used(602));
    assert(book.was_order_id_used(603));
}

int main() {
    test_empty_book();
    test_best_prices();
    test_order_index();
    test_duplicate_order_rejection();
    test_same_price_insertion();
    test_cancel_existing_order();
    test_cancel_best_price_level();
    test_cancel_nonexistent_order();
    test_cancel_last_ask();
    test_reject_partially_filled_order();
    test_reject_cancelled_order();
    test_reject_invalid_side();
    test_fifo_ordering();
    test_fifo_after_cancellation();
    test_empty_price_level_removal();
    test_fifo_after_cancel_and_insert();
    test_reject_reused_id_after_cancellation();
    test_rejected_order_does_not_consume_id();
    test_independent_order_id_namespaces();
    test_order_book_invariants();

    std::cout << "All OrderBook tests passed.\n";
    return 0;
}
