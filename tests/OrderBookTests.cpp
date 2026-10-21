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
#include <stdexcept>
#include <type_traits>
#include <vector>

#include "matching_engine/OrderBook.hpp"
#include "matching_engine/Trade.hpp"

using namespace matching_engine;

static_assert(!std::is_copy_constructible_v<OrderBook>);
static_assert(!std::is_copy_assignable_v<OrderBook>);
static_assert(!std::is_move_constructible_v<OrderBook>);
static_assert(!std::is_move_assignable_v<OrderBook>);

static_assert(std::is_nothrow_move_constructible_v<Trade>);
static_assert(std::is_nothrow_destructible_v<Trade>);

void test_empty_book() {
    OrderBook book;

    CHECK(!book.best_bid().has_value());
    CHECK(!book.best_ask().has_value());
}

void test_best_prices() {
    OrderBook book;

    CHECK(book.add_order(Order(1, Side::BUY, 11000, 100)));
    CHECK(book.add_order(Order(2, Side::BUY, 10800, 200)));
    CHECK(book.add_order(Order(3, Side::SELL, 11500, 100)));
    CHECK(book.add_order(Order(4, Side::SELL, 11200, 200)));

    CHECK(book.best_bid().has_value());
    CHECK(book.best_ask().has_value());

    CHECK(*book.best_bid() == 11000);
    CHECK(*book.best_ask() == 11200);
}

void test_order_index() {
    OrderBook book;

    CHECK(book.add_order(
        Order(101, Side::BUY, 10000, 100)
    ));

    CHECK(book.add_order(
        Order(102, Side::SELL, 10500, 200)
    ));

    CHECK(book.contains(101));
    CHECK(book.contains(102));
    CHECK(!book.contains(999));

    CHECK(book.order_count() == 2);
}

void test_duplicate_order_rejection() {
    OrderBook book;

    CHECK(book.add_order(
        Order(201, Side::BUY, 10000, 100)
    ));

    // Same ID, different price and side.
    CHECK(!book.add_order(
        Order(201, Side::SELL, 11000, 200)
    ));

    CHECK(book.order_count() == 1);

    CHECK(*book.best_bid() == 10000);
    CHECK(!book.best_ask().has_value());
}

void test_same_price_insertion() {
    OrderBook book;

    CHECK(book.add_order(
        Order(301, Side::BUY, 11000, 100)
    ));

    CHECK(book.add_order(
        Order(302, Side::BUY, 11000, 200)
    ));

    CHECK(book.add_order(
        Order(303, Side::BUY, 11000, 300)
    ));

    CHECK(book.order_count() == 3);
    CHECK(*book.best_bid() == 11000);
}

void test_cancel_existing_order() {
    OrderBook book;

    CHECK(book.add_order(
        Order(10, Side::BUY, 11000, 100)
    ));

    CHECK(book.add_order(
        Order(14, Side::BUY, 11000, 200)
    ));

    CHECK(book.add_order(
        Order(21, Side::BUY, 11000, 300)
    ));

    CHECK(book.cancel_order(14));

    CHECK(!book.contains(14));
    CHECK(book.contains(10));
    CHECK(book.contains(21));

    CHECK(book.order_count() == 2);
    CHECK(*book.best_bid() == 11000);
}

void test_cancel_best_price_level() {
    OrderBook book;

    CHECK(book.add_order(
        Order(10, Side::BUY, 11000, 100)
    ));

    CHECK(book.add_order(
        Order(20, Side::BUY, 10800, 100)
    ));

    CHECK(book.cancel_order(10));

    CHECK(book.order_count() == 1);
    CHECK(*book.best_bid() == 10800);
}

void test_cancel_nonexistent_order() {
    OrderBook book;

    CHECK(!book.cancel_order(999));
    CHECK(book.order_count() == 0);
}

