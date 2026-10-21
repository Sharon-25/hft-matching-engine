#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

#include "matching_engine/Order.hpp"

#define CHECK(condition)                                             \
    do {                                                             \
        if (!(condition)) {                                          \
            std::fprintf(stderr,                                    \
                         "CHECK failed: %s (%s:%d)\n",               \
                         #condition, __FILE__, __LINE__);           \
            std::abort();                                            \
        }                                                            \
    } while (false)

using namespace matching_engine;

void test_order_creation() {
    Order order(101, Side::BUY, 10250, 500);

    CHECK(order.id() == 101);
    CHECK(order.side() == Side::BUY);
    CHECK(order.price_ticks() == 10250);
    CHECK(order.original_quantity() == 500);
    CHECK(order.remaining_quantity() == 500);
    CHECK(order.executed_quantity() == 0);
    CHECK(!order.is_filled());
}

void test_partial_execution() {
    Order order(102, Side::SELL, 15000, 100);

    CHECK(order.execute(40));
    CHECK(order.remaining_quantity() == 60);
    CHECK(order.executed_quantity() == 40);
    CHECK(!order.is_filled());
}

void test_overfill_rejection() {
    Order order(103, Side::BUY, 10000, 100);

    CHECK(!order.execute(150));
    CHECK(order.remaining_quantity() == 100);
    CHECK(order.executed_quantity() == 0);
}

void test_full_execution() {
    Order order(104, Side::BUY, 10000, 100);

    CHECK(order.execute(40));
    CHECK(order.execute(60));
    CHECK(order.remaining_quantity() == 0);
    CHECK(order.executed_quantity() == 100);
    CHECK(order.is_filled());

    // A filled order cannot execute again.
    CHECK(!order.execute(1));
}

void test_zero_execution_rejection() {
    Order order(105, Side::SELL, 10000, 100);

    CHECK(!order.execute(0));
    CHECK(order.remaining_quantity() == 100);
}

void test_invalid_price() {
    bool exception_thrown = false;

    try {
        Order order(106, Side::BUY, 0, 100);
    } catch (const std::invalid_argument&) {
        exception_thrown = true;
    }

    CHECK(exception_thrown);
}

void test_invalid_order_id() {
    bool exception_thrown = false;

    try {
        Order order(0, Side::BUY, 10000, 100);
    } catch (const std::invalid_argument&) {
        exception_thrown = true;
    }

    CHECK(exception_thrown);
}

void test_invalid_initial_quantity() {
    bool exception_thrown = false;

    try {
        Order order(107, Side::SELL, 10000, 0);
    } catch (const std::invalid_argument&) {
        exception_thrown = true;
    }

    CHECK(exception_thrown);
}

void test_cancel_new_order() {
    Order order(201, Side::BUY, 10000, 100);

    CHECK(order.is_active());
    CHECK(order.cancel());

    CHECK(order.status() == OrderStatus::CANCELLED);
    CHECK(!order.is_active());
    CHECK(!order.is_filled());

    CHECK(order.remaining_quantity() == 100);
    CHECK(order.executed_quantity() == 0);

    CHECK(!order.execute(10));
    CHECK(!order.cancel());
}

void test_cancel_partially_filled_order() {
    Order order(202, Side::SELL, 15000, 100);

    CHECK(order.execute(40));
    CHECK(order.status() == OrderStatus::PARTIALLY_FILLED);

    CHECK(order.cancel());

    CHECK(order.status() == OrderStatus::CANCELLED);
    CHECK(order.remaining_quantity() == 60);
    CHECK(order.executed_quantity() == 40);

    CHECK(!order.execute(60));
}

void test_cannot_cancel_filled_order() {
    Order order(203, Side::BUY, 10500, 100);

    CHECK(order.execute(100));
    CHECK(order.is_filled());

    CHECK(!order.cancel());
    CHECK(order.status() == OrderStatus::FILLED);
    CHECK(order.remaining_quantity() == 0);
}

void test_limit_order_type() {
    Order order(1, Side::BUY, 11000, 100);

    CHECK(order.type() == OrderType::LIMIT);
    CHECK(order.price_ticks() == 11000);
    CHECK(order.remaining_quantity() == 100);
    CHECK(order.status() == OrderStatus::NEW);
}

void test_market_order_creation() {
    Order order(
        2,
        Side::SELL,
        OrderType::MARKET,
        0,
        250
    );

    CHECK(order.type() == OrderType::MARKET);
    CHECK(order.price_ticks() == 0);
    CHECK(order.original_quantity() == 250);
    CHECK(order.remaining_quantity() == 250);
    CHECK(order.status() == OrderStatus::NEW);
}

void test_market_order_rejects_nonzero_price() {
    bool threw = false;

    try {
        Order order(
            3,
            Side::BUY,
            OrderType::MARKET,
            11000,
            100
        );
    }
    catch (const std::invalid_argument&) {
        threw = true;
    }

    CHECK(threw);
}

void test_invalid_order_type() {
    bool threw = false;

    try {
        Order order(
            4,
            Side::BUY,
            static_cast<OrderType>(99),
            0,
            100
        );
    }
    catch (const std::invalid_argument&) {
        threw = true;
    }

    CHECK(threw);
}

int main() {
    test_order_creation();
    test_partial_execution();
    test_overfill_rejection();
    test_full_execution();
    test_zero_execution_rejection();
    test_invalid_price();
    test_invalid_order_id();
    test_invalid_initial_quantity();
    test_cancel_new_order();
    test_cancel_partially_filled_order();
    test_cannot_cancel_filled_order();
    test_limit_order_type();
    test_market_order_creation();
    test_market_order_rejects_nonzero_price();
    test_invalid_order_type();

    std::cout << "All Order tests passed.\n";
    return 0;
}
