#include <cassert>
#include <iostream>
#include <stdexcept>

#include "matching_engine/Order.hpp"

using namespace matching_engine;

void test_order_creation() {
    Order order(101, Side::BUY, 10250, 500);

    assert(order.id() == 101);
    assert(order.side() == Side::BUY);
    assert(order.price_ticks() == 10250);
    assert(order.original_quantity() == 500);
    assert(order.remaining_quantity() == 500);
    assert(order.executed_quantity() == 0);
    assert(!order.is_filled());
}

void test_partial_execution() {
    Order order(102, Side::SELL, 15000, 100);

    assert(order.execute(40));
    assert(order.remaining_quantity() == 60);
    assert(order.executed_quantity() == 40);
    assert(!order.is_filled());
}

void test_overfill_rejection() {
    Order order(103, Side::BUY, 10000, 100);

    assert(!order.execute(150));
    assert(order.remaining_quantity() == 100);
    assert(order.executed_quantity() == 0);
}

void test_full_execution() {
    Order order(104, Side::BUY, 10000, 100);

    assert(order.execute(40));
    assert(order.execute(60));
    assert(order.remaining_quantity() == 0);
    assert(order.executed_quantity() == 100);
    assert(order.is_filled());

    // A filled order cannot execute again.
    assert(!order.execute(1));
}

void test_zero_execution_rejection() {
    Order order(105, Side::SELL, 10000, 100);

    assert(!order.execute(0));
    assert(order.remaining_quantity() == 100);
}

void test_invalid_price() {
    bool exception_thrown = false;

    try {
        Order order(106, Side::BUY, 0, 100);
    } catch (const std::invalid_argument&) {
        exception_thrown = true;
    }

    assert(exception_thrown);
}

void test_invalid_order_id() {
    bool exception_thrown = false;

    try {
        Order order(0, Side::BUY, 10000, 100);
    } catch (const std::invalid_argument&) {
        exception_thrown = true;
    }

    assert(exception_thrown);
}

void test_invalid_initial_quantity() {
    bool exception_thrown = false;

    try {
        Order order(107, Side::SELL, 10000, 0);
    } catch (const std::invalid_argument&) {
        exception_thrown = true;
    }

    assert(exception_thrown);
}

void test_cancel_new_order() {
    Order order(201, Side::BUY, 10000, 100);

    assert(order.is_active());
    assert(order.cancel());

    assert(order.status() == OrderStatus::CANCELLED);
    assert(!order.is_active());
    assert(!order.is_filled());

    assert(order.remaining_quantity() == 100);
    assert(order.executed_quantity() == 0);

    assert(!order.execute(10));
    assert(!order.cancel());
}

void test_cancel_partially_filled_order() {
    Order order(202, Side::SELL, 15000, 100);

    assert(order.execute(40));
    assert(order.status() == OrderStatus::PARTIALLY_FILLED);

    assert(order.cancel());

    assert(order.status() == OrderStatus::CANCELLED);
    assert(order.remaining_quantity() == 60);
    assert(order.executed_quantity() == 40);

    assert(!order.execute(60));
}

void test_cannot_cancel_filled_order() {
    Order order(203, Side::BUY, 10500, 100);

    assert(order.execute(100));
    assert(order.is_filled());

    assert(!order.cancel());
    assert(order.status() == OrderStatus::FILLED);
    assert(order.remaining_quantity() == 0);
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

    std::cout << "All Order tests passed.\n";
    return 0;
}