void test_cancel_last_ask() {
    OrderBook book;

    CHECK(book.add_order(
        Order(50, Side::SELL, 11200, 100)
    ));

    CHECK(book.cancel_order(50));

    CHECK(!book.best_ask().has_value());
    CHECK(book.order_count() == 0);
}

void test_reject_partially_filled_order() {
    OrderBook book;

    Order order(401, Side::BUY, 10000, 100);

    CHECK(order.execute(40));

    CHECK(order.status() ==
           OrderStatus::PARTIALLY_FILLED);

    CHECK(!book.add_order(order));

    CHECK(book.order_count() == 0);
    CHECK(!book.best_bid().has_value());
}

void test_reject_cancelled_order() {
    OrderBook book;

    Order order(402, Side::SELL, 11000, 100);

    CHECK(order.cancel());

    CHECK(!book.add_order(order));

    CHECK(book.order_count() == 0);
    CHECK(!book.best_ask().has_value());
}

void test_reject_invalid_side() {
    bool threw = false;

    try {
        Order invalid(
            100,
            static_cast<Side>(99),
            11000,
            100
        );
    }
    catch (const std::invalid_argument&) {
        threw = true;
    }

    CHECK(threw);
}

void test_fifo_ordering() {
    OrderBook book;

    CHECK(book.add_order(
        Order(301, Side::BUY, 11000, 100)
    ));

    CHECK(book.add_order(
        Order(302, Side::BUY, 11000, 200)
    ));

    CHECK(book.add_order(
        Order(303, Side::BUY, 11000, 300)
    ));

    std::vector<OrderId> expected = {
        301, 302, 303
    };

    auto actual = book.order_ids_at_price(
        Side::BUY,
        11000
    );

    CHECK(actual == expected);
}

void test_fifo_after_cancellation() {
    OrderBook book;

    CHECK(book.add_order(
        Order(301, Side::BUY, 11000, 100)
    ));

    CHECK(book.add_order(
        Order(302, Side::BUY, 11000, 200)
    ));

    CHECK(book.add_order(
        Order(303, Side::BUY, 11000, 300)
    ));

    CHECK(book.cancel_order(302));

    std::vector<OrderId> expected = {
        301, 303
    };

    CHECK(
        book.order_ids_at_price(Side::BUY, 11000)
        == expected
    );

    CHECK(!book.contains(302));
    CHECK(book.order_count() == 2);
}

void test_empty_price_level_removal() {
    OrderBook book;

    CHECK(book.add_order(
        Order(401, Side::BUY, 11000, 100)
    ));

    CHECK(book.add_order(
        Order(402, Side::BUY, 10800, 100)
    ));

    CHECK(book.cancel_order(401));

    CHECK(*book.best_bid() == 10800);

    CHECK(
        book.order_ids_at_price(Side::BUY, 11000)
        .empty()
    );

    CHECK(book.cancel_order(402));

    CHECK(!book.best_bid().has_value());
    CHECK(book.order_count() == 0);
}

void test_fifo_after_cancel_and_insert() {
    OrderBook book;

    CHECK(book.add_order(
        Order(10, Side::BUY, 11000, 100)
    ));

    CHECK(book.add_order(
        Order(20, Side::BUY, 11000, 100)
    ));

    CHECK(book.add_order(
        Order(30, Side::BUY, 11000, 100)
    ));

    CHECK(book.add_order(
        Order(40, Side::BUY, 10800, 100)
    ));

    CHECK(book.cancel_order(20));

    CHECK(book.add_order(
        Order(50, Side::BUY, 11000, 100)
    ));

    CHECK((book.order_ids_at_price(
        Side::BUY, 11000
    ) == std::vector<OrderId>{10, 30, 50}));

    CHECK(book.cancel_order(10));
    CHECK(book.cancel_order(30));
    CHECK(book.cancel_order(50));

    CHECK(*book.best_bid() == 10800);
    CHECK(book.contains(40));
    CHECK(book.order_count() == 1);
}

