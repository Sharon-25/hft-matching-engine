#include <iostream>

#include "matching_engine/Types.hpp"

int main() {
    using namespace matching_engine;

    OrderId order_id = 101;
    Quantity quantity = 200;
    Price price = 10250;

    Side side = Side::BUY;
    OrderType type = OrderType::LIMIT;

    std::cout << "Order ID: " << order_id << '\n';
    std::cout << "Quantity: " << quantity << '\n';
    std::cout << "Price (ticks): " << price << '\n';

    if (side == Side::BUY) {
        std::cout << "Side: BUY\n";
    }

    if (type == OrderType::LIMIT) {
        std::cout << "Type: LIMIT\n";
    }

    return 0;
}
