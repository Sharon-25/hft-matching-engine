#include <iostream>

#include "matching_engine/OrderBook.hpp"

int main() {
    using namespace matching_engine;

    OrderBook book;

    book.add_order(Order(1, Side::BUY, 11000, 100));
    book.add_order(Order(2, Side::BUY, 10800, 200));
    book.add_order(Order(3, Side::BUY, 10500, 150));
    book.add_order(Order(4, Side::BUY, 11000, 300));

    book.add_order(Order(5, Side::SELL, 11200, 100));
    book.add_order(Order(6, Side::SELL, 11200, 200));
    book.add_order(Order(7, Side::SELL, 11500, 150));
    book.add_order(Order(8, Side::SELL, 11800, 100));

    book.print_book();

    if (auto bid = book.best_bid()) {
        std::cout << "Best Bid: " << *bid << '\n';
    } else {
        std::cout << "No BUY orders\n";
    }

    if (auto ask = book.best_ask()) {
        std::cout << "Best Ask: " << *ask << '\n';
    } else {
        std::cout << "No SELL orders\n";
    }

    return 0;
}