void test_reject_reused_id_after_cancellation() {
    OrderBook book;

    CHECK(book.add_order(
        Order(100, Side::BUY, 11000, 100)
    ));

    CHECK(book.contains(100));
    CHECK(book.was_order_id_used(100));

    CHECK(book.cancel_order(100));

    CHECK(!book.contains(100));
    CHECK(book.was_order_id_used(100));

    // Reusing the cancelled ID must fail.
    CHECK(!book.add_order(
        Order(100, Side::SELL, 11200, 200)
    ));

    CHECK(book.order_count() == 0);
    CHECK(!book.best_bid().has_value());
    CHECK(!book.best_ask().has_value());
}

void test_rejected_order_does_not_consume_id() {
    OrderBook book;

    bool threw = false;

    try {
        Order invalid(
            200,
            static_cast<Side>(99),
            11000,
            100
        );
    }
    catch (const std::invalid_argument&) {
        threw = true;
    }

    CHECK(threw);

    // Failed construction must not affect the book.
    CHECK(book.order_count() == 0);
    CHECK(!book.contains(200));
    CHECK(!book.was_order_id_used(200));

    // The same ID must remain available.
    Order valid(200, Side::BUY, 11000, 100);

    CHECK(book.add_order(valid));

    CHECK(book.contains(200));
    CHECK(book.was_order_id_used(200));
    CHECK(book.order_count() == 1);
    CHECK(book.validate_invariants());
}

void test_independent_order_id_namespaces() {
    OrderBook session_one;
    OrderBook session_two;

    CHECK(session_one.add_order(
        Order(300, Side::BUY, 10000, 100)
    ));

    CHECK(session_two.add_order(
        Order(300, Side::SELL, 11000, 200)
    ));

    CHECK(session_one.was_order_id_used(300));
    CHECK(session_two.was_order_id_used(300));

    CHECK(session_one.order_count() == 1);
    CHECK(session_two.order_count() == 1);
}

void test_order_book_invariants() {
    OrderBook book;

    // Insert three BUY orders.
    CHECK(book.add_order(
        Order(601, Side::BUY, 11000, 100)
    ));

    CHECK(book.add_order(
        Order(602, Side::BUY, 11000, 200)
    ));

    CHECK(book.add_order(
        Order(603, Side::BUY, 10800, 150)
    ));

    // Verify active index and session ID tracking.
    CHECK(book.order_count() == 3);

    CHECK(book.contains(601));
    CHECK(book.contains(602));
    CHECK(book.contains(603));

    CHECK(book.was_order_id_used(601));
    CHECK(book.was_order_id_used(602));
    CHECK(book.was_order_id_used(603));

    // Verify FIFO at the best price.
    CHECK((book.order_ids_at_price(
        Side::BUY, 11000
    ) == std::vector<OrderId>{601, 602}));

    CHECK(*book.best_bid() == 11000);

    // Cancel the first order.
    CHECK(book.cancel_order(601));

    CHECK(!book.contains(601));
    CHECK(book.was_order_id_used(601));

    CHECK((book.order_ids_at_price(
        Side::BUY, 11000
    ) == std::vector<OrderId>{602}));

    // Cancel the final order at price 11000.
    CHECK(book.cancel_order(602));

    // The next price level becomes the best bid.
    CHECK(*book.best_bid() == 10800);

    CHECK(book.order_count() == 1);

    // Reusing a previously accepted ID must fail.
    CHECK(!book.add_order(
        Order(601, Side::SELL, 11200, 100)
    ));

    CHECK(!book.best_ask().has_value());

    // Cancel the final active order.
    CHECK(book.cancel_order(603));

    CHECK(book.order_count() == 0);
    CHECK(!book.best_bid().has_value());
    CHECK(!book.best_ask().has_value());

    // Historical IDs remain reserved.
    CHECK(book.was_order_id_used(601));
    CHECK(book.was_order_id_used(602));
    CHECK(book.was_order_id_used(603));
}

void test_best_order_access() {
    OrderBook book;

    CHECK(book.best_order(Side::BUY) == nullptr);
    CHECK(book.best_order(Side::SELL) == nullptr);

    CHECK(book.add_order(
        Order(1, Side::BUY, 11000, 100)
    ));

    CHECK(book.add_order(
        Order(2, Side::BUY, 11000, 200)
    ));

    CHECK(book.add_order(
        Order(3, Side::BUY, 10800, 150)
    ));

    CHECK(book.add_order(
        Order(4, Side::SELL, 11500, 100)
    ));

    const Order* best_buy =
        book.best_order(Side::BUY);

    CHECK(best_buy != nullptr);
    CHECK(best_buy->id() == 1);
    CHECK(best_buy->price_ticks() == 11000);
    CHECK(best_buy->remaining_quantity() == 100);

    const Order* best_sell =
        book.best_order(Side::SELL);

    CHECK(best_sell != nullptr);
    CHECK(best_sell->id() == 4);

    // Cancel the first BUY order.
    CHECK(book.cancel_order(1));

    // Query again; the old pointer must not be reused.
    best_buy = book.best_order(Side::BUY);

    CHECK(best_buy != nullptr);
    CHECK(best_buy->id() == 2);

    CHECK(book.cancel_order(2));

    best_buy = book.best_order(Side::BUY);

    CHECK(best_buy != nullptr);
    CHECK(best_buy->id() == 3);
    CHECK(best_buy->price_ticks() == 10800);
}

void test_partial_execution_preserves_fifo() {
    OrderBook book;

    CHECK(book.add_order(
        Order(1, Side::SELL, 11000, 100)
    ));

    CHECK(book.add_order(
        Order(2, Side::SELL, 11000, 200)
    ));

    CHECK(book.execute_best_order(Side::SELL, 40));

    const Order* best = book.best_order(Side::SELL);

    CHECK(best != nullptr);
    CHECK(best->id() == 1);
    CHECK(best->remaining_quantity() == 60);
    CHECK(best->status() == OrderStatus::PARTIALLY_FILLED);

    CHECK(book.order_count() == 2);

    CHECK((book.order_ids_at_price(
        Side::SELL, 11000
    ) == std::vector<OrderId>{1, 2}));
}

void test_full_execution_removes_order() {
    OrderBook book;

    CHECK(book.add_order(
        Order(10, Side::BUY, 11000, 100)
    ));

    CHECK(book.add_order(
        Order(20, Side::BUY, 10800, 200)
    ));

    CHECK(book.execute_best_order(Side::BUY, 100));

    CHECK(!book.contains(10));
    CHECK(book.was_order_id_used(10));

    CHECK(book.order_count() == 1);

    const Order* best = book.best_order(Side::BUY);

    CHECK(best != nullptr);
    CHECK(best->id() == 20);
    CHECK(*book.best_bid() == 10800);
}

void test_invalid_execution_does_not_modify_book() {
    OrderBook book;

    CHECK(!book.execute_best_order(Side::BUY, 10));

    CHECK(book.add_order(
        Order(30, Side::SELL, 11500, 100)
    ));

    CHECK(!book.execute_best_order(Side::SELL, 0));
    CHECK(!book.execute_best_order(Side::SELL, 101));

    CHECK(!book.execute_best_order(
        static_cast<Side>(99), 10
    ));

    const Order* best = book.best_order(Side::SELL);

    CHECK(best != nullptr);
    CHECK(best->remaining_quantity() == 100);
    CHECK(best->status() == OrderStatus::NEW);
    CHECK(book.order_count() == 1);
}

void test_filled_order_id_cannot_be_reused() {
    OrderBook book;

    CHECK(book.add_order(
        Order(40, Side::BUY, 10000, 50)
    ));

    CHECK(book.execute_best_order(Side::BUY, 50));

    CHECK(!book.contains(40));
    CHECK(book.was_order_id_used(40));

    CHECK(!book.add_order(
        Order(40, Side::SELL, 11000, 100)
    ));

    CHECK(book.order_count() == 0);
    CHECK(!book.best_bid().has_value());
}

void test_market_order_cannot_rest() {
    OrderBook book;

    Order market(
        100,
        Side::BUY,
        OrderType::MARKET,
        0,
        500
    );

    CHECK(!book.add_order(market));
    CHECK(book.order_count() == 0);
    CHECK(!book.contains(100));
    CHECK(!book.was_order_id_used(100));
    CHECK(book.validate_invariants());
}

void test_execute_best_order_full_fill_removes_level_and_index() {
    OrderBook book;

    CHECK(book.add_order(Order(10, Side::BUY, 10000, 100)));
    CHECK(book.add_order(Order(20, Side::SELL, 10500, 150)));
    CHECK(book.validate_invariants());

    // Full fill of best bid
    CHECK(book.execute_best_order(Side::BUY, 100));
    CHECK(!book.contains(10));
    CHECK(book.was_order_id_used(10));
    CHECK(!book.best_bid().has_value());
    CHECK(book.order_count() == 1);
    CHECK(book.validate_invariants());

    // Full fill of best ask
    CHECK(book.execute_best_order(Side::SELL, 150));
    CHECK(!book.contains(20));
    CHECK(book.was_order_id_used(20));
    CHECK(!book.best_ask().has_value());
    CHECK(book.order_count() == 0);
    CHECK(book.validate_invariants());
}

void test_execute_best_order_partial_fill_retains_order_and_index() {
    OrderBook book;

    CHECK(book.add_order(Order(30, Side::BUY, 10000, 100)));
    CHECK(book.add_order(Order(40, Side::SELL, 10500, 200)));
    CHECK(book.validate_invariants());

    // Partial fill of best bid
    CHECK(book.execute_best_order(Side::BUY, 40));
    CHECK(book.contains(30));
    CHECK(book.order_count() == 2);
    const Order* bid = book.best_order(Side::BUY);
    CHECK(bid != nullptr);
    CHECK(bid->remaining_quantity() == 60);
    CHECK(bid->status() == OrderStatus::PARTIALLY_FILLED);
    CHECK(book.validate_invariants());

    // Partial fill of best ask
    CHECK(book.execute_best_order(Side::SELL, 70));
    CHECK(book.contains(40));
    CHECK(book.order_count() == 2);
    const Order* ask = book.best_order(Side::SELL);
    CHECK(ask != nullptr);
    CHECK(ask->remaining_quantity() == 130);
    CHECK(ask->status() == OrderStatus::PARTIALLY_FILLED);
    CHECK(book.validate_invariants());
}

void test_execute_best_order_fifo_same_price() {
    OrderBook book;

    CHECK(book.add_order(Order(1, Side::BUY, 10000, 100)));
    CHECK(book.add_order(Order(2, Side::BUY, 10000, 150)));
    CHECK(book.add_order(Order(3, Side::BUY, 10000, 200)));
    CHECK(book.order_count() == 3);
    CHECK(book.validate_invariants());

    // First execute matches Order 1 fully
    CHECK(book.execute_best_order(Side::BUY, 100));
    CHECK(!book.contains(1));
    CHECK(book.contains(2));
    CHECK(book.contains(3));
    CHECK(book.best_order(Side::BUY)->id() == 2);
    CHECK(book.best_order(Side::BUY)->remaining_quantity() == 150);
    CHECK(book.order_count() == 2);
    CHECK(book.validate_invariants());

    // Second execute partially matches Order 2
    CHECK(book.execute_best_order(Side::BUY, 50));
    CHECK(book.contains(2));
    CHECK(book.best_order(Side::BUY)->id() == 2);
    CHECK(book.best_order(Side::BUY)->remaining_quantity() == 100);
    CHECK(book.order_count() == 2);
    CHECK(book.validate_invariants());

    // Finish Order 2
    CHECK(book.execute_best_order(Side::BUY, 100));
    CHECK(!book.contains(2));
    CHECK(book.contains(3));
    CHECK(book.best_order(Side::BUY)->id() == 3);
    CHECK(book.best_order(Side::BUY)->remaining_quantity() == 200);
    CHECK(book.order_count() == 1);
    CHECK(book.validate_invariants());
}

void test_execute_best_order_invalid_quantity_leaves_book_unchanged() {
    OrderBook book;

    CHECK(book.add_order(Order(50, Side::BUY, 10000, 100)));
    CHECK(book.add_order(Order(60, Side::SELL, 10500, 100)));
    CHECK(book.validate_invariants());

    // Zero quantity
    CHECK(!book.execute_best_order(Side::BUY, 0));
    CHECK(!book.execute_best_order(Side::SELL, 0));

    // Quantity greater than available
    CHECK(!book.execute_best_order(Side::BUY, 101));
    CHECK(!book.execute_best_order(Side::SELL, 150));

    // Execution on empty side
    OrderBook empty_book;
    CHECK(!empty_book.execute_best_order(Side::BUY, 10));
    CHECK(!empty_book.execute_best_order(Side::SELL, 10));

    // Verify original orders are completely unchanged
    CHECK(book.order_count() == 2);
    const Order* bid = book.best_order(Side::BUY);
    CHECK(bid != nullptr && bid->remaining_quantity() == 100 && bid->status() == OrderStatus::NEW);
    const Order* ask = book.best_order(Side::SELL);
    CHECK(ask != nullptr && ask->remaining_quantity() == 100 && ask->status() == OrderStatus::NEW);
    CHECK(book.validate_invariants());
}

void test_execute_best_order_subsequent_matching_after_full_fill() {
    OrderBook book;

    CHECK(book.add_order(Order(101, Side::BUY, 10200, 100)));
    CHECK(book.add_order(Order(102, Side::BUY, 10100, 200)));
    CHECK(book.add_order(Order(201, Side::SELL, 10300, 100)));
    CHECK(book.add_order(Order(202, Side::SELL, 10400, 250)));
    CHECK(book.validate_invariants());

    // Full fill best bid (101 at 10200)
    CHECK(book.execute_best_order(Side::BUY, 100));
    CHECK(!book.contains(101));
    CHECK(*book.best_bid() == 10100);
    CHECK(book.best_order(Side::BUY)->id() == 102);
    CHECK(book.validate_invariants());

    // Subsequent match on new best bid (102 at 10100)
    CHECK(book.execute_best_order(Side::BUY, 75));
    CHECK(book.contains(102));
    CHECK(book.best_order(Side::BUY)->remaining_quantity() == 125);
    CHECK(book.validate_invariants());

    // Full fill best ask (201 at 10300)
    CHECK(book.execute_best_order(Side::SELL, 100));
    CHECK(!book.contains(201));
    CHECK(*book.best_ask() == 10400);
    CHECK(book.best_order(Side::SELL)->id() == 202);
    CHECK(book.validate_invariants());

    // Subsequent match on new best ask (202 at 10400)
    CHECK(book.execute_best_order(Side::SELL, 50));
    CHECK(book.contains(202));
    CHECK(book.best_order(Side::SELL)->remaining_quantity() == 200);
    CHECK(book.validate_invariants());
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
    test_best_order_access();
    test_partial_execution_preserves_fifo();
    test_full_execution_removes_order();
    test_invalid_execution_does_not_modify_book();
    test_filled_order_id_cannot_be_reused();
    test_market_order_cannot_rest();
    test_execute_best_order_full_fill_removes_level_and_index();
    test_execute_best_order_partial_fill_retains_order_and_index();
    test_execute_best_order_fifo_same_price();
    test_execute_best_order_invalid_quantity_leaves_book_unchanged();
    test_execute_best_order_subsequent_matching_after_full_fill();

    std::cout << "All OrderBook tests passed.\n";
    return 0;
}
